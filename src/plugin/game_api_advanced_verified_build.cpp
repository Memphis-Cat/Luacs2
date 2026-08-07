// Final validation layer over the complete ABI implementation. This file is
// intentionally part of the generated advanced adapter selected by CMake.
#include "game_api_advanced_final_build.cpp"

namespace {

// The generated resolver replaces the obsolete 44-byte placeholder with the
// pinned SDK type. These assertions prevent a future source/configuration drift
// from silently restoring a different ABI.
static_assert(sizeof(Ray_t) == sizeof(LuaCSAdvancedApi::NativeRay),
              "pinned Source 2 Ray_t layout changed");
static_assert(alignof(Ray_t) == alignof(LuaCSAdvancedApi::NativeRay),
              "pinned Source 2 Ray_t alignment changed");

bool trace_verified(void* context, const TraceRequest* request,
                    TraceResult* output, char* error,
                    std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !request || !output) {
        write_error(error, error_size,
                    "trace context, request, or output is null");
        return false;
    }

    constexpr std::uint64_t valid_object_sets =
        static_cast<std::uint64_t>(RNQUERY_OBJECTS_ALL);
    if ((request->ignore_entities_mask & ~valid_object_sets) != 0) {
        write_error(error, error_size,
                    "trace object_set_mask contains unsupported Source 2 bits");
        return false;
    }

    if (!trace_complete(context, request, output, error, error_size)) {
        return false;
    }

    // A miss has no collision point or plane. Report the requested end point
    // and full travelled distance instead of trusting hit-only fields that an
    // engine implementation may leave untouched.
    if (!output->hit) {
        output->hit_position = output->end;
        output->plane_normal = {};
        output->plane_distance = 0.0f;
        output->hit_offset = 0.0f;
        output->triangle = -1;
        output->bone = -1;
        output->hitbox = -1;
        output->hitgroup = 0;
        output->entity_index = -1;
        output->entity_handle = 0xFFFFFFFFu;
        output->surface_flags = 0;
        output->surface_name[0] = '\0';
        const float delta_x = output->end.x - output->start.x;
        const float delta_y = output->end.y - output->start.y;
        const float delta_z = output->end.z - output->start.z;
        output->distance =
            std::sqrt(delta_x * delta_x + delta_y * delta_y +
                      delta_z * delta_z);
    }
    return true;
}

bool schema_fixed_char_array(CSchemaType* type) {
    if (!type || type->m_eTypeCategory != SCHEMA_TYPE_FIXED_ARRAY) {
        return false;
    }
    const auto* array = static_cast<CSchemaType_FixedArray*>(type);
    return array->m_pElementType &&
           array->m_pElementType->m_eTypeCategory == SCHEMA_TYPE_BUILTIN &&
           static_cast<CSchemaType_Builtin*>(array->m_pElementType)
                   ->m_eBuiltinType == SCHEMA_BUILTIN_TYPE_CHAR;
}

bool schema_bitfield(CSchemaType* type) {
    return type && type->m_eTypeCategory == SCHEMA_TYPE_BITFIELD;
}

const SchemaClassFieldData_t* schema_find_field(
    CSchemaClassInfo* class_info, std::string_view name, std::uint32_t& offset,
    bool& networked, CSchemaClassInfo*& owner_class) {
    if (!class_info) return nullptr;
    for (std::uint16_t index = 0; index < class_info->m_nFieldCount; ++index) {
        const auto& field = class_info->m_pFields[index];
        if (!field.m_pszName || name != field.m_pszName) continue;
        offset += static_cast<std::uint32_t>(field.m_nSingleInheritanceOffset);
        networked = networked || has_network_metadata(field);
        owner_class = class_info;
        return &field;
    }
    for (std::uint8_t index = 0; index < class_info->m_nBaseClassCount;
         ++index) {
        const auto& base = class_info->m_pBaseClasses[index];
        std::uint32_t inherited = offset + base.m_nOffset;
        bool inherited_networked = networked;
        CSchemaClassInfo* inherited_owner{};
        const auto* result = schema_find_field(
            base.m_pClass, name, inherited, inherited_networked,
            inherited_owner);
        if (!result) continue;
        offset = inherited;
        networked = inherited_networked;
        owner_class = inherited_owner;
        return result;
    }
    return nullptr;
}

void schema_finalize_info(ResolvedProperty& property,
                          CSchemaClassInfo* owner_class, bool networked,
                          std::string_view path, int selected_index,
                          bool followed_pointer) {
    property.info = {};
    property.info.valid = true;
    property.info.networked = networked;
    property.info.kind = classify(property.type, property.info.element_size,
                                  property.info.array_count,
                                  property.info.element_size);
    property.info.fixed_array =
        property.type &&
        property.type->m_eTypeCategory == SCHEMA_TYPE_FIXED_ARRAY;
    property.info.collection = is_collection(property.type);
    property.info.pointer =
        property.type && property.type->m_eTypeCategory == SCHEMA_TYPE_POINTER;
    property.info.embedded_class = class_from_type(property.type) != nullptr;
    property.info.selected_index = selected_index;

    std::size_t byte_size{};
    if (schema_size(property.type, byte_size) &&
        byte_size <= std::numeric_limits<std::uint32_t>::max()) {
        property.info.byte_size = static_cast<std::uint32_t>(byte_size);
    }

    if (schema_bitfield(property.type)) {
        // Source 2's schema type exposes the bit count but not a stable public
        // bit offset for a safe field-level write. Treat the storage as raw,
        // readable bytes and refuse mutation rather than clobber neighbour bits.
        property.info.kind = PropertyKind::Raw;
        property.info.readable = property.info.byte_size != 0;
        property.info.writable = false;
    } else if (property.info.collection) {
        property.info.readable = false;
        property.info.writable = false;
    } else if (property.info.kind == PropertyKind::Invalid) {
        if (property.info.byte_size != 0) {
            property.info.kind = PropertyKind::Raw;
            property.info.readable = true;
            property.info.writable = true;
        }
    } else {
        property.info.readable = true;
        property.info.writable = true;
    }

    if (property.entity && !followed_pointer) {
        const auto root = reinterpret_cast<std::uintptr_t>(property.entity);
        const auto address = reinterpret_cast<std::uintptr_t>(property.address);
        if (address >= root) {
            const auto difference = address - root;
            if (difference <= std::numeric_limits<std::uint32_t>::max()) {
                property.info.offset = static_cast<std::uint32_t>(difference);
            }
        }
    }
    copy_text(property.info.name, sizeof(property.info.name), path);
    copy_text(property.info.type_name, sizeof(property.info.type_name),
              type_name(property.type));
    if (owner_class && owner_class->m_pszName) {
        copy_text(property.info.owner_class,
                  sizeof(property.info.owner_class), owner_class->m_pszName);
    }
}

bool resolve_schema_property(LuaCSAdvancedApi* api, int entity_index,
                             std::string_view path, int explicit_index,
                             ResolvedProperty& output, std::string& error) {
    if (!api || !api->ready(error)) return false;
    CEntityInstance* instance = api->entity(entity_index);
    if (!instance) {
        error = "entity index is invalid";
        return false;
    }
    CSchemaClassInfo* class_info = instance->Schema_DynamicBinding().Get();
    if (!class_info) {
        error = "entity has no dynamic schema binding";
        return false;
    }
    std::vector<PathPart> parts;
    if (!parse_path(path, parts, error)) return false;
    if (explicit_index < -1) {
        error = "property array index cannot be negative";
        return false;
    }

    void* base = instance;
    std::uint32_t root_offset{};
    bool networked{};
    bool followed_pointer{};
    int network_index{-1};
    int selected_index{-1};

    for (std::size_t part_index = 0; part_index < parts.size(); ++part_index) {
        const auto& part = parts[part_index];
        std::uint32_t local{};
        bool field_networked{};
        CSchemaClassInfo* owner_class{};
        const auto* field = schema_find_field(
            class_info, part.name, local, field_networked, owner_class);
        if (!field) {
            error = "schema property was not found: " + part.name;
            return false;
        }
        if (part_index == 0) root_offset = local;
        networked = networked || field_networked;

        CSchemaType* type = field->m_pType;
        if (!type) {
            error = "schema property has no type information";
            return false;
        }
        void* address = reinterpret_cast<std::uint8_t*>(base) + local;
        int index = part.index;
        if (part_index + 1 == parts.size() && explicit_index >= 0) {
            if (index >= 0 && index != explicit_index) {
                error =
                    "property path and argument specify different array indexes";
                return false;
            }
            index = explicit_index;
        }

        if (index >= 0) {
            if (type->m_eTypeCategory == SCHEMA_TYPE_FIXED_ARRAY) {
                auto* array = static_cast<CSchemaType_FixedArray*>(type);
                if (index >= array->m_nElementCount) {
                    error = "fixed-array index is out of range";
                    return false;
                }
                address = reinterpret_cast<std::uint8_t*>(address) +
                          static_cast<std::size_t>(index) *
                              array->m_nElementSize;
                type = array->m_pElementType;
            } else if (is_collection(type)) {
                auto* collection =
                    static_cast<CSchemaType_Atomic_CollectionOfT*>(type);
                if (!collection->m_pfnManipulator) {
                    error = "schema collection has no manipulator";
                    return false;
                }
                const auto count = reinterpret_cast<std::uintptr_t>(
                    collection->m_pfnManipulator(
                        SCHEMA_COLLECTION_MANIPULATOR_ACTION_GET_COUNT,
                        address, 0, 0));
                if (static_cast<std::size_t>(index) >= count) {
                    error = "schema collection index is out of range";
                    return false;
                }
                address = collection->m_pfnManipulator(
                    SCHEMA_COLLECTION_MANIPULATOR_ACTION_GET_ELEMENT,
                    address, index, 0);
                type = collection->m_pTemplateType;
                if (!address || !type) {
                    error = "schema collection element is unavailable";
                    return false;
                }
            } else {
                error = "property is not an indexable schema array";
                return false;
            }
            selected_index = index;
            if (part_index == 0) network_index = index;
        }

        if (part_index + 1 < parts.size()) {
            if (type->m_eTypeCategory == SCHEMA_TYPE_POINTER) {
                auto* pointer_type = static_cast<CSchemaType_Ptr*>(type);
                base = *reinterpret_cast<void**>(address);
                type = pointer_type->m_pObjectType;
                followed_pointer = true;
                if (!base) {
                    error = "nested schema pointer is null";
                    return false;
                }
            } else {
                base = address;
            }
            if (!type || type->m_eTypeCategory != SCHEMA_TYPE_DECLARED_CLASS) {
                error = "property path descends through a non-class field";
                return false;
            }
            class_info =
                static_cast<CSchemaType_DeclaredClass*>(type)->m_pClassInfo;
            if (!class_info) {
                error = "nested schema class metadata is unavailable";
                return false;
            }
            continue;
        }

        output = {};
        output.entity = instance;
        output.address = address;
        output.type = type;
        output.root_offset = root_offset;
        output.array_index = network_index;
        schema_finalize_info(output, owner_class, networked, path,
                             selected_index, followed_pointer);
        return true;
    }

    error = "property resolution failed";
    return false;
}

bool schema_typed_aggregate(const ResolvedProperty& property) {
    if (property.info.collection) return true;
    if (!property.info.fixed_array) return false;
    return !schema_fixed_char_array(property.type);
}

bool signed_value_fits(std::int64_t value, std::uint16_t width) {
    switch (width) {
        case 1:
            return value >= std::numeric_limits<std::int8_t>::min() &&
                   value <= std::numeric_limits<std::int8_t>::max();
        case 2:
            return value >= std::numeric_limits<std::int16_t>::min() &&
                   value <= std::numeric_limits<std::int16_t>::max();
        case 4:
            return value >= std::numeric_limits<std::int32_t>::min() &&
                   value <= std::numeric_limits<std::int32_t>::max();
        case 8:
            return true;
        default:
            return false;
    }
}

bool unsigned_value_fits(std::uint64_t value, std::uint16_t width) {
    switch (width) {
        case 1: return value <= std::numeric_limits<std::uint8_t>::max();
        case 2: return value <= std::numeric_limits<std::uint16_t>::max();
        case 4: return value <= std::numeric_limits<std::uint32_t>::max();
        case 8: return true;
        default: return false;
    }
}

std::size_t bounded_string_length(const char* value, std::size_t capacity) {
    if (!value) return 0;
    std::size_t length{};
    while (length < capacity && value[length] != '\0') ++length;
    return length;
}

bool read_schema_property(LuaCSAdvancedApi* api, int entity_index,
                          std::string_view path, int array_index,
                          PropertyValue& output, std::string& error) {
    ResolvedProperty property;
    if (!resolve_schema_property(api, entity_index, path, array_index, property,
                                 error)) {
        return false;
    }
    if (schema_typed_aggregate(property)) {
        error = "typed access to an array or collection requires an element index";
        return false;
    }
    if (property.info.kind == PropertyKind::Raw ||
        property.info.kind == PropertyKind::Invalid) {
        error = "schema property requires properties.get_raw";
        return false;
    }

    output = {};
    output.kind = property.info.kind;
    output.width = static_cast<std::uint8_t>(
        std::min<std::uint16_t>(property.info.element_size, 255));
    const auto width = property.info.element_size;
    switch (output.kind) {
        case PropertyKind::Boolean:
            output.boolean_value =
                LuaCSAdvancedApi::read_scalar<bool>(property.address);
            break;
        case PropertyKind::SignedInteger:
            switch (width) {
                case 1:
                    output.signed_value = LuaCSAdvancedApi::read_scalar<
                        std::int8_t>(property.address);
                    break;
                case 2:
                    output.signed_value = LuaCSAdvancedApi::read_scalar<
                        std::int16_t>(property.address);
                    break;
                case 4:
                    output.signed_value = LuaCSAdvancedApi::read_scalar<
                        std::int32_t>(property.address);
                    break;
                case 8:
                    output.signed_value = LuaCSAdvancedApi::read_scalar<
                        std::int64_t>(property.address);
                    break;
                default:
                    error = "unsupported signed integer width";
                    return false;
            }
            break;
        case PropertyKind::UnsignedInteger:
            switch (width) {
                case 1:
                    output.unsigned_value = LuaCSAdvancedApi::read_scalar<
                        std::uint8_t>(property.address);
                    break;
                case 2:
                    output.unsigned_value = LuaCSAdvancedApi::read_scalar<
                        std::uint16_t>(property.address);
                    break;
                case 4:
                    output.unsigned_value = LuaCSAdvancedApi::read_scalar<
                        std::uint32_t>(property.address);
                    break;
                case 8:
                    output.unsigned_value = LuaCSAdvancedApi::read_scalar<
                        std::uint64_t>(property.address);
                    break;
                default:
                    error = "unsupported unsigned integer width";
                    return false;
            }
            break;
        case PropertyKind::Float:
            if (width == 8) {
                output.float_value =
                    LuaCSAdvancedApi::read_scalar<double>(property.address);
            } else if (width == 4) {
                output.float_value =
                    LuaCSAdvancedApi::read_scalar<float>(property.address);
            } else {
                error = "unsupported floating-point width";
                return false;
            }
            break;
        case PropertyKind::Vector: {
            const Vector& value =
                LuaCSAdvancedApi::read_scalar<Vector>(property.address);
            output.vector_value = {value.x, value.y, value.z};
            break;
        }
        case PropertyKind::Angle: {
            const QAngle& value =
                LuaCSAdvancedApi::read_scalar<QAngle>(property.address);
            output.vector_value = {value.x, value.y, value.z};
            break;
        }
        case PropertyKind::EntityHandle: {
            const CEntityHandle handle =
                LuaCSAdvancedApi::read_scalar<CEntityHandle>(property.address);
            output.entity_handle = handle.ToInt();
            output.entity_index =
                handle.IsValid() ? handle.GetEntryIndex() : -1;
            break;
        }
        case PropertyKind::Pointer:
            output.unsigned_value = reinterpret_cast<std::uintptr_t>(
                LuaCSAdvancedApi::read_scalar<void*>(property.address));
            break;
        case PropertyKind::String: {
            const std::string name = type_name(property.type);
            if (schema_fixed_char_array(property.type)) {
                const auto* array =
                    static_cast<CSchemaType_FixedArray*>(property.type);
                const auto capacity = static_cast<std::size_t>(
                    std::max(array->m_nElementCount, 0));
                const auto* characters =
                    static_cast<const char*>(property.address);
                const std::size_t length =
                    bounded_string_length(characters, capacity);
                copy_text(output.string_value, sizeof(output.string_value),
                          std::string_view(characters, length));
            } else if (name.find("CUtlSymbolLarge") != std::string::npos) {
                const char* value = LuaCSAdvancedApi::read_scalar<CUtlSymbolLarge>(
                                        property.address)
                                        .String();
                copy_text(output.string_value, sizeof(output.string_value),
                          value ? value : "");
            } else {
                const char* value = LuaCSAdvancedApi::read_scalar<CUtlString>(
                                        property.address)
                                        .String();
                copy_text(output.string_value, sizeof(output.string_value),
                          value ? value : "");
            }
            break;
        }
        default:
            error = "schema property type is not readable";
            return false;
    }
    return true;
}

bool write_schema_property(LuaCSAdvancedApi* api, int entity_index,
                           std::string_view path, int array_index,
                           const PropertyValue& value, bool network,
                           std::string& error) {
    ResolvedProperty property;
    if (!resolve_schema_property(api, entity_index, path, array_index, property,
                                 error)) {
        return false;
    }
    if (schema_typed_aggregate(property)) {
        error = "typed access to an array or collection requires an element index";
        return false;
    }
    if (!property.info.writable || property.info.kind != value.kind) {
        error = "property value type does not match writable schema type";
        return false;
    }
    const auto width = property.info.element_size;

    switch (value.kind) {
        case PropertyKind::Boolean:
            LuaCSAdvancedApi::write_scalar(property.address,
                                           value.boolean_value);
            break;
        case PropertyKind::SignedInteger:
            if (!signed_value_fits(value.signed_value, width)) {
                error = "signed integer value does not fit the schema width";
                return false;
            }
            switch (width) {
                case 1:
                    LuaCSAdvancedApi::write_scalar(
                        property.address,
                        static_cast<std::int8_t>(value.signed_value));
                    break;
                case 2:
                    LuaCSAdvancedApi::write_scalar(
                        property.address,
                        static_cast<std::int16_t>(value.signed_value));
                    break;
                case 4:
                    LuaCSAdvancedApi::write_scalar(
                        property.address,
                        static_cast<std::int32_t>(value.signed_value));
                    break;
                case 8:
                    LuaCSAdvancedApi::write_scalar(property.address,
                                                   value.signed_value);
                    break;
                default:
                    error = "unsupported signed integer width";
                    return false;
            }
            break;
        case PropertyKind::UnsignedInteger:
            if (!unsigned_value_fits(value.unsigned_value, width)) {
                error = "unsigned integer value does not fit the schema width";
                return false;
            }
            switch (width) {
                case 1:
                    LuaCSAdvancedApi::write_scalar(
                        property.address,
                        static_cast<std::uint8_t>(value.unsigned_value));
                    break;
                case 2:
                    LuaCSAdvancedApi::write_scalar(
                        property.address,
                        static_cast<std::uint16_t>(value.unsigned_value));
                    break;
                case 4:
                    LuaCSAdvancedApi::write_scalar(
                        property.address,
                        static_cast<std::uint32_t>(value.unsigned_value));
                    break;
                case 8:
                    LuaCSAdvancedApi::write_scalar(property.address,
                                                   value.unsigned_value);
                    break;
                default:
                    error = "unsupported unsigned integer width";
                    return false;
            }
            break;
        case PropertyKind::Float:
            if (!std::isfinite(value.float_value)) {
                error = "floating-point schema value must be finite";
                return false;
            }
            if (width == 8) {
                LuaCSAdvancedApi::write_scalar(property.address,
                                               value.float_value);
            } else if (width == 4) {
                LuaCSAdvancedApi::write_scalar(
                    property.address, static_cast<float>(value.float_value));
            } else {
                error = "unsupported floating-point width";
                return false;
            }
            break;
        case PropertyKind::Vector:
            if (!finite_vector(value.vector_value)) {
                error = "schema Vector value must be finite";
                return false;
            }
            LuaCSAdvancedApi::write_scalar(
                property.address,
                Vector(value.vector_value.x, value.vector_value.y,
                       value.vector_value.z));
            break;
        case PropertyKind::Angle:
            if (!finite_vector(value.vector_value)) {
                error = "schema QAngle value must be finite";
                return false;
            }
            LuaCSAdvancedApi::write_scalar(
                property.address,
                QAngle(value.vector_value.x, value.vector_value.y,
                       value.vector_value.z));
            break;
        case PropertyKind::EntityHandle: {
            CEntityHandle handle;
            if (value.entity_index >= 0) {
                CEntityInstance* target = api->entity(value.entity_index);
                if (!target) {
                    error = "entity-handle target index is invalid";
                    return false;
                }
                handle.Set(target);
            } else {
                handle.Term();
            }
            LuaCSAdvancedApi::write_scalar(property.address, handle);
            break;
        }
        case PropertyKind::Pointer:
            LuaCSAdvancedApi::write_scalar(
                property.address,
                reinterpret_cast<void*>(static_cast<std::uintptr_t>(
                    value.unsigned_value)));
            break;
        case PropertyKind::String: {
            const std::size_t length = bounded_string_length(
                value.string_value, sizeof(value.string_value));
            if (length == sizeof(value.string_value)) {
                error = "schema string value is not null terminated";
                return false;
            }
            const std::string name = type_name(property.type);
            if (schema_fixed_char_array(property.type)) {
                auto* array =
                    static_cast<CSchemaType_FixedArray*>(property.type);
                const std::size_t capacity = static_cast<std::size_t>(
                    std::max(array->m_nElementCount, 0));
                if (!capacity || length >= capacity) {
                    error = "string value does not fit the fixed schema array";
                    return false;
                }
                std::memset(property.address, 0, capacity);
                if (length) {
                    std::memcpy(property.address, value.string_value, length);
                }
            } else if (name.find("CUtlSymbolLarge") != std::string::npos) {
                *reinterpret_cast<CUtlSymbolLarge*>(property.address) =
                    CUtlSymbolLarge(value.string_value);
            } else {
                *reinterpret_cast<CUtlString*>(property.address) =
                    value.string_value;
            }
            break;
        }
        default:
            error = "schema property type is not writable";
            return false;
    }

    if (network) {
        property.entity->NetworkStateChanged(NetworkStateChangedData(
            property.root_offset, property.array_index));
    }
    return true;
}

void fill_static_schema_info(CSchemaClassInfo* owner,
                             const SchemaClassFieldData_t& field,
                             std::uint32_t offset, PropertyInfo& info) {
    info = {};
    info.valid = true;
    info.networked = has_network_metadata(field);
    info.offset = offset;
    info.kind = classify(field.m_pType, info.element_size, info.array_count,
                         info.element_size);
    info.fixed_array = field.m_pType &&
                       field.m_pType->m_eTypeCategory == SCHEMA_TYPE_FIXED_ARRAY;
    info.collection = is_collection(field.m_pType);
    info.pointer = field.m_pType &&
                   field.m_pType->m_eTypeCategory == SCHEMA_TYPE_POINTER;
    info.embedded_class = class_from_type(field.m_pType) != nullptr;
    std::size_t byte_size{};
    if (schema_size(field.m_pType, byte_size) &&
        byte_size <= std::numeric_limits<std::uint32_t>::max()) {
        info.byte_size = static_cast<std::uint32_t>(byte_size);
    }
    if (schema_bitfield(field.m_pType)) {
        info.kind = PropertyKind::Raw;
        info.readable = info.byte_size != 0;
        info.writable = false;
    } else if (info.collection) {
        info.readable = false;
        info.writable = false;
    } else if (info.kind == PropertyKind::Invalid && info.byte_size != 0) {
        info.kind = PropertyKind::Raw;
        info.readable = true;
        info.writable = true;
    } else {
        info.readable = info.kind != PropertyKind::Invalid;
        info.writable = info.kind != PropertyKind::Invalid;
    }
    copy_text(info.name, sizeof(info.name), field.m_pszName ? field.m_pszName : "");
    copy_text(info.type_name, sizeof(info.type_name), type_name(field.m_pType));
    if (owner && owner->m_pszName) {
        copy_text(info.owner_class, sizeof(info.owner_class), owner->m_pszName);
    }
}

void enumerate_schema_class(CSchemaClassInfo* class_info, bool inherited,
                            std::vector<PropertyInfo>& output,
                            std::uint32_t base_offset = 0) {
    if (!class_info) return;
    for (std::uint16_t index = 0; index < class_info->m_nFieldCount; ++index) {
        const auto& field = class_info->m_pFields[index];
        PropertyInfo info;
        fill_static_schema_info(
            class_info, field,
            base_offset +
                static_cast<std::uint32_t>(field.m_nSingleInheritanceOffset),
            info);
        output.push_back(info);
    }
    if (!inherited) return;
    for (std::uint8_t index = 0; index < class_info->m_nBaseClassCount;
         ++index) {
        const auto& base = class_info->m_pBaseClasses[index];
        enumerate_schema_class(base.m_pClass, true, output,
                               base_offset + base.m_nOffset);
    }
}

bool schema_entity_class(LuaCSAdvancedApi* api, int entity_index,
                         CSchemaClassInfo*& output, std::string& error) {
    if (!api || !api->ready(error)) return false;
    CEntityInstance* instance = api->entity(entity_index);
    if (!instance) {
        error = "entity index is invalid";
        return false;
    }
    output = instance->Schema_DynamicBinding().Get();
    if (!output) error = "entity has no dynamic schema binding";
    return output != nullptr;
}

bool schema_child_class(LuaCSAdvancedApi* api, int entity_index,
                        std::string_view path, CSchemaClassInfo*& output,
                        std::string& error) {
    if (path.empty()) return schema_entity_class(api, entity_index, output, error);
    ResolvedProperty resolved;
    if (!resolve_schema_property(api, entity_index, path, -1, resolved, error)) {
        return false;
    }
    output = class_from_type(resolved.type);
    if (!output) {
        error = "schema property is not an embedded or pointed-to class";
        return false;
    }
    return true;
}

bool property_info_verified(void* context, int entity_index,
                            const char* property, PropertyInfo* output,
                            char* error, std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !property || !output) {
        write_error(error, error_size,
                    "property context, path, or metadata output is null");
        return false;
    }
    ResolvedProperty resolved;
    std::string message;
    if (!resolve_schema_property(static_cast<LuaCSAdvancedApi*>(context),
                                 entity_index, property, -1, resolved,
                                 message)) {
        write_error(error, error_size, message);
        return false;
    }
    *output = resolved.info;
    return true;
}

bool property_get_verified(void* context, int entity_index,
                           const char* property, int array_index,
                           PropertyValue* output, char* error,
                           std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !property || !output) {
        write_error(error, error_size,
                    "property context, path, or read output is null");
        return false;
    }
    std::string message;
    if (!read_schema_property(static_cast<LuaCSAdvancedApi*>(context),
                              entity_index, property, array_index, *output,
                              message)) {
        write_error(error, error_size, message);
        return false;
    }
    return true;
}

bool property_set_verified(void* context, int entity_index,
                           const char* property, int array_index,
                           const PropertyValue* value, bool network,
                           char* error, std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !property || !value) {
        write_error(error, error_size,
                    "property context, path, or write value is null");
        return false;
    }
    std::string message;
    if (!write_schema_property(static_cast<LuaCSAdvancedApi*>(context),
                               entity_index, property, array_index, *value,
                               network, message)) {
        write_error(error, error_size, message);
        return false;
    }
    return true;
}

std::size_t property_count_verified(void* context, int entity_index,
                                    bool inherited, char* error,
                                    std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context) {
        write_error(error, error_size, "property context is null");
        return 0;
    }
    CSchemaClassInfo* class_info{};
    std::string message;
    if (!schema_entity_class(static_cast<LuaCSAdvancedApi*>(context),
                             entity_index, class_info, message)) {
        write_error(error, error_size, message);
        return 0;
    }
    std::vector<PropertyInfo> values;
    enumerate_schema_class(class_info, inherited, values);
    return values.size();
}

bool property_at_verified(void* context, int entity_index, bool inherited,
                          std::size_t index, PropertyInfo* output, char* error,
                          std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !output) {
        write_error(error, error_size,
                    "property context or enumeration output is null");
        return false;
    }
    CSchemaClassInfo* class_info{};
    std::string message;
    if (!schema_entity_class(static_cast<LuaCSAdvancedApi*>(context),
                             entity_index, class_info, message)) {
        write_error(error, error_size, message);
        return false;
    }
    std::vector<PropertyInfo> values;
    enumerate_schema_class(class_info, inherited, values);
    if (index >= values.size()) {
        write_error(error, error_size, "property index is out of range");
        return false;
    }
    *output = values[index];
    return true;
}

bool property_get_raw_verified(void* context, int entity_index,
                               const char* property, int array_index,
                               RawPropertyValue* output, PropertyInfo* info,
                               char* error, std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !property || !output) {
        write_error(error, error_size,
                    "raw property context, path, or output is null");
        return false;
    }
    ResolvedProperty resolved;
    std::string message;
    if (!resolve_schema_property(static_cast<LuaCSAdvancedApi*>(context),
                                 entity_index, property, array_index, resolved,
                                 message)) {
        write_error(error, error_size, message);
        return false;
    }
    if (resolved.info.collection) {
        write_error(error, error_size,
                    "dynamic collection raw reads require an element index");
        return false;
    }
    std::size_t size{};
    if (!schema_size(resolved.type, size) || size == 0) {
        write_error(error, error_size,
                    "schema type does not expose a stable byte size");
        return false;
    }
    if (size > luacs::kPropertyRawCapacity) {
        write_error(error, error_size,
                    "schema value exceeds the bounded raw buffer capacity");
        return false;
    }
    output->size = size;
    std::memcpy(output->bytes, resolved.address, size);
    if (info) *info = resolved.info;
    return true;
}

bool property_set_raw_verified(void* context, int entity_index,
                               const char* property, int array_index,
                               const RawPropertyValue* input, bool network,
                               char* error, std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !property || !input) {
        write_error(error, error_size,
                    "raw property context, path, or input is null");
        return false;
    }
    ResolvedProperty resolved;
    std::string message;
    if (!resolve_schema_property(static_cast<LuaCSAdvancedApi*>(context),
                                 entity_index, property, array_index, resolved,
                                 message)) {
        write_error(error, error_size, message);
        return false;
    }
    if (resolved.info.collection) {
        write_error(error, error_size,
                    "dynamic collection raw writes require an element index");
        return false;
    }
    if (schema_bitfield(resolved.type)) {
        write_error(error, error_size,
                    "bitfield writes are unavailable because Source 2 does not expose a stable public bit offset");
        return false;
    }
    std::size_t size{};
    if (!schema_size(resolved.type, size) || size == 0) {
        write_error(error, error_size,
                    "schema type does not expose a stable byte size");
        return false;
    }
    if (size > luacs::kPropertyRawCapacity || input->size != size) {
        write_error(error, error_size,
                    "raw property byte count does not match the schema size");
        return false;
    }
    std::memcpy(resolved.address, input->bytes, size);
    if (network) {
        resolved.entity->NetworkStateChanged(NetworkStateChangedData(
            resolved.root_offset, resolved.array_index));
    }
    return true;
}

std::size_t property_collection_count_verified(
    void* context, int entity_index, const char* property, char* error,
    std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !property) {
        write_error(error, error_size,
                    "collection context or path is null");
        return 0;
    }
    ResolvedProperty resolved;
    std::string message;
    if (!resolve_schema_property(static_cast<LuaCSAdvancedApi*>(context),
                                 entity_index, property, -1, resolved,
                                 message)) {
        write_error(error, error_size, message);
        return 0;
    }
    if (!resolved.info.collection) {
        write_error(error, error_size,
                    "schema property is not a dynamic collection");
        return 0;
    }
    auto* collection =
        static_cast<CSchemaType_Atomic_CollectionOfT*>(resolved.type);
    if (!collection->m_pfnManipulator) {
        write_error(error, error_size,
                    "schema collection has no manipulator");
        return 0;
    }
    return reinterpret_cast<std::uintptr_t>(collection->m_pfnManipulator(
        SCHEMA_COLLECTION_MANIPULATOR_ACTION_GET_COUNT, resolved.address, 0,
        0));
}

bool property_collection_resize_verified(
    void* context, int entity_index, const char* property, std::size_t count,
    bool network, char* error, std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !property) {
        write_error(error, error_size,
                    "collection context or path is null");
        return false;
    }
    if (count > static_cast<std::size_t>(INT_MAX)) {
        write_error(error, error_size,
                    "collection size exceeds the engine index range");
        return false;
    }
    ResolvedProperty resolved;
    std::string message;
    if (!resolve_schema_property(static_cast<LuaCSAdvancedApi*>(context),
                                 entity_index, property, -1, resolved,
                                 message)) {
        write_error(error, error_size, message);
        return false;
    }
    if (!resolved.info.collection) {
        write_error(error, error_size,
                    "schema property is not a dynamic collection");
        return false;
    }
    auto* collection =
        static_cast<CSchemaType_Atomic_CollectionOfT*>(resolved.type);
    if (!collection->m_pfnManipulator) {
        write_error(error, error_size,
                    "schema collection has no manipulator");
        return false;
    }
    collection->m_pfnManipulator(SCHEMA_COLLECTION_MANIPULATOR_ACTION_SET_COUNT,
                                 resolved.address, static_cast<int>(count), 0);
    const auto actual = reinterpret_cast<std::uintptr_t>(
        collection->m_pfnManipulator(
            SCHEMA_COLLECTION_MANIPULATOR_ACTION_GET_COUNT, resolved.address,
            0, 0));
    if (actual != count) {
        write_error(error, error_size,
                    "Source 2 did not apply the requested collection size");
        return false;
    }
    if (network) {
        resolved.entity->NetworkStateChanged(
            NetworkStateChangedData(resolved.root_offset, -1));
    }
    return true;
}

std::size_t property_child_count_verified(void* context, int entity_index,
                                          const char* property,
                                          bool inherited, char* error,
                                          std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context) {
        write_error(error, error_size, "property context is null");
        return 0;
    }
    CSchemaClassInfo* class_info{};
    std::string message;
    if (!schema_child_class(static_cast<LuaCSAdvancedApi*>(context),
                            entity_index, property ? property : "", class_info,
                            message)) {
        write_error(error, error_size, message);
        return 0;
    }
    std::vector<PropertyInfo> values;
    enumerate_schema_class(class_info, inherited, values);
    return values.size();
}

bool property_child_at_verified(void* context, int entity_index,
                                const char* property, bool inherited,
                                std::size_t index, PropertyInfo* output,
                                char* error, std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !output) {
        write_error(error, error_size,
                    "property context or child output is null");
        return false;
    }
    const std::string parent = property ? property : "";
    CSchemaClassInfo* class_info{};
    std::string message;
    if (!schema_child_class(static_cast<LuaCSAdvancedApi*>(context),
                            entity_index, parent, class_info, message)) {
        write_error(error, error_size, message);
        return false;
    }
    std::vector<PropertyInfo> values;
    enumerate_schema_class(class_info, inherited, values);
    if (index >= values.size()) {
        write_error(error, error_size, "child property index is out of range");
        return false;
    }
    *output = values[index];
    std::string full_path;
    if (!parent.empty()) {
        full_path = parent;
        full_path.push_back('.');
    }
    full_path += values[index].name;
    copy_text(output->name, sizeof(output->name), full_path);

    // If the pointer is live, prefer exact metadata including the real
    // entity-relative offset and inherited network state. A null pointer must
    // not prevent schema reflection, so static type metadata remains valid.
    ResolvedProperty resolved;
    std::string resolve_error;
    if (resolve_schema_property(static_cast<LuaCSAdvancedApi*>(context),
                                entity_index, full_path, -1, resolved,
                                resolve_error)) {
        *output = resolved.info;
    }
    return true;
}

struct VerifiedAdvancedWorldRegistration {
    VerifiedAdvancedWorldRegistration() {
        auto& services = g_advanced_api.services;
        services.trace = &trace_verified;
        services.property_info = &property_info_verified;
        services.property_get = &property_get_verified;
        services.property_set = &property_set_verified;
        services.property_count = &property_count_verified;
        services.property_at = &property_at_verified;
        services.property_get_raw = &property_get_raw_verified;
        services.property_set_raw = &property_set_raw_verified;
        services.property_collection_count =
            &property_collection_count_verified;
        services.property_collection_resize =
            &property_collection_resize_verified;
        services.property_child_count = &property_child_count_verified;
        services.property_child_at = &property_child_at_verified;
    }
};

VerifiedAdvancedWorldRegistration g_verified_advanced_world_registration;

} // namespace

param(
    [Parameter(Mandatory = $true)]
    [string]$Source,

    [Parameter(Mandatory = $true)]
    [string]$Destination
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
    throw "game API source does not exist: $Source"
}

$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$text = [System.IO.File]::ReadAllText($sourcePath).Replace("`r`n", "`n")

$legacy = @'
void* find_pattern(HMODULE module, std::string_view text) {
    if (!module) return nullptr;
    const auto pattern = parse_pattern(text);
    if (pattern.empty()) return nullptr;

    const auto* base = reinterpret_cast<const std::uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    const std::size_t image_size = nt->OptionalHeader.SizeOfImage;
    if (image_size < pattern.size()) return nullptr;
    for (std::size_t offset = 0; offset <= image_size - pattern.size(); ++offset) {
        bool matches = true;
        for (std::size_t index = 0; index < pattern.size(); ++index) {
            if (pattern[index] >= 0 &&
                base[offset + index] != static_cast<std::uint8_t>(pattern[index])) {
                matches = false;
                break;
            }
        }
        if (matches) return const_cast<std::uint8_t*>(base + offset);
    }
    return nullptr;
}
'@
$legacy = $legacy.Replace("`r`n", "`n")

$replacement = @'
thread_local std::string g_pattern_scan_diagnostic;

bool readable_memory(const void* address, std::size_t size) {
    if (size == 0) return true;
    if (!address) return false;

    const auto start = reinterpret_cast<std::uintptr_t>(address);
    if (size > std::numeric_limits<std::uintptr_t>::max() - start) {
        return false;
    }
    const auto end = start + size;
    std::uintptr_t cursor = start;

    while (cursor < end) {
        MEMORY_BASIC_INFORMATION information{};
        const SIZE_T queried = VirtualQuery(
            reinterpret_cast<const void*>(cursor), &information,
            sizeof(information));
        if (queried != sizeof(information) ||
            information.State != MEM_COMMIT) {
            return false;
        }

        const DWORD protection = information.Protect;
        if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            return false;
        }
        const DWORD base_protection = protection & 0xFFu;
        switch (base_protection) {
            case PAGE_READONLY:
            case PAGE_READWRITE:
            case PAGE_WRITECOPY:
            case PAGE_EXECUTE_READ:
            case PAGE_EXECUTE_READWRITE:
            case PAGE_EXECUTE_WRITECOPY:
                break;
            default:
                return false;
        }

        const auto region_start =
            reinterpret_cast<std::uintptr_t>(information.BaseAddress);
        if (information.RegionSize == 0 ||
            information.RegionSize >
                std::numeric_limits<std::uintptr_t>::max() - region_start) {
            return false;
        }
        const auto region_end = region_start + information.RegionSize;
        if (cursor < region_start || cursor >= region_end) return false;
        cursor = std::min(end, region_end);
    }
    return true;
}

template <typename T>
bool read_live_value(const void* address, T& value) {
    if (!readable_memory(address, sizeof(T))) return false;
    std::memcpy(&value, address, sizeof(T));
    return true;
}

void* find_pattern(HMODULE module, std::string_view text) {
    g_pattern_scan_diagnostic.clear();
    if (!module) {
        g_pattern_scan_diagnostic = "module handle is null";
        return nullptr;
    }

    const auto pattern = parse_pattern(text);
    if (pattern.empty()) {
        g_pattern_scan_diagnostic =
            "signature pattern is empty or contains invalid hexadecimal tokens";
        return nullptr;
    }

    wchar_t module_path[32768]{};
    constexpr DWORD module_path_capacity = static_cast<DWORD>(
        sizeof(module_path) / sizeof(module_path[0]));
    const DWORD path_length =
        GetModuleFileNameW(module, module_path, module_path_capacity);
    if (path_length == 0 || path_length >= module_path_capacity) {
        g_pattern_scan_diagnostic =
            "GetModuleFileNameW failed with Windows error " +
            std::to_string(static_cast<unsigned long>(GetLastError()));
        return nullptr;
    }

    const std::filesystem::path disk_path(module_path);
    std::ifstream input{disk_path, std::ios::binary};
    if (!input) {
        g_pattern_scan_diagnostic =
            "could not open disk image '" + disk_path.string() + "'";
        return nullptr;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff file_size = input.tellg();
    if (file_size <= 0 ||
        static_cast<std::uintmax_t>(file_size) >
            static_cast<std::uintmax_t>(
                std::numeric_limits<std::streamsize>::max())) {
        g_pattern_scan_diagnostic =
            "disk image has an invalid or unsupported file size: '" +
            disk_path.string() + "'";
        return nullptr;
    }
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> file_bytes(static_cast<std::size_t>(file_size));
    input.read(reinterpret_cast<char*>(file_bytes.data()),
               static_cast<std::streamsize>(file_bytes.size()));
    if (!input) {
        g_pattern_scan_diagnostic =
            "failed to read the complete disk image '" + disk_path.string() +
            "'";
        return nullptr;
    }

    if (file_bytes.size() < sizeof(IMAGE_DOS_HEADER)) {
        g_pattern_scan_diagnostic = "disk image is smaller than a DOS header";
        return nullptr;
    }
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, file_bytes.data(), sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) {
        g_pattern_scan_diagnostic = "disk image has an invalid DOS header";
        return nullptr;
    }

    const std::size_t nt_offset = static_cast<std::size_t>(dos.e_lfanew);
    if (nt_offset > file_bytes.size() ||
        sizeof(IMAGE_NT_HEADERS64) > file_bytes.size() - nt_offset) {
        g_pattern_scan_diagnostic = "disk image has a truncated NT header";
        return nullptr;
    }

    IMAGE_NT_HEADERS64 nt{};
    std::memcpy(&nt, file_bytes.data() + nt_offset, sizeof(nt));
    if (nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        g_pattern_scan_diagnostic =
            "disk image is not a valid 64-bit PE executable";
        return nullptr;
    }

    const std::size_t sections_offset =
        nt_offset + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
        nt.FileHeader.SizeOfOptionalHeader;
    const std::size_t section_count = nt.FileHeader.NumberOfSections;
    if (section_count == 0 ||
        sections_offset > file_bytes.size() ||
        section_count >
            (file_bytes.size() - sections_offset) / sizeof(IMAGE_SECTION_HEADER)) {
        g_pattern_scan_diagnostic =
            "disk image has an invalid PE section table";
        return nullptr;
    }

    auto* live_base = reinterpret_cast<std::uint8_t*>(module);
    std::size_t executable_sections = 0;
    std::size_t executable_bytes = 0;
    for (std::size_t section_index = 0; section_index < section_count;
         ++section_index) {
        IMAGE_SECTION_HEADER section{};
        std::memcpy(
            &section,
            file_bytes.data() + sections_offset +
                section_index * sizeof(IMAGE_SECTION_HEADER),
            sizeof(section));

        if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        ++executable_sections;

        const std::size_t raw_offset = section.PointerToRawData;
        std::size_t raw_size = section.SizeOfRawData;
        if (section.Misc.VirtualSize != 0) {
            raw_size = std::min(
                raw_size, static_cast<std::size_t>(section.Misc.VirtualSize));
        }
        if (raw_offset > file_bytes.size() ||
            raw_size > file_bytes.size() - raw_offset) {
            continue;
        }
        executable_bytes += raw_size;
        if (raw_size < pattern.size()) continue;

        const auto* section_bytes = file_bytes.data() + raw_offset;
        for (std::size_t offset = 0; offset <= raw_size - pattern.size();
             ++offset) {
            bool matches = true;
            for (std::size_t index = 0; index < pattern.size(); ++index) {
                if (pattern[index] >= 0 &&
                    section_bytes[offset + index] !=
                        static_cast<std::uint8_t>(pattern[index])) {
                    matches = false;
                    break;
                }
            }
            if (!matches) continue;

            const std::size_t rva =
                static_cast<std::size_t>(section.VirtualAddress) + offset;
            if (rva >=
                static_cast<std::size_t>(nt.OptionalHeader.SizeOfImage)) {
                g_pattern_scan_diagnostic =
                    "disk match resolved outside the live image bounds";
                return nullptr;
            }

            void* live_match = live_base + rva;
            if (!readable_memory(live_match, pattern.size())) {
                g_pattern_scan_diagnostic =
                    "disk match could not safely map to readable live memory";
                return nullptr;
            }

            std::ostringstream diagnostic;
            diagnostic << "matched disk image '" << disk_path.string()
                       << "' at RVA " << rva;
            g_pattern_scan_diagnostic = diagnostic.str();
            return live_match;
        }
    }

    std::ostringstream diagnostic;
    diagnostic << "no match in disk image '" << disk_path.string()
               << "'; pattern-bytes=" << pattern.size()
               << "; executable-sections=" << executable_sections
               << "; executable-bytes-scanned=" << executable_bytes
               << "; image-bytes=" << file_bytes.size();
    g_pattern_scan_diagnostic = diagnostic.str();
    return nullptr;
}
'@
$replacement = $replacement.Replace("`r`n", "`n")

$vtableLegacy = @'
        if (descriptor_reference < read_only->data + 0x0C) continue;
        auto* locator = descriptor_reference - 0x0C;
        if (*reinterpret_cast<std::int32_t*>(locator) != 1) continue;
        if (*reinterpret_cast<std::int32_t*>(descriptor_reference - 0x08) != 0) {
            continue;
        }
'@
$vtableLegacy = $vtableLegacy.Replace("`r`n", "`n")
$vtableReplacement = @'
        if (descriptor_reference < read_only->data + 0x0C) continue;
        auto* locator = descriptor_reference - 0x0C;
        std::int32_t locator_signature{};
        std::int32_t locator_offset{};
        if (!read_live_value(locator, locator_signature) ||
            locator_signature != 1) {
            continue;
        }
        if (!read_live_value(descriptor_reference - 0x08, locator_offset) ||
            locator_offset != 0) {
            continue;
        }
'@
$vtableReplacement = $vtableReplacement.Replace("`r`n", "`n")

$occurrences = ([regex]::Matches($text, [regex]::Escape($legacy))).Count
if ($occurrences -ne 1) {
    throw "expected exactly one legacy live-image pattern scanner, found $occurrences"
}
$vtableOccurrences = ([regex]::Matches(
    $text, [regex]::Escape($vtableLegacy))).Count
if ($vtableOccurrences -ne 1) {
    throw "expected exactly one raw live RTTI dereference block, found $vtableOccurrences"
}

$updated = $text.Replace($legacy, $replacement)
$updated = $updated.Replace($vtableLegacy, $vtableReplacement)
if ($updated.Contains('offset <= image_size - pattern.size()')) {
    throw 'legacy whole-image live memory scan remains after generation'
}
foreach ($obsolete in @(
    '*reinterpret_cast<std::int32_t*>(locator)',
    '*reinterpret_cast<std::int32_t*>(descriptor_reference - 0x08)'
)) {
    if ($updated.Contains($obsolete)) {
        throw "generated game API still contains unsafe live dereference '$obsolete'"
    }
}
foreach ($required in @(
    'VirtualQuery(',
    'readable_memory(',
    'read_live_value(',
    'PAGE_GUARD | PAGE_NOACCESS',
    'if (!readable_memory(live_match, pattern.size()))',
    'could not safely map to readable live memory',
    'GetModuleFileNameW',
    'IMAGE_SCN_MEM_EXECUTE',
    'PointerToRawData',
    'live_base + rva',
    'g_pattern_scan_diagnostic',
    'pattern-bytes=',
    'executable-sections=',
    'executable-bytes-scanned='
)) {
    if (-not $updated.Contains($required)) {
        throw "generated disk-backed scanner is missing '$required'"
    }
}

$destinationDirectory = Split-Path -Parent $Destination
[System.IO.Directory]::CreateDirectory($destinationDirectory) | Out-Null
[System.IO.File]::WriteAllText(
    $Destination,
    $updated,
    [System.Text.UTF8Encoding]::new($false))

Write-Host 'Generated disk-backed executable-section scanner with guarded live-memory diagnostics.'

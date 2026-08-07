#include "smg.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" {
#include "lauxlib.h"
#include "lualib.h"
}

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr std::string_view kReset = "\x1b[0m";
constexpr std::string_view kDim = "\x1b[2m";
constexpr std::string_view kBold = "\x1b[1m";
constexpr std::string_view kCyan = "\x1b[96m";
constexpr std::string_view kGreen = "\x1b[92m";
constexpr std::string_view kYellow = "\x1b[93m";
constexpr std::string_view kRed = "\x1b[91m";
constexpr std::string_view kWhite = "\x1b[97m";
constexpr std::string_view kGray = "\x1b[90m";

constexpr std::array<std::string_view, 15> kKnownCs2Modules{
    "cs2.events",     "cs2.timers",   "cs2.players", "cs2.commands",
    "cs2.math",       "cs2.weapons",  "cs2.hud",     "cs2.cvars",
    "cs2.teams",      "cs2.rounds",   "cs2.entities", "cs2.sounds",
    "cs2.properties", "cs2.traces",   "cs2.grenades"};

struct MemoryTracker {
    std::size_t current{};
    std::size_t peak{};
};

struct BytecodeBuffer {
    std::vector<std::uint8_t> bytes;
    bool allocation_failed{};
};

struct CompileReport {
    bool success{};
    bool cached{};
    std::size_t code_size{};
    std::size_t data_size{};
    std::size_t working_memory{};
    std::size_t total_requirements{};
    double elapsed_seconds{};
    std::string output_path;
    std::string error;
    std::vector<std::string> warnings;
};

std::string path_text(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

std::filesystem::path executable_path() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    buffer.resize(length);
    return buffer;
}

bool enable_modern_console() {
    const bool output_utf8 = SetConsoleOutputCP(CP_UTF8) != FALSE;
    const bool input_utf8 = SetConsoleCP(CP_UTF8) != FALSE;
    SetConsoleTitleW(L"LuaCS Compiler");

    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == INVALID_HANDLE_VALUE || output == nullptr) {
        return output_utf8 && input_utf8;
    }

    DWORD mode{};
    if (!GetConsoleMode(output, &mode)) return output_utf8 && input_utf8;
    const bool virtual_terminal =
        SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != FALSE;
    return output_utf8 && input_utf8 && virtual_terminal;
}

bool owns_console() {
    DWORD process_ids[2]{};
    return GetConsoleProcessList(process_ids, 2) == 1;
}

void pause_if_needed(bool pause) {
    if (!pause) return;
    std::cout << "\n" << kDim << "Press Enter to exit ..." << kReset;
    std::cout.flush();
    std::string ignored;
    std::getline(std::cin, ignored);
}

bool read_file(const std::filesystem::path& path,
               std::vector<std::uint8_t>& bytes, std::string& error) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        error = "Could not open the source file.";
        return false;
    }

    const std::streamoff size = stream.tellg();
    if (size < 0) {
        error = "Could not determine the source file size.";
        return false;
    }
    const auto unsigned_size = static_cast<std::uintmax_t>(size);
    if (unsigned_size > std::numeric_limits<std::size_t>::max() ||
        unsigned_size > static_cast<std::uintmax_t>(
                            std::numeric_limits<std::streamsize>::max())) {
        error = "Source file is too large to compile safely.";
        return false;
    }

    bytes.resize(static_cast<std::size_t>(unsigned_size));
    stream.seekg(0);
    if (!stream) {
        error = "Could not seek to the start of the source file.";
        return false;
    }
    if (!bytes.empty() &&
        !stream.read(reinterpret_cast<char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()))) {
        error = "Could not read the source file.";
        return false;
    }
    return true;
}

void* tracking_allocator(void* user_data, void* pointer, std::size_t old_size,
                         std::size_t new_size) {
    auto* tracker = static_cast<MemoryTracker*>(user_data);

    // Lua uses osize as an object-type tag for fresh allocations where ptr is
    // null. It is not an allocated byte count in that case.
    const std::size_t accounted_old_size = pointer ? old_size : 0;

    if (new_size == 0) {
        if (pointer) {
            tracker->current =
                accounted_old_size <= tracker->current
                    ? tracker->current - accounted_old_size
                    : 0;
            std::free(pointer);
        }
        return nullptr;
    }

    void* replacement = std::realloc(pointer, new_size);
    if (!replacement) return nullptr;

    if (accounted_old_size <= tracker->current) {
        tracker->current =
            tracker->current - accounted_old_size + new_size;
    } else {
        // Do not underflow if a foreign/mismatched allocator size is ever
        // observed. The peak remains a diagnostic, not an allocator contract.
        tracker->current = new_size;
    }
    tracker->peak = std::max(tracker->peak, tracker->current);
    return replacement;
}

int bytecode_writer(lua_State*, const void* data, size_t size,
                    void* user_data) {
    auto* output = static_cast<BytecodeBuffer*>(user_data);
    try {
        const auto* begin = static_cast<const std::uint8_t*>(data);
        output->bytes.insert(output->bytes.end(), begin, begin + size);
        return 0;
    } catch (...) {
        output->allocation_failed = true;
        return 1;
    }
}

std::string trim_ascii(std::string value) {
    const auto is_space = [](unsigned char value) {
        return value == ' ' || value == '\t' || value == '\r' ||
               value == '\n' || value == '\f' || value == '\v';
    };
    auto first = std::find_if_not(value.begin(), value.end(), [&](char ch) {
        return is_space(static_cast<unsigned char>(ch));
    });
    auto last = std::find_if_not(value.rbegin(), value.rend(), [&](char ch) {
        return is_space(static_cast<unsigned char>(ch));
    }).base();
    if (first >= last) return {};
    return {first, last};
}

std::vector<std::string> load_deprecated_symbols(
    const std::filesystem::path& path) {
    std::vector<std::string> symbols;
    std::ifstream input(path, std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
        line = trim_ascii(std::move(line));
        if (!line.empty() && line.front() != '#') symbols.push_back(line);
    }
    return symbols;
}

std::vector<std::string> find_warnings(
    std::string_view source, const std::vector<std::string>& symbols) {
    std::vector<std::string> warnings;
    for (const auto& symbol : symbols) {
        std::size_t offset = 0;
        while ((offset = source.find(symbol, offset)) != std::string_view::npos) {
            const auto line = 1 + static_cast<int>(
                std::count(source.begin(), source.begin() + offset, '\n'));
            warnings.push_back("Line " + std::to_string(line) +
                               ": deprecated API name '" + symbol + "'");
            offset += symbol.size();
        }
    }
    return warnings;
}

bool valid_utf8(std::span<const std::uint8_t> bytes, std::size_t& bad_offset) {
    std::size_t index = 0;
    while (index < bytes.size()) {
        const std::uint8_t first = bytes[index];
        if (first <= 0x7F) {
            ++index;
            continue;
        }

        std::size_t continuation_count = 0;
        std::uint32_t codepoint = 0;
        if (first >= 0xC2 && first <= 0xDF) {
            continuation_count = 1;
            codepoint = first & 0x1Fu;
        } else if (first >= 0xE0 && first <= 0xEF) {
            continuation_count = 2;
            codepoint = first & 0x0Fu;
        } else if (first >= 0xF0 && first <= 0xF4) {
            continuation_count = 3;
            codepoint = first & 0x07u;
        } else {
            bad_offset = index;
            return false;
        }
        if (index + continuation_count >= bytes.size()) {
            bad_offset = index;
            return false;
        }

        for (std::size_t part = 1; part <= continuation_count; ++part) {
            const std::uint8_t next = bytes[index + part];
            if ((next & 0xC0u) != 0x80u) {
                bad_offset = index + part;
                return false;
            }
            codepoint = (codepoint << 6u) | (next & 0x3Fu);
        }

        if ((continuation_count == 2 && codepoint < 0x800u) ||
            (continuation_count == 3 && codepoint < 0x10000u) ||
            codepoint > 0x10FFFFu ||
            (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
            bad_offset = index;
            return false;
        }
        index += continuation_count + 1;
    }
    return true;
}

bool is_identifier_start(char value) {
    const unsigned char ch = static_cast<unsigned char>(value);
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
           ch == '_';
}

bool is_identifier_continue(char value) {
    const unsigned char ch = static_cast<unsigned char>(value);
    return is_identifier_start(value) || (ch >= '0' && ch <= '9');
}

void skip_space(std::string_view source, std::size_t& offset) {
    while (offset < source.size()) {
        const char ch = source[offset];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
            ch == '\f' || ch == '\v') {
            ++offset;
            continue;
        }
        break;
    }
}

void skip_quoted(std::string_view source, std::size_t& offset) {
    const char quote = source[offset++];
    while (offset < source.size()) {
        const char ch = source[offset++];
        if (ch == '\\' && offset < source.size()) {
            ++offset;
            continue;
        }
        if (ch == quote) return;
    }
}

void skip_long_bracket(std::string_view source, std::size_t& offset) {
    if (offset + 1 >= source.size() || source[offset] != '[' ||
        source[offset + 1] != '[') {
        return;
    }
    offset += 2;
    const std::size_t end = source.find("]]", offset);
    offset = end == std::string_view::npos ? source.size() : end + 2;
}

bool known_cs2_module(std::string_view module) {
    if (module == "cs2") return true;
    return std::find(kKnownCs2Modules.begin(), kKnownCs2Modules.end(), module) !=
           kKnownCs2Modules.end();
}

std::optional<std::string> validate_literal_requires(std::string_view source) {
    std::size_t offset = 0;
    while (offset < source.size()) {
        if (source[offset] == '-' && offset + 1 < source.size() &&
            source[offset + 1] == '-') {
            offset += 2;
            if (offset + 1 < source.size() && source[offset] == '[' &&
                source[offset + 1] == '[') {
                skip_long_bracket(source, offset);
            } else {
                const std::size_t newline = source.find('\n', offset);
                offset = newline == std::string_view::npos ? source.size()
                                                            : newline + 1;
            }
            continue;
        }
        if (source[offset] == '\'' || source[offset] == '"') {
            skip_quoted(source, offset);
            continue;
        }
        if (offset + 1 < source.size() && source[offset] == '[' &&
            source[offset + 1] == '[') {
            skip_long_bracket(source, offset);
            continue;
        }
        if (!is_identifier_start(source[offset])) {
            ++offset;
            continue;
        }

        const std::size_t begin = offset++;
        while (offset < source.size() &&
               is_identifier_continue(source[offset])) {
            ++offset;
        }
        if (source.substr(begin, offset - begin) != "require") continue;

        std::size_t argument = offset;
        skip_space(source, argument);
        if (argument < source.size() && source[argument] == '(') {
            ++argument;
            skip_space(source, argument);
        }
        if (argument >= source.size() ||
            (source[argument] != '\'' && source[argument] != '"')) {
            continue;
        }

        const char quote = source[argument++];
        const std::size_t value_begin = argument;
        bool escaped = false;
        while (argument < source.size() && source[argument] != quote) {
            if (source[argument] == '\\') {
                escaped = true;
                break;
            }
            ++argument;
        }
        if (escaped || argument >= source.size()) continue;

        const std::string_view module =
            source.substr(value_begin, argument - value_begin);
        if (module.starts_with("cs2.") && !known_cs2_module(module)) {
            const auto line = 1 + static_cast<int>(
                std::count(source.begin(), source.begin() + begin, '\n'));
            return "Line " + std::to_string(line) +
                   ": unknown LuaCS module '" + std::string(module) + "'";
        }
    }
    return std::nullopt;
}

std::optional<std::string> validate_source(
    std::span<const std::uint8_t> source, std::size_t source_offset) {
    const std::span<const std::uint8_t> text_bytes = source.subspan(source_offset);
    std::size_t bad_offset = 0;
    if (!valid_utf8(text_bytes, bad_offset)) {
        return "Source is not valid UTF-8 at byte offset " +
               std::to_string(source_offset + bad_offset) + ".";
    }
    if (std::find(text_bytes.begin(), text_bytes.end(), std::uint8_t{0}) !=
        text_bytes.end()) {
        return "Source contains a NUL byte; LuaCS source files must be text.";
    }

    const char* data = text_bytes.empty()
                           ? ""
                           : reinterpret_cast<const char*>(text_bytes.data());
    return validate_literal_requires(
        std::string_view(data, text_bytes.size()));
}

std::optional<luacs::smg::Package> unchanged_package(
    const std::filesystem::path& output,
    std::span<const std::uint8_t> source,
    std::span<const std::uint8_t, 32> key) {
    std::error_code exists_error;
    if (!std::filesystem::exists(output, exists_error) || exists_error) {
        return std::nullopt;
    }

    luacs::smg::Package package;
    std::string error;
    if (!luacs::smg::read(output, key, package, error)) return std::nullopt;
    if (package.header.source_sha256 != luacs::smg::sha256(source)) {
        return std::nullopt;
    }
    return package;
}

std::string format_bytes(std::size_t bytes) {
    std::ostringstream output;
    output << bytes << " bytes";
    if (bytes >= 1024) {
        output << "  " << kDim << "(" << std::fixed << std::setprecision(2)
               << static_cast<double>(bytes) / 1024.0 << " KiB)" << kReset;
    }
    return output.str();
}

void print_header(const std::filesystem::path& plugins_dir) {
    std::cout
        << "\n" << kCyan << kBold
        << "╭──────────────────────────────────────────────────────────────╮\n"
        << "│                       LuaCS Compiler                         │\n"
        << "╰──────────────────────────────────────────────────────────────╯"
        << kReset << "\n"
        << kDim
        << "  Lua 5.5.1  •  authenticated SMG bytecode  •  incremental build\n"
        << "  Output: " << path_text(plugins_dir) << kReset << "\n\n";
}

void print_report(const std::filesystem::path& source,
                  const CompileReport& report) {
    const auto status_color =
        report.success ? (report.cached ? kCyan : kGreen) : kRed;
    const std::string_view status = report.success
                                        ? (report.cached ? "ALREADY COMPILED"
                                                         : "COMPILED")
                                        : "FAILED";

    std::cout << kGray << "╭─ " << kWhite << kBold
              << path_text(source.filename()) << kReset << kGray
              << "\n│\n│  " << status_color << "● " << status << kReset
              << kGray << "\n";

    if (report.success) {
        std::cout
            << "│\n"
            << "│  " << kDim << "Code size           " << kReset
            << format_bytes(report.code_size) << "\n"
            << kGray << "│  " << kDim << "Data size           " << kReset
            << format_bytes(report.data_size) << "\n"
            << kGray << "│  " << kDim << "Stack/heap size     " << kReset
            << format_bytes(report.working_memory)
            << (report.cached
                    ? "  " + std::string(kDim) +
                          "(cached; no Lua VM created)" + std::string(kReset)
                    : "")
            << "\n"
            << kGray << "│  " << kDim << "Total requirements  " << kReset
            << format_bytes(report.total_requirements) << "\n"
            << kGray << "│\n"
            << "│  " << kDim << "Compilation time    " << kReset
            << std::fixed << std::setprecision(3) << report.elapsed_seconds
            << " sec\n"
            << kGray << "│  " << kDim << "Output              " << kReset
            << report.output_path << "\n";
    } else {
        std::cout << "│\n│  " << kRed << report.error << kReset << "\n";
    }

    for (const auto& warning : report.warnings) {
        std::cout << kGray << "│  " << kYellow << "⚠ " << warning << kReset
                  << "\n";
    }

    std::cout << kGray
              << "│\n╰──────────────────────────────────────────────────────────────"
              << kReset << "\n\n";
}

CompileReport compile_one(
    const std::filesystem::path& source_path,
    const std::filesystem::path& output_path,
    std::span<const std::uint8_t, 32> key,
    const std::vector<std::string>& deprecated_symbols) {
    const auto started = std::chrono::steady_clock::now();
    CompileReport report;
    report.output_path = path_text(output_path);

    std::vector<std::uint8_t> source;
    if (!read_file(source_path, source, report.error)) {
        report.elapsed_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started)
                                     .count();
        return report;
    }
    report.data_size = source.size();

    const bool has_utf8_bom =
        source.size() >= 3 && source[0] == 0xEF && source[1] == 0xBB &&
        source[2] == 0xBF;
    const std::size_t source_offset = has_utf8_bom ? 3 : 0;
    const char* source_data =
        source.size() == source_offset
            ? ""
            : reinterpret_cast<const char*>(source.data() + source_offset);
    const std::string_view source_text(source_data,
                                       source.size() - source_offset);

    report.warnings = find_warnings(source_text, deprecated_symbols);
    if (const auto validation = validate_source(source, source_offset)) {
        report.error = *validation;
        report.elapsed_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started)
                                     .count();
        return report;
    }

    if (auto package = unchanged_package(output_path, source, key)) {
        report.success = true;
        report.cached = true;
        report.code_size = package->bytecode.size();
        report.working_memory = 0;
        std::error_code size_error;
        const auto disk_size = std::filesystem::file_size(output_path, size_error);
        report.total_requirements =
            !size_error && disk_size <= std::numeric_limits<std::size_t>::max()
                ? static_cast<std::size_t>(disk_size)
                : report.code_size + report.data_size;
        report.elapsed_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started)
                                     .count();
        return report;
    }

    MemoryTracker memory;
    lua_State* state = lua_newstate(&tracking_allocator, &memory,
                                    static_cast<unsigned>(GetTickCount()));
    if (!state) {
        report.error = "Could not create the Lua compiler state.";
        report.elapsed_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started)
                                     .count();
        return report;
    }

    const std::string chunk_name = "@" + path_text(source_path);
    const int load_status = luaL_loadbufferx(
        state, source_data, source_text.size(), chunk_name.c_str(), "t");

    if (load_status != LUA_OK) {
        const char* message = lua_tostring(state, -1);
        report.error = message ? message : "Unknown Lua parser error.";
        report.working_memory = memory.peak;
        lua_close(state);
        report.total_requirements =
            report.code_size + report.data_size + report.working_memory;
        report.elapsed_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started)
                                     .count();
        return report;
    }

    BytecodeBuffer bytecode;
    const int dump_status =
        lua_dump(state, bytecode_writer, &bytecode, 1);
    report.working_memory = memory.peak;
    lua_close(state);

    if (dump_status != 0 || bytecode.allocation_failed) {
        report.error = bytecode.allocation_failed
                           ? "Lua bytecode output ran out of memory."
                           : "Lua could not serialize the compiled chunk.";
        report.total_requirements = report.data_size + report.working_memory;
        report.elapsed_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started)
                                     .count();
        return report;
    }

    report.code_size = bytecode.bytes.size();
    report.total_requirements =
        report.code_size + report.data_size + report.working_memory;

    std::string write_error;
    if (!luacs::smg::write(output_path, source, bytecode.bytes, key,
                           write_error)) {
        report.error = write_error;
        report.elapsed_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started)
                                     .count();
        return report;
    }

    report.success = true;
    report.elapsed_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started)
                                 .count();
    return report;
}

void print_summary(std::size_t compiled, std::size_t cached,
                   std::size_t failed, double elapsed) {
    std::cout << kBold << "Build summary" << kReset << "\n"
              << "  " << kGreen << compiled << " compiled" << kReset
              << "  •  " << kCyan << cached << " cached" << kReset
              << "  •  " << (failed ? kRed : kDim) << failed << " failed"
              << kReset << "  •  " << std::fixed << std::setprecision(3)
              << elapsed << " sec total\n";
}

bool lua_extension(const std::filesystem::path& path) {
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t ch) {
                       return ch >= L'A' && ch <= L'Z'
                                  ? static_cast<wchar_t>(ch - L'A' + L'a')
                                  : ch;
                   });
    return extension == L".lua";
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    enable_modern_console();

    bool no_pause = false;
    bool show_help = false;
    std::vector<std::filesystem::path> requested_sources;
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument(argv[index]);
        if (argument == L"--no-pause") {
            no_pause = true;
        } else if (argument == L"--help" || argument == L"-h" ||
                   argument == L"/?") {
            show_help = true;
        } else {
            requested_sources.emplace_back(argv[index]);
        }
    }
    const bool pause = !no_pause && owns_console();

    if (show_help) {
        std::cout
            << "LuaCS Compiler\n\n"
            << "  compile.exe                 Compile every .lua file beside the executable\n"
            << "  compile.exe plugin.lua      Compile one or more specific files\n"
            << "  compile.exe --no-pause      Never wait for Enter before exiting\n"
            << "\nCompiler errors include source/filesystem failures, invalid UTF-8, unknown literal cs2.* modules, Lua parser errors, bytecode serialization failures, and authenticated SMG write failures.\n";
        pause_if_needed(pause);
        return 0;
    }

    const auto exe = executable_path();
    if (exe.empty()) {
        std::cerr << kRed
                  << "LuaCS compiler could not locate its executable directory."
                  << kReset << "\n";
        pause_if_needed(pause);
        return 2;
    }

    const auto scripting_dir = exe.parent_path();
    const auto root = scripting_dir.parent_path();
    const auto plugins_dir = root / "plugins";
    const auto config_dir = root / "config";
    const auto gamedata_dir = root / "gamedata";

    std::error_code filesystem_error;
    std::filesystem::create_directories(plugins_dir, filesystem_error);
    if (filesystem_error) {
        std::cerr << kRed << "Could not create the LuaCS plugins directory: "
                  << filesystem_error.message() << kReset << "\n";
        pause_if_needed(pause);
        return 2;
    }
    filesystem_error.clear();
    std::filesystem::create_directories(config_dir, filesystem_error);
    if (filesystem_error) {
        std::cerr << kRed << "Could not create the LuaCS config directory: "
                  << filesystem_error.message() << kReset << "\n";
        pause_if_needed(pause);
        return 2;
    }

    print_header(plugins_dir);

    std::array<std::uint8_t, 32> key{};
    std::string key_error;
    if (!luacs::smg::load_or_create_key(config_dir / "luacs.key", true, key,
                                        key_error)) {
        std::cerr << kRed << "Encryption key error: " << key_error << kReset
                  << "\n";
        pause_if_needed(pause);
        return 2;
    }

    std::vector<std::filesystem::path> sources;
    std::size_t preflight_failures = 0;
    if (!requested_sources.empty()) {
        for (const auto& requested : requested_sources) {
            std::error_code path_error;
            const auto path = std::filesystem::absolute(requested, path_error);
            bool readable = !path_error && lua_extension(path);
            if (readable) {
                std::error_code regular_error;
                readable = std::filesystem::is_regular_file(path, regular_error) &&
                           !regular_error;
            }
            if (readable) {
                sources.push_back(path);
            } else {
                CompileReport report;
                report.error = path_error
                                   ? "Could not resolve the requested path: " +
                                         path_error.message()
                                   : "Not a readable .lua file.";
                print_report(requested, report);
                ++preflight_failures;
            }
        }
    } else {
        std::error_code iterator_error;
        std::filesystem::directory_iterator iterator(scripting_dir,
                                                       iterator_error);
        const std::filesystem::directory_iterator end;
        for (; iterator != end && !iterator_error;
             iterator.increment(iterator_error)) {
            std::error_code entry_error;
            if (iterator->is_regular_file(entry_error) && !entry_error &&
                lua_extension(iterator->path())) {
                sources.push_back(iterator->path());
            }
        }
        if (iterator_error) {
            std::cerr << kRed << "Could not enumerate Lua source directory: "
                      << iterator_error.message() << kReset << "\n";
            pause_if_needed(pause);
            return 2;
        }
    }

    std::sort(sources.begin(), sources.end());
    sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
    if (sources.empty()) {
        if (preflight_failures != 0) {
            print_summary(0, 0, preflight_failures, 0.0);
            pause_if_needed(pause);
            return 1;
        }
        std::cout << kYellow << "No Lua source files were found.\n"
                  << kReset << kDim
                  << "Put .lua files beside compile.exe or drag them onto the executable."
                  << kReset << "\n";
        pause_if_needed(pause);
        return 0;
    }

    const auto deprecated =
        load_deprecated_symbols(gamedata_dir / "deprecated_symbols.txt");
    const auto all_started = std::chrono::steady_clock::now();
    std::size_t compiled = 0;
    std::size_t cached = 0;
    std::size_t failed = preflight_failures;

    for (const auto& source : sources) {
        const auto output = plugins_dir / (source.stem().wstring() + L".smg");
        const auto report = compile_one(source, output, key, deprecated);
        print_report(source, report);
        if (!report.success) {
            ++failed;
        } else if (report.cached) {
            ++cached;
        } else {
            ++compiled;
        }
    }

    const double total_elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - all_started)
                                     .count();
    print_summary(compiled, cached, failed, total_elapsed);
    pause_if_needed(pause);
    return failed == 0 ? 0 : 1;
}

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
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
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

struct MemoryTracker {
    std::size_t current{};
    std::size_t peak{};
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

std::filesystem::path executable_path() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    buffer.resize(length);
    return buffer;
}

bool enable_modern_console() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleTitleW(L"LuaCS Compiler");

    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == INVALID_HANDLE_VALUE || output == nullptr) return false;

    DWORD mode{};
    if (!GetConsoleMode(output, &mode)) return false;
    return SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != FALSE;
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

bool read_file(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes,
               std::string& error) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        error = "Could not open the source file.";
        return false;
    }

    const auto size = stream.tellg();
    if (size < 0) {
        error = "Could not determine the source file size.";
        return false;
    }

    bytes.resize(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!bytes.empty() &&
        !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
        error = "Could not read the source file.";
        return false;
    }
    return true;
}

void* tracking_allocator(void* user_data, void* pointer, std::size_t old_size,
                         std::size_t new_size) {
    auto* tracker = static_cast<MemoryTracker*>(user_data);

    if (new_size == 0) {
        if (pointer) {
            tracker->current = old_size <= tracker->current ? tracker->current - old_size : 0;
            std::free(pointer);
        }
        return nullptr;
    }

    void* replacement = std::realloc(pointer, new_size);
    if (!replacement) return nullptr;

    tracker->current = old_size <= tracker->current
        ? tracker->current - old_size + new_size
        : new_size;
    tracker->peak = std::max(tracker->peak, tracker->current);
    return replacement;
}

int bytecode_writer(lua_State*, const void* data, size_t size, void* user_data) {
    auto* output = static_cast<std::vector<std::uint8_t>*>(user_data);
    const auto* begin = static_cast<const std::uint8_t*>(data);
    output->insert(output->end(), begin, begin + size);
    return 0;
}

std::vector<std::string> load_deprecated_symbols(const std::filesystem::path& path) {
    std::vector<std::string> symbols;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.front() != '#') symbols.push_back(line);
    }
    return symbols;
}

std::vector<std::string> find_warnings(std::string_view source,
                                       const std::vector<std::string>& symbols) {
    std::vector<std::string> warnings;
    for (const auto& symbol : symbols) {
        std::size_t offset = 0;
        while ((offset = source.find(symbol, offset)) != std::string_view::npos) {
            const auto line =
                1 + static_cast<int>(std::count(source.begin(), source.begin() + offset, '\n'));
            warnings.push_back("Line " + std::to_string(line) +
                               ": deprecated API name '" + symbol + "'");
            offset += symbol.size();
        }
    }
    return warnings;
}

std::optional<luacs::smg::Package> unchanged_package(
    const std::filesystem::path& output, std::span<const std::uint8_t> source,
    std::span<const std::uint8_t, 32> key) {
    if (!std::filesystem::exists(output)) return std::nullopt;

    luacs::smg::Package package;
    std::string error;
    if (!luacs::smg::read(output, key, package, error)) return std::nullopt;
    if (package.header.source_sha256 != luacs::smg::sha256(source)) return std::nullopt;
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
        << kDim << "  Lua 5.5.1  •  authenticated SMG bytecode  •  incremental build\n"
        << "  Output: " << plugins_dir.string() << kReset << "\n\n";
}

void print_report(const std::filesystem::path& source, const CompileReport& report) {
    const auto status_color = report.success ? (report.cached ? kCyan : kGreen) : kRed;
    const std::string_view status = report.success
        ? (report.cached ? "ALREADY COMPILED" : "COMPILED")
        : "FAILED";

    std::cout << kGray
              << "╭─ " << kWhite << kBold << source.filename().string() << kReset << kGray
              << "\n│\n│  " << status_color << "● " << status << kReset << kGray << "\n";

    if (report.success) {
        std::cout
            << "│\n"
            << "│  " << kDim << "Code size           " << kReset
            << format_bytes(report.code_size) << "\n"
            << kGray << "│  " << kDim << "Data size           " << kReset
            << format_bytes(report.data_size) << "\n"
            << kGray << "│  " << kDim << "Stack/heap size     " << kReset
            << format_bytes(report.working_memory)
            << (report.cached ? "  " + std::string(kDim) + "(cached; no Lua VM created)" +
                                    std::string(kReset)
                              : "")
            << "\n"
            << kGray << "│  " << kDim << "Total requirements  " << kReset
            << format_bytes(report.total_requirements) << "\n"
            << kGray << "│\n"
            << "│  " << kDim << "Compilation time    " << kReset
            << std::fixed << std::setprecision(3) << report.elapsed_seconds << " sec\n"
            << kGray << "│  " << kDim << "Output              " << kReset
            << report.output_path << "\n";
    } else {
        std::cout << "│\n│  " << kRed << report.error << kReset << "\n";
    }

    for (const auto& warning : report.warnings) {
        std::cout << kGray << "│  " << kYellow << "⚠ " << warning << kReset << "\n";
    }

    std::cout << kGray
              << "│\n╰──────────────────────────────────────────────────────────────"
              << kReset << "\n\n";
}

CompileReport compile_one(const std::filesystem::path& source_path,
                          const std::filesystem::path& output_path,
                          std::span<const std::uint8_t, 32> key,
                          const std::vector<std::string>& deprecated_symbols) {
    const auto started = std::chrono::steady_clock::now();
    CompileReport report;
    report.output_path = output_path.string();

    std::vector<std::uint8_t> source;
    if (!read_file(source_path, source, report.error)) {
        report.elapsed_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        return report;
    }
    report.data_size = source.size();

    if (auto package = unchanged_package(output_path, source, key)) {
        report.success = true;
        report.cached = true;
        report.code_size = package->bytecode.size();
        report.working_memory = 0;
        report.total_requirements =
            std::filesystem::exists(output_path)
                ? static_cast<std::size_t>(std::filesystem::file_size(output_path))
                : report.code_size + report.data_size;
        report.elapsed_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        return report;
    }

    MemoryTracker memory;
    lua_State* state = lua_newstate(
        &tracking_allocator, &memory, static_cast<unsigned>(GetTickCount()));
    if (!state) {
        report.error = "Could not create the Lua compiler state.";
        report.elapsed_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        return report;
    }

    const bool has_utf8_bom =
        source.size() >= 3 && source[0] == 0xEF && source[1] == 0xBB && source[2] == 0xBF;
    const std::size_t source_offset = has_utf8_bom ? 3 : 0;
    const std::string chunk_name = "@" + source_path.string();
    const int load_status = luaL_loadbufferx(
        state, reinterpret_cast<const char*>(source.data() + source_offset),
        source.size() - source_offset, chunk_name.c_str(), "t");

    if (load_status != LUA_OK) {
        const char* message = lua_tostring(state, -1);
        report.error = message ? message : "Unknown Lua parser error.";
        report.working_memory = memory.peak;
        lua_close(state);
        report.total_requirements =
            report.code_size + report.data_size + report.working_memory;
        report.elapsed_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        return report;
    }

    std::vector<std::uint8_t> bytecode;
    const int dump_status = lua_dump(state, bytecode_writer, &bytecode, 1);
    report.working_memory = memory.peak;
    lua_close(state);

    if (dump_status != 0) {
        report.error = "Lua could not serialize the compiled chunk.";
        report.total_requirements = report.data_size + report.working_memory;
        report.elapsed_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        return report;
    }

    report.code_size = bytecode.size();
    report.total_requirements =
        report.code_size + report.data_size + report.working_memory;
    report.warnings = find_warnings(
        std::string_view(reinterpret_cast<const char*>(source.data() + source_offset),
                         source.size() - source_offset),
        deprecated_symbols);

    std::string write_error;
    if (!luacs::smg::write(output_path, source, bytecode, key, write_error)) {
        report.error = write_error;
        report.elapsed_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        return report;
    }

    report.success = true;
    report.elapsed_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return report;
}

void print_summary(std::size_t compiled, std::size_t cached, std::size_t failed,
                   double elapsed) {
    std::cout << kBold << "Build summary" << kReset << "\n"
              << "  " << kGreen << compiled << " compiled" << kReset
              << "  •  " << kCyan << cached << " cached" << kReset
              << "  •  " << (failed ? kRed : kDim) << failed << " failed" << kReset
              << "  •  " << std::fixed << std::setprecision(3) << elapsed << " sec total\n";
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
        } else if (argument == L"--help" || argument == L"-h" || argument == L"/?") {
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
            << "  compile.exe --no-pause      Never wait for Enter before exiting\n";
        pause_if_needed(pause);
        return 0;
    }

    const auto exe = executable_path();
    if (exe.empty()) {
        std::cerr << kRed << "LuaCS compiler could not locate its executable directory."
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
    std::filesystem::create_directories(config_dir, filesystem_error);
    if (filesystem_error) {
        std::cerr << kRed << "Could not create the LuaCS output directories: "
                  << filesystem_error.message() << kReset << "\n";
        pause_if_needed(pause);
        return 2;
    }

    print_header(plugins_dir);

    std::array<std::uint8_t, 32> key{};
    std::string key_error;
    if (!luacs::smg::load_or_create_key(config_dir / "luacs.key", true, key, key_error)) {
        std::cerr << kRed << "Encryption key error: " << key_error << kReset << "\n";
        pause_if_needed(pause);
        return 2;
    }

    std::vector<std::filesystem::path> sources;
    if (!requested_sources.empty()) {
        for (const auto& requested : requested_sources) {
            const auto path = std::filesystem::absolute(requested);
            if (path.extension() == ".lua" && std::filesystem::is_regular_file(path)) {
                sources.push_back(path);
            } else {
                CompileReport report;
                report.error = "Not a readable .lua file.";
                print_report(requested, report);
            }
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(scripting_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".lua") {
                sources.push_back(entry.path());
            }
        }
    }

    std::sort(sources.begin(), sources.end());
    sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
    if (sources.empty()) {
        std::cout << kYellow
                  << "No Lua source files were found.\n"
                  << kReset << kDim
                  << "Put .lua files beside compile.exe or drag them onto the executable."
                  << kReset << "\n";
        pause_if_needed(pause);
        return 0;
    }

    const auto deprecated = load_deprecated_symbols(gamedata_dir / "deprecated_symbols.txt");
    const auto all_started = std::chrono::steady_clock::now();
    std::size_t compiled = 0;
    std::size_t cached = 0;
    std::size_t failed = 0;

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

    const double total_elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - all_started).count();
    print_summary(compiled, cached, failed, total_elapsed);
    pause_if_needed(pause);
    return failed == 0 ? 0 : 1;
}

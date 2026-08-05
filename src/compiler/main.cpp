#include "smg.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" {
#include "lauxlib.h"
#include "lualib.h"
}

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path executable_path() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    buffer.resize(length);
    return buffer;
}

bool read_file(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes, std::string& error) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        error = "could not open source file";
        return false;
    }
    const auto size = stream.tellg();
    if (size < 0) {
        error = "could not determine source file size";
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
        error = "could not read source file";
        return false;
    }
    return true;
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

void print_warnings(std::string_view source, const std::vector<std::string>& symbols) {
    for (const auto& symbol : symbols) {
        std::size_t offset = 0;
        while ((offset = source.find(symbol, offset)) != std::string_view::npos) {
            const auto line = 1 + static_cast<int>(std::count(source.begin(), source.begin() + offset, '\n'));
            std::cout << "  warning L" << line << ": deprecated API name '" << symbol << "'\n";
            offset += symbol.size();
        }
    }
}

bool source_unchanged(const std::filesystem::path& output,
                      std::span<const std::uint8_t> source,
                      std::span<const std::uint8_t, 32> key) {
    if (!std::filesystem::exists(output)) return false;
    luacs::smg::Package package;
    std::string error;
    if (!luacs::smg::read(output, key, package, error)) return false;
    return package.header.source_sha256 == luacs::smg::sha256(source);
}

bool compile_one(const std::filesystem::path& source_path,
                 const std::filesystem::path& output_path,
                 std::span<const std::uint8_t, 32> key,
                 const std::vector<std::string>& deprecated_symbols) {
    std::vector<std::uint8_t> source;
    std::string error;
    if (!read_file(source_path, source, error)) {
        std::cerr << "[ERROR] " << source_path.filename().string() << ": " << error << "\n";
        return false;
    }

    std::cout << "[CHECK] " << source_path.filename().string() << "\n";
    if (source_unchanged(output_path, source, key)) {
        std::cout << "  unchanged; skipped\n";
        return true;
    }

    lua_State* state = luaL_newstate();
    if (!state) {
        std::cerr << "  error: could not create Lua compiler state\n";
        return false;
    }

    const std::string chunk_name = "@" + source_path.string();
    const int load_status = luaL_loadbufferx(state,
                                             reinterpret_cast<const char*>(source.data()),
                                             source.size(), chunk_name.c_str(), "t");
    if (load_status != LUA_OK) {
        const char* message = lua_tostring(state, -1);
        std::cerr << "  syntax error: " << (message ? message : "unknown Lua parser error") << "\n";
        lua_close(state);
        return false;
    }

    std::vector<std::uint8_t> bytecode;
    const int dump_status = lua_dump(state, bytecode_writer, &bytecode, 1);
    lua_close(state);
    if (dump_status != 0) {
        std::cerr << "  error: Lua could not serialize the compiled chunk\n";
        return false;
    }

    print_warnings(std::string_view(reinterpret_cast<const char*>(source.data()), source.size()),
                   deprecated_symbols);

    if (!luacs::smg::write(output_path, source, bytecode, key, error)) {
        std::cerr << "  error: " << error << "\n";
        return false;
    }
    std::cout << "  compiled -> " << output_path.string() << "\n";
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const auto exe = executable_path();
    if (exe.empty()) {
        std::cerr << "LuaCS compiler could not locate its executable directory.\n";
        return 2;
    }

    const auto scripting_dir = exe.parent_path();
    const auto root = scripting_dir.parent_path();
    const auto plugins_dir = root / "plugins";
    const auto config_dir = root / "config";
    const auto gamedata_dir = root / "gamedata";
    std::filesystem::create_directories(plugins_dir);
    std::filesystem::create_directories(config_dir);

    std::array<std::uint8_t, 32> key{};
    std::string error;
    if (!luacs::smg::load_or_create_key(config_dir / "luacs.key", true, key, error)) {
        std::cerr << "LuaCS compiler: " << error << "\n";
        return 2;
    }

    std::vector<std::filesystem::path> sources;
    if (argc > 1) {
        for (int index = 1; index < argc; ++index) {
            std::filesystem::path path(argv[index]);
            if (path.extension() == ".lua" && std::filesystem::is_regular_file(path)) {
                sources.push_back(std::filesystem::absolute(path));
            } else {
                std::cerr << "[SKIP] Not a readable .lua file: " << path.string() << "\n";
            }
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(scripting_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".lua") sources.push_back(entry.path());
        }
    }

    std::sort(sources.begin(), sources.end());
    sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
    if (sources.empty()) {
        std::cout << "No .lua files were found. Put single-file plugins beside compile.exe or drag files onto it.\n";
        return 0;
    }

    const auto deprecated = load_deprecated_symbols(gamedata_dir / "deprecated_symbols.txt");
    int failed = 0;
    for (const auto& source : sources) {
        const auto output = plugins_dir / (source.stem().wstring() + L".smg");
        if (!compile_one(source, output, key, deprecated)) ++failed;
    }

    std::cout << "\nFinished: " << (sources.size() - failed) << " succeeded, " << failed << " failed.\n";
    return failed == 0 ? 0 : 1;
}

#include "smg.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <system_error>

#pragma comment(lib, "bcrypt.lib")

namespace luacs::smg {
namespace {

constexpr std::array<char, 8> kMagic{'L', 'U', 'A', 'C', 'S', 'M', 'G', '\0'};

struct AlgHandle {
    BCRYPT_ALG_HANDLE value{};
    ~AlgHandle() {
        if (value) BCryptCloseAlgorithmProvider(value, 0);
    }
};

struct KeyHandle {
    BCRYPT_KEY_HANDLE value{};
    ~KeyHandle() {
        if (value) BCryptDestroyKey(value);
    }
};

struct HashHandle {
    BCRYPT_HASH_HANDLE value{};
    ~HashHandle() {
        if (value) BCryptDestroyHash(value);
    }
};

std::string path_text(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

std::string nt_error(const char* operation, NTSTATUS status) {
    char buffer[160]{};
    std::snprintf(buffer, sizeof(buffer), "%s failed with NTSTATUS 0x%08lX",
                  operation, static_cast<unsigned long>(status));
    return buffer;
}

bool open_aes(AlgHandle& algorithm, std::string& error) {
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm.value,
                                                   BCRYPT_AES_ALGORITHM,
                                                   nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_error("BCryptOpenAlgorithmProvider(AES)", status);
        return false;
    }
    status = BCryptSetProperty(
        algorithm.value, BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
        static_cast<ULONG>((std::wcslen(BCRYPT_CHAIN_MODE_GCM) + 1) *
                           sizeof(wchar_t)),
        0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_error("BCryptSetProperty(GCM)", status);
        return false;
    }
    return true;
}

bool make_key(BCRYPT_ALG_HANDLE algorithm,
              std::span<const std::uint8_t, 32> bytes, KeyHandle& key,
              std::vector<std::uint8_t>& object, std::string& error) {
    ULONG object_size = 0;
    ULONG received = 0;
    NTSTATUS status = BCryptGetProperty(
        algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &received,
        0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_error("BCryptGetProperty(BCRYPT_OBJECT_LENGTH)", status);
        return false;
    }
    if (received != sizeof(object_size) || object_size == 0) {
        error = "BCrypt returned an invalid AES key-object size";
        return false;
    }

    object.resize(object_size);
    status = BCryptGenerateSymmetricKey(
        algorithm, &key.value, object.data(), object_size,
        const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_error("BCryptGenerateSymmetricKey", status);
        return false;
    }
    return true;
}

std::vector<std::uint8_t> authenticated_header(const Header& header) {
    Header copy = header;
    copy.authentication_tag.fill(0);
    const auto* begin = reinterpret_cast<const std::uint8_t*>(&copy);
    return {begin, begin + sizeof(copy)};
}

bool hash_sha256(std::span<const std::uint8_t> data,
                 std::array<std::uint8_t, 32>& digest,
                 std::string& error) {
    digest.fill(0);

    AlgHandle algorithm;
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm.value, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_error("BCryptOpenAlgorithmProvider(SHA256)", status);
        return false;
    }

    ULONG object_size = 0;
    ULONG received = 0;
    status = BCryptGetProperty(
        algorithm.value, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &received,
        0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_error("BCryptGetProperty(SHA256 object length)", status);
        return false;
    }
    if (received != sizeof(object_size) || object_size == 0) {
        error = "BCrypt returned an invalid SHA-256 hash-object size";
        return false;
    }

    std::vector<std::uint8_t> object(object_size);
    HashHandle hash;
    status = BCryptCreateHash(algorithm.value, &hash.value, object.data(),
                              object_size, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_error("BCryptCreateHash", status);
        return false;
    }

    std::size_t offset = 0;
    while (offset < data.size()) {
        const std::size_t remaining = data.size() - offset;
        const ULONG chunk = static_cast<ULONG>(std::min<std::size_t>(
            remaining, std::numeric_limits<ULONG>::max()));
        status = BCryptHashData(
            hash.value, const_cast<PUCHAR>(data.data() + offset), chunk, 0);
        if (!BCRYPT_SUCCESS(status)) {
            error = nt_error("BCryptHashData", status);
            return false;
        }
        offset += chunk;
    }

    status = BCryptFinishHash(hash.value, digest.data(),
                              static_cast<ULONG>(digest.size()), 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_error("BCryptFinishHash", status);
        digest.fill(0);
        return false;
    }
    return true;
}

bool crypt(bool encrypt, std::span<const std::uint8_t> input,
           std::span<const std::uint8_t, 32> key_bytes, Header& header,
           std::vector<std::uint8_t>& output, std::string& error) {
    if (input.size() > std::numeric_limits<ULONG>::max()) {
        error = "SMG payload is too large for one Windows CNG AES-GCM operation";
        return false;
    }

    AlgHandle algorithm;
    if (!open_aes(algorithm, error)) return false;

    KeyHandle key;
    std::vector<std::uint8_t> key_object;
    if (!make_key(algorithm.value, key_bytes, key, key_object, error)) {
        return false;
    }

    auto aad = authenticated_header(header);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth_info;
    BCRYPT_INIT_AUTH_MODE_INFO(auth_info);
    auth_info.pbNonce = header.nonce.data();
    auth_info.cbNonce = static_cast<ULONG>(header.nonce.size());
    auth_info.pbAuthData = aad.data();
    auth_info.cbAuthData = static_cast<ULONG>(aad.size());
    auth_info.pbTag = header.authentication_tag.data();
    auth_info.cbTag = static_cast<ULONG>(header.authentication_tag.size());

    output.resize(input.size());
    ULONG written = 0;
    PUCHAR input_data = input.empty()
                            ? nullptr
                            : const_cast<PUCHAR>(input.data());
    PUCHAR output_data = output.empty() ? nullptr : output.data();
    const NTSTATUS status = encrypt
        ? BCryptEncrypt(key.value, input_data, static_cast<ULONG>(input.size()),
                        &auth_info, nullptr, 0, output_data,
                        static_cast<ULONG>(output.size()), &written, 0)
        : BCryptDecrypt(key.value, input_data, static_cast<ULONG>(input.size()),
                        &auth_info, nullptr, 0, output_data,
                        static_cast<ULONG>(output.size()), &written, 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_error(encrypt ? "BCryptEncrypt" : "BCryptDecrypt", status);
        output.clear();
        return false;
    }
    if (written > output.size()) {
        error = "Windows CNG reported an impossible SMG output size";
        output.clear();
        return false;
    }
    output.resize(written);
    return true;
}

bool read_all(const std::filesystem::path& path,
              std::vector<std::uint8_t>& bytes, std::string& error) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        error = "Could not open " + path_text(path);
        return false;
    }
    const std::streamoff size = stream.tellg();
    if (size < 0) {
        error = "Could not determine the size of " + path_text(path);
        return false;
    }
    const auto unsigned_size = static_cast<std::uintmax_t>(size);
    if (unsigned_size > std::numeric_limits<std::size_t>::max() ||
        unsigned_size > static_cast<std::uintmax_t>(
                            std::numeric_limits<std::streamsize>::max())) {
        error = "File is too large to read safely: " + path_text(path);
        return false;
    }

    bytes.resize(static_cast<std::size_t>(unsigned_size));
    stream.seekg(0);
    if (!stream) {
        error = "Could not seek to the start of " + path_text(path);
        return false;
    }
    if (!bytes.empty() &&
        !stream.read(reinterpret_cast<char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()))) {
        error = "Could not read " + path_text(path);
        return false;
    }
    return true;
}

bool replace_file(const std::filesystem::path& temporary,
                  const std::filesystem::path& destination,
                  std::string& error) {
    if (MoveFileExW(temporary.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    const DWORD code = GetLastError();
    std::error_code cleanup_error;
    std::filesystem::remove(temporary, cleanup_error);
    error = "Could not replace " + path_text(destination) +
            " (Win32 error " + std::to_string(code) + ")";
    return false;
}

} // namespace

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data) {
    std::array<std::uint8_t, 32> digest{};
    std::string ignored;
    if (!hash_sha256(data, digest, ignored)) digest.fill(0);
    return digest;
}

bool read_header(const std::filesystem::path& path, Header& header,
                 std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream ||
        !stream.read(reinterpret_cast<char*>(&header), sizeof(header))) {
        error = "Could not read SMG header from " + path_text(path);
        return false;
    }
    if (header.magic != kMagic || header.version != kVersion ||
        header.flags != kFlagAes256Gcm) {
        error = "Unsupported or invalid SMG file: " + path_text(path);
        return false;
    }
    return true;
}

bool write(const std::filesystem::path& path,
           std::span<const std::uint8_t> source,
           std::span<const std::uint8_t> bytecode,
           std::span<const std::uint8_t, 32> key, std::string& error) {
    Header header{};
    header.magic = kMagic;
    header.version = kVersion;
    header.flags = kFlagAes256Gcm;
    if (!hash_sha256(source, header.source_sha256, error)) return false;
    header.plain_size = bytecode.size();
    header.cipher_size = bytecode.size();

    const NTSTATUS random_status = BCryptGenRandom(
        nullptr, header.nonce.data(), static_cast<ULONG>(header.nonce.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(random_status)) {
        error = nt_error("BCryptGenRandom", random_status);
        return false;
    }

    std::vector<std::uint8_t> encrypted;
    if (!crypt(true, bytecode, key, header, encrypted, error)) return false;
    header.cipher_size = encrypted.size();

    std::error_code directory_error;
    std::filesystem::create_directories(path.parent_path(), directory_error);
    if (directory_error) {
        error = "Could not create SMG output directory: " +
                directory_error.message();
        return false;
    }

    const std::filesystem::path temporary = path.wstring() + L".tmp";
    {
        std::ofstream stream(temporary,
                             std::ios::binary | std::ios::trunc);
        if (!stream ||
            !stream.write(reinterpret_cast<const char*>(&header),
                          sizeof(header)) ||
            (!encrypted.empty() &&
             !stream.write(reinterpret_cast<const char*>(encrypted.data()),
                           static_cast<std::streamsize>(encrypted.size()))) ||
            !stream.flush()) {
            error = "Could not write " + path_text(path);
            stream.close();
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
            return false;
        }
    }
    return replace_file(temporary, path, error);
}

bool read(const std::filesystem::path& path,
          std::span<const std::uint8_t, 32> key, Package& package,
          std::string& error) {
    package = {};
    std::vector<std::uint8_t> bytes;
    if (!read_all(path, bytes, error)) return false;
    if (bytes.size() < sizeof(Header)) {
        error = "SMG file is truncated: " + path_text(path);
        return false;
    }
    std::memcpy(&package.header, bytes.data(), sizeof(Header));
    if (package.header.magic != kMagic || package.header.version != kVersion ||
        package.header.flags != kFlagAes256Gcm) {
        error = "Unsupported or invalid SMG file: " + path_text(path);
        return false;
    }
    if (package.header.cipher_size != bytes.size() - sizeof(Header)) {
        error = "SMG payload size does not match its header: " + path_text(path);
        return false;
    }
    if (package.header.plain_size > std::numeric_limits<ULONG>::max() ||
        package.header.cipher_size > std::numeric_limits<ULONG>::max()) {
        error = "SMG payload exceeds the supported Windows CNG size";
        return false;
    }

    const std::span<const std::uint8_t> encrypted(
        bytes.data() + sizeof(Header), bytes.size() - sizeof(Header));
    if (!crypt(false, encrypted, key, package.header, package.bytecode, error)) {
        return false;
    }
    if (package.bytecode.size() != package.header.plain_size) {
        error = "SMG decrypted size does not match its header: " + path_text(path);
        package.bytecode.clear();
        return false;
    }
    return true;
}

bool load_or_create_key(const std::filesystem::path& path,
                        bool create_if_missing,
                        std::array<std::uint8_t, 32>& key,
                        std::string& error) {
    std::error_code exists_error;
    const bool exists = std::filesystem::exists(path, exists_error);
    if (exists_error) {
        error = "Could not inspect LuaCS key path: " + exists_error.message();
        return false;
    }

    if (exists) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            error = "Could not open LuaCS key: " + path_text(path);
            return false;
        }
        input.read(reinterpret_cast<char*>(key.data()),
                   static_cast<std::streamsize>(key.size()));
        if (input.gcount() != static_cast<std::streamsize>(key.size()) ||
            input.peek() != EOF) {
            error = "LuaCS key must be exactly 32 bytes: " + path_text(path);
            return false;
        }
        return true;
    }

    if (!create_if_missing) {
        error = "LuaCS key is missing. Run scripting\\compile.exe first: " +
                path_text(path);
        return false;
    }

    std::error_code directory_error;
    std::filesystem::create_directories(path.parent_path(), directory_error);
    if (directory_error) {
        error = "Could not create LuaCS key directory: " +
                directory_error.message();
        return false;
    }

    const NTSTATUS status = BCryptGenRandom(
        nullptr, key.data(), static_cast<ULONG>(key.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_error("BCryptGenRandom(key)", status);
        return false;
    }

    const std::filesystem::path temporary = path.wstring() + L".tmp";
    {
        std::ofstream output(temporary,
                             std::ios::binary | std::ios::trunc);
        if (!output ||
            !output.write(reinterpret_cast<const char*>(key.data()),
                          static_cast<std::streamsize>(key.size())) ||
            !output.flush()) {
            error = "Could not create LuaCS key: " + path_text(path);
            output.close();
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
            key.fill(0);
            return false;
        }
    }
    if (!replace_file(temporary, path, error)) {
        key.fill(0);
        return false;
    }
    return true;
}

} // namespace luacs::smg

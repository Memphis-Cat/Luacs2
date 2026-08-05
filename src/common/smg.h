#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace luacs::smg {

inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::uint32_t kFlagAes256Gcm = 1;

#pragma pack(push, 1)
struct Header {
    std::array<char, 8> magic{};
    std::uint32_t version{};
    std::uint32_t flags{};
    std::array<std::uint8_t, 32> source_sha256{};
    std::array<std::uint8_t, 12> nonce{};
    std::array<std::uint8_t, 16> authentication_tag{};
    std::uint64_t plain_size{};
    std::uint64_t cipher_size{};
};
#pragma pack(pop)

struct Package {
    Header header{};
    std::vector<std::uint8_t> bytecode;
};

[[nodiscard]] std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data);
[[nodiscard]] bool read_header(const std::filesystem::path& path, Header& header, std::string& error);
[[nodiscard]] bool write(const std::filesystem::path& path,
                         std::span<const std::uint8_t> source,
                         std::span<const std::uint8_t> bytecode,
                         std::span<const std::uint8_t, 32> key,
                         std::string& error);
[[nodiscard]] bool read(const std::filesystem::path& path,
                        std::span<const std::uint8_t, 32> key,
                        Package& package,
                        std::string& error);
[[nodiscard]] bool load_or_create_key(const std::filesystem::path& path,
                                      bool create_if_missing,
                                      std::array<std::uint8_t, 32>& key,
                                      std::string& error);

} // namespace luacs::smg

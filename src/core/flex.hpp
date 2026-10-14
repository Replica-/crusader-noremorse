#pragma once
// Origin "Flex" archive (.FLX), as read by FILE\FLEX.C in the original.
//
// Layout (little-endian):
//   0x00..0x51  comment padded with 0x1A
//   0x54        u32 entry count
//   0x80        index: count * { u32 offset, u32 size }, offset 0 = empty entry

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace crusader {

class FlexArchive {
public:
    static FlexArchive open(const std::filesystem::path& path);

    std::size_t count() const { return index_.size(); }
    std::span<const std::uint8_t> entry(std::size_t i) const;
    const std::string& comment() const { return comment_; }

private:
    struct Slot {
        std::uint32_t offset;
        std::uint32_t size;
    };
    std::vector<std::uint8_t> data_;
    std::vector<Slot> index_;
    std::string comment_;
};

std::vector<std::uint8_t> read_file(const std::filesystem::path& path);

}  // namespace crusader

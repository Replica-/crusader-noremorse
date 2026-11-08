#pragma once
// Minimal PNG reader (its own inflate, no zlib needed): 8-bit greyscale, RGB, palette and RGBA pictures, not
// interlaced. Reads the upscaled sprites (assets/sprites, the "UPSCALED SPRITES" option).

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace crusader {

struct RgbaImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;  // 0xAARRGGBB, row by row
};

// nullopt when the file is missing or not a PNG this reader knows.
std::optional<RgbaImage> read_png(const std::filesystem::path& path);
std::optional<RgbaImage> decode_png(std::span<const std::uint8_t> data);

}  // namespace crusader

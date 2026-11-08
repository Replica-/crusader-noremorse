#include "core/png_reader.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string_view>

namespace crusader {

namespace {

// Inflate (RFC 1951) after "puff" (zlib's contrib): the stream read bit by bit, Huffman codes decoded by their
// canonical lengths. Returns false on a broken stream.
class Inflater {
public:
    explicit Inflater(std::span<const std::uint8_t> in) : in_(in) {}

    // @orig none: PNG reading for the upscaled sprites
    bool run(std::vector<std::uint8_t>& out) {
        bool last = false;
        while (!last) {
            last = bits(1) != 0;
            const int type = bits(2);
            bool ok = false;
            if (type == 0) ok = stored(out);
            else if (type == 1) ok = fixed(out);
            else if (type == 2) ok = dynamic(out);
            if (!ok || broken_) return false;
        }
        return true;
    }

private:
    struct Huffman {
        std::array<std::uint16_t, 16> count{};
        std::array<std::uint16_t, 320> symbol{};
    };

    std::span<const std::uint8_t> in_;
    std::size_t pos_ = 0;
    std::uint32_t buf_ = 0;
    int have_ = 0;
    bool broken_ = false;

    int bits(int n) {
        while (have_ < n) {
            if (pos_ >= in_.size()) {
                broken_ = true;
                return 0;
            }
            buf_ |= std::uint32_t{in_[pos_++]} << have_;
            have_ += 8;
        }
        const int v = static_cast<int>(buf_ & ((1u << n) - 1));
        buf_ >>= n;
        have_ -= n;
        return v;
    }

    static bool build(Huffman& h, const std::uint8_t* lengths, int n) {
        h.count.fill(0);
        for (int i = 0; i < n; ++i) ++h.count[lengths[i]];
        if (h.count[0] == n) return true;  // no codes: fine as long as none is read
        int left = 1;
        for (int len = 1; len < 16; ++len) {
            left = (left << 1) - h.count[len];
            if (left < 0) return false;  // more codes than the lengths allow
        }
        std::array<std::uint16_t, 16> offs{};
        for (int len = 1; len < 15; ++len) offs[len + 1] = static_cast<std::uint16_t>(offs[len] + h.count[len]);
        for (int i = 0; i < n; ++i) {
            if (lengths[i]) h.symbol[offs[lengths[i]]++] = static_cast<std::uint16_t>(i);
        }
        return true;
    }

    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= bits(1);
            const int count = h.count[len];
            if (code - count < first) return h.symbol[static_cast<std::size_t>(index + (code - first))];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        broken_ = true;
        return -1;
    }

    bool stored(std::vector<std::uint8_t>& out) {
        buf_ = 0, have_ = 0;  // to the next byte
        if (pos_ + 4 > in_.size()) return false;
        const unsigned len = in_[pos_] | in_[pos_ + 1] << 8;
        const unsigned nlen = in_[pos_ + 2] | in_[pos_ + 3] << 8;
        pos_ += 4;
        if (len != (~nlen & 0xFFFFu) || pos_ + len > in_.size()) return false;
        out.insert(out.end(), in_.begin() + static_cast<std::ptrdiff_t>(pos_), in_.begin() + static_cast<std::ptrdiff_t>(pos_ + len));
        pos_ += len;
        return true;
    }

    bool codes(std::vector<std::uint8_t>& out, const Huffman& lencode, const Huffman& distcode) {
        static constexpr std::array<std::uint16_t, 29> kLenBase = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static constexpr std::array<std::uint8_t, 29> kLenExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                                   2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static constexpr std::array<std::uint16_t, 30> kDistBase = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                                                    33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                                                    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static constexpr std::array<std::uint8_t, 30> kDistExtra = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                                    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int symbol = decode(lencode);
            if (broken_ || symbol < 0) return false;
            if (symbol < 256) {
                out.push_back(static_cast<std::uint8_t>(symbol));
                continue;
            }
            if (symbol == 256) return true;
            symbol -= 257;
            if (symbol >= 29) return false;
            const std::size_t len = kLenBase[static_cast<std::size_t>(symbol)] + static_cast<std::size_t>(bits(kLenExtra[static_cast<std::size_t>(symbol)]));
            symbol = decode(distcode);
            if (broken_ || symbol < 0 || symbol >= 30) return false;
            const std::size_t dist = kDistBase[static_cast<std::size_t>(symbol)] + static_cast<std::size_t>(bits(kDistExtra[static_cast<std::size_t>(symbol)]));
            if (dist > out.size()) return false;
            const std::size_t from = out.size() - dist;
            for (std::size_t i = 0; i < len; ++i) out.push_back(out[from + i]);
        }
    }

    bool fixed(std::vector<std::uint8_t>& out) {
        static const auto tables = [] {
            std::array<std::uint8_t, 288> lengths{};
            for (int i = 0; i < 288; ++i) lengths[static_cast<std::size_t>(i)] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
            std::pair<Huffman, Huffman> t;
            build(t.first, lengths.data(), 288);
            lengths.fill(5);
            build(t.second, lengths.data(), 30);
            return t;
        }();
        return codes(out, tables.first, tables.second);
    }

    bool dynamic(std::vector<std::uint8_t>& out) {
        static constexpr std::array<std::uint8_t, 19> kOrder = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        const int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
        if (nlen > 286 || ndist > 30) return false;
        std::array<std::uint8_t, 320> lengths{};
        for (int i = 0; i < ncode; ++i) lengths[kOrder[static_cast<std::size_t>(i)]] = static_cast<std::uint8_t>(bits(3));
        Huffman lencode, distcode;
        if (!build(lencode, lengths.data(), 19)) return false;
        int index = 0;
        while (index < nlen + ndist) {
            const int symbol = decode(lencode);
            if (broken_ || symbol < 0) return false;
            if (symbol < 16) {
                lengths[static_cast<std::size_t>(index++)] = static_cast<std::uint8_t>(symbol);
                continue;
            }
            std::uint8_t len = 0;
            int repeat = 0;
            if (symbol == 16) {
                if (index == 0) return false;
                len = lengths[static_cast<std::size_t>(index - 1)];
                repeat = 3 + bits(2);
            } else if (symbol == 17) {
                repeat = 3 + bits(3);
            } else {
                repeat = 11 + bits(7);
            }
            if (index + repeat > nlen + ndist) return false;
            while (repeat--) lengths[static_cast<std::size_t>(index++)] = len;
        }
        if (lengths[256] == 0) return false;  // no end of block
        if (!build(lencode, lengths.data(), nlen) || !build(distcode, lengths.data() + nlen, ndist)) return false;
        return codes(out, lencode, distcode);
    }
};

std::uint32_t be32(const std::uint8_t* p) {
    return std::uint32_t{p[0]} << 24 | std::uint32_t{p[1]} << 16 | std::uint32_t{p[2]} << 8 | p[3];
}

int paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

}  // namespace

// @orig none: PNG reading for the upscaled sprites
std::optional<RgbaImage> decode_png(std::span<const std::uint8_t> data) {
    static constexpr std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (data.size() < 8 || !std::equal(kSignature, kSignature + 8, data.begin())) return std::nullopt;
    int width = 0, height = 0, type = -1;
    std::vector<std::uint8_t> zdata;
    std::array<std::uint32_t, 256> palette{};
    palette.fill(0xFF000000u);
    for (std::size_t pos = 8; pos + 12 <= data.size();) {
        const std::uint32_t len = be32(&data[pos]);
        if (len > data.size() - pos - 12) return std::nullopt;
        const std::uint8_t* chunk = &data[pos + 8];
        const std::string_view tag(reinterpret_cast<const char*>(&data[pos + 4]), 4);
        if (tag == "IHDR") {
            if (len < 13) return std::nullopt;
            width = static_cast<int>(be32(chunk)), height = static_cast<int>(be32(chunk + 4));
            type = chunk[9];
            if (chunk[8] != 8 || chunk[10] != 0 || chunk[11] != 0 || chunk[12] != 0) return std::nullopt;  // 8 bits, no interlace
            if (width <= 0 || height <= 0 || width > 16384 || height > 16384) return std::nullopt;
        } else if (tag == "PLTE") {
            for (std::uint32_t i = 0; i < len / 3 && i < 256; ++i) {
                palette[i] = 0xFF000000u | std::uint32_t{chunk[i * 3]} << 16 | std::uint32_t{chunk[i * 3 + 1]} << 8 | chunk[i * 3 + 2];
            }
        } else if (tag == "tRNS" && type == 3) {
            for (std::uint32_t i = 0; i < len && i < 256; ++i) palette[i] = (palette[i] & 0xFFFFFFu) | std::uint32_t{chunk[i]} << 24;
        } else if (tag == "IDAT") {
            zdata.insert(zdata.end(), chunk, chunk + len);
        } else if (tag == "IEND") {
            break;
        }
        pos += 12 + len;
    }
    int channels = 0;
    switch (type) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default: return std::nullopt;
    }
    if (zdata.size() < 2 || (zdata[0] & 0x0F) != 8) return std::nullopt;  // zlib: deflate
    std::vector<std::uint8_t> raw;
    const std::size_t stride = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
    raw.reserve((stride + 1) * static_cast<std::size_t>(height));
    if (!Inflater(std::span(zdata).subspan(2)).run(raw) || raw.size() < (stride + 1) * static_cast<std::size_t>(height)) return std::nullopt;

    // The filters undone, row by row, in place.
    for (int y = 0; y < height; ++y) {
        std::uint8_t* row = &raw[static_cast<std::size_t>(y) * (stride + 1)];
        const std::uint8_t* prev = y > 0 ? row - (stride + 1) : nullptr;
        const int filter = row[0];
        std::uint8_t* px = row + 1;
        const std::uint8_t* up = prev ? prev + 1 : nullptr;
        for (std::size_t i = 0; i < stride; ++i) {
            const int a = i >= static_cast<std::size_t>(channels) ? px[i - static_cast<std::size_t>(channels)] : 0;
            const int b = up ? up[i] : 0;
            const int c = up && i >= static_cast<std::size_t>(channels) ? up[i - static_cast<std::size_t>(channels)] : 0;
            int v = px[i];
            switch (filter) {
            case 0: break;
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: v += paeth(a, b, c); break;
            default: return std::nullopt;
            }
            px[i] = static_cast<std::uint8_t>(v);
        }
    }

    RgbaImage img;
    img.width = width, img.height = height;
    img.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* px = &raw[static_cast<std::size_t>(y) * (stride + 1) + 1];
        std::uint32_t* out = &img.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)];
        for (int x = 0; x < width; ++x, px += channels) {
            switch (type) {
            case 0: out[x] = 0xFF000000u | std::uint32_t{px[0]} * 0x010101u; break;
            case 2: out[x] = 0xFF000000u | std::uint32_t{px[0]} << 16 | std::uint32_t{px[1]} << 8 | px[2]; break;
            case 3: out[x] = palette[px[0]]; break;
            case 4: out[x] = std::uint32_t{px[1]} << 24 | std::uint32_t{px[0]} * 0x010101u; break;
            default: out[x] = std::uint32_t{px[3]} << 24 | std::uint32_t{px[0]} << 16 | std::uint32_t{px[1]} << 8 | px[2]; break;
            }
        }
    }
    return img;
}

// @orig none: PNG reading for the upscaled sprites
std::optional<RgbaImage> read_png(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    const std::vector<std::uint8_t> data{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return decode_png(data);
}

}  // namespace crusader

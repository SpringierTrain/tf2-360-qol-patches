// Minimal XEX2 reader/writer: just enough to get at the basefile image of a
// decrypted, uncompressed ("basic" block) XEX and write changes back in place.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace xex {

inline uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
inline uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
inline void put_be32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = uint8_t(v); }

enum Encryption : uint16_t { kNotEncrypted = 0, kEncrypted = 1 };
enum Compression : uint16_t { kNone = 0, kBasic = 1, kLzx = 2, kDelta = 3 };

struct File {
    std::vector<uint8_t> bytes;
    uint32_t data_offset = 0;   // where the basefile starts in the file
    uint32_t load_address = 0;  // image base
    uint32_t image_size = 0;
    uint16_t encryption = 0;
    uint16_t compression = 0;
    struct Block { uint32_t data_size, zero_size; };
    std::vector<Block> blocks;  // only for kBasic

    // Returns an error message, or "" on success.
    std::string parse(std::vector<uint8_t> data);

    // Only valid when encryption == kNotEncrypted and compression is kNone/kBasic.
    bool image_accessible() const {
        return encryption == kNotEncrypted && (compression == kNone || compression == kBasic);
    }
    std::vector<uint8_t> read_image() const;
    void write_image(const std::vector<uint8_t>& image);
};

inline std::string File::parse(std::vector<uint8_t> data) {
    bytes = std::move(data);
    const uint8_t* d = bytes.data();
    if (bytes.size() < 0x18 || be32(d) != 0x58455832 /* 'XEX2' */)
        return "not an XEX2 file";

    data_offset = be32(d + 0x08);
    const uint32_t security = be32(d + 0x10);
    const uint32_t count = be32(d + 0x14);
    if (security + 0x114 > bytes.size() || 0x18 + count * 8 > bytes.size())
        return "truncated XEX header";
    image_size = be32(d + security + 0x004);
    load_address = be32(d + security + 0x110);

    uint32_t format = 0;
    for (uint32_t i = 0; i < count; i++)
        if (be32(d + 0x18 + i * 8) == 0x000003FF)  // XEX_HEADER_FILE_FORMAT_INFO
            format = be32(d + 0x18 + i * 8 + 4);
    if (!format || format + 8 > bytes.size())
        return "XEX has no file format info";

    const uint32_t info_size = be32(d + format);
    encryption = be16(d + format + 4);
    compression = be16(d + format + 6);
    blocks.clear();
    if (compression == kBasic) {
        for (uint32_t off = 8; off + 8 <= info_size; off += 8)
            blocks.push_back({be32(d + format + off), be32(d + format + off + 4)});
    }
    return "";
}

inline std::vector<uint8_t> File::read_image() const {
    std::vector<uint8_t> image(image_size, 0);
    if (compression == kNone) {
        size_t n = std::min<size_t>(image_size, bytes.size() - data_offset);
        std::copy_n(bytes.begin() + data_offset, n, image.begin());
        return image;
    }
    size_t src = data_offset, dst = 0;
    for (const Block& b : blocks) {
        std::copy_n(bytes.begin() + src, b.data_size, image.begin() + dst);
        src += b.data_size;
        dst += b.data_size + b.zero_size;
    }
    return image;
}

inline void File::write_image(const std::vector<uint8_t>& image) {
    if (compression == kNone) {
        size_t n = std::min<size_t>(image_size, bytes.size() - data_offset);
        std::copy_n(image.begin(), n, bytes.begin() + data_offset);
        return;
    }
    size_t dst = data_offset, src = 0;
    for (const Block& b : blocks) {
        std::copy_n(image.begin() + src, b.data_size, bytes.begin() + dst);
        dst += b.data_size;
        src += b.data_size + b.zero_size;
    }
}

}  // namespace xex

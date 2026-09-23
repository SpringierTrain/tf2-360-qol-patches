// Tiny PowerPC assembler and byte pattern helpers shared by the patches.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ppc {

using Code = std::vector<uint32_t>;

constexpr uint32_t d_form(uint32_t op, uint32_t rt, uint32_t ra, int32_t imm) {
    return op << 26 | rt << 21 | ra << 16 | (uint32_t(imm) & 0xFFFF);
}
constexpr uint32_t cmpwi(int cr, int ra, int16_t si)   { return 11u << 26 | cr << 23 | ra << 16 | uint16_t(si); }
constexpr uint32_t cmplwi(int cr, int ra, uint16_t ui) { return 10u << 26 | cr << 23 | ra << 16 | ui; }
constexpr uint32_t addi(int rd, int ra, int16_t si)    { return d_form(14, rd, ra, si); }
constexpr uint32_t addic_(int rd, int ra, int16_t si)  { return d_form(13, rd, ra, si); }
constexpr uint32_t li(int rd, int16_t si)              { return addi(rd, 0, si); }
constexpr uint32_t lis(int rd, uint16_t hi)            { return d_form(15, rd, 0, hi); }
constexpr uint32_t lwz(int rd, int16_t d, int ra)      { return d_form(32, rd, ra, d); }
constexpr uint32_t stw(int rs, int16_t d, int ra)      { return d_form(36, rs, ra, d); }
constexpr uint32_t lbz(int rd, int16_t d, int ra)      { return d_form(34, rd, ra, d); }
constexpr uint32_t stb(int rs, int16_t d, int ra)      { return d_form(38, rs, ra, d); }
constexpr uint32_t lfs(int fd, int16_t d, int ra)      { return d_form(48, fd, ra, d); }
constexpr uint32_t stfs(int fs, int16_t d, int ra)     { return d_form(52, fs, ra, d); }
constexpr uint32_t stfd(int fs, int16_t d, int ra)     { return d_form(54, fs, ra, d); }
constexpr uint32_t mr(int ra, int rs)                  { return 31u << 26 | rs << 21 | ra << 16 | rs << 11 | 444 << 1; }
constexpr uint32_t fmr(int fd, int fb)                 { return 63u << 26 | fd << 21 | fb << 11 | 72 << 1; }
constexpr uint32_t fmuls(int fd, int fa, int fc)       { return 59u << 26 | fd << 21 | fa << 16 | fc << 6 | 25 << 1; }
constexpr uint32_t fctiwz(int fd, int fb)              { return 63u << 26 | fd << 21 | fb << 11 | 15 << 1; }
constexpr uint32_t fcmpu(int cr, int fa, int fb)       { return 63u << 26 | cr << 23 | fa << 16 | fb << 11; }
constexpr uint32_t MFLR_R12 = 0x7D8802A6, MTCTR_R11 = 0x7D6903A6;
constexpr uint32_t BLR = 0x4E800020, BCTR = 0x4E800420, BCTRL = 0x4E800421, NOP = 0x60000000;

// Conditional branches on cr6 (or cr0 for the record forms), `disp` in bytes.
enum Cond : uint32_t { kLt = 0, kGt = 1, kEq = 2 };
constexpr uint32_t bc(uint32_t bo, uint32_t bi, int32_t disp) {
    return 16u << 26 | bo << 21 | bi << 16 | (uint32_t(disp) & 0xFFFC);
}
constexpr uint32_t bt6(Cond c, int32_t disp) { return bc(12, 4 * 6 + c, disp); }  // branch if set
constexpr uint32_t bf6(Cond c, int32_t disp) { return bc(4, 4 * 6 + c, disp); }   // branch if clear
constexpr uint32_t beq6(int32_t disp) { return bt6(kEq, disp); }
constexpr uint32_t bne6(int32_t disp) { return bf6(kEq, disp); }
constexpr uint32_t blt6(int32_t disp) { return bt6(kLt, disp); }
constexpr uint32_t bne0(int32_t disp) { return bc(4, kEq, disp); }
constexpr uint32_t b(int32_t disp, bool link = false) {
    return 18u << 26 | (uint32_t(disp) & 0x3FFFFFC) | uint32_t(link);
}
constexpr uint32_t bl(int32_t disp) { return b(disp, true); }

// Target of a relative b/bl at image offset `at`, as an image offset.
inline int64_t branch_target(uint32_t insn, size_t at) {
    int32_t disp = int32_t(insn << 6) >> 6 & ~3;
    return int64_t(at) + disp;
}

// @ha / @l halves for a lis + addi/lwz/lfs pair.
constexpr uint16_t ha(uint32_t addr) { return uint16_t((addr + 0x8000) >> 16); }
constexpr int16_t lo(uint32_t addr) { return int16_t(addr & 0xFFFF); }

inline std::vector<uint8_t> bytes_of(const Code& code) {
    std::vector<uint8_t> out;
    for (uint32_t w : code)
        for (int s = 24; s >= 0; s -= 8) out.push_back(uint8_t(w >> s));
    return out;
}

inline uint32_t read_word(const std::vector<uint8_t>& img, size_t off) {
    return uint32_t(img[off]) << 24 | img[off + 1] << 16 | img[off + 2] << 8 | img[off + 3];
}

inline void write_code(std::vector<uint8_t>& img, size_t off, const Code& code) {
    auto b = bytes_of(code);
    std::copy(b.begin(), b.end(), img.begin() + off);
}

// A code pattern: each word must match `value` in the bits set in `mask`.
struct Pattern {
    Code value, mask;
    Pattern& add(uint32_t v, uint32_t m = 0xFFFFFFFF) { value.push_back(v); mask.push_back(m); return *this; }
    size_t size() const { return value.size(); }
};
constexpr uint32_t kAnyImm = 0xFFFF0000;   // any 16-bit immediate / displacement
constexpr uint32_t kAnyBranch = 0xFC000003; // any b/bl displacement

// Word-aligned matches of `p` in the image.
inline std::vector<size_t> find_all(const std::vector<uint8_t>& img, const Pattern& p) {
    std::vector<size_t> hits;
    if (p.size() * 4 > img.size()) return hits;
    for (size_t i = 0; i + p.size() * 4 <= img.size(); i += 4) {
        size_t j = 0;
        while (j < p.size() && ((read_word(img, i + j * 4) ^ p.value[j]) & p.mask[j]) == 0) j++;
        if (j == p.size()) hits.push_back(i);
    }
    return hits;
}

inline Pattern exact(const Code& code) {
    Pattern p;
    for (uint32_t w : code) p.add(w);
    return p;
}

inline std::string hex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%08X", v);
    return buf;
}

}  // namespace ppc

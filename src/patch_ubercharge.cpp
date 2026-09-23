// Backports the "UBERCHARGE: %charge%%%" label value from later PC TF2 to the
// 2007 Xbox 360 client. The 2013 code has, in CHudMedicChargeMeter:
//
//   ctor:    m_flLastChargeValue = -1;
//   OnTick:  m_pChargeMeter->SetProgress( flCharge );
//            SetDialogVariable( "charge", (int)( flCharge * 100 ) );
//
// 2007 vgui already supports dialog variables (Label::OnDialogVariablesChanged
// rebuilds a localized label with ConstructString), so the label only needs a
// localization string with %charge% in it, e.g. in tf_english.txt:
//
//   "TF_Ubercharge"   "ÜBERCHARGE: %charge%%%"
//
// Layout (TU5 addresses):
//   CHudMedicChargeMeter::OnTick 874862B0, 90 instructions. `this` (the
//   EditablePanel) is r30; m_pChargeMeter +0xE0, m_bCharged +0xE4,
//   m_flLastChargeValue +0xE8. The original repeats the "start animation"
//   sequence for HudMedicCharged and HudMedicChargedStop; the rewrite shares
//   one copy, which leaves room for the SetDialogVariable call (vtable slot
//   0x338, EditablePanel::SetDialogVariable(const char*, int)) after
//   SetProgress. Same prologue, frame and epilogue as the original.
//   Constructor: `m_flLastChargeValue = 0.0f` becomes -1.0f, so the first
//   tick always sets the variable instead of leaving %charge% unfilled.
//   "charge" is the tail of the existing "Recharge" string (KeyValues names
//   are case-insensitive), and 100.0f comes from .rdata.

#include "patch.hpp"
#include "ppc.hpp"

#include <cstring>

namespace patches {
namespace {

using namespace ppc;

constexpr int kSetDialogVariableInt = 0x338;  // EditablePanel vtable slot
constexpr int kScratch = 0x50;                // free 8 bytes in OnTick's frame

// The original OnTick. Words with a zero mask part differ between builds
// (branch targets, globals, CWeaponMedigun field offsets).
Pattern orig_on_tick() {
    Pattern p;
    p.add(MFLR_R12).add(bl(0), kAnyBranch).add(stfd(31, -0x28, 1)).add(0x9421FF80)  // stwu r1,-0x80(r1)
     .add(mr(30, 3)).add(bl(0), kAnyBranch).add(cmplwi(6, 3, 0)).add(beq6(0x140))
     .add(bl(0), kAnyBranch).add(mr(31, 3)).add(cmplwi(6, 31, 0)).add(beq6(0x130))
     .add(lwz(11, 0, 31)).add(lwz(11, 0x4F0, 11)).add(MTCTR_R11).add(BCTRL)       // GetWeaponID
     .add(cmpwi(6, 3, 0x2E)).add(bne6(0x118))                                     // TF_WEAPON_MEDIGUN
     .add(lfs(31, 0, 31), kAnyImm)                                                // GetChargeLevel
     .add(lfs(0, 0xE8, 30)).add(fcmpu(6, 31, 0)).add(beq6(0x104))
     .add(lwz(3, 0xE0, 30)).add(cmplwi(6, 3, 0)).add(beq6(0x18))
     .add(lwz(11, 0, 3)).add(fmr(1, 31)).add(lwz(11, 0x2FC, 11)).add(MTCTR_R11).add(BCTRL)  // SetProgress
     .add(lbz(11, 0xE4, 30)).add(cmplwi(6, 11, 0)).add(bne6(0x68))
     .add(lis(11, 0), kAnyImm).add(lfs(0, 0, 11), kAnyImm).add(fcmpu(6, 31, 0)).add(blt6(0xC8))  // >= 1.0f
     .add(addic_(11, 30, -0x2C)).add(mr(29, 30)).add(bne0(8)).add(li(29, 0))
     .add(lis(11, 0), kAnyImm).add(lwz(3, 0, 11), kAnyImm)                        // g_pClientMode
     .add(lis(11, 0), kAnyImm).add(addi(31, 11, 0), kAnyImm)                      // "HudMedicCharged"
     .add(lwz(10, 0, 3)).add(lwz(11, 0x24, 10)).add(MTCTR_R11).add(BCTRL)
     .add(mr(4, 29)).add(mr(5, 31)).add(bl(0), kAnyBranch)                        // StartAnimationSequence
     .add(li(11, 1)).add(stfs(31, 0xE8, 30)).add(stb(11, 0xE4, 30))
     .add(addi(1, 1, 0x80)).add(0xCBE1FFD8).add(b(0), kAnyBranch)                 // lfd f31,-0x28(r1)
     .add(lbz(11, 0, 31), kAnyImm).add(cmplwi(6, 11, 0)).add(beq6(0x14))          // IsReleasingCharge
     .add(lbz(11, 0, 31), kAnyImm).add(cmplwi(6, 11, 0)).add(li(11, 1)).add(beq6(8))
     .add(li(11, 0)).add(0x556B063E).add(cmplwi(6, 11, 0)).add(bne6(0x48))       // clrlwi r11,r11,24
     .add(addic_(11, 30, -0x2C)).add(mr(29, 30)).add(bne0(8)).add(li(29, 0))
     .add(lis(11, 0), kAnyImm).add(lwz(3, 0, 11), kAnyImm)
     .add(lis(11, 0), kAnyImm).add(addi(31, 11, 0), kAnyImm)                      // "HudMedicChargedStop"
     .add(lwz(10, 0, 3)).add(lwz(11, 0x24, 10)).add(MTCTR_R11).add(BCTRL)
     .add(mr(4, 29)).add(mr(5, 31)).add(bl(0), kAnyBranch)
     .add(li(11, 0)).add(stb(11, 0xE4, 30))
     .add(stfs(31, 0xE8, 30)).add(addi(1, 1, 0x80)).add(0xCBE1FFD8).add(b(0), kAnyBranch);
    return p;
}

// Indices into the original, for the parts we carry over.
enum OrigWord {
    kSaveGpr = 1, kGetLocalPlayer = 5, kGetActiveWeapon = 8, kChargeLevel = 18,
    kOneHi = 33, kOneLo = 34, kModeHi = 41, kModeLo = 42, kChargedHi = 43, kChargedLo = 44,
    kStartAnim = 51, kRestGpr = 57, kReleasing = 58, kNotReleased = 61,
    kStopHi = 75, kStopLo = 76, kOrigSize = 90,
};

// The constructor's `m_flLastChargeValue = 0.0f; m_bCharged = false;` tail.
Pattern orig_ctor_tail() {
    Pattern p;
    p.add(lis(11, 0), kAnyImm).add(mr(3, 31)).add(lfs(0, 0, 11), kAnyImm).add(li(11, 0))
     .add(stfs(0, 0x114, 31)).add(stb(11, 0x110, 31));
    return p;
}
const Code kNewCtorTail = {
    lis(11, 0xBF80),        // -1.0f
    mr(3, 31),
    stw(11, 0x114, 31),     // m_flLastChargeValue = -1.0f
    li(11, 0),
    NOP,
    stb(11, 0x110, 31),     // m_bCharged = false
};

// The inserted dialog variable update, minus the two address loads. Used to
// recognise an already patched OnTick.
const Code kDialogVarCore = {fmuls(0, 31, 0), fctiwz(0, 0), stfd(0, kScratch, 1), lwz(5, kScratch + 4, 1)};

struct Rdata { size_t begin = 0, end = 0; };

// Finds .rdata in the PE header at the start of the image.
Rdata find_rdata(const std::vector<uint8_t>& img) {
    auto le16 = [&](size_t o) { return uint32_t(img[o] | img[o + 1] << 8); };
    auto le32 = [&](size_t o) { return le16(o) | le16(o + 2) << 16; };
    if (img.size() < 0x40 || img[0] != 'M' || img[1] != 'Z') return {};
    const size_t pe = le32(0x3C);
    if (pe + 24 > img.size()) return {};
    const uint32_t count = le16(pe + 6);
    const size_t sections = pe + 24 + le16(pe + 20);
    for (uint32_t i = 0; i < count; i++) {
        const size_t s = sections + i * 40;
        if (s + 40 > img.size()) break;
        if (std::memcmp(&img[s], ".rdata", 6) == 0) {
            size_t begin = le32(s + 12), end = begin + le32(s + 8);
            if (end <= img.size()) return {begin, end};
        }
    }
    return {};
}

size_t find_in(const std::vector<uint8_t>& img, Rdata r, const void* needle, size_t n, size_t align) {
    for (size_t i = r.begin; i + n <= r.end; i += align)
        if (std::memcmp(&img[i], needle, n) == 0) return i;
    return 0;
}

}  // namespace

Result ubercharge(std::vector<uint8_t>& img, uint32_t base) {
    const auto dv = find_all(img, exact(kDialogVarCore));
    const auto new_ctor = find_all(img, exact(kNewCtorTail));
    if (dv.size() == 1 && new_ctor.size() == 1)
        return {Status::AlreadyPatched, "OnTick is already patched\n"};

    const auto tick = find_all(img, orig_on_tick());
    const auto ctor = find_all(img, orig_ctor_tail());
    const Rdata rdata = find_rdata(img);
    const float hundred_f = 100.0f;
    uint8_t hundred[4];
    uint32_t bits;
    std::memcpy(&bits, &hundred_f, 4);
    for (int i = 0; i < 4; i++) hundred[i] = uint8_t(bits >> (24 - 8 * i));
    const size_t f100 = rdata.end ? find_in(img, rdata, hundred, 4, 4) : 0;
    const size_t charge = rdata.end ? find_in(img, rdata, "charge", 7, 1) : 0;

    std::string log;
    auto report = [&](const char* what, const std::vector<size_t>& hits) {
        log += std::string(what) + ": " + std::to_string(hits.size()) + " match(es)";
        for (size_t h : hits) log += " @ " + hex(base + uint32_t(h));
        log += "\n";
        return hits.size() == 1;
    };
    auto report_one = [&](const char* what, size_t off) {
        log += std::string(what) + ": " + (off ? hex(base + uint32_t(off)) : std::string("not found")) + "\n";
        return off != 0;
    };
    bool ok = report("MedicCharge OnTick", tick);
    ok &= report("MedicCharge ctor  ", ctor);
    ok &= report_one("100.0f in .rdata  ", f100);
    ok &= report_one("\"charge\" string   ", charge);
    if (!ok) return {Status::NotFound, log};

    const size_t t = tick[0];
    auto word = [&](int i) { return read_word(img, t + size_t(i) * 4); };
    auto target = [&](int i) { return branch_target(word(i), t + size_t(i) * 4); };
    auto imm = [&](int i) { return int16_t(word(i) & 0xFFFF); };
    const uint32_t f100_va = base + uint32_t(f100), charge_va = base + uint32_t(charge);

    // New OnTick. Labels are word indices; `at` is the word being emitted.
    enum { EXIT = 77, STORE = 76, SETVAR = 30, CHARGED = 54, NOTREL = 60, ANIM = 63, ANIMGO = 74 };
    Code c;
    auto to = [&](int label) { return (label - int(c.size())) * 4; };
    auto call = [&](int orig_index) {  // bl/b to the same target as the original word
        return uint32_t(int32_t(target(orig_index) - int64_t(t + c.size() * 4)) & 0x3FFFFFC);
    };
    c.push_back(MFLR_R12);                                         //  0
    c.push_back(18u << 26 | call(kSaveGpr) | 1);                   //  1 bl __savegprlr
    c.push_back(stfd(31, -0x28, 1));                               //  2
    c.push_back(0x9421FF80);                                       //  3 stwu r1,-0x80(r1)
    c.push_back(mr(30, 3));                                        //  4 r30 = this
    c.push_back(18u << 26 | call(kGetLocalPlayer) | 1);            //  5 C_TFPlayer::GetLocalTFPlayer
    c.push_back(cmplwi(6, 3, 0));                                  //  6
    c.push_back(beq6(to(EXIT)));                                   //  7
    c.push_back(18u << 26 | call(kGetActiveWeapon) | 1);           //  8 GetActiveTFWeapon
    c.push_back(mr(31, 3));                                        //  9 r31 = weapon
    c.push_back(cmplwi(6, 31, 0));                                 // 10
    c.push_back(beq6(to(EXIT)));                                   // 11
    c.push_back(lwz(11, 0, 31));                                   // 12
    c.push_back(lwz(11, 0x4F0, 11));                               // 13
    c.push_back(MTCTR_R11);                                        // 14
    c.push_back(BCTRL);                                            // 15 GetWeaponID
    c.push_back(cmpwi(6, 3, 0x2E));                                // 16 TF_WEAPON_MEDIGUN?
    c.push_back(bne6(to(EXIT)));                                   // 17
    c.push_back(word(kChargeLevel));                               // 18 f31 = GetChargeLevel()
    c.push_back(lfs(0, 0xE8, 30));                                 // 19
    c.push_back(fcmpu(6, 31, 0));                                  // 20 == m_flLastChargeValue?
    c.push_back(beq6(to(STORE)));                                  // 21
    c.push_back(lwz(3, 0xE0, 30));                                 // 22 m_pChargeMeter
    c.push_back(cmplwi(6, 3, 0));                                  // 23
    c.push_back(beq6(to(SETVAR)));                                 // 24
    c.push_back(lwz(11, 0, 3));                                    // 25
    c.push_back(fmr(1, 31));                                       // 26
    c.push_back(lwz(11, 0x2FC, 11));                               // 27
    c.push_back(MTCTR_R11);                                        // 28
    c.push_back(BCTRL);                                            // 29 SetProgress(flCharge)
    c.push_back(lis(11, ha(f100_va)));                             // 30 SETVAR
    c.push_back(lfs(0, lo(f100_va), 11));                          // 31
    for (uint32_t w : kDialogVarCore) c.push_back(w);              // 32-35 r5 = (int)(flCharge * 100)
    c.push_back(lis(4, ha(charge_va)));                            // 36
    c.push_back(addi(4, 4, lo(charge_va)));                        // 37 r4 = "charge"
    c.push_back(mr(3, 30));                                        // 38
    c.push_back(lwz(11, 0, 30));                                   // 39
    c.push_back(lwz(11, kSetDialogVariableInt, 11));               // 40
    c.push_back(MTCTR_R11);                                        // 41
    c.push_back(BCTRL);                                            // 42 SetDialogVariable
    c.push_back(lbz(11, 0xE4, 30));                                // 43 m_bCharged?
    c.push_back(cmplwi(6, 11, 0));                                 // 44
    c.push_back(bne6(to(CHARGED)));                                // 45
    c.push_back(word(kOneHi));                                     // 46 f0 = 1.0f
    c.push_back(word(kOneLo));                                     // 47
    c.push_back(fcmpu(6, 31, 0));                                  // 48
    c.push_back(blt6(to(STORE)));                                  // 49
    c.push_back(li(11, 1));                                        // 50 m_bCharged = true
    c.push_back(word(kChargedHi) & 0xFFFF | lis(31, 0));           // 51 r31 = "HudMedicCharged"
    c.push_back(addi(31, 31, imm(kChargedLo)));                    // 52
    c.push_back(b(to(ANIM)));                                      // 53
    c.push_back(word(kReleasing));                                 // 54 CHARGED: IsReleasingCharge()?
    c.push_back(cmplwi(6, 11, 0));                                 // 55
    c.push_back(beq6(to(NOTREL)));                                 // 56
    c.push_back(word(kNotReleased));                               // 57
    c.push_back(cmplwi(6, 11, 0));                                 // 58
    c.push_back(beq6(to(STORE)));                                  // 59   releasing -> no change
    c.push_back(li(11, 0));                                        // 60 NOTREL: m_bCharged = false
    c.push_back(word(kStopHi) & 0xFFFF | lis(31, 0));              // 61 r31 = "HudMedicChargedStop"
    c.push_back(addi(31, 31, imm(kStopLo)));                       // 62
    c.push_back(stb(11, 0xE4, 30));                                // 63 ANIM
    c.push_back(word(kModeHi));                                    // 64 g_pClientMode
    c.push_back(word(kModeLo));                                    // 65
    c.push_back(lwz(10, 0, 3));                                    // 66
    c.push_back(lwz(11, 0x24, 10));                                // 67
    c.push_back(MTCTR_R11);                                        // 68
    c.push_back(BCTRL);                                            // 69 GetViewportAnimationController
    c.push_back(addic_(11, 30, -0x2C));                            // 70 `this` as Panel*, or null
    c.push_back(mr(4, 30));                                        // 71
    c.push_back(bne0(to(ANIMGO)));                                 // 72
    c.push_back(li(4, 0));                                         // 73
    c.push_back(mr(5, 31));                                        // 74 ANIMGO
    c.push_back(18u << 26 | call(kStartAnim) | 1);                 // 75 StartAnimationSequence
    c.push_back(stfs(31, 0xE8, 30));                               // 76 STORE: m_flLastChargeValue
    c.push_back(addi(1, 1, 0x80));                                 // 77 EXIT
    c.push_back(0xCBE1FFD8);                                       // 78 lfd f31,-0x28(r1)
    c.push_back(18u << 26 | call(kRestGpr));                       // 79 b __restgprlr
    while (c.size() < kOrigSize) c.push_back(NOP);

    write_code(img, t, c);
    write_code(img, ctor[0], kNewCtorTail);
    return {Status::Patched, log};
}

}  // namespace patches

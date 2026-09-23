// Backports the later PC behaviour of CTFHudDeathNotice to the 2007 Xbox 360
// TF2 client:
//
//   GetTeamColor( team, bLocalPlayerInvolved ):
//       TEAM_UNASSIGNED -> bLocalPlayerInvolved ? 65 65 65 255 : white
//   GetInfoTextColor( msg ):
//       msg.bLocalPlayerInvolved ? 65 65 65 255 : white
//
// so death notice text stays readable on a light "LocalBackgroundColor".
//
// Layout (TU5 addresses):
//   GetTeamColor 8747E6E0  r3 = Color* out, r4 = this, r5 = team.
//       Rewritten in its original 0x80 bytes, now also reads r6 = local flag.
//       The tighter body leaves room at the end for two small entry points:
//         TRAMP: lbz r6,0xF4(r25) ; bctr            (virtual call + flag)
//         INFO:  addi r3,r1,0x50 ; lbz r6,0xF4(r25) ; b <unassigned path>
//   CHudBaseDeathNotice::Paint  (r25 = current DeathNoticeItem, +0xF4 = bLocalPlayerInvolved)
//       killer / victim name: the virtual GetTeamColor `bctrl` -> `bl TRAMP`.
//           CTR already holds the vtable target, so other classes that share
//           this Paint still get their own GetTeamColor and just ignore r6.
//       info text: the inline Color(255,255,255,255) -> `bl INFO`.

#include "patch.hpp"
#include "ppc.hpp"

namespace patches {
namespace {

using namespace ppc;

// Original CTFHudDeathNotice::GetTeamColor (identical in retail and TU5).
const Code kOrigGetTeamColor = {
    cmpwi(6, 5, 0), beq6(0x64), cmpwi(6, 5, 2), beq6(0x34), cmpwi(6, 5, 3), bne6(0x54),
    addi(11, 4, 0x17D),
    lbz(10, 0, 11), lbz(9, 1, 11), lbz(8, 2, 11), lbz(11, 3, 11),
    stb(10, 0, 3), stb(9, 1, 3), stb(8, 2, 3), stb(11, 3, 3), BLR,
    addi(11, 4, 0x182),
    lbz(10, 0, 11), lbz(9, 1, 11), lbz(8, 2, 11), lbz(11, 3, 11),
    stb(10, 0, 3), stb(9, 1, 3), stb(8, 2, 3), stb(11, 3, 3), BLR,
    li(11, 0xFF), stb(11, 2, 3), stb(11, 1, 3), stb(11, 0, 3), stb(11, 3, 3), BLR,
};

constexpr int kInfoColorSp = 0x50;       // Paint's stack slot for the info text Color
constexpr int kLocalFlag = 0xF4;         // DeathNoticeItem::bLocalPlayerInvolved
constexpr int kTramp = 27, kInfo = 29;   // entry points inside the new GetTeamColor

// New GetTeamColor. All branches are relative, so the bytes don't depend on
// where it lives, which also lets us recognise an already patched file.
Code new_get_team_color() {
    constexpr int STORE = 13, COPY = 18, UNASSIGNED = 6;
    auto to = [](int from, int target) { return (target - from) * 4; };
    return {
        cmpwi(6, 5, 3),                     //  0 TF_TEAM_BLUE?
        addi(11, 4, 0x17D),                 //  1   &m_clrBlueText
        beq6(to(2, COPY)),                  //  2
        cmpwi(6, 5, 2),                     //  3 TF_TEAM_RED?
        addi(11, 4, 0x182),                 //  4   &m_clrRedText
        beq6(to(5, COPY)),                  //  5
        li(11, 0xFF),                       //  6 UNASSIGNED: rgb = 255
        li(10, 0xFF),                       //  7             a   = 255
        cmpwi(6, 5, 0),                     //  8 TEAM_UNASSIGNED?
        bne6(to(9, STORE)),                 //  9   no  -> white (default case)
        cmplwi(6, 6, 0),                    // 10 bLocalPlayerInvolved?
        beq6(to(11, STORE)),                // 11   no  -> white
        li(11, 65),                         // 12   yes -> 65 65 65 255
        stb(11, 0, 3),                      // 13 STORE
        stb(11, 1, 3),                      // 14
        stb(11, 2, 3),                      // 15
        stb(10, 3, 3),                      // 16
        BLR,                                // 17
        lbz(10, 0, 11),                     // 18 COPY
        lbz(9, 1, 11),                      // 19
        lbz(8, 2, 11),                      // 20
        lbz(11, 3, 11),                     // 21
        stb(10, 0, 3),                      // 22
        stb(9, 1, 3),                       // 23
        stb(8, 2, 3),                       // 24
        stb(11, 3, 3),                      // 25
        BLR,                                // 26
        lbz(6, kLocalFlag, 25),             // 27 TRAMP: r6 = msg.bLocalPlayerInvolved
        BCTR,                               // 28        on to the vtable target
        addi(3, 1, kInfoColorSp),           // 29 INFO:  r3 = &info text Color
        lbz(6, kLocalFlag, 25),             // 30        r6 = msg.bLocalPlayerInvolved
        b(to(31, UNASSIGNED)),              // 31        (caller sets r5 = 0)
    };
}

// Paint: killer / victim name lookup, ending in the virtual GetTeamColor call.
//   lwz r11,0(r17) ; mr r4,r17 ; lwz r5,<team>(r25) ; addi r3,r1,<tmp>
//   lwz r11,0x38(r11) ; mtctr r11 ; bctrl
Pattern name_call(int team_off) {
    Pattern p;
    p.add(lwz(11, 0, 17)).add(mr(4, 17)).add(lwz(5, int16_t(team_off), 25))
     .add(addi(3, 1, 0), kAnyImm)
     .add(lwz(11, 0x38, 11)).add(MTCTR_R11).add(BCTRL);
    return p;
}

// Paint: Color(255,255,255,255) for msg.wzInfoText, built on the stack.
//   li r11,0xFF ; lwz r3,<g_pSurface>(r31) ; lwz r29,0(r23) ; mr r5,r24
//   add r4,r30,r10 ; stb r11,0x50(r1) x4
constexpr uint32_t ADD_R4_R30_R10 = 0x7C9E5214;
Pattern info_color() {
    Pattern p;
    p.add(li(11, 0xFF)).add(lwz(3, 0, 31), kAnyImm).add(lwz(29, 0, 23)).add(mr(5, 24))
     .add(ADD_R4_R30_R10);
    for (int i = 0; i < 4; i++) p.add(stb(11, int16_t(kInfoColorSp + i), 1));
    return p;
}

}  // namespace

Result deathnotice(std::vector<uint8_t>& img, uint32_t base) {
    if (!find_all(img, exact(new_get_team_color())).empty())
        return {Status::AlreadyPatched, "GetTeamColor is already patched\n"};

    const auto gtc = find_all(img, exact(kOrigGetTeamColor));
    const auto killer = find_all(img, name_call(0x40));
    const auto victim = find_all(img, name_call(0x84));
    const auto info = find_all(img, info_color());

    std::string log;
    auto report = [&](const char* what, const std::vector<size_t>& hits) {
        log += std::string(what) + ": " + std::to_string(hits.size()) + " match(es)";
        for (size_t h : hits) log += " @ " + hex(base + uint32_t(h));
        log += "\n";
        return hits.size() == 1;
    };
    bool ok = report("GetTeamColor      ", gtc);
    ok &= report("Paint killer call ", killer);
    ok &= report("Paint victim call ", victim);
    ok &= report("Paint info colour ", info);
    if (!ok) return {Status::NotFound, log};

    const size_t tramp = gtc[0] + kTramp * 4, info_entry = gtc[0] + kInfo * 4;
    auto rel = [](size_t from, size_t to) { return int32_t(int64_t(to) - int64_t(from)); };

    write_code(img, gtc[0], new_get_team_color());

    const size_t kill_bctrl = killer[0] + 6 * 4, vict_bctrl = victim[0] + 6 * 4;
    write_code(img, kill_bctrl, {bl(rel(kill_bctrl, tramp))});
    write_code(img, vict_bctrl, {bl(rel(vict_bctrl, tramp))});

    // GetTeamColor's unassigned path clobbers r10/r11, so do `add r4,r30,r10`
    // before the call, then the rest of the original sequence after it.
    const size_t i0 = info[0];
    const uint32_t lwz_surface = read_word(img, i0 + 4);
    write_code(img, i0, {
        ADD_R4_R30_R10,
        li(5, 0),                              // team = TEAM_UNASSIGNED
        bl(rel(i0 + 8, info_entry)),           // Color @ 0x50(r1)
        lwz_surface,                           // lwz r3, g_pSurface
        lwz(29, 0, 23),
        mr(5, 24),
        NOP, NOP, NOP,
    });

    return {Status::Patched, log};
}

}  // namespace patches

# How the patches work

## Finding the code

Patches find their code by byte pattern, with the parts that differ between
builds (branch targets, global addresses, some field offsets) masked out and
read back from the original instructions. A file that doesn't match is left
untouched.

The tool was run against 25 distinct `Client_360.dll` and `Server_360.dll`
files from The Orange Box (HL2, Episode Two, Portal, TF2), Portal: Still Alive
(Sep 2008 prototype) and a Postal III dump. Only the disc and TU5 TF clients
matched. The TF client in the Postal III dump has a June 2007 build date and
doesn't match either patch.

## Addresses

| | Disc | TU5 |
|---|---|---|
| `CTFHudDeathNotice::GetTeamColor` | `8747D510` | `8747E6E0` |
| `CHudBaseDeathNotice::Paint`, killer name colour call | `8735D2E0` | `8735DC10` |
| `CHudBaseDeathNotice::Paint`, victim name colour call | `8735D454` | `8735DD84` |
| `CHudBaseDeathNotice::Paint`, icon text colour | `8735D3B8` | `8735DCE8` |
| `CHudMedicChargeMeter::OnTick` | `874852E0` | `874862B0` |
| `CHudMedicChargeMeter` constructor, field setup | `874851CC` | `8748619C` |

## deathnotice

`GetTeamColor` takes `r3` = pointer to the returned `Color`, `r4` = `this`,
`r5` = team. The original is 32 instructions with the colour copy written out
twice. The replacement fits in the same 0x80 bytes, reads a fourth argument
`r6` = `bLocalPlayerInvolved`, and has 5 spare instructions at the end, used
for two extra entry points.

`Paint` keeps the current notice in `r25`. Its `bLocalPlayerInvolved` byte is
at offset `0xF4`.

- The killer and victim name colours come from a virtual call to
  `GetTeamColor` (`bctrl`). Each `bctrl` becomes a `bl` to
  `lbz r6,0xF4(r25) ; bctr`. `CTR` still holds the function pointer from the
  vtable, so the call still goes through the vtable. Two other vtables in the
  DLL point at the same `Paint`, and their `GetTeamColor` ignores `r6`.
- The icon text colour was built on the stack at `0x50(r1)` as four
  `stb` of `0xFF`. That sequence now calls an entry point that sets
  `r3 = r1 + 0x50`, loads the flag into `r6`, and jumps into `GetTeamColor`'s
  unassigned team code with `r5 = 0`.

## ubercharge

`OnTick` is 90 instructions, with `this` (the `EditablePanel` part of the
object) in `r30`: `m_pChargeMeter` at `+0xE0`, `m_bCharged` at `+0xE4`,
`m_flLastChargeValue` at `+0xE8`. The original repeats a 15 instruction
"start animation" sequence, once for `HudMedicCharged` and once for
`HudMedicChargedStop`. The rewrite shares one copy, which frees room for the
new call and ends at 80 instructions in the same space. The prologue, the
0x80 byte stack frame and the epilogue are unchanged.

After `SetProgress`, the new code converts the charge with `fctiwz` through
the unused 8 bytes at `0x50(r1)`, then calls vtable slot `0x338` on `r30`.
That slot is `EditablePanel::SetDialogVariable(const char*, int)`: it creates
the `"DialogVariables"` KeyValues, calls `KeyValues::SetInt` (type 2), and
notifies child panels. The existing demoman pipe and engineer metal HUDs call
it through the same slot.

Two values come from data already in the DLL. The name `"charge"` points 2
bytes into the `"Recharge"` string, since KeyValues names are case-insensitive.
100.0f is a constant in `.rdata`.

The medigun field offsets differ between builds: the charge level is at `0x914`
on TU5 and `0x90C` on the disc, and the two release flags are 8 bytes lower on
the disc as well. The patch copies them from the original instructions, along
with every call target and global address.

The constructor set `m_flLastChargeValue` to 0.0. It now stores -1.0
(`0xBF800000`), as PC does, so the first `OnTick` always sets the variable.
With 0.0, a Medic spawning at 0% charge would see the unformatted string,
`%charge%` included, until the charge first changed.

## Exit codes

For scripts: 0 when at least one patch was applied or everything selected was
already applied, 1 when no selected patch matched (nothing is written), 2 for
any other error.

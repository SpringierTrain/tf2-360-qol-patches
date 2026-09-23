# What the patches change

Both patches bring a behaviour from current PC TF2 into the 2007 Xbox 360
client. This page compares the two versions. For the machine code side, see
[internals.md](internals.md).

## deathnotice

In the kill feed, a notice you took part in (as killer, victim or assister) gets
its own background colour, `LocalBackgroundColor`. On PC that background is
cream (`245 229 196 200`), so PC TF2 draws two kinds of text in dark grey
(`65 65 65 255`, the `HudBlack` scheme colour) on it:

- names of players on no team (`TEAM_UNASSIGNED`)
- the text next to the weapon icon, such as "bid farewell, cruel world!" or
  "captured"

The 360 client draws both in white every time. The patch ports the PC rule:
dark grey when you're in the notice, white otherwise. Red and blue team names
keep their team colours.

The 360's stock `scripts/hudlayout.res` sets that background to black
(`0 0 0 255`), and white text reads fine on black. The patch is meant for the
cream background that `resources.xdelta` sets in `hudlayout.res`.

## ubercharge

PC TF2's `CHudMedicChargeMeter::OnTick` calls
`SetDialogVariable( "charge", (int)( flCharge * 100 ) )` every time the charge
changes, and its `TF_Ubercharge` string is `"ÜBERCHARGE: %charge%%%"`. The 2007
code has neither. vgui in 2007 already rebuilds a label from a localized string
when a dialog variable changes, so the DLL only needed the call. `%%` in the
string prints one `%`.

`ChargeLabel` in `hudmediccharge.res` uses `#TF_Ubercharge`, so it picks up
the new string. Without the string change the label keeps showing plain
"ÜBERCHARGE", same as before.

`resources.xdelta` also switches the label to `HudFontSmallest`, so
"ÜBERCHARGE: 100%" fits in the stock 90 unit wide label.

If you edit `tf_english.txt` yourself, keep it UTF-16 LE with a byte order
mark or the Ü breaks.

## Why an xdelta for the resource files

An earlier version shipped the three edited files in a small zip meant to
replace `zip0.360.zip`, with the stock zip renamed to `zip1.360.zip`. That
worked for `tf_english.txt` and `hudmediccharge.res`, but the game kept
drawing the stock black background from the stock `hudlayout.res`. Editing the
files inside the stock zip avoids the question of which zip wins.

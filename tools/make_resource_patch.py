"""Build attachments/resources.xdelta from a stock tf/zip0.360.zip.

The patch edits three uncompressed files inside the zip in place:

  scripts/hudlayout.res            HudDeathNotice LocalBackgroundColor -> cream
  resource/tf_english.txt          TF_Ubercharge -> "ÜBERCHARGE: %charge%%%"
  resource/ui/hudmediccharge.res   ChargeLabel font -> HudFontSmallest

Every edited file keeps its exact size: whatever an edit adds is taken back
out of runs of two or more tabs between keys and values, which KeyValues
ignores. Nothing in the zip moves, so only the edited bytes and the CRC32
fields in the local and central headers change. The xdelta is made without a
checksum (-n), so it applies to any copy of zip0.360.zip that has the three
stock files at the same offsets, even if other files in it differ.

usage: make_resource_patch.py <stock zip0.360.zip> <xdelta3 exe> [out.xdelta]
"""

import re
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

BS = "\\"


def edits():
    """(zip path, encoding, [(old, new)]) for each file."""
    return [
        ("scripts" + BS + "hudlayout.res", "latin-1", [
            ('"LocalBackgroundColor"\t"0 0 0 255"',
             '"LocalBackgroundColor"\t"245 229 196 200"'),
        ]),
        ("resource" + BS + "tf_english.txt", "utf-16-le", [
            ('"TF_Ubercharge"\t\t\t\t"ÜBERCHARGE"',
             '"TF_Ubercharge"\t\t"ÜBERCHARGE: %charge%%%"'),
        ]),
        ("resource" + BS + "ui" + BS + "hudmediccharge.res", "latin-1", [
            ('"font"\t\t\t"HudFontSmall"', '"font"\t\t\t"HudFontSmallest"'),
        ]),
    ]


def read_entries(zipdata):
    """Maps lowercase name -> (central header offset, local header offset,
    method, crc, size). Valve's 360 zips pad between central records, so
    records are found by signature rather than by walking."""
    eocd = zipdata.rfind(b"PK\x05\x06")
    total, = struct.unpack_from("<H", zipdata, eocd + 10)
    cd, = struct.unpack_from("<I", zipdata, eocd + 16)
    entries = {}
    for m in re.finditer(re.escape(b"PK\x01\x02"), zipdata[cd:eocd]):
        o = cd + m.start()
        method, = struct.unpack_from("<H", zipdata, o + 10)
        crc, csize, usize = struct.unpack_from("<III", zipdata, o + 16)
        namelen, = struct.unpack_from("<H", zipdata, o + 28)
        local, = struct.unpack_from("<I", zipdata, o + 42)
        name = zipdata[o + 46:o + 46 + namelen].decode("latin-1").lower()
        entries[name] = (o, local, method, crc, usize)
    if len(entries) != total:
        sys.exit(f"found {len(entries)} central records, expected {total}")
    return entries


def trim_tabs(text, extra, keep_near):
    """Removes `extra` tab characters from runs of 2+ tabs that sit between
    two tokens on a line, starting with the runs closest to `keep_near`."""
    runs = [m for m in re.finditer(r'(?<=")\t{2,}(?=")', text)]
    runs.sort(key=lambda m: abs(m.start() - keep_near))
    cut = []
    for m in runs:
        if extra == 0:
            break
        n = min(extra, len(m.group()) - 1)
        cut.append((m.start(), n))
        extra -= n
    if extra:
        sys.exit("not enough spare tabs to keep the file size")
    for start, n in sorted(cut, reverse=True):
        text = text[:start] + text[start + n:]
    return text


def patch_file(data, encoding, changes):
    text = data.decode(encoding)
    at = 0
    for old, new in changes:
        if text.count(old) != 1:
            sys.exit(f"expected exactly one {old!r}")
        at = text.index(old)
        text = text.replace(old, new)
    unit = 2 if encoding.startswith("utf-16") else 1
    extra = (len(text.encode(encoding)) - len(data)) // unit
    if extra < 0:
        sys.exit("edit made the file shorter; pad it instead")
    text = trim_tabs(text, extra, at)
    out = text.encode(encoding)
    assert len(out) == len(data)
    return out


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    stock, xdelta = Path(sys.argv[1]), sys.argv[2]
    out = Path(sys.argv[3] if len(sys.argv) == 4 else "attachments/resources.xdelta")

    zipdata = bytearray(stock.read_bytes())
    entries = read_entries(zipdata)
    for name, encoding, changes in edits():
        central, local, method, crc, size = entries[name.lower()]
        if method != 0:
            sys.exit(f"{name} is compressed")
        namelen, extralen = struct.unpack_from("<HH", zipdata, local + 26)
        start = local + 30 + namelen + extralen
        old = bytes(zipdata[start:start + size])
        if zlib.crc32(old) != crc:
            sys.exit(f"{name} isn't the stock file")
        new = patch_file(old, encoding, changes)
        new_crc = zlib.crc32(new)
        zipdata[start:start + size] = new
        struct.pack_into("<I", zipdata, local + 14, new_crc)
        struct.pack_into("<I", zipdata, central + 16, new_crc)
        print(f"{name}: {sum(a != b for a, b in zip(old, new))} bytes changed")

    with tempfile.TemporaryDirectory() as tmp:
        patched = Path(tmp) / "zip0.360.zip"
        patched.write_bytes(zipdata)
        out.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([xdelta, "-e", "-f", "-9", "-n", "-A", "-B", str(1 << 29),
                        "-s", str(stock), str(patched), str(out)], check=True)
    print(f"wrote {out} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()

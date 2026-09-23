# Building

The tool needs a C++17 compiler and has no dependencies. Both build files link
the C++ runtime statically on MSVC, so the exe runs without the Visual C++
Redistributable.

## CMake

```
cmake -S . -B build
cmake --build build --config Release
```

With Visual Studio the exe ends up in `build\Release\`.

## xmake

```
xmake
```

From Git Bash on Windows, xmake picks MinGW by default. Run
`xmake f -p windows` once first to use MSVC.

## Source layout

| File | Contents |
|---|---|
| `src/main.cpp` | command line, xextool calls |
| `src/xex.hpp` | reads and writes the image inside a decrypted XEX |
| `src/ppc.hpp` | PowerPC instruction encoders and byte pattern search |
| `src/patch.hpp` | the list of patches |
| `src/patch_deathnotice.cpp` | `deathnotice` |
| `src/patch_ubercharge.cpp` | `ubercharge` |

A new patch goes in its own `src/patch_<name>.cpp` and gets a line in
`patches::all()` in `patch.hpp`. xmake picks up new source files by itself;
CMake needs the file added to `add_executable`.

## Resource patch

`attachments/resources.xdelta` is built by `tools/make_resource_patch.py`
(Python 3, no packages) from a stock `tf\zip0.360.zip`:

```
python tools/make_resource_patch.py <stock zip0.360.zip> <xdelta3.exe>
```

The three files are stored uncompressed in the zip. The script edits them in
place and keeps each one at its original size: whatever an edit adds, it
removes from runs of two or more tabs between a key and its value on nearby
lines, never going below one tab. It then fixes the CRC32 in the local and
central headers and runs xdelta3 with `-n`. Without a checksum, the patch
applies to any copy of the zip that has the three stock files at the same
offsets, even if other files in it differ.

The edits are listed in `edits()` at the top of the script. Valve's 360 zips
pad between central directory records, so the script finds records by
signature instead of walking them.

# Castlevania: Harmony of Despair Recomp

ReXGlue project for a Windows recompilation of the Xbox 360 version of
Castlevania: Harmony of Despair.

This repository does not contain game files. Use it only with game data from a
copy you legally own, and do not distribute extracted or processed game files.

## What You Need

- Windows 10 or newer.
- Python 3.10 or newer for the extraction helper.
- Visual Studio 2022 with the C++ desktop workload if you want to build from
  source.
- The pinned ReXGlue SDK submodule at:

```text
thirdparty\rexglue-sdk
```

If you cloned without submodules, initialize them from this project folder:

```powershell
git submodule update --init --recursive
```

- A Castlevania: Harmony of Despair Xbox 360 STFS package. The expected title id
  is `58410A7A`, and XBLA packages normally live under a path like:

```text
...\58410A7A\000D0000\<long package file name>
```

## Game Data Layout

When running from this source tree, the app automatically looks for a sibling
game-data folder:

```text
RexGlue\
  Castlevania Harmony of Despair\
  Castlevania-Harmony-of-Despair\
    default.xex
    58410A7A.stfsheader
    data\
```

For a packaged player build, put the extracted files in an `assets` folder next
to the executable:

```text
castlevania_harmony_of_despair.exe
assets\
  default.xex
  58410A7A.stfsheader
  data\
```

The full LIVE package is not needed after extraction. The extractor writes the
small `58410A7A.stfsheader` sidecar for metadata/debugging.

## Extract Game Data

From this project folder:

```powershell
py -3 .\scripts\extract_game_data.py `
  "D:\Xenia\58410A7A\000D0000\652844FE3155A56E8CE9CA6EF3D78208784BB55558" `
  --output "..\Castlevania-Harmony-of-Despair"
```

By default the tool extracts every file in the package and refuses to overwrite
existing files. Add `--overwrite` only when you intentionally want to replace the
output folder contents.

Useful checks:

```powershell
# List package contents without writing files.
py -3 .\scripts\extract_game_data.py "D:\path\to\package" --list

# Extract only the files this game normally needs at runtime.
py -3 .\scripts\extract_game_data.py "D:\path\to\package" `
  --output ".\assets" `
  --only "default.xex" `
  --only "data/*"
```

The helper supports single-file `CON`, `LIVE`, and `PIRS` STFS packages. It does
not support multi-file SVOD packages, and it does not decrypt, patch, or download
game files.

## Raw vs Expanded Executables

The STFS package contains the original Xbox executable payloads. In this working
dump, those raw files are kept as `.orig`, while the active names are expanded
files used by ReXGlue codegen:

```text
default.xex.orig
default.xex
data\player_dll\dllAlucard.dll.orig
data\player_dll\dllAlucard.dll
...
```

For a developer/codegen dump, extract with `--orig-executables` so the helper
does not overwrite active expanded files:

```powershell
py -3 .\scripts\extract_game_data.py "D:\path\to\package" `
  --output "..\Castlevania-Harmony-of-Despair" `
  --orig-executables
```

After that, use your own XEX processing workflow to produce the active
`default.xex` and `data\player_dll\*.dll` files. This Python helper only unpacks
the container.

## Build From Source

From this project folder:

```powershell
.\scripts\configure-release.ps1
.\scripts\codegen.ps1
.\scripts\build-release.ps1
```

The configure step builds against the pinned SDK source under
`thirdparty\rexglue-sdk`; no separate ReXGlue SDK install is required.

The release executable is written to:

```text
out\build\win-amd64-release\castlevania_harmony_of_despair.exe
```

If Visual Studio fails from the IDE, use `.\scripts\build-release.ps1`; it sets
up the VS and LLVM environment expected by this project.

## Run

Double-click the built executable, or launch it from PowerShell:

```powershell
.\out\build\win-amd64-release\castlevania_harmony_of_despair.exe
```

The window title should be:

```text
Castlevania: Harmony of Despair
```

Runtime logs are written under:

```text
out\build\win-amd64-release\logs
```

Crash dumps, when Windows creates them, are usually under:

```text
%LOCALAPPDATA%\CrashDumps
```

## License State

The current app initializes the XBLA entitlement mask internally if the runtime
config has not already set one. You do not need to keep the original full LIVE
package next to the extracted data.

If you are debugging license behavior, the runtime config lives next to the
built executable:

```text
out\build\win-amd64-release\castlevania_harmony_of_despair.toml
```

Integer cvars are decimal. A nonzero `license_mask` value in that file takes
priority over the app default.

## Troubleshooting

- `Package title id is ... expected 0x58410A7A`: you pointed the extractor at a
  different title's package.
- `Refusing to overwrite existing file`: choose a clean output folder or rerun
  with `--overwrite`.
- The app cannot find game data: put the extracted files in `assets` next to the
  exe, or use the sibling `..\Castlevania-Harmony-of-Despair` folder when
  running from this source tree.
- A crash after pressing Start usually means the recomp hit another missing or
  mis-sized generated function. Check the newest log and crash dump first.

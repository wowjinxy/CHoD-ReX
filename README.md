# Castlevania Harmony of Despair Recomp

ReXGlue scaffold for a Castlevania Harmony of Despair Xbox 360 recompilation project.

## Local SDK

The SDK is expected at:

```powershell
..\rexglue-sdk\out\install\win-amd64
```

The CLI is available at:

```powershell
..\rexglue-sdk\out\install\win-amd64\bin\rexglue.exe
```

## Game Files

This project currently points at the local dump next to the project:

```text
..\Castlevania-Harmony-of-Despair
```

The manifest includes the main `default.xex` plus the six character modules under:

```text
data\player_dll\
```

Do not commit game binaries, extracted assets, or generated recomp output.

## Workflow

From this folder:

```powershell
.\scripts\codegen.ps1
.\scripts\configure-release.ps1
.\scripts\build-release.ps1
```

## License State

The runtime reports XBLA entitlement through the `license_mask` cvar used by
`XamContentGetLicenseMask`. The config file is loaded from next to the built
executable:

```text
out\build\win-amd64-release\castlevania_harmony_of_despair.toml
```

Integer cvars are decimal. A legitimate entitlement mask can be set there as:

```toml
license_mask = 0
```

At startup, the app first respects any nonzero configured `license_mask`. If the
mask is still `0`, it scans the game data folder for this title's STFS package
and copies the package's active license bits when active license flags are
present.

To inspect the STFS package header and see whether it contains active license
flags:

```powershell
.\scripts\inspect-stfs-license.ps1
```

To avoid keeping a full duplicate LIVE package after extraction, export only
the fixed STFS header to the extracted game root, next to `default.xex` and
`data\`:

```powershell
.\scripts\export-stfs-license-header.ps1 `
  -PackagePath "D:\Xenia\58410A7A\000D0000\652844FE3155A56E8CE9CA6EF3D78208784BB55558" `
  -OutputPath "..\Castlevania-Harmony-of-Despair\58410A7A.stfsheader"
```

The sidecar is about 38 KB and contains the STFS metadata needed for entitlement
mask detection, not the extracted game payload.

The extracted local package currently declares license bit `0x00000001`, but
its package header has no active license flags, so the derived mask is `0`.

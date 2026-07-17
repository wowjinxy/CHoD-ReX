$ErrorActionPreference = "Stop"

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$rexglue = Resolve-Path (Join-Path $projectRoot "..\rexglue-sdk\out\install\win-amd64\bin\rexglue.exe")
$manifest = Join-Path $projectRoot "castlevania_harmony_of_despair_manifest.toml"

Push-Location $projectRoot
try {
    & $rexglue codegen $manifest
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}

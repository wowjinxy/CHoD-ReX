$ErrorActionPreference = "Stop"

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildDir = Join-Path $projectRoot "out\build\win-amd64-release"
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
$llvm = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin"

if (-not (Test-Path -LiteralPath $vcvars)) {
    throw "Visual Studio vcvars64.bat not found: $vcvars"
}

if (-not (Test-Path -LiteralPath (Join-Path $buildDir "build.ninja"))) {
    & (Join-Path $PSScriptRoot "configure-release.ps1")
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$command = "`"$vcvars`" >nul && set `"PATH=$llvm;%PATH%`" && cd /d `"$projectRoot`" && cmake --build --preset win-amd64-release --target castlevania_harmony_of_despair_codegen"
cmd /d /c $command
exit $LASTEXITCODE

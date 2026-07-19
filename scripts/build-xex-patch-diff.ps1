$ErrorActionPreference = "Stop"

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$toolRoot = Join-Path $projectRoot "tools\XexPatchDiff"
$sdkInstall = Resolve-Path (Join-Path $projectRoot "..\rexglue-sdk\out\install\win-amd64")
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
$llvm = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin"
$buildDir = Join-Path $toolRoot "out\build\release"

if (-not (Test-Path -LiteralPath $vcvars)) {
    throw "Visual Studio vcvars64.bat not found: $vcvars"
}

$command = "`"$vcvars`" >nul && set `"PATH=$llvm;%PATH%`" && cd /d `"$toolRoot`" && cmake -S . -B `"$buildDir`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=`"$sdkInstall`" && cmake --build `"$buildDir`""
cmd /d /c $command
exit $LASTEXITCODE

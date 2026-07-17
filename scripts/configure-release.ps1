$ErrorActionPreference = "Stop"

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$sdkRoot = Resolve-Path (Join-Path $projectRoot "thirdparty\rexglue-sdk")
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
$llvm = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin"

if (-not (Test-Path -LiteralPath $vcvars)) {
    throw "Visual Studio vcvars64.bat not found: $vcvars"
}

$command = "`"$vcvars`" >nul && set `"PATH=$llvm;%PATH%`" && cd /d `"$projectRoot`" && cmake --preset win-amd64-release -DREXSDK_DIR=`"$sdkRoot`""
cmd /d /c $command
exit $LASTEXITCODE

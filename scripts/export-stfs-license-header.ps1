param(
  [Parameter(Mandatory = $true)]
  [string]$PackagePath,

  [Parameter(Mandatory = $false)]
  [string]$OutputPath
)

$HeaderLength = 0x971A

$resolvedPath = Resolve-Path -LiteralPath $PackagePath -ErrorAction Stop
$fs = [System.IO.File]::OpenRead($resolvedPath.Path)
try {
  if ($fs.Length -lt $HeaderLength) {
    throw "Package is too small to contain an STFS header."
  }

  $buffer = New-Object byte[] $HeaderLength
  $bytesRead = $fs.Read($buffer, 0, $buffer.Length)
  if ($bytesRead -ne $HeaderLength) {
    throw "Failed to read the complete STFS header."
  }
} finally {
  $fs.Dispose()
}

function Read-U32BE([int]$offset) {
  return [uint32](
    ([uint32]$buffer[$offset] -shl 24) -bor
    ([uint32]$buffer[$offset + 1] -shl 16) -bor
    ([uint32]$buffer[$offset + 2] -shl 8) -bor
    [uint32]$buffer[$offset + 3]
  )
}

$magic = [Text.Encoding]::ASCII.GetString($buffer, 0, 4)
if ($magic -ne "LIVE" -and $magic -ne "PIRS" -and $magic -ne "CON ") {
  throw "Invalid STFS package magic '$magic'."
}

$titleId = Read-U32BE 0x360
$contentType = Read-U32BE 0x344
$licenseMask = [uint32]0

for ($i = 0; $i -lt 16; $i++) {
  $offset = 0x22C + ($i * 0x10)
  $licenseBits = Read-U32BE ($offset + 8)
  $licenseFlags = Read-U32BE ($offset + 12)

  if ($licenseFlags -ne 0) {
    $licenseMask = $licenseMask -bor $licenseBits
  }
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
  $packageFolder = Split-Path -Parent $resolvedPath.Path
  $OutputPath = Join-Path $packageFolder ("{0:X8}.stfsheader" -f $titleId)
}

$resolvedOutput = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
$outputFolder = Split-Path -Parent $resolvedOutput
if (![string]::IsNullOrWhiteSpace($outputFolder)) {
  [System.IO.Directory]::CreateDirectory($outputFolder) | Out-Null
}

[System.IO.File]::WriteAllBytes($resolvedOutput, $buffer)

[pscustomobject]@{
  Source             = $resolvedPath.Path
  Output             = $resolvedOutput
  Bytes              = $HeaderLength
  Magic              = $magic
  TitleId            = ("0x{0:X8}" -f $titleId)
  ContentType        = ("0x{0:X8}" -f $contentType)
  LicenseMaskHex     = ("0x{0:X8}" -f $licenseMask)
  LicenseMaskDecimal = $licenseMask
}

if ($licenseMask -eq 0) {
  Write-Warning "The exported header has no active license flags."
}

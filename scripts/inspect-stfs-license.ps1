param(
  [Parameter(Mandatory = $false)]
  [string]$PackagePath = "..\Castlevania-Harmony-of-Despair\652844FE3155A56E8CE9CA6EF3D78208784BB55558"
)

$resolvedPath = Resolve-Path -LiteralPath $PackagePath -ErrorAction Stop
$fs = [System.IO.File]::OpenRead($resolvedPath.Path)
try {
  $buffer = New-Object byte[] 0x9800
  [void]$fs.Read($buffer, 0, $buffer.Length)
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

function Read-U64BE([int]$offset) {
  $hi = [uint64](Read-U32BE $offset)
  $lo = [uint64](Read-U32BE ($offset + 4))
  return (($hi -shl 32) -bor $lo)
}

$magic = [Text.Encoding]::ASCII.GetString($buffer, 0, 4)
$licenseMask = [uint32]0
$licenses = @()

for ($i = 0; $i -lt 16; $i++) {
  $offset = 0x22C + ($i * 0x10)
  $licenseeId = Read-U64BE $offset
  $licenseBits = Read-U32BE ($offset + 8)
  $licenseFlags = Read-U32BE ($offset + 12)

  if ($licenseFlags -ne 0) {
    $licenseMask = $licenseMask -bor $licenseBits
  }

  if ($licenseBits -ne 0 -or $licenseFlags -ne 0) {
    $licenses += [pscustomobject]@{
      Index     = $i
      Licensee  = ("0x{0:X16}" -f $licenseeId)
      Bits      = ("0x{0:X8}" -f $licenseBits)
      Flags     = ("0x{0:X8}" -f $licenseFlags)
      Active    = ($licenseFlags -ne 0)
    }
  }
}

[pscustomobject]@{
  Package            = $resolvedPath.Path
  Magic              = $magic
  TitleId            = ("0x{0:X8}" -f (Read-U32BE 0x360))
  ContentType        = ("0x{0:X8}" -f (Read-U32BE 0x344))
  LicenseMaskHex     = ("0x{0:X8}" -f $licenseMask)
  LicenseMaskDecimal = $licenseMask
}

if ($licenses.Count -gt 0) {
  $licenses | Format-Table -AutoSize
}

if ($licenseMask -eq 0) {
  Write-Warning "No active license flags were found in this package header."
} else {
  Write-Host "Runtime config value: license_mask = $licenseMask"
}

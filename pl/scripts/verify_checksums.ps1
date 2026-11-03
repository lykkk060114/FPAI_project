$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$manifest = Join-Path $root 'FILE_SHA256.csv'
if (-not (Test-Path -LiteralPath $manifest)) { throw 'FILE_SHA256.csv is missing.' }
$count = 0
foreach ($row in (Import-Csv -LiteralPath $manifest -Encoding UTF8)) {
    $file = Join-Path $root $row.path
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing: $($row.path)" }
    $actual = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
    if ($actual -ne $row.sha256) { throw "Checksum mismatch: $($row.path)" }
    $count++
}
Write-Output "PACKAGE_CHECKSUM_PASS files=$count"

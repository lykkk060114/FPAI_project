$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$manifest = Join-Path $root 'FILE_SHA256.csv'
$rows = Get-ChildItem -LiteralPath $root -Recurse -File -Force |
    Where-Object { $_.FullName -ne $manifest } |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($root.Length + 1).Replace('\', '/')
        [PSCustomObject]@{
            path = $relative
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            bytes = $_.Length
        }
    }
$rows | Export-Csv -LiteralPath $manifest -NoTypeInformation -Encoding UTF8
Write-Output "CHECKSUM_MANIFEST_CREATED files=$($rows.Count)"

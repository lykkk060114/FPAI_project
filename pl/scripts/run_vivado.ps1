[CmdletBinding()]
param(
    [ValidateSet('check','synth','bit')][string]$Action = 'check',
    [string]$VivadoBin = $env:VIVADO_BIN,
    [string]$ProciseRoot = $env:FMSH_PROCISE_PATH,
    [ValidateRange(1,16)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $VivadoBin) { $VivadoBin = 'D:\vivado2018\Vivado\2018.3\bin' }
$vivado = Join-Path $VivadoBin 'vivado.bat'
if (-not (Test-Path -LiteralPath $vivado -PathType Leaf)) { throw "Vivado not found: $vivado. Specify -VivadoBin." }
if ($Action -eq 'bit' -and (-not $ProciseRoot -or -not (Test-Path -LiteralPath (Join-Path $ProciseRoot 'bin\procise.exe')))) {
    throw 'Specify -ProciseRoot (Procise installation root, containing bin\procise.exe).'
}
$logDir = Join-Path $root 'verification\logs'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$oldJfm = $env:JFM_PATH
$oldProcise = $env:FMSH_PROCISE_PATH
Push-Location -LiteralPath $root
try {
    $env:JFM_PATH = $root
    if ($ProciseRoot) { $env:FMSH_PROCISE_PATH = $ProciseRoot }
    & $vivado -mode batch -log (Join-Path $logDir "$Action.log") -journal (Join-Path $logDir "$Action.jou") -source (Join-Path $PSScriptRoot 'build.tcl') -tclargs $Action $Jobs
    $code = $LASTEXITCODE
    if ($code -ne 0) { throw "Vivado exited with code $code. See verification\logs\$Action.log." }
} finally {
    Pop-Location
    $env:JFM_PATH = $oldJfm
    $env:FMSH_PROCISE_PATH = $oldProcise
}

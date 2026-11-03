[CmdletBinding()]
param([string]$VivadoBin = $env:VIVADO_BIN)
$ErrorActionPreference = 'Stop'
if (-not $VivadoBin) { $VivadoBin = 'D:\vivado2018\Vivado\2018.3\bin' }
$root = Split-Path -Parent $PSScriptRoot
foreach ($exe in @('xvlog','xelab','xsim')) {
    if (-not (Test-Path -LiteralPath (Join-Path $VivadoBin "$exe.bat"))) { throw "Missing $exe.bat in $VivadoBin" }
}
$tests = @(
    @{ Name='input_bridge'; Top='tb_input_bridge'; Pass='ALL INPUT BRIDGE TESTS PASSED'; Sources=@(
        'rtl/xunjing/integration/xj_reset_release.sv',
        'rtl/xunjing/integration/xj_command_cdc.sv',
        'rtl/xunjing/integration/xj_pixel_fifo.sv',
        'rtl/xunjing/npu_input_format/rgbu8_bgr_fp32_pack.sv',
        'rtl/xunjing/integration/xj_input_control.sv',
        'verification/tb/tb_input_bridge.sv') },
    @{ Name='frame_fifo_error'; Top='tb_frame_fifo_error_cdc'; Pass='FRAME_FIFO_ERROR_CDC_PASS'; Sources=@(
        'rtl/display/frame_fifo_write.v',
        'verification/tb/tb_frame_fifo_error_cdc.sv') },
    @{ Name='p3_osd'; Top='p3_osd_tb'; Pass='P3_OSD_PASS'; Sources=@(
        'rtl/xunjing/integration/xj_command_cdc.sv',
        'rtl/xunjing/integration/xj_osd_control.sv',
        'rtl/display/video_osd_overlay.v',
        'verification/tb/p3_osd_tb.sv') }
)
$results = @()
foreach ($test in $tests) {
    $work = Join-Path $root "verification\sim\$($test.Name)"
    New-Item -ItemType Directory -Path $work -Force | Out-Null
    Push-Location -LiteralPath $work
    try {
        $sources = @($test.Sources | ForEach-Object { Join-Path $root $_ })
        & (Join-Path $VivadoBin 'xvlog.bat') -sv @sources *> compile.log
        if ($LASTEXITCODE -ne 0) { throw "$($test.Name): xvlog failed; see $work\compile.log" }
        & (Join-Path $VivadoBin 'xelab.bat') $test.Top -s "$($test.Name)_sim" *> elaborate.log
        if ($LASTEXITCODE -ne 0) { throw "$($test.Name): xelab failed; see $work\elaborate.log" }
        & (Join-Path $VivadoBin 'xsim.bat') "$($test.Name)_sim" -runall -log simulation.log *> console.log
        if ($LASTEXITCODE -ne 0) { throw "$($test.Name): xsim failed; see $work\simulation.log" }
        $log = Get-Content -LiteralPath 'simulation.log' -Raw
        if (-not $log.Contains($test.Pass) -or $log -match '(?m)^Fatal:') { throw "$($test.Name): missing PASS or fatal error; see $work\simulation.log" }
        Write-Output "$($test.Name): PASS"
        $results += [PSCustomObject]@{test=$test.Name; result='PASS'; marker=$test.Pass}
    } finally { Pop-Location }
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'verification\simulation_results.json') -Encoding UTF8

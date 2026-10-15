# Build the LILYGO T-Display-S3 firmware and merge it into one factory image
# that is flashed at offset 0x0.
#
#   .\build_tdisplay_s3.ps1            -> dist\waze_hud_tdisplay_s3_factory.bin
#   .\build_tdisplay_s3.ps1 -Mock      -> dist\waze_hud_tdisplay_s3_mock_factory.bin (no phone needed)
#   .\build_tdisplay_s3.ps1 -Port COM9 -> also flashes the merged image
param(
    [switch]$Mock,
    [string]$Port = '',
    [string]$IdfProfile = 'C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1'
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
. $IdfProfile | Out-Null

$suffix = if ($Mock) { '-mock' } else { '' }
$buildDir = "build$suffix"
$sdkconfig = "sdkconfig$($suffix -replace '-', '.')"
$defaults = 'sdkconfig.defaults'
if ($Mock) { $defaults += ';sdkconfig.mock.defaults' }
$output = "dist\waze_hud_tdisplay_s3$($suffix -replace '-', '_')_factory.bin"

$common = @('-B', $buildDir, '-D', "SDKCONFIG=$sdkconfig", '-D', "SDKCONFIG_DEFAULTS=$defaults")
if (-not (Test-Path "$buildDir\CMakeCache.txt")) {
    idf.py @common set-target esp32s3
    if ($LASTEXITCODE -ne 0) { throw 'set-target failed' }
}
idf.py @common build
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

New-Item -ItemType Directory -Force dist | Out-Null
Push-Location $buildDir
try {
    # flash_args already carries the flash mode, frequency and size chosen in sdkconfig.
    python -m esptool --chip esp32s3 merge_bin -o "..\$output" '@flash_args'
    if ($LASTEXITCODE -ne 0) { throw 'merge_bin failed' }
} finally { Pop-Location }
Write-Host "Factory image: $output (flash at 0x0)"

if ($Port) {
    python -m esptool --chip esp32s3 -p $Port -b 460800 write_flash 0x0 $output
    if ($LASTEXITCODE -ne 0) { throw 'flash failed' }
}

# Render the HUD on the PC (no ESP-IDF, no board) to check the layout.
#
#   pip install ziglang          (provides a C++ compiler; no other tools needed)
#   .\run.ps1                    -> out\sheet_*.png contact sheets (2x scale) of many scenarios
#   .\run.ps1 -Test              -> also checks that incremental (dirty-region) redraws equal a fresh
#                                   full redraw for a sequence of state/settings changes
#
# The real hud_renderer.cpp, bitmap_font.cpp and generated_assets.cpp are compiled against stub ESP
# headers; the display driver is replaced by a framebuffer. Marquee text depends on elapsed time, so
# the consistency test uses short street names.
param([switch]$Test)
$ErrorActionPreference = 'Stop'
$main = (Resolve-Path "$PSScriptRoot\..\..\waze-hud\main").Path
$out = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force $out | Out-Null
$common = @('-std=gnu++17', '-O1', '-Wno-nullability-completeness',
            '-include', "$PSScriptRoot\stubs\gmtime_stub.h", '-include', 'algorithm',
            "-I$PSScriptRoot\stubs", "-I$main")
$sources = @("$main\display\hud_renderer.cpp", "$main\display\bitmap_font.cpp", "$main\assets\generated_assets.cpp")

python -m ziglang c++ @common "$PSScriptRoot\preview.cpp" @sources -o "$out\preview.exe"
if ($LASTEXITCODE -ne 0) { throw 'preview build failed' }
$env:OUTDIR = $out
& "$out\preview.exe"
$sheets = @(
  @('s00_base','s01_cam_police','s02_three_alerts','s03_nopassing','s04_roundabout','s05_overspeed'),
  @('s06_disconnected','s07_long_names','s08_rb_straight','s09_rail_flood_school','s10_traffic','s11_ten_lanes'),
  @('s12_sixteen_lanes','s13_no_lanes','s14_freedrive','s15_freedrive_alert','m00_limitonly','m01_limitonly_nolanes'),
  @('m02_limitonly_free','m03_nostreet','m04_battery','m05_status','m06_night','m07_3digit','m08_nolimit','s06b_connected_wait'))
foreach ($names in $sheets) { python "$PSScriptRoot\sheet.py" $out @names }

if ($Test) {
    python -m ziglang c++ @common "$PSScriptRoot\consistency_test.cpp" @sources -o "$out\consistency_test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'consistency test build failed' }
    & "$out\consistency_test.exe"
}

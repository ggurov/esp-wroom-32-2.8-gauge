<#
.SYNOPSIS
    Run the test suites for esp-wroom-32-2.8-gauge.

.DESCRIPTION
    Two layers, cheapest first:

      1. host unit tests   - pure C (gauge_math / gauge_theme / gauge_presets /
                             gfx / gauge_render) compiled with the system GCC
                             against a stubbed panel.  Sub-second, no hardware.
      2. contract checks   - Python guards on assumptions about the generated
                             fonts, the enabled text, the host preview renderer,
                             LVGL never coming back, and the doc links.

    Tests register themselves with a constructor in the host framework, so
    there is no list to keep up to date.

.EXAMPLE
    tools\test.ps1                       # host + contracts
    tools\test.ps1 -Filter gauge_math    # one suite
#>
[CmdletBinding()]
param(
    [string] $Gcc    = 'C:\msys64\mingw64\bin\gcc.exe',
    [string] $Filter = '',
    [switch] $SkipHost
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot 'build\tests'
$exe      = Join-Path $buildDir 'host_tests.exe'

$failed = @()

function Write-Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }
function Write-Ok($msg)   { Write-Host "    $msg" -ForegroundColor Green }
function Write-Bad($msg)  { Write-Host "    $msg" -ForegroundColor Red }

# ---------------------------------------------------------------------------
# 1. host unit tests
# ---------------------------------------------------------------------------
if (-not $SkipHost) {
    Write-Step 'host unit tests'

    if (-not (Test-Path $Gcc)) {
        Write-Bad "gcc not found at $Gcc"
        Write-Host '    Install MSYS2 (pacman -S mingw-w64-x86_64-gcc) or pass -Gcc <path>.'
        $failed += 'host tests (no compiler)'
    } else {
        New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

        # MinGW's gcc needs its own bin directory on PATH to find its DLLs when
        # it is launched by absolute path.
        $gccDir = Split-Path -Parent $Gcc
        $env:PATH = "$gccDir;$env:PATH"

        $sources = @(
            'tests\host\test_main.c'
            'tests\host\test_framework.c'
            'tests\host\stub_bsp.c'
            'tests\host\test_gauge_math.c'
            'tests\host\test_gauge_theme.c'
            'tests\host\test_gauge_presets.c'
            'tests\host\test_gfx.c'
            'tests\host\test_gauge_render.c'
            'firmware\components\gauge\gauge_math.c'
            'firmware\components\gauge\gauge_theme.c'
            'firmware\components\gauge\gauge_presets.c'
            'firmware\components\gauge\gauge_render.c'
            'firmware\components\gfx\gfx.c'
            'firmware\components\gfx\gfx_text.c'
            'firmware\components\gfx\gfx_font_data.c'
        ) | ForEach-Object { Join-Path $repoRoot $_ }

        # esp_stub first: it satisfies bsp.h's esp_err.h / esp_lcd_types.h so
        # gfx and the renderer can be built without ESP-IDF
        $args = @(
            '-std=gnu17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
            '-O1', '-g',
            '-Itests/host/esp_stub',
            '-Ifirmware/components/gauge/include',
            '-Ifirmware/components/gfx/include',
            '-Ifirmware/components/bsp/include',
            '-Itests/host'
        ) + $sources + @('-o', $exe, '-lm')

        Push-Location $repoRoot
        try {
            & $Gcc @args
            if ($LASTEXITCODE -ne 0) {
                Write-Bad 'host tests failed to compile'
                $failed += 'host tests (compile)'
            } else {
                if ($Filter) { & $exe $Filter } else { & $exe }
                if ($LASTEXITCODE -ne 0) {
                    Write-Bad "host tests reported failures"
                    $failed += 'host tests'
                } else {
                    Write-Ok 'all host tests passed'
                }
            }
        } finally {
            Pop-Location
        }
    }
}

# ---------------------------------------------------------------------------
# 2. contract checks
# ---------------------------------------------------------------------------
Write-Step 'contract checks'
Push-Location $repoRoot
try {
    & python 'tests\py\test_contracts.py'
    if ($LASTEXITCODE -ne 0) {
        Write-Bad 'contract checks failed'
        $failed += 'contracts'
    } else {
        Write-Ok 'all contracts hold'
    }
} finally {
    Pop-Location
}

# ---------------------------------------------------------------------------
Write-Host ''
if ($failed.Count -eq 0) {
    Write-Host '==================================================' -ForegroundColor Green
    Write-Host '  ALL SUITES PASSED (host, contracts)' -ForegroundColor Green
    Write-Host '==================================================' -ForegroundColor Green
    exit 0
} else {
    Write-Host '==================================================' -ForegroundColor Red
    Write-Host '  FAILURES:' -ForegroundColor Red
    foreach ($f in $failed) { Write-Host "    - $f" -ForegroundColor Red }
    Write-Host '==================================================' -ForegroundColor Red
    exit 1
}
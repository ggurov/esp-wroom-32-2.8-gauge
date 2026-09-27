<#
.SYNOPSIS
    Build, flash and monitor the 2.8" gauge.

.DESCRIPTION
    The board's CH340 has DTR wired to GPIO0 and RTS to EN, so esptool can
    reset it into the ROM bootloader and restart it afterwards by itself.
    There is nothing to press and no console command to type first.

    Note that opening the serial port resets the board - that is the wiring,
    not a fault; the app comes back about two seconds later.

.EXAMPLE
    tools\flash.ps1
    tools\flash.ps1 -Port COM7
    tools\flash.ps1 -NoMonitor
#>
[CmdletBinding()]
param(
    [string] $Port = 'COM49',
    [switch] $NoMonitor
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }

Write-Step 'build'
& (Join-Path $PSScriptRoot 'idf.bat') build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$idfArgs = @('-p', $Port, 'flash')
if (-not $NoMonitor) { $idfArgs += 'monitor' }

Write-Step "idf.py $($idfArgs -join ' ')"
& (Join-Path $PSScriptRoot 'idf.bat') @idfArgs
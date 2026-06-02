param(
    [ValidateSet("all", "debug", "release")]
    [string]$Config = "all",
    [switch]$NoBuild,
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Invoke-Step {
    param(
        [string]$Title,
        [scriptblock]$Command
    )

    Write-Host "[test] $Title"
    & $Command
}

function Invoke-TestExe {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Test executable not found: $Path"
    }
    $global:LASTEXITCODE = 0
    & $Path
    $exitCode = if ($null -eq $LASTEXITCODE) { 0 } else { $LASTEXITCODE }
    if ($exitCode -ne 0) {
        throw "Integration smoke failed: $Path"
    }
}

$root = [System.IO.Path]::GetFullPath($ProjectRoot)
$buildBat = Join-Path $root "build.bat"
if (-not (Test-Path -LiteralPath $buildBat)) {
    throw "build.bat not found: $buildBat"
}

$configs = if ($Config -eq "all") { @("debug", "release") } else { @($Config) }

foreach ($item in $configs) {
    if (-not $NoBuild) {
        if ($item -eq "debug") {
            Invoke-Step "build Debug integration tests" {
                cmd.exe /c "`"$buildBat`" --debug-only --tests-only"
                if ($LASTEXITCODE -ne 0) {
                    throw "Debug test build failed."
                }
            }
        } else {
            Invoke-Step "build Release integration tests" {
                cmd.exe /c "`"$buildBat`" --release-only --tests-only"
                if ($LASTEXITCODE -ne 0) {
                    throw "Release test build failed."
                }
            }
        }
    }

    if ($item -eq "debug") {
        Invoke-Step "run Debug integration smoke" {
            Invoke-TestExe (Join-Path $root "bin/debug/yaos_tests.exe")
        }
    } else {
        Invoke-Step "run Release integration smoke" {
            Invoke-TestExe (Join-Path $root "bin/yaos_tests.exe")
        }
    }
}

Write-Host "[test] OK"

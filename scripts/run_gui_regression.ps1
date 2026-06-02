param(
    [ValidateSet("all", "debug", "release")]
    [string]$Config = "debug",
    [ValidateSet("runtime-page", "provider-models", "oauth-status", "control-templates", "daemon-provider-config", "remote-provider-config", "chat-provider", "extensions-catalog", "all")]
    [string]$Case = "runtime-page",
    [switch]$NoBuild,
    [int]$TimeoutMs = 80000,
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Invoke-CommandChecked {
    param(
        [string]$Title,
        [scriptblock]$Command
    )
    Write-Host "[gui-regression] $Title"
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "Step failed: $Title"
    }
}

function Stop-GuiRegressionChildProcesses {
    param(
        [string]$CaseName,
        [int]$GuiProcessId
    )

    if ($CaseName -eq "daemon-provider-config") {
        $serverName = "gui-regression-$GuiProcessId"
        Get-CimInstance Win32_Process -Filter "name = 'yaosd.exe'" |
            Where-Object { $_.CommandLine -like "*--server $serverName*" } |
            ForEach-Object {
                try {
                    Stop-Process -Id $_.ProcessId -Force
                } catch {
                }
            }
    }
}

function Invoke-GuiRegression {
    param(
        [string]$Exe,
        [string]$CaseName,
        [string]$JsonOut,
        [int]$TimeoutMs,
        [int]$MaxUiGapMs,
        [string]$HomeDir
    )

    Write-Host "[gui-regression] $CaseName"
    $arguments = @(
        "gui-regression",
        "--case", $CaseName,
        "--json-out", $JsonOut,
        "--timeout-ms", [string]$TimeoutMs,
        "--max-ui-gap-ms", [string]$MaxUiGapMs
    )

    if (-not (Test-Path -LiteralPath $HomeDir)) {
        New-Item -ItemType Directory -Force -Path $HomeDir | Out-Null
    }

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Exe
    $psi.WorkingDirectory = Split-Path -Parent $Exe
    $psi.UseShellExecute = $false
    $psi.Arguments = ($arguments | ForEach-Object {
        if ($_ -match '[\s"]') {
            '"' + ($_ -replace '"', '\"') + '"'
        } else {
            $_
        }
    }) -join " "
    $psi.EnvironmentVariables["HOME"] = $HomeDir
    $psi.EnvironmentVariables["USERPROFILE"] = $HomeDir

    $process = [System.Diagnostics.Process]::Start($psi)
    try {
        $completed = $process.WaitForExit($TimeoutMs + 30000)
        if (-not $completed) {
            $resultOk = $false
            if (Test-Path -LiteralPath $JsonOut) {
                try {
                    $partialResult = Get-Content -Encoding UTF8 -LiteralPath $JsonOut -Raw | ConvertFrom-Json
                    $resultOk = [bool]$partialResult.ok
                } catch {
                    $resultOk = $false
                }
            }
            try {
                $process.Kill()
            } catch {
            }
            if ($resultOk) {
                Write-Host "[gui-regression] process did not exit after writing an OK result; terminated stale GUI process"
                return
            }
            throw "GUI regression process timed out: $CaseName"
        }
        if ($process.ExitCode -ne 0) {
            throw "GUI regression process failed with exit code $($process.ExitCode): $CaseName"
        }
    } finally {
        Stop-GuiRegressionChildProcesses $CaseName $process.Id
    }
}

$root = [System.IO.Path]::GetFullPath($ProjectRoot)
$buildBat = Join-Path $root "build.bat"
$configs = if ($Config -eq "all") { @("debug", "release") } else { @($Config) }
$cases = if ($Case -eq "all") {
    @(
        "runtime-page",
        "provider-models",
        "oauth-status",
        "control-templates",
        "daemon-provider-config",
        "remote-provider-config",
        "extensions-catalog",
        "chat-provider"
    )
} else { @($Case) }

foreach ($configName in $configs) {
    if (-not $NoBuild) {
        $buildArgs = if ($cases -contains "daemon-provider-config") { @() } else { @("--skip-daemon") }
        if ($configName -eq "debug") {
            Invoke-CommandChecked "build Debug GUI" {
                cmd.exe /c "`"$buildBat`" --debug-only $($buildArgs -join ' ')"
            }
        } else {
            Invoke-CommandChecked "build Release GUI" {
                cmd.exe /c "`"$buildBat`" --release-only $($buildArgs -join ' ')"
            }
        }
    }

    $exe = if ($configName -eq "debug") {
        Join-Path $root "bin/debug/yaos.exe"
    } else {
        Join-Path $root "bin/yaos.exe"
    }
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "YAOS executable not found: $exe"
    }

    foreach ($caseName in $cases) {
        $outDir = Join-Path $root "build/$configName"
        if (-not (Test-Path -LiteralPath $outDir)) {
            New-Item -ItemType Directory -Force -Path $outDir | Out-Null
        }
        $jsonOut = Join-Path $outDir ("gui-regression-{0}-{1}.json" -f $caseName, $configName)
        $homeDir = Join-Path $outDir ("gui-home-{0}-{1}" -f $caseName, [guid]::NewGuid().ToString("N"))
        $maxUiGapMs = if ($caseName -in @("daemon-provider-config", "remote-provider-config")) { 8000 } else { 4000 }
        Invoke-GuiRegression $exe $caseName $jsonOut $TimeoutMs $maxUiGapMs $homeDir
        $result = Get-Content -Encoding UTF8 -LiteralPath $jsonOut -Raw | ConvertFrom-Json
        if (-not $result.ok) {
            throw "GUI regression failed: $jsonOut"
        }
        Write-Host "[gui-regression] OK $jsonOut"
    }
}

Write-Host "[gui-regression] OK"

param(
    [ValidateSet("all", "debug", "release")]
    [string]$Config = "all",
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Add-Finding {
    param(
        [System.Collections.Generic.List[string]]$Findings,
        [string]$Message
    )
    $Findings.Add($Message) | Out-Null
}

function Find-Dumpbin {
    $candidates = @(
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.28.29910\bin\HostX64\x64\dumpbin.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\14.28.29910\bin\HostX64\x64\dumpbin.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    $roots = @(
        "C:\Program Files (x86)\Microsoft Visual Studio\2019",
        "C:\Program Files\Microsoft Visual Studio\2022"
    )
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) {
            continue
        }
        $found = Get-ChildItem -LiteralPath $root -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -like "*\bin\HostX64\x64\dumpbin.exe" } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($found) {
            return $found.FullName
        }
    }
    throw "dumpbin.exe not found."
}

function Get-Dependencies {
    param(
        [string]$Dumpbin,
        [string]$ExePath,
        [string]$OutputPath
    )
    if (-not (Test-Path -LiteralPath $ExePath)) {
        throw "Executable not found: $ExePath"
    }
    $outputDir = Split-Path -Parent $OutputPath
    if (-not (Test-Path -LiteralPath $outputDir)) {
        New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    }
    & $Dumpbin /dependents $ExePath | Set-Content -Encoding UTF8 -LiteralPath $OutputPath
    return @(Select-String -LiteralPath $OutputPath -Pattern "^\s+[^\s]+\.dll$" |
        ForEach-Object { $_.Line.Trim() })
}

function Test-NoFiles {
    param(
        [System.Collections.Generic.List[string]]$Findings,
        [string]$Directory,
        [string[]]$Patterns,
        [string]$Reason
    )
    foreach ($pattern in $Patterns) {
        $matches = @(Get-ChildItem -LiteralPath $Directory -File -Filter $pattern -ErrorAction SilentlyContinue)
        foreach ($match in $matches) {
            Add-Finding $Findings "$Reason`: $($match.FullName)"
        }
    }
}

function Test-RequiredFiles {
    param(
        [System.Collections.Generic.List[string]]$Findings,
        [string]$Directory,
        [string[]]$Names
    )
    foreach ($name in $Names) {
        $path = Join-Path $Directory $name
        if (-not (Test-Path -LiteralPath $path)) {
            Add-Finding $Findings "Required runtime file missing: $path"
        }
    }
}

function Test-ForbiddenDeps {
    param(
        [System.Collections.Generic.List[string]]$Findings,
        [string]$Target,
        [string[]]$Deps,
        [string[]]$Forbidden
    )
    foreach ($name in $Forbidden) {
        if ($Deps -contains $name) {
            Add-Finding $Findings "$Target must not depend on $name"
        }
    }
}

$root = [System.IO.Path]::GetFullPath($ProjectRoot)
$dumpbin = Find-Dumpbin
$findings = New-Object "System.Collections.Generic.List[string]"
$configs = if ($Config -eq "all") { @("release", "debug") } else { @($Config) }

foreach ($item in $configs) {
    if ($item -eq "release") {
        $bin = Join-Path $root "bin"
        Test-RequiredFiles $findings $bin @("yaos.exe", "yaosd.exe", "FastNet.dll", "Qt5Core.dll", "Qt5Sql.dll", "libcrypto-3-x64.dll", "libssl-3-x64.dll")
        Test-NoFiles $findings $bin @("FastNetd.dll", "Qt5*d.dll", "msvcp140d.dll", "vcruntime140d.dll", "vcruntime140_1d.dll", "ucrtbased.dll", "libssl-1_1-x64.dll", "libcrypto-1_1-x64.dll") "Release directory contains forbidden Debug/legacy DLL"

        $yaosDeps = Get-Dependencies $dumpbin (Join-Path $bin "yaos.exe") (Join-Path $root "build/release/dependents-yaos.txt")
        $yaosdDeps = Get-Dependencies $dumpbin (Join-Path $bin "yaosd.exe") (Join-Path $root "build/release/dependents-yaosd.txt")
        Test-ForbiddenDeps $findings "Release yaos.exe" $yaosDeps @("Qt5Network.dll")
        Test-ForbiddenDeps $findings "Release yaosd.exe" $yaosdDeps @("Qt5Gui.dll", "Qt5Concurrent.dll", "Qt5Network.dll")
    } else {
        $bin = Join-Path $root "bin/debug"
        Test-RequiredFiles $findings $bin @("yaos.exe", "yaosd.exe", "FastNetd.dll", "Qt5Cored.dll", "Qt5Sqld.dll", "msvcp140d.dll", "vcruntime140d.dll", "vcruntime140_1d.dll", "ucrtbased.dll", "libcrypto-3-x64.dll", "libssl-3-x64.dll")
        Test-NoFiles $findings $bin @("libssl-1_1-x64.dll", "libcrypto-1_1-x64.dll") "Debug directory contains forbidden legacy OpenSSL DLL"

        $yaosDeps = Get-Dependencies $dumpbin (Join-Path $bin "yaos.exe") (Join-Path $root "build/debug/dependents-yaos.txt")
        $yaosdDeps = Get-Dependencies $dumpbin (Join-Path $bin "yaosd.exe") (Join-Path $root "build/debug/dependents-yaosd.txt")
        Test-ForbiddenDeps $findings "Debug yaos.exe" $yaosDeps @("Qt5Networkd.dll")
        Test-ForbiddenDeps $findings "Debug yaosd.exe" $yaosdDeps @("Qt5Guid.dll", "Qt5Concurrentd.dll", "Qt5Networkd.dll")
    }
}

if ($findings.Count -gt 0) {
    Write-Host "[runtime-layout] FAILED: $($findings.Count) issue(s)." -ForegroundColor Red
    foreach ($finding in $findings) {
        Write-Host $finding
    }
    exit 1
}

Write-Host "[runtime-layout] OK"
exit 0

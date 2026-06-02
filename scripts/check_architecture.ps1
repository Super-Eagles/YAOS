param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Resolve-ProjectPath {
    param([string]$Path)
    return [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot $Path.Replace("/", "\")))
}

function Add-Finding {
    param(
        [System.Collections.Generic.List[object]]$Findings,
        [string]$Rule,
        [string]$File,
        [int]$Line,
        [string]$Text
    )
    $Findings.Add([pscustomobject]@{
        Rule = $Rule
        File = $File
        Line = $Line
        Text = $Text.Trim()
    }) | Out-Null
}

function Get-ModuleFiles {
    param([string]$ModulePri)

    $path = Resolve-ProjectPath $ModulePri
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Module file not found: $path"
    }

    $files = New-Object "System.Collections.Generic.HashSet[string]"
    Get-Content -LiteralPath $path | ForEach-Object {
        if ($_ -match "(src/[A-Za-z0-9_./-]+\.(cpp|h|hpp))") {
            $file = Resolve-ProjectPath $Matches[1]
            if (Test-Path -LiteralPath $file) {
                [void]$files.Add($file)
            }
        }
    }
    return @($files)
}

function Test-RegexInFiles {
    param(
        [System.Collections.Generic.List[object]]$Findings,
        [string]$Rule,
        [string[]]$Files,
        [string]$Pattern
    )

    foreach ($file in $Files) {
        if (-not (Test-Path -LiteralPath $file)) {
            continue
        }
        Select-String -LiteralPath $file -Pattern $Pattern | ForEach-Object {
            Add-Finding $Findings $Rule $_.Path $_.LineNumber $_.Line
        }
    }
}

function Test-RemoteStudioOverrides {
    param(
        [System.Collections.Generic.List[object]]$Findings
    )

    $headerPath = Resolve-ProjectPath "src/ui/StudioBackend.h"
    $uiSourceFiles = @(
        Get-ChildItem -LiteralPath (Resolve-ProjectPath "src/ui") -File -Include "*.cpp", "*.h" |
            ForEach-Object { $_.FullName }
    )
    $uiSourceText = (($uiSourceFiles | ForEach-Object {
        Get-Content -LiteralPath $_ -Raw
    }) -join "`n")

    $required = @(
        @{ Method = "extensionCatalog"; Invoke = "studio.extensionCatalog" },
        @{ Method = "installCatalogEntry"; Invoke = "studio.installCatalogEntry" },
        @{ Method = "saveConfiguration"; Invoke = "studio.saveConfiguration" },
        @{ Method = "fetchProviderModels"; Invoke = "studio.fetchProviderModels" },
        @{ Method = "providerAuthStatus"; Invoke = "studio.providerAuthStatus" },
        @{ Method = "startProviderDeviceFlow"; Invoke = "studio.startProviderDeviceFlow" },
        @{ Method = "pollProviderDeviceFlow"; Invoke = "studio.pollProviderDeviceFlow" },
        @{ Method = "refreshProviderOAuth"; Invoke = "studio.refreshProviderOAuth" },
        @{ Method = "logoutProviderOAuth"; Invoke = "studio.logoutProviderOAuth" },
        @{ Method = "startProviderBrowserOAuth"; Invoke = "studio.startProviderBrowserOAuth" },
        @{ Method = "completeProviderBrowserOAuth"; Invoke = "studio.completeProviderBrowserOAuth" },
        @{ Method = "pushDelegationTemplatesToControl"; Invoke = "studio.pushDelegationTemplatesToControl" },
        @{ Method = "pullDelegationTemplatesFromControl"; Invoke = "studio.pullDelegationTemplatesFromControl" }
    )

    foreach ($item in $required) {
        $method = [regex]::Escape($item.Method)
        $overridePattern = "RemoteStudioBackend::${method}\s*\("
        if ($uiSourceText -notmatch $overridePattern) {
            Add-Finding $Findings `
                "remote-studio-missing-override" `
                $headerPath `
                1 `
                "RemoteStudioBackend must override $($item.Method) because the embedded implementation performs GUI-process business/config work."
        }

        $invokePattern = [regex]::Escape($item.Invoke)
        if ($uiSourceText -notmatch $invokePattern) {
            Add-Finding $Findings `
                "remote-studio-missing-invoke" `
                $headerPath `
                1 `
                "RemoteStudioBackend override for $($item.Method) must call $($item.Invoke)."
        }
    }
}

$findings = New-Object "System.Collections.Generic.List[object]"

$baseFiles = @(Get-ModuleFiles "qmake/modules/base.pri")
$businessFiles = @(Get-ModuleFiles "qmake/modules/business.pri")
$baseBusinessFiles = @($baseFiles + $businessFiles | Sort-Object -Unique)

$sourceFiles = @(
    Get-ChildItem -LiteralPath (Resolve-ProjectPath "src") -Recurse -File |
        Where-Object { $_.Extension -in @(".cpp", ".h", ".hpp") } |
        ForEach-Object { $_.FullName }
    Get-ChildItem -LiteralPath $ProjectRoot -File |
        Where-Object { $_.Extension -eq ".pro" } |
        ForEach-Object { $_.FullName }
    Get-ChildItem -LiteralPath (Resolve-ProjectPath "qmake") -Recurse -File |
        Where-Object { $_.Extension -eq ".pri" } |
        ForEach-Object { $_.FullName }
) | Sort-Object -Unique

$forbiddenNetworkPattern = "\b(QSslSocket|QTcpServer|QTcpSocket|QUdpSocket|QLocalServer|QLocalSocket|QNetworkAccessManager|QNetworkReply|QNetworkRequest|QWebSocket|QHostAddress)\b"
Test-RegexInFiles $findings "forbidden-qt-network-api" $sourceFiles $forbiddenNetworkPattern
Test-RegexInFiles $findings "forbidden-qmake-network-module" $sourceFiles "QT\s*\+=.*\bnetwork\b"
Test-RegexInFiles $findings "forbidden-openSSL-1-1-runtime" $sourceFiles "lib(crypto|ssl)-1_1-x64\.dll"
Test-RegexInFiles $findings "forbidden-quick-style-basic" $sourceFiles "QQuickStyle::setStyle\s*\(\s*QStringLiteral\s*\(\s*`"Basic`"\s*\)"

$qtGuiHeaderPattern = "#\s*include\s*[<`"](?:QtGui/)?(QTextDocument|QImage|QPixmap|QPainter|QColor|QFont|QGuiApplication|QApplication|QIcon|QDesktopServices|QClipboard|QScreen|QWindow)\b"
Test-RegexInFiles $findings "base-business-must-not-include-qtgui" $baseBusinessFiles $qtGuiHeaderPattern
Test-RegexInFiles $findings "business-must-not-include-ui" $businessFiles "#\s*include\s*[<`"](?:\.\./)*src/ui/|#\s*include\s*[<`"](?:\.\./)*ui/"

$baseForbiddenIncludePattern = "#\s*include\s*[<`"](?:\.\./)*(?:src/)?(ui|providers|channels|agent)/"
Test-RegexInFiles $findings "base-must-not-include-business-or-ui" $baseFiles $baseForbiddenIncludePattern
Test-RemoteStudioOverrides $findings

if ($findings.Count -gt 0) {
    Write-Host "[architecture-check] FAILED: $($findings.Count) issue(s) found." -ForegroundColor Red
    foreach ($finding in $findings) {
        $relative = Resolve-Path -LiteralPath $finding.File -Relative -ErrorAction SilentlyContinue
        if (-not $relative) {
            $relative = $finding.File
        }
        Write-Host ("[{0}] {1}:{2}: {3}" -f $finding.Rule, $relative, $finding.Line, $finding.Text)
    }
    exit 1
}

Write-Host "[architecture-check] OK"
exit 0

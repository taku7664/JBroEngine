param(
    [ValidateSet("Debug_Editor", "Release_Editor")]
    [string]$Configuration = "Debug_Editor",
    [string]$OutputRoot = "Dist",
    [switch]$IncludeSymbols
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptDir "..")
$solutionPath = Join-Path $repoRoot "JBroEngine.sln"
$buildDir = Join-Path $repoRoot ("Build\" + $Configuration)
$distRoot = Join-Path $repoRoot $OutputRoot
$distDir = Join-Path $distRoot ("JBroEngineEditor-" + $Configuration)

function Find-MSBuild {
    # vswhere 로 설치된 최신 VS 의 MSBuild 를 우선 탐색한다(버전 무관 — VS2022/VS18/이후 모두).
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $found = & $vswhere -latest -prerelease -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" 2>$null | Select-Object -First 1
        if ($found -and (Test-Path -LiteralPath $found)) {
            return $found
        }
    }
    $candidates = @()
    if ($env:VSINSTALLDIR) {
        $candidates += Join-Path $env:VSINSTALLDIR "MSBuild\Current\Bin\MSBuild.exe"
    }
    # vswhere 가 없거나 실패하면 표준 설치 경로를 버전 폴더 무관으로 훑는다(2022 하드코딩 금지).
    $roots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) | Where-Object { $_ }
    $editions = @("Community", "Professional", "Enterprise", "BuildTools")
    foreach ($root in $roots) {
        $vsRoot = Join-Path $root "Microsoft Visual Studio"
        if (-not (Test-Path -LiteralPath $vsRoot)) { continue }
        foreach ($verDir in (Get-ChildItem -LiteralPath $vsRoot -Directory -ErrorAction SilentlyContinue)) {
            foreach ($edition in $editions) {
                $candidates += Join-Path $verDir.FullName ($edition + "\MSBuild\Current\Bin\MSBuild.exe")
            }
        }
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }
    return "MSBuild.exe"
}

function Copy-DirectoryClean {
    param(
        [Parameter(Mandatory=$true)][string]$Source,
        [Parameter(Mandatory=$true)][string]$Destination
    )

    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Destination -Recurse -Force
    }
}

$msbuild = Find-MSBuild
Write-Host "Building editor: $Configuration"
& $msbuild $solutionPath /m /p:Configuration=$Configuration /p:Platform=x64 /v:minimal /nr:false
if ($LASTEXITCODE -ne 0) {
    throw "Editor build failed. ExitCode=$LASTEXITCODE"
}

if (Test-Path -LiteralPath $distDir) {
    $resolvedDist = (Resolve-Path -LiteralPath $distDir).Path
    $resolvedRoot = (Resolve-Path -LiteralPath $repoRoot).Path
    if (-not $resolvedDist.StartsWith($resolvedRoot)) {
        throw "Refusing to delete outside repository: $resolvedDist"
    }
    Remove-Item -LiteralPath $distDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $distDir | Out-Null

$application = Join-Path $buildDir "Application.exe"
if (-not (Test-Path -LiteralPath $application)) {
    throw "Application.exe was not found: $application"
}
Copy-Item -LiteralPath $application -Destination $distDir -Force

Get-ChildItem -LiteralPath $buildDir -Filter "*.dll" -File | Where-Object {
    $_.Name -notlike "GameScript*.dll" -and $_.Name -notlike "GameScriptSample*.dll"
} | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $distDir -Force
}

if ($IncludeSymbols) {
    Get-ChildItem -LiteralPath $buildDir -Filter "*.pdb" -File | Where-Object {
        $_.Name -notlike "GameScript*.pdb" -and $_.Name -notlike "GameScriptSample*.pdb"
    } | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $distDir -Force
    }
}

Copy-DirectoryClean -Source (Join-Path $repoRoot "Localization") -Destination (Join-Path $distDir "Localization")
Copy-DirectoryClean -Source (Join-Path $repoRoot "SDK") -Destination (Join-Path $distDir "SDK")

Write-Host "Packaged editor: $distDir"

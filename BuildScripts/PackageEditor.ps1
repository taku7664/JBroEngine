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
    $candidates = @()
    if ($env:VSINSTALLDIR) {
        $candidates += Join-Path $env:VSINSTALLDIR "MSBuild\Current\Bin\MSBuild.exe"
    }
    $roots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) | Where-Object { $_ }
    $editions = @("Community", "Professional", "Enterprise", "BuildTools")
    foreach ($root in $roots) {
        foreach ($edition in $editions) {
            $candidates += Join-Path $root ("Microsoft Visual Studio\2022\" + $edition + "\MSBuild\Current\Bin\MSBuild.exe")
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

param(
    [Parameter(Mandatory=$true)]
    [string]$Project,

    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [string]$OutputRoot = "",
    [string]$EmsdkRoot = "",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildGameScript = Join-Path $scriptDir "BuildGame.ps1"
if (-not (Test-Path -LiteralPath $buildGameScript -PathType Leaf)) {
    throw "BuildGame.ps1 was not found: $buildGameScript"
}

$arguments = @{
    Project = $Project
    Platform = "Web"
    Configuration = $Configuration
}

if (-not [string]::IsNullOrWhiteSpace($OutputRoot)) {
    $arguments.OutputRoot = $OutputRoot
}
if (-not [string]::IsNullOrWhiteSpace($EmsdkRoot)) {
    $arguments.EmsdkRoot = $EmsdkRoot
}
if ($Clean) {
    $arguments.Clean = $true
}

& $buildGameScript @arguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

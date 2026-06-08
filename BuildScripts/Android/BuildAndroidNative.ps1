<#
.SYNOPSIS
    Cross-compiles libJBroGame.so for Android using the NDK clang directly.

.DESCRIPTION
    Mirrors the Web build model (BuildScripts/BuildGame.ps1 web path): a single
    compiler invocation over an explicit source list, no CMake. CMake's
    try_compile crashes on this machine (0xC0000409 fastfail) across every
    toolchain (MSVC / clang / Android), so the native build invokes the NDK
    clang++ straight, exactly like the web build invokes emcc.

    Sources:
      - BuildScripts/Android/android_engine_sources.txt   (engine + yaml-cpp)
      - <Project>/Contents/{pch,GameModule,GeneratedScriptRegistry}.cpp
      - <Project>/Contents/Scripts/*.cpp                  (static game scripts)
      - Application/Entry/AndroidMain.cpp                  (if present)
      - $NDK/.../native_app_glue/android_native_app_glue.c (if present)

    Output -> Build\Android\<abi>\<Configuration>\libJBroGame.so
    (the location BuildGame.ps1 -Platform Android already looks for).
#>
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [ValidateSet("arm64-v8a", "armeabi-v7a", "x86_64", "x86")]
    [string]$Abi = "arm64-v8a",
    [int]$ApiLevel = 26,

    [string]$ContentPath = "",
    [string]$NdkRoot = ""
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot  = (Resolve-Path (Join-Path $scriptDir "..\..")).Path

# --- Resolve NDK ---------------------------------------------------------
if ([string]::IsNullOrWhiteSpace($NdkRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($env:ANDROID_NDK_HOME)) {
        $NdkRoot = $env:ANDROID_NDK_HOME
    } else {
        $NdkRoot = "C:\Android\ndk\27.3.13750724"
    }
}
if (-not (Test-Path -LiteralPath $NdkRoot -PathType Container)) {
    throw "Android NDK not found: $NdkRoot"
}
$clangxx = Join-Path $NdkRoot "toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe"
if (-not (Test-Path -LiteralPath $clangxx -PathType Leaf)) {
    throw "NDK clang++ not found: $clangxx"
}

# --- ABI -> target triple ------------------------------------------------
$triplePrefix = switch ($Abi) {
    "arm64-v8a"   { "aarch64-linux-android" }
    "armeabi-v7a" { "armv7a-linux-androideabi" }
    "x86_64"      { "x86_64-linux-android" }
    "x86"         { "i686-linux-android" }
}
$target = "$triplePrefix$ApiLevel"

# --- Content path --------------------------------------------------------
if ([string]::IsNullOrWhiteSpace($ContentPath)) {
    $ContentPath = Join-Path $repoRoot "SampleProject\Contents"
}
$ContentPath = (Resolve-Path -LiteralPath $ContentPath).Path

# --- Gather sources ------------------------------------------------------
$sourceListFile = Join-Path $scriptDir "android_engine_sources.txt"
if (-not (Test-Path -LiteralPath $sourceListFile -PathType Leaf)) {
    throw "Source list not found: $sourceListFile"
}
$sources = @()
foreach ($line in Get-Content -LiteralPath $sourceListFile) {
    $rel = $line.Trim()
    if ([string]::IsNullOrWhiteSpace($rel)) { continue }
    if ($rel.StartsWith("#")) { continue }
    $abs = Join-Path $repoRoot $rel
    if (-not (Test-Path -LiteralPath $abs -PathType Leaf)) {
        throw "Source listed but missing: $abs"
    }
    $sources += $abs
}

# Game static script sources.
foreach ($name in @("pch.cpp", "GameModule.cpp", "GeneratedScriptRegistry.cpp")) {
    $candidate = Join-Path $ContentPath $name
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $sources += $candidate }
}
$scriptsDir = Join-Path $ContentPath "Scripts"
if (Test-Path -LiteralPath $scriptsDir -PathType Container) {
    $sources += Get-ChildItem -LiteralPath $scriptsDir -Recurse -Filter "*.cpp" -File |
        Sort-Object FullName | ForEach-Object { $_.FullName }
}

# Android entry + native_app_glue (present from M3 onward).
# native_app_glue.c is C; clang++ would miscompile it as C++. It is only needed
# once AndroidMain exists (M3), where it is compiled separately as C. Until then
# (no entry), skip it so the engine .so builds clean.
$androidMain = Join-Path $repoRoot "Application\Entry\AndroidMain.cpp"
$haveEntry = Test-Path -LiteralPath $androidMain -PathType Leaf
if ($haveEntry) { $sources += $androidMain }
$glue = Join-Path $NdkRoot "sources\android\native_app_glue\android_native_app_glue.c"
$haveGlue = $haveEntry -and (Test-Path -LiteralPath $glue -PathType Leaf)

# --- Output --------------------------------------------------------------
$stageDir = Join-Path $repoRoot ("Build\Android\{0}\{1}" -f $Abi, $Configuration)
New-Item -ItemType Directory -Path $stageDir -Force | Out-Null
$outputSo = Join-Path $stageDir "libJBroGame.so"

# --- Compose argument response file --------------------------------------
$optArgs = if ($Configuration -eq "Debug") { @("-O0", "-g") } else { @("-O2", "-DNDEBUG") }

$args = @(
    "--target=$target"
    "-fPIC"
    "-shared"
    "-std=c++20"
    "-static-libstdc++"     # ANDROID_STL=c++_static
    "-fvisibility=default"
)
$args += $optArgs
$args += @(
    "-I$repoRoot"
    "-I$(Join-Path $repoRoot 'Application')"
    "-I$(Join-Path $repoRoot 'Engine')"
    "-I$(Join-Path $repoRoot 'Engine\ThirdParty')"
    "-I$(Join-Path $repoRoot 'Engine\ThirdParty\yaml-cpp\src')"
    "-I$ContentPath"
    "-I$(Join-Path $ContentPath 'Scripts')"
)
if ($haveGlue) {
    $args += "-I$(Join-Path $NdkRoot 'sources\android\native_app_glue')"
}
$args += @(
    "-DJBRO_PLATFORM_ANDROID"
    "-DJBRO_GAME"
    "-DJBRO_RHI_VULKAN"
    "-DYAML_CPP_STATIC_DEFINE"
    "-DVK_USE_PLATFORM_ANDROID_KHR"
)
$args += $sources
$args += @("-lvulkan", "-llog", "-landroid")
$args += @("-o", $outputSo)

# C sources (native_app_glue) compiled by clang++ are fine; force C where needed
# is unnecessary — clang detects .c. Write a response file (UTF-8, no BOM) to
# survive the long, non-ASCII argument list.
$rspFile = Join-Path $stageDir "android_build.rsp"
# clang response files treat backslash as an escape character, so emit all
# paths with forward slashes (clang accepts them on Windows). Quote args that
# contain whitespace.
$rspLines = $args | ForEach-Object {
    $a = $_ -replace '\\', '/'
    if ($a -match '\s') { '"' + $a + '"' } else { $a }
}
[System.IO.File]::WriteAllLines($rspFile, $rspLines, (New-Object System.Text.UTF8Encoding($false)))

Write-Host "=== JBroEngine Android native build (direct clang) ==="
Write-Host "  NDK         : $NdkRoot"
Write-Host "  Target      : $target"
Write-Host "  ABI / Config: $Abi / $Configuration"
Write-Host "  Content     : $ContentPath"
Write-Host "  Sources     : $($sources.Count)"
Write-Host "  AndroidMain : $haveEntry   native_app_glue: $haveGlue"
Write-Host "  Output      : $outputSo"
Write-Host ""

& $clangxx "@$rspFile"
if ($LASTEXITCODE -ne 0) { throw "Android native build failed. ExitCode=$LASTEXITCODE" }

if (-not (Test-Path -LiteralPath $outputSo -PathType Leaf)) {
    throw "Build reported success but output missing: $outputSo"
}
Write-Host ""
Write-Host "libJBroGame.so -> $outputSo"
Write-Host "=== Done ==="

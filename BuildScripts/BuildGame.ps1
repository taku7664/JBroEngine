param(
    [Parameter(Mandatory=$true)]
    [string]$Project,

    [ValidateSet("Windows", "Web")]
    [string]$Platform = "Windows",

    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [string]$OutputRoot = "",
    [switch]$IncludeSymbols,
    [switch]$SkipEngineBuild,
    [switch]$SkipScriptBuild,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path
$solutionPath = Join-Path $repoRoot "JBroEngine.sln"

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

function ConvertFrom-JBroScalar {
    param([string]$Value)

    $trimmed = $Value.Trim()
    if ($trimmed.Length -ge 2) {
        if (($trimmed[0] -eq '"' -and $trimmed[$trimmed.Length - 1] -eq '"') -or
            ($trimmed[0] -eq "'" -and $trimmed[$trimmed.Length - 1] -eq "'")) {
            return $trimmed.Substring(1, $trimmed.Length - 2)
        }
    }
    return $trimmed
}

function ConvertTo-SafeFileName {
    param([string]$Value)

    $invalidChars = [System.IO.Path]::GetInvalidFileNameChars()
    $result = $Value
    foreach ($char in $invalidChars) {
        $result = $result.Replace($char, '_')
    }
    if ([string]::IsNullOrWhiteSpace($result)) {
        return "Game"
    }
    return $result
}

function Read-JBroProject {
    param([string]$ProjectPath)

    $projectInfo = [ordered]@{
        Version = 1
        RootPath = "."
        ResolutionWidth = 1920
        ResolutionHeight = 1080
        LastOpenedScenePath = ""
        Build = [ordered]@{
            ProductName = [System.IO.Path]::GetFileNameWithoutExtension($ProjectPath)
            TargetPlatform = "Windows"
            BuildConfiguration = "Release"
            OutputDirectory = "Dist/Games"
            StartupScene = ""
            BuildScenes = @()
            ScriptMode = "DynamicLibrary"
            ScriptProjectPath = "Contents/GameScript.vcxproj"
            ScriptBuildConfiguration = "Release"
            ScriptOutputLibraryPath = "GameScript.dll"
        }
    }

    $inBuild = $false
    $inBuildScenes = $false
    foreach ($line in Get-Content -LiteralPath $ProjectPath) {
        if ($line.Trim().Length -eq 0) {
            continue
        }

        if ($line -match '^\s*-\s*(.+)$' -and $inBuild -and $inBuildScenes) {
            $projectInfo.Build.BuildScenes += ConvertFrom-JBroScalar $Matches[1]
            continue
        }

        if ($line -match '^([A-Za-z0-9_]+):\s*(.*)$') {
            $key = $Matches[1]
            $value = ConvertFrom-JBroScalar $Matches[2]
            $inBuild = $false
            $inBuildScenes = $false

            switch ($key) {
                "Version" { $projectInfo.Version = [int]$value }
                "RootPath" { $projectInfo.RootPath = $value }
                "ResolutionWidth" { $projectInfo.ResolutionWidth = [int]$value }
                "ResolutionHeight" { $projectInfo.ResolutionHeight = [int]$value }
                "LastOpenedScenePath" { $projectInfo.LastOpenedScenePath = $value }
                "Build" { $inBuild = $true }
            }
            continue
        }

        if ($line -match '^\s+([A-Za-z0-9_]+):\s*(.*)$' -and $inBuild) {
            $key = $Matches[1]
            $value = ConvertFrom-JBroScalar $Matches[2]
            $inBuildScenes = $false

            if ($key -eq "BuildScenes") {
                $inBuildScenes = $true
                continue
            }

            if ($projectInfo.Build.Contains($key)) {
                $projectInfo.Build[$key] = $value
            }
        }
    }

    if ([string]::IsNullOrWhiteSpace($projectInfo.Build.ProductName)) {
        $projectInfo.Build.ProductName = [System.IO.Path]::GetFileNameWithoutExtension($ProjectPath)
    }
    if ($projectInfo.Build.BuildScenes.Count -eq 0 -and
        -not [string]::IsNullOrWhiteSpace($projectInfo.Build.StartupScene)) {
        $projectInfo.Build.BuildScenes += $projectInfo.Build.StartupScene
    }

    return $projectInfo
}

function Copy-DirectoryClean {
    param(
        [Parameter(Mandatory=$true)][string]$Source,
        [Parameter(Mandatory=$true)][string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Source)) {
        New-Item -ItemType Directory -Force -Path $Destination | Out-Null
        return
    }

    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Destination -Recurse -Force
    }
}

function Remove-DirectoryInside {
    param(
        [Parameter(Mandatory=$true)][string]$Root,
        [Parameter(Mandatory=$true)][string]$Target
    )

    if (-not (Test-Path -LiteralPath $Target)) {
        return
    }

    if (-not (Test-Path -LiteralPath $Root)) {
        New-Item -ItemType Directory -Force -Path $Root | Out-Null
    }

    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $resolvedTarget = (Resolve-Path -LiteralPath $Target).Path
    $rootWithSeparator = $resolvedRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    if (-not $resolvedTarget.StartsWith($rootWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to delete outside output root: $resolvedTarget"
    }
    Remove-Item -LiteralPath $Target -Recurse -Force
}

function Assert-RootArtifactMissing {
    param(
        [Parameter(Mandatory=$true)][string]$PackageDir,
        [Parameter(Mandatory=$true)][string[]]$Names
    )

    foreach ($name in $Names) {
        $candidate = Join-Path $PackageDir $name
        if (Test-Path -LiteralPath $candidate) {
            throw "Forbidden editor-only artifact found in game package: $candidate"
        }
    }
}

function ConvertTo-AssetTypeValue {
    param(
        [string]$Type,
        [string]$Importer,
        [string]$AssetPath
    )

    $value = if ([string]::IsNullOrWhiteSpace($Type)) { $Importer } else { $Type }
    switch ($value) {
        "Texture" { return 1 }
        "Sprite" { return 1 }
        "Mesh" { return 2 }
        "Material" { return 3 }
        "Shader" { return 4 }
        "Scene" { return 5 }
        "Prefab" { return 6 }
        "Script" { return 7 }
        "Audio" { return 8 }
        "Custom" { return 9 }
    }

    $ext = [System.IO.Path]::GetExtension($AssetPath).ToLowerInvariant()
    switch ($ext) {
        ".png" { return 1 }
        ".jpg" { return 1 }
        ".jpeg" { return 1 }
        ".bmp" { return 1 }
        ".tga" { return 1 }
        ".jscene" { return 5 }
        ".jprefab" { return 6 }
        ".hlsl" { return 4 }
        ".fx" { return 4 }
        ".cpp" { return 7 }
        ".h" { return 7 }
        ".hpp" { return 7 }
        ".wav" { return 8 }
        ".mp3" { return 8 }
        ".flac" { return 8 }
        ".ogg" { return 8 }
        default { return 9 }
    }
}

function Read-JBroMeta {
    param(
        [Parameter(Mandatory=$true)][string]$MetaPath,
        [Parameter(Mandatory=$true)][string]$AssetRoot
    )

    $assetPath = $MetaPath.Substring(0, $MetaPath.Length - ".Jmeta".Length)
    if (-not (Test-Path -LiteralPath $assetPath -PathType Leaf)) {
        return $null
    }

    $meta = [ordered]@{
        Guid = ""
        Type = "Unknown"
        TypeValue = 0
        Version = 1
        Importer = "Default"
        ImportOptionsYaml = ""
        AssetPath = $assetPath
        RelativePath = ""
    }

    $lines = Get-Content -LiteralPath $MetaPath
    $collectImportOptions = $false
    $importOptionLines = @()
    foreach ($line in $lines) {
        if ($collectImportOptions) {
            if ($line -match '^[A-Za-z0-9_]+:\s*') {
                $collectImportOptions = $false
            } else {
                $importOptionLines += $line.Trim()
                continue
            }
        }

        if ($line -match '^([A-Za-z0-9_]+):\s*(.*)$') {
            $key = $Matches[1]
            $value = ConvertFrom-JBroScalar $Matches[2]
            switch ($key) {
                "Guid" { $meta.Guid = $value }
                "Type" { $meta.Type = $value }
                "Version" { $meta.Version = [int]$value }
                "Importer" { $meta.Importer = $value }
                "ImportOptions" {
                    $collectImportOptions = $true
                    if (-not [string]::IsNullOrWhiteSpace($value)) {
                        $importOptionLines += $value
                    }
                }
            }
        }
    }

    if ([string]::IsNullOrWhiteSpace($meta.Guid)) {
        throw "Asset meta has no Guid: $MetaPath"
    }

    $rootFull = [System.IO.Path]::GetFullPath($AssetRoot).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    $assetFull = [System.IO.Path]::GetFullPath($assetPath)
    $rootUri = New-Object System.Uri($rootFull)
    $assetUri = New-Object System.Uri($assetFull)
    $relative = [System.Uri]::UnescapeDataString($rootUri.MakeRelativeUri($assetUri).ToString()).Replace('\', '/')
    $meta.RelativePath = $relative
    $meta.TypeValue = ConvertTo-AssetTypeValue -Type $meta.Type -Importer $meta.Importer -AssetPath $assetPath
    $meta.ImportOptionsYaml = ($importOptionLines -join "`n").Trim()
    if ([string]::IsNullOrWhiteSpace($meta.ImportOptionsYaml)) {
        $meta.ImportOptionsYaml = "{}"
    }
    return $meta
}

function Write-JBroAssetPack {
    param(
        [Parameter(Mandatory=$true)][string]$AssetRoot,
        [Parameter(Mandatory=$true)][string]$PackPath
    )

    if (-not (Test-Path -LiteralPath $AssetRoot -PathType Container)) {
        throw "Asset root was not found: $AssetRoot"
    }

    $source = @"
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

public sealed class JBroPackEntry
{
    public string Guid = "";
    public string Path = "";
    public uint Type;
    public uint Version;
    public string Importer = "";
    public string ImportOptionsYaml = "";
}

public static class JBroPackWriter
{
    const ulong FnvOffset = 14695981039346656037UL;
    const ulong FnvPrime = 1099511628211UL;
    const ulong CryptKey = 0x9E3779B97F4A7C15UL;
    const uint HeaderSize = 72;

    sealed class Record
    {
        public JBroPackEntry Entry = new JBroPackEntry();
        public ulong Offset;
        public ulong StoredSize;
        public ulong PayloadHash;
        public string SourceExtension = "";
    }

    static ulong HashBytes(byte[] bytes)
    {
        ulong hash = FnvOffset;
        foreach (byte b in bytes)
        {
            hash ^= b;
            hash *= FnvPrime;
        }
        hash ^= (ulong)bytes.LongLength;
        hash *= FnvPrime;
        return hash == 0 ? FnvOffset : hash;
    }

    static void HashAppend(ref ulong hash, byte[] bytes)
    {
        foreach (byte b in bytes)
        {
            hash ^= b;
            hash *= FnvPrime;
        }
    }

    static void CryptBytes(byte[] bytes, ulong seed)
    {
        ulong state = seed ^ CryptKey;
        for (int i = 0; i < bytes.Length; ++i)
        {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            bytes[i] = (byte)(bytes[i] ^ (byte)((state >> ((i & 7) * 8)) & 0xFF));
        }
    }

    static void WriteString(BinaryWriter writer, string value)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(value ?? "");
        writer.Write((uint)bytes.Length);
        writer.Write(bytes);
    }

    static byte[] SerializeIndex(List<Record> records, string packId)
    {
        using (var ms = new MemoryStream())
        using (var writer = new BinaryWriter(ms, Encoding.UTF8))
        {
            writer.Write((uint)1);
            writer.Write((ulong)records.Count);
            foreach (Record record in records)
            {
                WriteString(writer, record.Entry.Guid);
                WriteString(writer, record.Entry.Guid);
                writer.Write(record.Entry.Type);
                writer.Write((uint)1);
                writer.Write(record.Entry.Version);
                writer.Write((uint)2);
                writer.Write(record.Offset);
                writer.Write(record.StoredSize);
                writer.Write(record.StoredSize);
                writer.Write(record.PayloadHash);
                WriteString(writer, packId);
                WriteString(writer, record.Entry.Importer);
                WriteString(writer, record.Entry.ImportOptionsYaml);
                WriteString(writer, record.SourceExtension);
            }
            return ms.ToArray();
        }
    }

    public static void WritePack(string packPath, JBroPackEntry[] entries)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(packPath));
        string packId = Path.GetFileNameWithoutExtension(packPath);
        var sorted = entries.OrderBy(e => e.Guid, StringComparer.Ordinal).ToList();
        var records = new List<Record>();
        ulong payloadHash = FnvOffset;

        using (var fs = new FileStream(packPath, FileMode.Create, FileAccess.Write, FileShare.None))
        using (var writer = new BinaryWriter(fs, Encoding.UTF8))
        {
            fs.Position = HeaderSize;
            foreach (JBroPackEntry entry in sorted)
            {
                byte[] payload = File.ReadAllBytes(entry.Path);
                byte[] plainPayload = (byte[])payload.Clone();
                var record = new Record();
                record.Entry = entry;
                record.Offset = (ulong)fs.Position;
                record.StoredSize = (ulong)payload.LongLength;
                record.PayloadHash = HashBytes(plainPayload);
                record.SourceExtension = Path.GetExtension(entry.Path).Replace("\\", "/");
                CryptBytes(payload, record.PayloadHash ^ record.Offset);
                if (payload.Length > 0)
                {
                    writer.Write(payload);
                    HashAppend(ref payloadHash, plainPayload);
                }
                records.Add(record);
            }

            ulong payloadOffset = HeaderSize;
            ulong payloadSize = (ulong)fs.Position - payloadOffset;
            ulong indexOffset = (ulong)fs.Position;
            byte[] index = SerializeIndex(records, packId);
            ulong indexHash = HashBytes(index);
            CryptBytes(index, payloadHash ^ (uint)records.Count);
            writer.Write(index);

            fs.Position = 0;
            writer.Write(new byte[] { (byte)'J', (byte)'B', (byte)'P', (byte)'A', (byte)'C', (byte)'K', (byte)'1', 0 });
            writer.Write((uint)1);
            writer.Write(HeaderSize);
            writer.Write((uint)0);
            writer.Write((uint)records.Count);
            writer.Write(payloadOffset);
            writer.Write(payloadSize);
            writer.Write(indexOffset);
            writer.Write((ulong)index.LongLength);
            writer.Write(indexHash);
            writer.Write(payloadHash);
        }
    }
}
"@

    if (-not ("JBroPackWriter" -as [type])) {
        Add-Type -TypeDefinition $source -Language CSharp
    }

    $entries = New-Object System.Collections.Generic.List[JBroPackEntry]
    Get-ChildItem -LiteralPath $AssetRoot -Recurse -Filter "*.Jmeta" -File | ForEach-Object {
        $meta = Read-JBroMeta -MetaPath $_.FullName -AssetRoot $AssetRoot
        if ($meta) {
            $entry = New-Object JBroPackEntry
            $entry.Guid = $meta.Guid
            $entry.Path = $meta.AssetPath
            $entry.Type = [uint32]$meta.TypeValue
            $entry.Version = [uint32]$meta.Version
            $entry.Importer = $meta.Importer
            $entry.ImportOptionsYaml = $meta.ImportOptionsYaml
            $entries.Add($entry)
        }
    }

    if ($entries.Count -eq 0) {
        throw "No asset meta files were found for pack build: $AssetRoot"
    }

    [JBroPackWriter]::WritePack($PackPath, $entries.ToArray())
}

function Write-JBroBuildManifest {
    param(
        [Parameter(Mandatory=$true)][string]$ManifestPath,
        [Parameter(Mandatory=$true)][string]$StartupSceneGuid,
        [string]$StartupScene = "",
        [int]$Width = 1280,
        [int]$Height = 720
    )

    $source = @"
using System;
using System.IO;
using System.Text;

public static class JBroBuildManifestWriter
{
    const ulong FnvOffset = 14695981039346656037UL;
    const ulong FnvPrime = 1099511628211UL;
    const ulong CryptKey = 0xC3A5C85C97CB3127UL;
    const uint Version = 1;

    static void WriteString(BinaryWriter writer, string value)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(value ?? "");
        writer.Write((uint)bytes.Length);
        writer.Write(bytes);
    }

    static ulong HashBytes(byte[] bytes)
    {
        ulong hash = FnvOffset;
        foreach (byte b in bytes)
        {
            hash ^= b;
            hash *= FnvPrime;
        }
        return hash;
    }

    static void CryptBytes(byte[] bytes, ulong seed)
    {
        ulong state = seed ^ CryptKey;
        for (int i = 0; i < bytes.Length; ++i)
        {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            bytes[i] ^= (byte)((state >> ((i & 7) * 8)) & 0xFF);
        }
    }

    public static void WriteFile(string path, int width, int height, string startupSceneGuid, string startupScene)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path));
        byte[] payload;
        using (var ms = new MemoryStream())
        using (var payloadWriter = new BinaryWriter(ms, Encoding.UTF8))
        {
            payloadWriter.Write(width);
            payloadWriter.Write(height);
            WriteString(payloadWriter, startupSceneGuid);
            payloadWriter.Flush();
            payload = ms.ToArray();
        }
        ulong payloadHash = HashBytes(payload);
        CryptBytes(payload, payloadHash ^ Version);

        using (var fs = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.None))
        using (var writer = new BinaryWriter(fs, Encoding.UTF8))
        {
            writer.Write(new byte[] { (byte)'J', (byte)'B', (byte)'M', (byte)'A', (byte)'N', (byte)'1', 0, 0 });
            writer.Write(Version);
            writer.Write((uint)payload.Length);
            writer.Write(payloadHash);
            writer.Write(payload);
        }
    }
}
"@
    if (-not ("JBroBuildManifestWriter" -as [type])) {
        Add-Type -TypeDefinition $source -Language CSharp
    }
    [JBroBuildManifestWriter]::WriteFile($ManifestPath, $Width, $Height, $StartupSceneGuid, $StartupScene)
}

function Find-JBroAssetGuid {
    param(
        [Parameter(Mandatory=$true)][string]$AssetRoot,
        [Parameter(Mandatory=$true)][string]$RelativePath
    )

    $assetPath = Join-Path $AssetRoot $RelativePath
    $metaPath = "$assetPath.Jmeta"
    if (-not (Test-Path -LiteralPath $metaPath -PathType Leaf)) {
        return ""
    }
    $meta = Read-JBroMeta -MetaPath $metaPath -AssetRoot $AssetRoot
    return $meta.Guid
}

if ($Platform -ne "Windows") {
    throw "BuildGame.ps1 currently stages Windows packages only. Keep this script as the platform entrypoint; add platform packagers instead of adding editor dependencies."
}

$projectPath = (Resolve-Path -LiteralPath $Project).Path
$projectDir = Split-Path -Parent $projectPath
$projectInfo = Read-JBroProject -ProjectPath $projectPath

$rootPath = $projectInfo.RootPath
if ([System.IO.Path]::IsPathRooted($rootPath)) {
    $projectRoot = (Resolve-Path -LiteralPath $rootPath).Path
} else {
    $projectRoot = (Resolve-Path -LiteralPath (Join-Path $projectDir $rootPath)).Path
}

$contentPath = Join-Path $projectRoot "Contents"
$assetPath = Join-Path $contentPath "Assets"
$productName = ConvertTo-SafeFileName $projectInfo.Build.ProductName
$engineConfiguration = if ($Configuration -eq "Debug") { "Debug_Game" } else { "Release_Game" }
$scriptConfiguration = if ($Configuration -eq "Debug") { "Debug" } else { "Release" }
$outputRootFromArgument = -not [string]::IsNullOrWhiteSpace($OutputRoot)
$selectedOutputRoot = if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $projectInfo.Build.OutputDirectory } else { $OutputRoot }
if (-not [System.IO.Path]::IsPathRooted($selectedOutputRoot)) {
    $outputBase = if ($outputRootFromArgument) { (Get-Location).Path } else { $projectRoot }
    $selectedOutputRoot = Join-Path $outputBase $selectedOutputRoot
}
$selectedOutputRoot = [System.IO.Path]::GetFullPath($selectedOutputRoot)
$packageDir = Join-Path $selectedOutputRoot ("{0}-{1}-{2}" -f $productName, $Platform, $Configuration)
$packageContentDir = Join-Path $packageDir "Content"
$packageAssetPack = Join-Path $packageContentDir "game_assets.jbpack"

$msbuild = Find-MSBuild

Write-Host "Project: $projectPath"
Write-Host "Package: $packageDir"

if ([string]::IsNullOrWhiteSpace($projectInfo.Build.StartupScene)) {
    throw "Startup scene is not configured. Set Build.StartupScene in the project build settings."
}

$scenesToValidate = @()
if (-not [string]::IsNullOrWhiteSpace($projectInfo.Build.StartupScene)) {
    $scenesToValidate += $projectInfo.Build.StartupScene
}
$scenesToValidate += @($projectInfo.Build.BuildScenes)
$scenesToValidate = $scenesToValidate | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique
foreach ($scene in $scenesToValidate) {
    $scenePath = Join-Path $assetPath $scene
    if (-not (Test-Path -LiteralPath $scenePath)) {
        throw "Build scene was not found under Contents/Assets: $scene"
    }
}
if (-not $SkipEngineBuild) {
    Write-Host "Building engine/application: $engineConfiguration|x64"
    & $msbuild $solutionPath /m /p:Configuration=$engineConfiguration /p:Platform=x64 /v:minimal /nr:false
    if ($LASTEXITCODE -ne 0) {
        throw "Engine game build failed. ExitCode=$LASTEXITCODE"
    }
}

$scriptDllSource = $null
if ($projectInfo.Build.ScriptMode -eq "DynamicLibrary") {
    $scriptProjectPath = $projectInfo.Build.ScriptProjectPath
    if (-not [System.IO.Path]::IsPathRooted($scriptProjectPath)) {
        $scriptProjectPath = Join-Path $projectRoot $scriptProjectPath
    }
    $scriptProjectPath = [System.IO.Path]::GetFullPath($scriptProjectPath)

    if (-not $SkipScriptBuild) {
        if (-not (Test-Path -LiteralPath $scriptProjectPath)) {
            throw "Script project was not found: $scriptProjectPath"
        }
        Write-Host "Building script module: $scriptConfiguration|x64"
        & $msbuild $scriptProjectPath /m /p:Configuration=$scriptConfiguration /p:Platform=x64 /v:minimal /nr:false
        if ($LASTEXITCODE -ne 0) {
            throw "GameScript build failed. ExitCode=$LASTEXITCODE"
        }
    }

    $scriptOutput = $projectInfo.Build.ScriptOutputLibraryPath
    if ([string]::IsNullOrWhiteSpace($scriptOutput)) {
        $scriptOutput = "GameScript.dll"
    }

    $scriptOutputCandidates = @()
    if ([System.IO.Path]::IsPathRooted($scriptOutput)) {
        $scriptOutputCandidates += $scriptOutput
    } else {
        $scriptOutputCandidates += (Join-Path $projectRoot $scriptOutput)
        $scriptOutputCandidates += (Join-Path $projectRoot ("x64\{0}\{1}" -f $scriptConfiguration, [System.IO.Path]::GetFileName($scriptOutput)))
        $scriptOutputCandidates += (Join-Path (Split-Path -Parent $scriptProjectPath) ("..\x64\{0}\{1}" -f $scriptConfiguration, [System.IO.Path]::GetFileName($scriptOutput)))
    }

    foreach ($candidate in $scriptOutputCandidates) {
        $fullCandidate = [System.IO.Path]::GetFullPath($candidate)
        if (Test-Path -LiteralPath $fullCandidate) {
            $scriptDllSource = $fullCandidate
            break
        }
    }
    if (-not $scriptDllSource) {
        throw "GameScript output dll was not found. Checked: $($scriptOutputCandidates -join '; ')"
    }
}

if ($Clean) {
    Remove-DirectoryInside -Root $selectedOutputRoot -Target $packageDir
}
New-Item -ItemType Directory -Force -Path $packageDir | Out-Null
New-Item -ItemType Directory -Force -Path $packageContentDir | Out-Null

$applicationSource = Join-Path $repoRoot ("Build\{0}\Application.exe" -f $engineConfiguration)
if (-not (Test-Path -LiteralPath $applicationSource)) {
    throw "Application.exe was not found: $applicationSource"
}
$applicationDest = Join-Path $packageDir ("{0}.exe" -f $productName)
Copy-Item -LiteralPath $applicationSource -Destination $applicationDest -Force

if ($IncludeSymbols) {
    $applicationPdb = [System.IO.Path]::ChangeExtension($applicationSource, ".pdb")
    if (Test-Path -LiteralPath $applicationPdb) {
        Copy-Item -LiteralPath $applicationPdb -Destination (Join-Path $packageDir ("{0}.pdb" -f $productName)) -Force
    }
}

if ($scriptDllSource) {
    Copy-Item -LiteralPath $scriptDllSource -Destination (Join-Path $packageDir "GameScript.dll") -Force
    if ($IncludeSymbols) {
        $scriptPdb = [System.IO.Path]::ChangeExtension($scriptDllSource, ".pdb")
        if (Test-Path -LiteralPath $scriptPdb) {
            Copy-Item -LiteralPath $scriptPdb -Destination (Join-Path $packageDir "GameScript.pdb") -Force
        }
    }
}

Write-JBroAssetPack -AssetRoot $assetPath -PackPath $packageAssetPack
$startupSceneGuid = ""
if (-not [string]::IsNullOrWhiteSpace($projectInfo.Build.StartupScene)) {
    $startupSceneGuid = Find-JBroAssetGuid -AssetRoot $assetPath -RelativePath $projectInfo.Build.StartupScene
    if ([string]::IsNullOrWhiteSpace($startupSceneGuid)) {
        throw "Startup scene has no registered asset GUID: $($projectInfo.Build.StartupScene)"
    }
}

$manifestPath = Join-Path $packageContentDir "build_manifest.jbmanifest"
Write-JBroBuildManifest `
    -ManifestPath $manifestPath `
    -StartupSceneGuid $startupSceneGuid `
    -StartupScene $projectInfo.Build.StartupScene `
    -Width ([int]$projectInfo.ResolutionWidth) `
    -Height ([int]$projectInfo.ResolutionHeight)

Assert-RootArtifactMissing -PackageDir $packageDir -Names @("SDK", "Localization", "Editor")

if (-not (Test-Path -LiteralPath $applicationDest)) {
    throw "Package verification failed: executable missing."
}
if ($projectInfo.Build.ScriptMode -eq "DynamicLibrary" -and -not (Test-Path -LiteralPath (Join-Path $packageDir "GameScript.dll"))) {
    throw "Package verification failed: GameScript.dll missing."
}
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Package verification failed: build_manifest.jbmanifest missing."
}
if (-not (Test-Path -LiteralPath $packageAssetPack)) {
    throw "Package verification failed: asset pack missing."
}
if (Test-Path -LiteralPath (Join-Path $packageContentDir "Assets")) {
    throw "Package verification failed: loose asset folder must not exist."
}

Write-Host "Packaged Windows game: $packageDir"

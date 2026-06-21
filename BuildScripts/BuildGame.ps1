param(
    [Parameter(Mandatory=$true)]
    [string]$Project,

    [ValidateSet("Windows", "Web", "Android", "IOS")]
    [string]$Platform = "Windows",

    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [string]$OutputRoot = "",
    [string]$EmsdkRoot = "",
    [string]$AndroidSdkRoot = "",
    [switch]$IncludeSymbols,
    [switch]$SkipEngineBuild,
    [switch]$SkipScriptBuild,
    [switch]$Clean,
    [switch]$CleanOnly
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path
$solutionPath = Join-Path $repoRoot "JBroEngine.sln"

function Import-EmsdkEnvironment {
    param([string]$RootOrEnvBat)

    if ([string]::IsNullOrWhiteSpace($RootOrEnvBat)) {
        if ($env:EMSDK) {
            $RootOrEnvBat = $env:EMSDK
        } elseif (Test-Path -LiteralPath "C:\emsdk\emsdk_env.bat" -PathType Leaf) {
            $RootOrEnvBat = "C:\emsdk"
        } else {
            return
        }
    }

    $envBat = $RootOrEnvBat
    if (Test-Path -LiteralPath $envBat -PathType Container) {
        $envBat = Join-Path $envBat "emsdk_env.bat"
    }
    if (-not (Test-Path -LiteralPath $envBat -PathType Leaf)) {
        throw "emsdk_env.bat was not found: $envBat"
    }

    $output = & cmd.exe /d /c "`"$envBat`" >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "emsdk_env.bat failed: $envBat"
    }

    foreach ($line in $output) {
        $index = $line.IndexOf('=')
        if ($index -le 0) {
            continue
        }
        $name = $line.Substring(0, $index)
        $value = $line.Substring($index + 1)
        [System.Environment]::SetEnvironmentVariable($name, $value, [System.EnvironmentVariableTarget]::Process)
    }
}

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

function Test-JBroToolOutdated {
    param(
        [Parameter(Mandatory=$true)][string]$ToolExe,
        [Parameter(Mandatory=$true)][string]$SourceRoot
    )

    if (-not (Test-Path -LiteralPath $ToolExe -PathType Leaf)) {
        return $true
    }
    if (-not (Test-Path -LiteralPath $SourceRoot -PathType Container)) {
        return $true
    }

    $toolTime = (Get-Item -LiteralPath $ToolExe).LastWriteTimeUtc
    $newestSource = Get-ChildItem -LiteralPath $SourceRoot -Recurse -File |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if (-not $newestSource) {
        return $false
    }

    return $newestSource.LastWriteTimeUtc -gt $toolTime
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
        PixelsPerUnit = 100.0
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
            WindowsIconGuid = ""
            AndroidApplicationId = "com.jbro.game"
            AndroidMinSdkVersion = 26
            AndroidTargetSdkVersion = 35
            AndroidAbi = "arm64-v8a"
            AndroidOrientation = "Landscape"
            IOSBundleIdentifier = "com.jbro.game"
            IOSTeamId = ""
            IOSMinimumOSVersion = "15.0"
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
                "PixelsPerUnit" { $projectInfo.PixelsPerUnit = [float]$value }
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
    if ($projectInfo.PixelsPerUnit -lt 1.0) {
        $projectInfo.PixelsPerUnit = 100.0
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

function Write-Utf8File {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$Text
    )

    $parent = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Text, $utf8NoBom)
}

function ConvertTo-XmlEscaped {
    param([string]$Value)
    return [System.Security.SecurityElement]::Escape($Value)
}

function ConvertTo-GradleSingleQuoted {
    param([string]$Value)
    return $Value.Replace('\', '\\').Replace("'", "\'")
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

public sealed class JBroPackEntryV2
{
    public string Guid = "";
    public string Path = "";
    public uint Type;
    public uint Version;
    public string ImportOptionsYaml = "";
}

public static class JBroPackWriterV2
{
    const ulong FnvOffset = 14695981039346656037UL;
    const ulong FnvPrime = 1099511628211UL;
    const ulong CryptKey = 0x9E3779B97F4A7C15UL;
    const uint HeaderSize = 72;

    sealed class Record
    {
        public JBroPackEntryV2 Entry = new JBroPackEntryV2();
        public ulong Offset;
        public ulong StoredSize;
        public ulong PayloadHash;
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

    static uint SelectPayloadType(uint assetType)
    {
        // EAssetType values are serialized by the engine. Keep this in sync with AssetTypes.h.
        // Script-side packaging keeps Sprite/Audio raw-compatible until it is moved to an engine-owned pack tool.
        switch (assetType)
        {
            case 5: return 4; // Scene -> SerializedScene
            case 6: return 5; // Prefab -> SerializedPrefab
            case 2: // Mesh
            case 3: // Material
            case 4: // Shader
            case 7: // Script
            case 9: // Custom
            case 10: // AudioEffect
                return 6; // BinaryBlob
            default:
                return 1; // RawSource
        }
    }

    static byte[] SerializeIndex(List<Record> records, string packId)
    {
        using (var ms = new MemoryStream())
        using (var writer = new BinaryWriter(ms, Encoding.UTF8))
        {
            writer.Write((uint)2);
            writer.Write((ulong)records.Count);
            foreach (Record record in records)
            {
                WriteString(writer, record.Entry.Guid);
                WriteString(writer, record.Entry.Guid);
                writer.Write(record.Entry.Type);
                writer.Write(SelectPayloadType(record.Entry.Type));
                writer.Write(record.Entry.Version);
                writer.Write((uint)2);
                writer.Write(record.Offset);
                writer.Write(record.StoredSize);
                writer.Write(record.StoredSize);
                writer.Write(record.PayloadHash);
                WriteString(writer, packId);
                WriteString(writer, record.Entry.ImportOptionsYaml);
            }
            return ms.ToArray();
        }
    }

    public static void WritePack(string packPath, JBroPackEntryV2[] entries)
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
            foreach (JBroPackEntryV2 entry in sorted)
            {
                byte[] payload = File.ReadAllBytes(entry.Path);
                byte[] plainPayload = (byte[])payload.Clone();
                var record = new Record();
                record.Entry = entry;
                record.Offset = (ulong)fs.Position;
                record.StoredSize = (ulong)payload.LongLength;
                record.PayloadHash = HashBytes(plainPayload);
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

    if (-not ("JBroPackWriterV2" -as [type])) {
        Add-Type -TypeDefinition $source -Language CSharp
    }

    $entries = New-Object System.Collections.Generic.List[JBroPackEntryV2]
    Get-ChildItem -LiteralPath $AssetRoot -Recurse -Filter "*.Jmeta" -File | ForEach-Object {
        $meta = Read-JBroMeta -MetaPath $_.FullName -AssetRoot $AssetRoot
        if ($meta) {
            $entry = New-Object JBroPackEntryV2
            $entry.Guid = $meta.Guid
            $entry.Path = $meta.AssetPath
            $entry.Type = [uint32]$meta.TypeValue
            $entry.Version = [uint32]$meta.Version
            $entry.ImportOptionsYaml = $meta.ImportOptionsYaml
            $entries.Add($entry)
        }
    }

    if ($entries.Count -eq 0) {
        throw "No asset meta files were found for pack build: $AssetRoot"
    }

    [JBroPackWriterV2]::WritePack($PackPath, $entries.ToArray())
}

function Get-JBroBuildManifestTool {
    $toolConfiguration = if ($Configuration -eq "Debug") { "Debug_Game" } else { "Release_Game" }
    $toolRoot = Join-Path $repoRoot "BuildTools\BuildManifestTool"
    $toolProject = Join-Path $toolRoot "BuildManifestTool.vcxproj"
    $toolExe = Join-Path $repoRoot ("Build\Tools\BuildManifestTool\{0}\BuildManifestTool.exe" -f $toolConfiguration)

    if (Test-JBroToolOutdated -ToolExe $toolExe -SourceRoot $toolRoot) {
        $msbuild = Find-MSBuild
        & $msbuild $toolProject /m /p:Configuration=$toolConfiguration /p:Platform=x64 "/p:SolutionDir=$repoRoot\" /p:BuildProjectReferences=false /v:minimal /nr:false
        if ($LASTEXITCODE -ne 0) {
            throw "BuildManifestTool build failed."
        }
    }

    return $toolExe
}

function Write-JBroBuildManifest {
    param(
        [Parameter(Mandatory=$true)][string]$ManifestPath,
        [Parameter(Mandatory=$true)][string]$StartupSceneGuid,
        [string]$ProductName = "",
        [string]$StartupScene = "",
        [int]$Width = 1280,
        [int]$Height = 720,
        [float]$PixelsPerUnit = 100.0,
        [string]$TargetPlatform = "",
        [string]$ScriptMode = "",
        [string]$ScriptModule = "",
        [string]$Orientation = ""
    )

    $toolExe = Get-JBroBuildManifestTool

    # 빈 문자열 인자는 Windows PowerShell 5.x 의 native-exe splatting 에서 누락되어 인자가
    # 한 칸씩 어긋난다(예: --script-module 뒤 값이 사라져 tool 이 Usage 출력 후 exit 2 →
    # "BuildManifestTool failed to write manifest"). tool 에서 선택 인자인 product-name /
    # startup-scene / script-module 은 비어 있으면 아예 넘기지 않는다(빈 기본값으로 처리됨).
    $args = @(
        "--out", $ManifestPath,
        "--startup-scene-guid", $StartupSceneGuid,
        "--width", ([string]$Width),
        "--height", ([string]$Height),
        "--pixels-per-unit", ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0}", $PixelsPerUnit)),
        "--target-platform", $TargetPlatform,
        "--script-mode", $ScriptMode
    )
    if (-not [string]::IsNullOrWhiteSpace($ProductName)) {
        $args += @("--product-name", $ProductName)
    }
    if (-not [string]::IsNullOrWhiteSpace($StartupScene)) {
        $args += @("--startup-scene", $StartupScene)
    }
    if (-not [string]::IsNullOrWhiteSpace($ScriptModule)) {
        $args += @("--script-module", $ScriptModule)
    }
    if (-not [string]::IsNullOrWhiteSpace($Orientation)) {
        $args += @("--orientation", $Orientation)
    }
    & $toolExe @args
    if ($LASTEXITCODE -ne 0) {
        throw "BuildManifestTool failed to write manifest: $ManifestPath"
    }
}

function Test-JBroBinaryFileMagic {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][byte[]]$Magic,
        [Parameter(Mandatory=$true)][string]$Name
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Package smoke failed: $Name missing: $Path"
    }

    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        if ($stream.Length -lt $Magic.Length) {
            throw "Package smoke failed: $Name is too small: $Path"
        }

        $buffer = New-Object byte[] $Magic.Length
        $read = $stream.Read($buffer, 0, $buffer.Length)
        if ($read -ne $Magic.Length) {
            throw "Package smoke failed: failed to read $Name header: $Path"
        }
        for ($i = 0; $i -lt $Magic.Length; ++$i) {
            if ($buffer[$i] -ne $Magic[$i]) {
                throw "Package smoke failed: $Name header magic is invalid: $Path"
            }
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Invoke-JBroBuildManifestValidation {
    param(
        [Parameter(Mandatory=$true)][string]$ManifestPath,
        [Parameter(Mandatory=$true)][string]$StartupSceneGuid,
        [string]$ProductName = "",
        [Parameter(Mandatory=$true)][string]$TargetPlatform,
        [Parameter(Mandatory=$true)][string]$ScriptMode,
        [string]$ScriptModule = "",
        [int]$Width = 1280,
        [int]$Height = 720
    )

    $toolExe = Get-JBroBuildManifestTool
    # 빈 문자열 인자는 Windows PowerShell 5.x splatting 에서 누락되어 인자가 어긋난다.
    # 선택 인자(product-name / script-module)는 값이 있을 때만 추가한다.
    $args = @(
        "--validate", $ManifestPath,
        "--startup-scene-guid", $StartupSceneGuid,
        "--width", ([string]$Width),
        "--height", ([string]$Height),
        "--target-platform", $TargetPlatform,
        "--script-mode", $ScriptMode
    )
    if (-not [string]::IsNullOrWhiteSpace($ProductName)) {
        $args += @("--product-name", $ProductName)
    }
    if (-not [string]::IsNullOrWhiteSpace($ScriptModule)) {
        $args += @("--script-module", $ScriptModule)
    }

    & $toolExe @args
    if ($LASTEXITCODE -ne 0) {
        throw "Package smoke failed: build manifest validation failed."
    }
}

function Invoke-ReleasePackageSmokeTests {
    param(
        [Parameter(Mandatory=$true)][string]$PackageDir,
        [Parameter(Mandatory=$true)][string]$PackageContentDir,
        [Parameter(Mandatory=$true)][string]$ManifestPath,
        [Parameter(Mandatory=$true)][string]$AssetPackPath,
        [Parameter(Mandatory=$true)][string]$StartupSceneGuid,
        [string]$ProductName = "",
        [Parameter(Mandatory=$true)][string]$TargetPlatform,
        [Parameter(Mandatory=$true)][string]$ScriptMode,
        [string]$ScriptModule = "",
        [int]$Width = 1280,
        [int]$Height = 720
    )

    if ([string]::IsNullOrWhiteSpace($StartupSceneGuid)) {
        throw "Package smoke failed: startup scene GUID is empty."
    }

    Test-JBroBinaryFileMagic -Path $ManifestPath -Magic ([byte[]](0x4A,0x42,0x4D,0x41,0x4E,0x31,0x00,0x00)) -Name "build manifest"
    Invoke-JBroBuildManifestValidation `
        -ManifestPath $ManifestPath `
        -StartupSceneGuid $StartupSceneGuid `
        -ProductName $ProductName `
        -TargetPlatform $TargetPlatform `
        -ScriptMode $ScriptMode `
        -ScriptModule $ScriptModule `
        -Width $Width `
        -Height $Height
    Test-JBroBinaryFileMagic -Path $AssetPackPath -Magic ([byte[]](0x4A,0x42,0x50,0x41,0x43,0x4B,0x31,0x00)) -Name "asset pack"

    $forbiddenContentItems = @(
        "Assets",
        "build_manifest.yaml",
        "build_manifest.json",
        "game_assets.yaml",
        "game_assets.json"
    )
    foreach ($name in $forbiddenContentItems) {
        if (Test-Path -LiteralPath (Join-Path $PackageContentDir $name)) {
            throw "Package smoke failed: forbidden Content item exists: $name"
        }
    }

    Assert-RootArtifactMissing -PackageDir $PackageDir -Names @("SDK", "Localization", "Editor", "Application.pdb")
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

function Find-JBroAssetMetaByGuid {
    param(
        [Parameter(Mandatory=$true)][string]$AssetRoot,
        [Parameter(Mandatory=$true)][string]$Guid
    )

    if ([string]::IsNullOrWhiteSpace($Guid)) {
        return $null
    }

    foreach ($metaFile in Get-ChildItem -LiteralPath $AssetRoot -Recurse -Filter "*.Jmeta" -File) {
        $meta = Read-JBroMeta -MetaPath $metaFile.FullName -AssetRoot $AssetRoot
        if ($meta -and $meta.Guid -eq $Guid) {
            return $meta
        }
    }
    return $null
}

function Set-WindowsExecutableIcon {
    param(
        [Parameter(Mandatory=$true)][string]$ExePath,
        [Parameter(Mandatory=$true)][string]$IconPath
    )

    $source = @"
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;

public static class JBroWindowsIconResourceUpdater
{
    const int RT_ICON = 3;
    const int RT_GROUP_ICON = 14;
    const ushort IconResourceBase = 200;
    const ushort IdiAppIcon = 107;
    const ushort IdiSmall = 108;
    const ushort KoreanDefaultLanguage = 0x0412;

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr BeginUpdateResource(string pFileName, bool bDeleteExistingResources);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool UpdateResource(IntPtr hUpdate, IntPtr lpType, IntPtr lpName, ushort wLanguage, byte[] lpData, uint cbData);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool EndUpdateResource(IntPtr hUpdate, bool fDiscard);

    static IntPtr MakeIntResource(int value)
    {
        return new IntPtr(value);
    }

    static ushort ReadU16(byte[] bytes, int offset)
    {
        return (ushort)(bytes[offset] | (bytes[offset + 1] << 8));
    }

    static uint ReadU32(byte[] bytes, int offset)
    {
        return (uint)(bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) | (bytes[offset + 3] << 24));
    }

    static void WriteU16(BinaryWriter writer, ushort value)
    {
        writer.Write(value);
    }

    static void WriteU32(BinaryWriter writer, uint value)
    {
        writer.Write(value);
    }

    static void ThrowLastWin32(string message)
    {
        throw new Win32Exception(Marshal.GetLastWin32Error(), message);
    }

    public static void Apply(string exePath, string iconPath)
    {
        byte[] iconBytes = File.ReadAllBytes(iconPath);
        if (iconBytes.Length < 6 || ReadU16(iconBytes, 0) != 0 || ReadU16(iconBytes, 2) != 1)
        {
            throw new InvalidDataException("Invalid .ico header: " + iconPath);
        }

        ushort iconCount = ReadU16(iconBytes, 4);
        if (iconCount == 0 || iconBytes.Length < 6 + iconCount * 16)
        {
            throw new InvalidDataException("Invalid .ico entry table: " + iconPath);
        }

        IntPtr update = BeginUpdateResource(exePath, false);
        if (update == IntPtr.Zero)
        {
            ThrowLastWin32("BeginUpdateResource failed");
        }

        bool success = false;
        try
        {
            byte[] groupBytes;
            using (var ms = new MemoryStream())
            using (var writer = new BinaryWriter(ms))
            {
                WriteU16(writer, 0);
                WriteU16(writer, 1);
                WriteU16(writer, iconCount);

                for (ushort i = 0; i < iconCount; ++i)
                {
                    int entryOffset = 6 + i * 16;
                    uint imageSize = ReadU32(iconBytes, entryOffset + 8);
                    uint imageOffset = ReadU32(iconBytes, entryOffset + 12);
                    if (imageSize == 0 || imageOffset > iconBytes.Length || imageSize > iconBytes.Length - imageOffset)
                    {
                        throw new InvalidDataException("Invalid .ico image payload: " + iconPath);
                    }

                    ushort resourceId = (ushort)(IconResourceBase + i);
                    byte[] payload = new byte[(int)imageSize];
                    Buffer.BlockCopy(iconBytes, (int)imageOffset, payload, 0, (int)imageSize);
                    if (!UpdateResource(update, MakeIntResource(RT_ICON), MakeIntResource(resourceId), KoreanDefaultLanguage, payload, (uint)payload.Length))
                    {
                        ThrowLastWin32("UpdateResource(RT_ICON) failed");
                    }

                    writer.Write(iconBytes[entryOffset + 0]);
                    writer.Write(iconBytes[entryOffset + 1]);
                    writer.Write(iconBytes[entryOffset + 2]);
                    writer.Write(iconBytes[entryOffset + 3]);
                    WriteU16(writer, ReadU16(iconBytes, entryOffset + 4));
                    WriteU16(writer, ReadU16(iconBytes, entryOffset + 6));
                    WriteU32(writer, imageSize);
                    WriteU16(writer, resourceId);
                }
                groupBytes = ms.ToArray();
            }

            foreach (ushort groupId in new ushort[] { IdiAppIcon, IdiSmall })
            {
                if (!UpdateResource(update, MakeIntResource(RT_GROUP_ICON), MakeIntResource(groupId), KoreanDefaultLanguage, groupBytes, (uint)groupBytes.Length))
                {
                    ThrowLastWin32("UpdateResource(RT_GROUP_ICON) failed");
                }
            }
            success = true;
        }
        finally
        {
            if (!EndUpdateResource(update, !success))
            {
                ThrowLastWin32("EndUpdateResource failed");
            }
        }
    }
}
"@

    if (-not ("JBroWindowsIconResourceUpdater" -as [type])) {
        Add-Type -TypeDefinition $source -Language CSharp
    }
    [JBroWindowsIconResourceUpdater]::Apply($ExePath, $IconPath)
}

function Find-Emcc {
    $command = Get-Command "emcc" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    throw "emcc was not found. Run emsdk_env.bat first."
}

function Invoke-WebApplicationBuild {
    param(
        [Parameter(Mandatory=$true)][string]$PackageDir,
        [Parameter(Mandatory=$true)][string]$PackageContentDir,
        [Parameter(Mandatory=$true)][string]$ContentPath,
        [string]$EmsdkRoot = "",
        [Parameter(Mandatory=$true)][string]$Configuration
    )

    Import-EmsdkEnvironment -RootOrEnvBat $EmsdkRoot
    $emcc = Find-Emcc
    $sourceList = Join-Path $repoRoot "BuildScripts\Web\web_game_sources.txt"
    $shellFile = Join-Path $repoRoot "PlatformBuild\Web\shell.html"
    if (-not (Test-Path -LiteralPath $sourceList -PathType Leaf)) {
        throw "Web source list was not found: $sourceList"
    }
    if (-not (Test-Path -LiteralPath $shellFile -PathType Leaf)) {
        throw "Web shell template was not found: $shellFile"
    }

    $tempRoot = Join-Path $env:SystemDrive ("JBroWebBuildTemp\" + [guid]::NewGuid().ToString("N"))
    $tempOutputDir = Join-Path $tempRoot "Out"
    $tempContentDir = Join-Path $tempRoot "Content"
    New-Item -ItemType Directory -Path $tempOutputDir -Force | Out-Null
    New-Item -ItemType Directory -Path $tempContentDir -Force | Out-Null
    Copy-Item -Path (Join-Path $PackageContentDir "*") -Destination $tempContentDir -Recurse -Force

    $outputHtml = Join-Path $tempOutputDir "index.html"
    $optimizationArgs = if ($Configuration -eq "Debug") {
        @("-O0", "-gsource-map", "-sASSERTIONS=1")
    } else {
        @("-O2", "-sASSERTIONS=0")
    }

    $arguments = @(
        "@BuildScripts\Web\web_game_sources.txt",
        "-I.",
        "-IApplication",
        "-IEngine",
        "-IEngine\ThirdParty",
        "-IEngine\ThirdParty\yaml-cpp\src",
        "-I$ContentPath",
        ("-I{0}" -f (Join-Path $ContentPath "Scripts")),
        "-std=c++20",
        "-DJBRO_PLATFORM_WEB",
        "-DJBRO_GAME",
        "-DYAML_CPP_STATIC_DEFINE"
    )
    $arguments += $optimizationArgs

    $scriptSources = @()
    foreach ($relativeSource in @("pch.cpp", "GameModule.cpp", "GeneratedScriptRegistry.cpp")) {
        $candidate = Join-Path $ContentPath $relativeSource
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $scriptSources += $candidate
        }
    }
    $scriptRoot = Join-Path $ContentPath "Scripts"
    if (Test-Path -LiteralPath $scriptRoot -PathType Container) {
        $scriptSources += Get-ChildItem -LiteralPath $scriptRoot -Recurse -Filter "*.cpp" -File |
            Sort-Object FullName |
            ForEach-Object { $_.FullName }
    }
    if ($scriptSources.Count -gt 0) {
        Write-Host "Including Web static script sources: $($scriptSources.Count)"
        $arguments += $scriptSources
    } else {
        Write-Host "No Web static script sources were found under Contents."
    }

    $arguments += @(
        "--use-port=emdawnwebgpu",
        "-sALLOW_MEMORY_GROWTH=1",
        "-sASYNCIFY=1",
        "--preload-file",
        ("{0}@/Content" -f $tempContentDir),
        "--shell-file",
        $shellFile,
        "-o",
        $outputHtml
    )

    Push-Location $repoRoot
    try {
        & $emcc @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Web application build failed. ExitCode=$LASTEXITCODE"
        }

        Get-ChildItem -LiteralPath $tempOutputDir -Filter "index.*" -File |
            ForEach-Object {
                Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $PackageDir $_.Name) -Force
            }
    }
    finally {
        Pop-Location
        if ((Test-Path -LiteralPath $tempRoot) -and $tempRoot.StartsWith((Join-Path $env:SystemDrive "JBroWebBuildTemp"), [System.StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
        $tempParent = Join-Path $env:SystemDrive "JBroWebBuildTemp"
        if (Test-Path -LiteralPath $tempParent) {
            $hasChildren = Get-ChildItem -LiteralPath $tempParent -Force -ErrorAction SilentlyContinue | Select-Object -First 1
            if (-not $hasChildren) {
                Remove-Item -LiteralPath $tempParent -Force -ErrorAction SilentlyContinue
            }
        }
    }
}

function Find-AndroidNativeLibrary {
    param(
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [Parameter(Mandatory=$true)][string]$ProjectRoot,
        [Parameter(Mandatory=$true)][string]$Abi,
        [Parameter(Mandatory=$true)][string]$Configuration,
        [Parameter(Mandatory=$true)][ref]$CheckedPaths
    )

    $candidatePaths = @(
        (Join-Path $ProjectRoot ("Build\Android\{0}\{1}\libJBroGame.so" -f $Abi, $Configuration)),
        (Join-Path $ProjectRoot ("Build\Android\{0}\{1}\libJBroGame.so" -f $Configuration, $Abi)),
        (Join-Path $RepoRoot ("Build\Android\{0}\{1}\libJBroGame.so" -f $Abi, $Configuration)),
        (Join-Path $RepoRoot ("Build\Android\{0}\{1}\libJBroGame.so" -f $Configuration, $Abi)),
        (Join-Path $RepoRoot ("PlatformBuild\Android\libs\{0}\libJBroGame.so" -f $Abi))
    )

    $CheckedPaths.Value = $candidatePaths
    foreach ($candidate in $candidatePaths) {
        $fullCandidate = [System.IO.Path]::GetFullPath($candidate)
        if (Test-Path -LiteralPath $fullCandidate -PathType Leaf) {
            return $fullCandidate
        }
    }
    return ""
}

function Find-GradleCommand {
    param([string]$GradleProjectDir)

    $wrapper = Join-Path $GradleProjectDir "gradlew.bat"
    if (Test-Path -LiteralPath $wrapper -PathType Leaf) {
        return $wrapper
    }

    $command = Get-Command "gradle" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    # PATH 에 없을 때 알려진 설치 위치를 탐색한다(에디터 인앱 빌드는 PATH 를 주입하지 않으므로).
    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($env:GRADLE_HOME)) {
        $candidates += (Join-Path $env:GRADLE_HOME "bin\gradle.bat")
    }
    $installRoots = @("C:\Android\gradle", "C:\Gradle", "C:\gradle")
    foreach ($root in $installRoots) {
        if (Test-Path -LiteralPath $root -PathType Container) {
            $found = Get-ChildItem -LiteralPath $root -Directory -Filter "gradle-*" -ErrorAction SilentlyContinue |
                Sort-Object Name -Descending |
                ForEach-Object { Join-Path $_.FullName "bin\gradle.bat" } |
                Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
                Select-Object -First 1
            if ($found) { $candidates += $found }
        }
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }
    return ""
}

function Invoke-AndroidNativeLibraryBuild {
    param(
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [Parameter(Mandatory=$true)][string]$Abi,
        [Parameter(Mandatory=$true)][string]$Configuration,
        [Parameter(Mandatory=$true)][string]$ContentPath
    )

    $nativeScript = Join-Path $RepoRoot "BuildScripts\Android\BuildAndroidNative.ps1"
    if (-not (Test-Path -LiteralPath $nativeScript -PathType Leaf)) {
        throw "BuildAndroidNative.ps1 was not found: $nativeScript"
    }

    Write-Host "Building Android native library (libJBroGame.so) for $Abi/$Configuration ..."
    & $nativeScript -Configuration $Configuration -Abi $Abi -ContentPath $ContentPath
    if ($LASTEXITCODE -ne 0) {
        throw "Android native library build failed. ExitCode=$LASTEXITCODE"
    }
}

function Invoke-AndroidApplicationBuild {
    param(
        [Parameter(Mandatory=$true)][string]$PackageDir,
        [Parameter(Mandatory=$true)][string]$PackageContentDir,
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [Parameter(Mandatory=$true)][string]$ProjectRoot,
        [Parameter(Mandatory=$true)][string]$ProductName,
        [Parameter(Mandatory=$true)][string]$ApplicationId,
        [Parameter(Mandatory=$true)][int]$MinSdkVersion,
        [Parameter(Mandatory=$true)][int]$TargetSdkVersion,
        [Parameter(Mandatory=$true)][string[]]$Abis,
        [Parameter(Mandatory=$true)][string]$Configuration,
        [string]$AndroidSdkRoot = "",
        [string]$Orientation = "Landscape"
    )

    if ($ApplicationId -notmatch '^[A-Za-z][A-Za-z0-9_]*(\.[A-Za-z][A-Za-z0-9_]*)+$') {
        throw "Android Application ID is invalid: $ApplicationId"
    }
    if ($null -eq $Abis -or $Abis.Count -eq 0) {
        throw "Android ABI list is empty."
    }

    if (-not [string]::IsNullOrWhiteSpace($AndroidSdkRoot)) {
        $env:ANDROID_HOME = $AndroidSdkRoot
        $env:ANDROID_SDK_ROOT = $AndroidSdkRoot
    }

    $gradleProjectDir = Join-Path $PackageDir "GradleProject"
    $appDir = Join-Path $gradleProjectDir "app"
    $mainDir = Join-Path $appDir "src\main"
    $assetContentDir = Join-Path $mainDir "assets\Content"
    $resValuesDir = Join-Path $mainDir "res\values"

    if (Test-Path -LiteralPath $gradleProjectDir) {
        Remove-Item -LiteralPath $gradleProjectDir -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $assetContentDir | Out-Null
    New-Item -ItemType Directory -Force -Path $resValuesDir | Out-Null

    Copy-DirectoryClean -Source $PackageContentDir -Destination $assetContentDir

    # Android 의 AAssetManager 는 디렉토리 재귀 열거를 지원하지 않으므로, 패키지에 담긴 모든
    # asset 파일의 상대 경로 목록(_assetindex.txt)을 생성한다. 런타임(AndroidMain)이 이 목록을
    # 읽어 중첩 디렉토리까지 그대로 내부 저장소로 추출한다.
    $assetIndexEntries = @(Get-ChildItem -LiteralPath $assetContentDir -Recurse -File |
        ForEach-Object { ($_.FullName.Substring($assetContentDir.Length).TrimStart('\', '/') -replace '\\', '/') } |
        Sort-Object)
    Write-Utf8File -Path (Join-Path $assetContentDir "_assetindex.txt") -Text (($assetIndexEntries -join "`n") + "`n")

    $abiFiltersText = ($Abis | ForEach-Object { "'$_'" }) -join ', '

    # 게임 desired orientation → AndroidManifest screenOrientation. 엔진 회전 보정과 짝을 이룬다.
    $screenOrientation = switch ($Orientation) {
        "Portrait" { "sensorPortrait" }
        "Auto"     { "fullSensor" }
        default    { "sensorLandscape" }
    }

    $escapedLabel = ConvertTo-XmlEscaped $ProductName
    $gradleProductName = ConvertTo-GradleSingleQuoted $ProductName
    $settingsGradle = @"
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}
rootProject.name = '$gradleProductName-Android'
include ':app'
"@
    Write-Utf8File -Path (Join-Path $gradleProjectDir "settings.gradle") -Text $settingsGradle

    $rootBuildGradle = @"
plugins {
    id 'com.android.application' version '8.7.3' apply false
}
"@
    Write-Utf8File -Path (Join-Path $gradleProjectDir "build.gradle") -Text $rootBuildGradle

    $appBuildGradle = @"
plugins {
    id 'com.android.application'
}

android {
    namespace '$ApplicationId'
    compileSdk $TargetSdkVersion

    defaultConfig {
        applicationId '$ApplicationId'
        minSdk $MinSdkVersion
        targetSdk $TargetSdkVersion
        versionCode 1
        versionName '1.0'
        ndk {
            abiFilters $abiFiltersText
        }
    }

    sourceSets {
        main {
            assets.srcDirs = ['src/main/assets']
            jniLibs.srcDirs = ['src/main/jniLibs']
        }
    }
}
"@
    Write-Utf8File -Path (Join-Path $appDir "build.gradle") -Text $appBuildGradle

    $manifest = @"
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
    <uses-feature android:name="android.hardware.vulkan.version" android:version="0x00400000" android:required="true" />
    <application
        android:allowBackup="false"
        android:label="$escapedLabel"
        android:hasCode="false"
        android:theme="@style/JBroTheme">
        <activity
            android:name="android.app.NativeActivity"
            android:exported="true"
            android:configChanges="keyboardHidden|orientation|screenSize"
            android:screenOrientation="$screenOrientation">
            <meta-data android:name="android.app.lib_name" android:value="JBroGame" />
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
"@
    Write-Utf8File -Path (Join-Path $mainDir "AndroidManifest.xml") -Text $manifest

    $styles = @"
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <style name="JBroTheme" parent="@android:style/Theme.DeviceDefault.NoActionBar">
        <item name="android:windowFullscreen">true</item>
        <item name="android:windowNoTitle">true</item>
        <item name="android:windowActionBar">false</item>
        <item name="android:windowIsTranslucent">false</item>
    </style>
</resources>
"@
    Write-Utf8File -Path (Join-Path $resValuesDir "styles.xml") -Text $styles

    # AGP 는 비ASCII 프로젝트 경로를 거부한다(한글 사용자명 경로 등). Java/Kotlin 소스가 없는
    # 네이티브 전용 패키지라 경로 검사를 끄고 빌드해도 안전하다(검증 완료).
    $gradleProperties = @"
android.overridePathCheck=true
org.gradle.jvmargs=-Xmx2048m
"@
    Write-Utf8File -Path (Join-Path $gradleProjectDir "gradle.properties") -Text $gradleProperties

    foreach ($abi in $Abis) {
        $checkedPaths = @()
        $nativeLibrary = Find-AndroidNativeLibrary `
            -RepoRoot $RepoRoot `
            -ProjectRoot $ProjectRoot `
            -Abi $abi `
            -Configuration $Configuration `
            -CheckedPaths ([ref]$checkedPaths)
        if ([string]::IsNullOrWhiteSpace($nativeLibrary)) {
            throw "Android native library is missing for ABI '$abi': libJBroGame.so. Generated Gradle project: $gradleProjectDir. Checked: $($checkedPaths -join '; ')"
        }
        $jniLibDir = Join-Path $mainDir ("jniLibs\{0}" -f $abi)
        New-Item -ItemType Directory -Force -Path $jniLibDir | Out-Null
        Copy-Item -LiteralPath $nativeLibrary -Destination (Join-Path $jniLibDir "libJBroGame.so") -Force
    }

    $gradle = Find-GradleCommand -GradleProjectDir $gradleProjectDir
    if ([string]::IsNullOrWhiteSpace($gradle)) {
        throw "Gradle was not found. Generated Gradle project: $gradleProjectDir. Install Gradle or add gradlew.bat before running Android packaging."
    }

    $gradleTask = if ($Configuration -eq "Debug") { "assembleDebug" } else { "assembleRelease" }
    Push-Location $gradleProjectDir
    try {
        & $gradle $gradleTask
        if ($LASTEXITCODE -ne 0) {
            throw "Android Gradle build failed. Task=$gradleTask ExitCode=$LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }

    $apkName = if ($Configuration -eq "Debug") { "app-debug.apk" } else { "app-release-unsigned.apk" }
    $apkPath = Join-Path $appDir ("build\outputs\apk\{0}\{1}" -f $Configuration.ToLowerInvariant(), $apkName)
    if (-not (Test-Path -LiteralPath $apkPath -PathType Leaf)) {
        throw "Android APK was not generated: $apkPath"
    }
    Copy-Item -LiteralPath $apkPath -Destination (Join-Path $PackageDir ("{0}.apk" -f $ProductName)) -Force
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

if ($CleanOnly) {
    Remove-DirectoryInside -Root $selectedOutputRoot -Target $packageDir
    Write-Host "Cleaned package: $packageDir"
    exit 0
}

if ($Platform -eq "IOS") {
    throw "iOS package build is not implemented yet. Platform=IOS. The project settings are recognized, but Xcode signing and MoltenVK packaging are not implemented."
}

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

$applicationDest = $null
$scriptDllSource = $null
if ($Platform -eq "Windows") {
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

    $applicationSource = Join-Path $repoRoot ("Build\{0}\Application.exe" -f $engineConfiguration)
    if (-not (Test-Path -LiteralPath $applicationSource)) {
        throw "Application.exe was not found: $applicationSource"
    }
    $applicationDest = Join-Path $packageDir ("{0}.exe" -f $productName)
} elseif ($Platform -ne "Web" -and $Platform -ne "Android") {
    throw "Unsupported target platform: $Platform"
}

if ($Clean) {
    Remove-DirectoryInside -Root $selectedOutputRoot -Target $packageDir
}
New-Item -ItemType Directory -Force -Path $packageDir | Out-Null
New-Item -ItemType Directory -Force -Path $packageContentDir | Out-Null

if ($Platform -eq "Windows") {
    Copy-Item -LiteralPath $applicationSource -Destination $applicationDest -Force

    if (-not [string]::IsNullOrWhiteSpace($projectInfo.Build.WindowsIconGuid)) {
        $iconMeta = Find-JBroAssetMetaByGuid -AssetRoot $assetPath -Guid $projectInfo.Build.WindowsIconGuid
        if (-not $iconMeta) {
            throw "Windows icon asset GUID is not registered: $($projectInfo.Build.WindowsIconGuid)"
        }
        if ([System.IO.Path]::GetExtension($iconMeta.AssetPath).ToLowerInvariant() -ne ".ico") {
            throw "Windows icon asset must be an .ico file: $($iconMeta.AssetPath)"
        }
        Set-WindowsExecutableIcon -ExePath $applicationDest -IconPath $iconMeta.AssetPath
    }

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
$manifestScriptMode = if ($Platform -eq "Windows") { "DynamicLibrary" } else { "Static" }
$manifestScriptModule = if ($Platform -eq "Windows") { "GameScript.dll" } else { "" }
Write-JBroBuildManifest `
    -ManifestPath $manifestPath `
    -StartupSceneGuid $startupSceneGuid `
    -ProductName $productName `
    -StartupScene $projectInfo.Build.StartupScene `
    -Width ([int]$projectInfo.ResolutionWidth) `
    -Height ([int]$projectInfo.ResolutionHeight) `
    -PixelsPerUnit ([float]$projectInfo.PixelsPerUnit) `
    -TargetPlatform $Platform `
    -ScriptMode $manifestScriptMode `
    -ScriptModule $manifestScriptModule `
    -Orientation $projectInfo.Build.AndroidOrientation

Assert-RootArtifactMissing -PackageDir $packageDir -Names @("SDK", "Localization", "Editor")

if ($Platform -eq "Windows") {
    if (-not (Test-Path -LiteralPath $applicationDest)) {
        throw "Package verification failed: executable missing."
    }
    if ($projectInfo.Build.ScriptMode -eq "DynamicLibrary" -and -not (Test-Path -LiteralPath (Join-Path $packageDir "GameScript.dll"))) {
        throw "Package verification failed: GameScript.dll missing."
    }
} elseif ($Platform -eq "Web") {
    Invoke-WebApplicationBuild -PackageDir $packageDir -PackageContentDir $packageContentDir -ContentPath $contentPath -EmsdkRoot $EmsdkRoot -Configuration $Configuration

    if (-not (Test-Path -LiteralPath (Join-Path $packageDir "index.html"))) {
        throw "Web package verification failed: index.html missing."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $packageDir "index.js"))) {
        throw "Web package verification failed: index.js missing."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $packageDir "index.wasm"))) {
        throw "Web package verification failed: index.wasm missing."
    }
    if (Test-Path -LiteralPath (Join-Path $packageDir "GameScript.dll")) {
        throw "Web package verification failed: GameScript.dll must not exist."
    }
} elseif ($Platform -eq "Android") {
    # AndroidAbi 는 쉼표/세미콜론으로 구분된 여러 ABI 를 허용한다(예: "arm64-v8a,x86_64").
    $androidAbis = @($projectInfo.Build.AndroidAbi -split '[,;]' |
        ForEach-Object { $_.Trim() } |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($androidAbis.Count -eq 0) {
        throw "Android ABI list is empty: $($projectInfo.Build.AndroidAbi)"
    }

    foreach ($abi in $androidAbis) {
        Invoke-AndroidNativeLibraryBuild `
            -RepoRoot $repoRoot `
            -Abi $abi `
            -Configuration $Configuration `
            -ContentPath $contentPath
    }

    Invoke-AndroidApplicationBuild `
        -PackageDir $packageDir `
        -PackageContentDir $packageContentDir `
        -RepoRoot $repoRoot `
        -ProjectRoot $projectRoot `
        -ProductName $productName `
        -ApplicationId $projectInfo.Build.AndroidApplicationId `
        -MinSdkVersion ([int]$projectInfo.Build.AndroidMinSdkVersion) `
        -TargetSdkVersion ([int]$projectInfo.Build.AndroidTargetSdkVersion) `
        -Abis $androidAbis `
        -Configuration $Configuration `
        -AndroidSdkRoot $AndroidSdkRoot `
        -Orientation $projectInfo.Build.AndroidOrientation

    $androidGradleProject = Join-Path $packageDir "GradleProject"
    $androidAssetsContent = Join-Path $androidGradleProject "app\src\main\assets\Content"
    if (-not (Test-Path -LiteralPath (Join-Path $androidAssetsContent "build_manifest.jbmanifest"))) {
        throw "Android package verification failed: Gradle asset manifest missing."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $androidAssetsContent "game_assets.jbpack"))) {
        throw "Android package verification failed: Gradle asset pack missing."
    }
    if (Test-Path -LiteralPath (Join-Path $androidAssetsContent "Assets")) {
        throw "Android package verification failed: loose asset folder must not exist."
    }
    if (Test-Path -LiteralPath (Join-Path $packageDir "GameScript.dll")) {
        throw "Android package verification failed: GameScript.dll must not exist."
    }
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
if ($Configuration -eq "Release") {
    Invoke-ReleasePackageSmokeTests `
        -PackageDir $packageDir `
        -PackageContentDir $packageContentDir `
        -ManifestPath $manifestPath `
        -AssetPackPath $packageAssetPack `
        -StartupSceneGuid $startupSceneGuid `
        -ProductName $productName `
        -TargetPlatform $Platform `
        -ScriptMode $manifestScriptMode `
        -ScriptModule $manifestScriptModule `
        -Width ([int]$projectInfo.ResolutionWidth) `
        -Height ([int]$projectInfo.ResolutionHeight)
}

Write-Host "Packaged $Platform game: $packageDir"

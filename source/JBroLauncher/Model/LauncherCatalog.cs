using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace JBro.Launcher.Model;

/// <summary>목록에 올라온 프로젝트 하나다. 진실은 `.jproject` 에 있고 여기 있는 것은 캐시다.</summary>
public sealed class ProjectEntry
{
    public string ProjectFilePath { get; set; } = string.Empty;

    /// <summary>목록에 보여 줄 이름이다. 파일을 못 읽을 때도 무엇이었는지 보이게 남겨 둔다.</summary>
    public string DisplayName { get; set; } = string.Empty;

    /// <summary>마지막으로 읽었을 때의 엔진 버전이다. 파일을 읽으면 이 값을 덮어쓴다.</summary>
    public string EngineVersion { get; set; } = string.Empty;

    public DateTimeOffset LastOpenedAt { get; set; }
}

/// <summary>설치된 엔진 하나다. 사용자가 폴더를 골라 등록한다.</summary>
public sealed class EngineEntry
{
    public string Version { get; set; } = string.Empty;

    /// <summary>`JBroEditorHost.exe` 가 있는 폴더다. 로컬라이징 표와 아이콘 글꼴도 그 옆에 있다.</summary>
    public string InstallDirectory { get; set; } = string.Empty;

    /// <summary>
    /// 런처가 `..\Editor` 에서 알아서 찾은 것인지다(D-103). **저장하지 않는다** - 다음에 뜰 때
    /// 다시 훑어서 정하고, 그래야 엔진을 지우거나 새로 넣은 것이 그대로 보인다.
    /// </summary>
    [JsonIgnore]
    public bool IsDiscovered { get; set; }

    [JsonIgnore]
    public string EditorPath => Path.Combine(InstallDirectory, "JBroEditorHost.exe");
}

/// <summary>
/// 런처가 기억하는 것 전부다. `%APPDATA%\JBro\Launcher\catalog.json` 에 쌓인다.
/// **여기에 프로젝트의 진실을 두지 않는다**(D-99) - 엔진 버전과 차원은 `.jproject` 가 정하고,
/// 이 파일이 갖는 것은 경로 목록과 화면에 쓰는 캐시다.
/// </summary>
public sealed class LauncherCatalog
{
    public List<ProjectEntry> Projects { get; set; } = [];

    public List<EngineEntry> Engines { get; set; } = [];

    /// <summary>
    /// 화면에 보이는 엔진 목록이다. **알아서 찾은 것이 먼저다**(D-103). 같은 폴더를 손으로
    /// 등록해 뒀으면 찾은 쪽만 남긴다 - 같은 엔진이 두 줄로 보이면 어느 것을 고른 것인지
    /// 알 수 없다.
    /// </summary>
    public List<EngineEntry> ResolveEngines()
    {
        List<EngineEntry> resolved = EngineDiscovery.Discover();
        foreach (EngineEntry registered in Engines)
        {
            bool already = resolved.Any(found => string.Equals(
                found.InstallDirectory, registered.InstallDirectory, StringComparison.OrdinalIgnoreCase));
            if (already)
            {
                continue;
            }
            registered.IsDiscovered = false;
            resolved.Add(registered);
        }
        return resolved;
    }

    private static readonly JsonSerializerOptions SerializerOptions = new()
    {
        WriteIndented = true,
    };

    public static string CatalogPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "JBro",
        "Launcher",
        "catalog.json");

    public static LauncherCatalog Load()
    {
        try
        {
            string path = CatalogPath;
            if (false == File.Exists(path))
            {
                return new LauncherCatalog();
            }
            string text = File.ReadAllText(path);
            return JsonSerializer.Deserialize<LauncherCatalog>(text, SerializerOptions) ?? new LauncherCatalog();
        }
        catch (Exception exception) when (exception is IOException or JsonException or UnauthorizedAccessException)
        {
            // 목록을 못 읽었다고 런처가 뜨지 않으면 안 된다. 빈 목록으로 시작하고,
            // 사용자가 프로젝트를 다시 추가하면 그때 다시 쓰인다.
            return new LauncherCatalog();
        }
    }

    public void Save()
    {
        string path = CatalogPath;
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, JsonSerializer.Serialize(this, SerializerOptions));
    }
}

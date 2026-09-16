using System;
using System.Collections.Generic;
using System.IO;

namespace JBro.Launcher.Model;

/// <summary>
/// 설치본의 폴더 모양이다(D-103).
///
/// <code>
/// JBroEngine/
///   Launcher/           런처 실행 파일
///   Editor/
///     v0.1.0/           JBroEditorHost.exe · Localization/ · ThirdParty/
///     v0.1.1/
/// </code>
///
/// 런처는 자기 실행 파일 옆의 `..\Editor` 를 훑어 설치된 엔진을 **알아서 찾는다.** 사용자가
/// 등록해 줘야만 아는 것은 이 자리에 없는 엔진뿐이다.
///
/// **폴더 이름은 보여 주기용이고 버전이 아니다.** 버전은 실행 파일의 버전 리소스가
/// 말한다(D-101) - 폴더 이름을 믿으면 이름만 바꿔도 다른 엔진이 되고, 그 엔진으로 열린
/// 프로젝트가 왜 이상한지 알 길이 없다.
/// </summary>
public static class EngineDiscovery
{
    /// <summary>엔진들이 놓이는 폴더다. 없으면 이 설치는 그런 모양이 아니다.</summary>
    public static string EditorRootPath => Path.GetFullPath(
        Path.Combine(AppContext.BaseDirectory, "..", "Editor"));

    public static List<EngineEntry> Discover()
    {
        return Discover(EditorRootPath);
    }

    /// <summary>훑을 폴더를 직접 준다. 테스트가 가짜 설치본을 세워 보는 자리다.</summary>
    public static List<EngineEntry> Discover(string root)
    {
        var found = new List<EngineEntry>();
        if (false == Directory.Exists(root))
        {
            return found;
        }

        string[] directories;
        try
        {
            directories = Directory.GetDirectories(root);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return found;
        }

        Array.Sort(directories, StringComparer.OrdinalIgnoreCase);
        foreach (string directory in directories)
        {
            string editorPath = Path.Combine(directory, "JBroEditorHost.exe");
            if (false == File.Exists(editorPath))
            {
                continue;
            }
            // 버전을 말하지 않는 것은 싣지 않는다. 짐작해서 목록에 올리면 그 엔진으로
            // 프로젝트가 열릴 수 있다.
            if (false == EngineVersionReader.TryRead(editorPath, out string version))
            {
                continue;
            }
            found.Add(new EngineEntry
            {
                Version = version,
                InstallDirectory = directory,
                IsDiscovered = true,
            });
        }
        return found;
    }
}

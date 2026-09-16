using System;
using System.Diagnostics;
using System.IO;

namespace JBro.Launcher.Model;

/// <summary>
/// 엔진 설치가 자기 버전을 말하는 자리는 `JBroEditorHost.exe` 의 버전 리소스다(D-101).
/// 그 값이 프로젝트의 `EngineVersion` 과 맞는지로 어느 설치를 띄울지 고른다.
///
/// **폴더 이름으로 짐작하지 않는다.** 짐작한 값이 프로젝트와 우연히 맞으면 엉뚱한 엔진으로
/// 프로젝트를 열게 되고, 그때 무엇이 잘못됐는지 알 길이 없다. 말하지 않는 설치는 거절한다.
/// </summary>
public static class EngineVersionReader
{
    public static bool TryRead(string editorPath, out string version)
    {
        version = string.Empty;
        try
        {
            string? product = FileVersionInfo.GetVersionInfo(editorPath).ProductVersion;
            if (string.IsNullOrWhiteSpace(product))
            {
                return false;
            }
            product = product.Trim();
            // 버전 리소스가 없는 실행 파일도 0.0.0 으로 읽힐 수 있다. 그것은 버전이 아니다.
            if (product == "0.0.0" || product == "0.0.0.0")
            {
                return false;
            }
            version = product;
            return true;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or FileNotFoundException)
        {
            return false;
        }
    }
}

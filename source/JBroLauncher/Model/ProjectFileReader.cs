using System;
using System.Globalization;
using System.IO;

namespace JBro.Launcher.Model;

/// <summary>읽지 못한 이유다. 사람에게 그대로 보여 준다.</summary>
public sealed class ProjectFileError
{
    /// <summary>0 이면 파일 자체를 열지 못한 것이다. 엔진의 ProjectFileError 와 같은 뜻이다.</summary>
    public int Line { get; init; }

    public required string Message { get; init; }
}

/// <summary>
/// `.jproject` 를 읽는다. **엔진의 파서를 그대로 옮긴 것이 아니라 런처가 쓰는 키만 본다** —
/// 목록에 보여 줄 이름과 해상도, 그리고 어느 엔진으로 어느 차원을 여는지다. 나머지 키와
/// 모르는 블록은 건너뛴다. 엔진이 형식을 다시 검사하므로, 여기서 통과한 파일이 반드시
/// 열린다고 보지 않는다 - 목록을 그리기 위한 읽기다.
/// </summary>
public static class ProjectFileReader
{
    public static bool TryRead(string path, out ProjectFileInfo info, out ProjectFileError error)
    {
        info = null!;
        string[] lines;
        try
        {
            lines = File.ReadAllLines(path);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            error = new ProjectFileError { Line = 0, Message = "프로젝트 파일을 열지 못했습니다." };
            return false;
        }

        string? engineVersion = null;
        FrameworkKind? framework = null;
        string productName = string.Empty;
        int width = 0;
        int height = 0;
        // `Build:` 아래로 들어갔는지 본다. 제품 이름이 거기 있다.
        bool inBuild = false;

        for (int index = 0; index < lines.Length; ++index)
        {
            string line = lines[index];
            string trimmed = line.TrimStart(' ');
            if (trimmed.Length == 0 || trimmed.StartsWith('#'))
            {
                continue;
            }
            int indent = line.Length - trimmed.Length;
            int colon = trimmed.IndexOf(':');
            if (colon <= 0)
            {
                // 시퀀스 항목이거나 우리가 읽지 않는 줄이다. 엔진이 형식을 따진다.
                continue;
            }

            string key = trimmed[..colon];
            string value = Unquote(trimmed[(colon + 1)..].Trim());

            if (indent == 0)
            {
                inBuild = key == "Build";
            }

            if (indent == 0)
            {
                switch (key)
                {
                    case "EngineVersion":
                        engineVersion = value;
                        break;
                    case "Framework":
                        framework = ParseFramework(value);
                        if (framework is null)
                        {
                            error = new ProjectFileError
                            {
                                Line = index + 1,
                                Message = $"Framework 는 2D 또는 3D 여야 합니다: {value}",
                            };
                            return false;
                        }
                        break;
                    case "ResolutionWidth":
                        width = ParseInt(value);
                        break;
                    case "ResolutionHeight":
                        height = ParseInt(value);
                        break;
                }
            }
            else if (inBuild && key == "ProductName")
            {
                productName = value;
            }
        }

        if (string.IsNullOrEmpty(engineVersion))
        {
            error = new ProjectFileError
            {
                Line = 0,
                Message = "어느 엔진 버전으로 여는지 적혀 있지 않습니다(EngineVersion).",
            };
            return false;
        }
        if (framework is null)
        {
            error = new ProjectFileError
            {
                Line = 0,
                Message = "2D 인지 3D 인지 적혀 있지 않습니다(Framework).",
            };
            return false;
        }

        info = new ProjectFileInfo
        {
            EngineVersion = engineVersion,
            Framework = framework.Value,
            ProductName = productName,
            ResolutionWidth = width,
            ResolutionHeight = height,
        };
        error = null!;
        return true;
    }

    private static FrameworkKind? ParseFramework(string value) => value.ToUpperInvariant() switch
    {
        "2D" => FrameworkKind.Framework2D,
        "3D" => FrameworkKind.Framework3D,
        _ => null,
    };

    private static string Unquote(string value)
    {
        if (value.Length >= 2 && value[0] == '"' && value[^1] == '"')
        {
            return value[1..^1];
        }
        return value;
    }

    private static int ParseInt(string value) =>
        int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int parsed) ? parsed : 0;
}

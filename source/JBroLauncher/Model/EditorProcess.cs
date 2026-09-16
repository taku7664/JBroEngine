using System;
using System.Diagnostics;
using System.IO;

namespace JBro.Launcher.Model;

/// <summary>에디터를 띄운 결과다. 띄우지 못한 것과 띄웠다가 실패한 것은 다른 일이다.</summary>
public sealed class EditorStartResult
{
    public bool Started { get; init; }

    /// <summary>띄우지 못했을 때의 사유다. 띄웠으면 비어 있다.</summary>
    public string Message { get; init; } = string.Empty;

    public Process? Process { get; init; }
}

/// <summary>
/// `JBroEditorHost.exe` 를 인자와 함께 띄운다(D-97). 런처와 에디터는 프로세스 경계로만
/// 만나므로, 여기가 둘이 닿는 유일한 자리다.
/// </summary>
public static class EditorProcess
{
    /// <summary>
    /// 종료 코드가 무슨 뜻인지다. 에디터가 정한 값이고(D-97) 사람에게 보여 줄 말로 바꾼다.
    /// </summary>
    public static string DescribeExitCode(int exitCode) => exitCode switch
    {
        0 => "정상 종료",
        1 => "엔진을 초기화하지 못했습니다.",
        2 => "프로젝트를 열지 못했습니다.",
        3 => "에디터 화면을 시작하지 못했습니다.",
        64 => "실행 인자가 잘못됐습니다.",
        _ => $"알 수 없는 종료 코드({exitCode})",
    };

    public static EditorStartResult Start(EngineEntry engine, string projectFilePath)
    {
        if (false == File.Exists(engine.EditorPath))
        {
            return new EditorStartResult
            {
                Started = false,
                Message = $"엔진 실행 파일이 없습니다: {engine.EditorPath}",
            };
        }
        if (false == File.Exists(projectFilePath))
        {
            return new EditorStartResult
            {
                Started = false,
                Message = $"프로젝트 파일이 없습니다: {projectFilePath}",
            };
        }

        // 차원은 넘기지 않는다. `.jproject` 가 정한다(D-99).
        // 작업 폴더를 엔진 설치 폴더로 잡는다 - 로컬라이징 표와 아이콘 글꼴이 그 옆에 있다.
        var startInfo = new ProcessStartInfo
        {
            FileName = engine.EditorPath,
            WorkingDirectory = engine.InstallDirectory,
            UseShellExecute = false,
        };
        startInfo.ArgumentList.Add("--project");
        startInfo.ArgumentList.Add(projectFilePath);

        try
        {
            Process? process = Process.Start(startInfo);
            if (process is null)
            {
                return new EditorStartResult { Started = false, Message = "에디터를 시작하지 못했습니다." };
            }
            return new EditorStartResult { Started = true, Process = process };
        }
        catch (Exception exception) when (exception is System.ComponentModel.Win32Exception or InvalidOperationException)
        {
            return new EditorStartResult { Started = false, Message = $"에디터를 시작하지 못했습니다: {exception.Message}" };
        }
    }
}

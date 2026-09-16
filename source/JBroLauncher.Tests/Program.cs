using System;
using System.IO;
using JBro.Launcher.Model;

namespace JBro.Launcher.Tests;

internal static class Program
{
    private static int _failures;

    private static void Check(bool condition, string message)
    {
        if (condition)
        {
            return;
        }
        Console.WriteLine($"test failure: {message}");
        ++_failures;
    }

    /// <summary>테스트가 쓰는 파일이다. 지우는 것까지 호출한 쪽이 한다.</summary>
    private static string WriteTemp(string name, string text)
    {
        string path = Path.Combine(Path.GetTempPath(), name);
        File.WriteAllText(path, text);
        return path;
    }

    private const string RequiredKeys = "Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\n";

    private static void ReadsWhatTheLauncherShows()
    {
        string path = WriteTemp("JBroLauncherTest.jproject",
            RequiredKeys +
            "ResolutionWidth: 1280\n" +
            "ResolutionHeight: 720\n" +
            "ScriptOutputLibraryPath: \"\"\n" +
            "Build:\n" +
            "  ProductName: 테스트 게임\n");
        try
        {
            Check(ProjectFileReader.TryRead(path, out ProjectFileInfo info, out ProjectFileError error),
                "a project with both required keys must be read");
            if (info is null)
            {
                return;
            }
            Check(info.EngineVersion == "0.1.0", "the engine version must come through");
            Check(info.Framework == FrameworkKind.Framework2D, "and the framework");
            // 제품 이름은 `Build:` 안에 있다. 들여쓰기를 보지 않으면 최상위 키와 섞인다.
            Check(info.ProductName == "테스트 게임", "the product name inside Build must come through");
            Check(info.ResolutionWidth == 1280 && info.ResolutionHeight == 720,
                "the resolution must come through");
        }
        finally
        {
            File.Delete(path);
        }
    }

    private static void RefusesWhatIsNotAProject()
    {
        string missingEngine = WriteTemp("JBroLauncherNoEngine.jproject", "Version: 1\nFramework: 2D\n");
        string missingFramework = WriteTemp("JBroLauncherNoFramework.jproject", "Version: 1\nEngineVersion: 0.1.0\n");
        string emptyEngine = WriteTemp("JBroLauncherEmptyEngine.jproject",
            "Version: 1\nEngineVersion: \"\"\nFramework: 2D\n");
        string wrongFramework = WriteTemp("JBroLauncherWrongFramework.jproject",
            "Version: 1\nEngineVersion: 0.1.0\nFramework: 4D\n");
        try
        {
            Check(false == ProjectFileReader.TryRead(missingEngine, out _, out ProjectFileError noEngine),
                "a project without the engine version must be refused");
            Check(noEngine.Message.Contains("EngineVersion"), "and must name the key it wants");

            Check(false == ProjectFileReader.TryRead(missingFramework, out _, out ProjectFileError noFramework),
                "a project without the framework must be refused");
            Check(noFramework.Message.Contains("Framework"), "and must name that key");

            Check(false == ProjectFileReader.TryRead(emptyEngine, out _, out _),
                "an empty engine version is the same as not saying which engine");

            Check(false == ProjectFileReader.TryRead(wrongFramework, out _, out ProjectFileError badFramework),
                "the framework must be 2D or 3D and nothing else");
            Check(badFramework.Line == 3, "and the refusal must name the line that said it");

            string missingFile = Path.Combine(Path.GetTempPath(), "JBroLauncherNoSuchFile.jproject");
            Check(false == ProjectFileReader.TryRead(missingFile, out _, out ProjectFileError noFile),
                "a file that is not there must be refused");
            Check(noFile.Line == 0, "and a missing file has no line to point at");
        }
        finally
        {
            File.Delete(missingEngine);
            File.Delete(missingFramework);
            File.Delete(emptyEngine);
            File.Delete(wrongFramework);
        }
    }

    private static void ReadsA3DProjectFromAKoreanPath()
    {
        // 이 기계의 사용자 폴더 이름에 한글이 들어 있다. 프로젝트 경로도 마찬가지일 것이다.
        string directory = Path.Combine(Path.GetTempPath(), "JBro런처테스트");
        Directory.CreateDirectory(directory);
        string path = Path.Combine(directory, "한글.jproject");
        File.WriteAllText(path, "Version: 1\nEngineVersion: 0.2.0\nFramework: 3D\n");
        try
        {
            Check(ProjectFileReader.TryRead(path, out ProjectFileInfo info, out _),
                "a project under a Korean path must be read");
            Check(info?.Framework == FrameworkKind.Framework3D, "and must come back as the 3D project it is");
            Check(info?.EngineVersion == "0.2.0", "with the engine version it named");
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }

    private static void ProductNameOutsideBuildIsNotTheProductName()
    {
        // 최상위 `ProductName` 은 이 형식에 없는 키다. 들여쓰기를 보지 않으면 이것이
        // `Build.ProductName` 자리에 들어앉는다.
        string stray = WriteTemp("JBroLauncherStrayName.jproject", RequiredKeys + "ProductName: 엉뚱한 이름\n");
        // 그리고 **어느 블록 아래인지도 봐야 한다.** 파일에는 이 엔진이 읽지 않는 블록이
        // 여럿 있고, 그 안에 같은 이름의 키가 있을 수 있다. 들여쓰기만 보고 넘기면
        // 엉뚱한 블록의 값이 제품 이름이 된다.
        string otherBlock = WriteTemp("JBroLauncherOtherBlock.jproject",
            RequiredKeys +
            // **`Build` 를 먼저 둔다.** 뒤에 두면 나중 값이 이겨서, 블록을 보지 않는 구현도
            // 우연히 같은 답을 낸다. 순서를 뒤집어야 그 구분이 실제로 재진다.
            "Build:\n" +
            "  ProductName: 진짜 이름\n" +
            "Packaging:\n" +
            "  ProductName: 다른 블록의 이름\n");
        try
        {
            Check(ProjectFileReader.TryRead(stray, out ProjectFileInfo info, out _), "the project must still read");
            Check(info?.ProductName == string.Empty,
                "a top-level ProductName must not be taken for the one inside Build");

            Check(ProjectFileReader.TryRead(otherBlock, out ProjectFileInfo nested, out _),
                "a project with blocks we do not read must still read");
            Check(nested?.ProductName == "진짜 이름",
                "a ProductName under another block must not be taken for the one inside Build");
        }
        finally
        {
            File.Delete(stray);
            File.Delete(otherBlock);
        }
    }

    private static void ExitCodesSayWhatHappened()
    {
        // 에디터가 정한 값이다(D-97). 여기가 어긋나면 사용자는 엉뚱한 사유를 본다.
        Check(EditorProcess.DescribeExitCode(2).Contains("프로젝트"), "exit code 2 is about the project");
        Check(EditorProcess.DescribeExitCode(64).Contains("인자"), "exit code 64 is about the arguments");
        Check(EditorProcess.DescribeExitCode(7).Contains("7"), "an unknown code must still show its number");
    }

    private static void StartRefusesBeforeItSpawnsAnything()
    {
        var engine = new EngineEntry { Version = "0.1.0", InstallDirectory = Path.GetTempPath() };
        EditorStartResult result = EditorProcess.Start(engine, "no-such.jproject");
        Check(false == result.Started, "starting without an editor executable must fail");
        Check(result.Message.Length > 0, "and must say why rather than come back silent");
        Check(result.Process is null, "and must not leave a process behind");
    }

    private static int Main()
    {
        ReadsWhatTheLauncherShows();
        RefusesWhatIsNotAProject();
        ReadsA3DProjectFromAKoreanPath();
        ProductNameOutsideBuildIsNotTheProductName();
        ExitCodesSayWhatHappened();
        StartRefusesBeforeItSpawnsAnything();

        if (_failures > 0)
        {
            Console.WriteLine($"{_failures} launcher test(s) failed.");
            return 1;
        }
        Console.WriteLine("Launcher tests passed.");
        return 0;
    }
}

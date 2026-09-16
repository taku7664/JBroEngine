using System;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using JBro.Launcher.Model;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.Storage;
using Windows.Storage.Pickers;

namespace JBro.Launcher;

/// <summary>목록 한 줄이다. 화면에 보이는 것만 담는다.</summary>
public sealed class ProjectRow
{
    public required string Name { get; init; }

    public required string Path { get; init; }

    /// <summary>오른쪽에 붙는 한 줄이다. 엔진 버전과 차원, 또는 읽지 못한 이유다.</summary>
    public required string Detail { get; init; }
}

public sealed class EngineRow
{
    public required string Version { get; init; }

    public required string Directory { get; init; }
}

public sealed partial class MainWindow : Window
{
    private readonly LauncherCatalog _catalog = LauncherCatalog.Load();
    private readonly ObservableCollection<ProjectRow> _projectRows = [];
    private readonly ObservableCollection<EngineRow> _engineRows = [];

    public MainWindow()
    {
        InitializeComponent();
        ProjectList.ItemsSource = _projectRows;
        EngineList.ItemsSource = _engineRows;
        RefreshRows();
    }

    private void RefreshRows()
    {
        _projectRows.Clear();
        foreach (ProjectEntry entry in _catalog.Projects)
        {
            // 목록을 그릴 때마다 파일을 다시 읽는다. 밖에서 고쳐졌을 수 있고, 진실은
            // 런처 목록이 아니라 그 파일에 있다(D-99).
            if (ProjectFileReader.TryRead(entry.ProjectFilePath, out ProjectFileInfo info, out ProjectFileError error))
            {
                entry.EngineVersion = info.EngineVersion;
                entry.DisplayName = string.IsNullOrEmpty(info.ProductName)
                    ? Path.GetFileNameWithoutExtension(entry.ProjectFilePath)
                    : info.ProductName;
                string dimension = info.Framework == FrameworkKind.Framework3D ? "3D" : "2D";
                _projectRows.Add(new ProjectRow
                {
                    Name = entry.DisplayName,
                    Path = entry.ProjectFilePath,
                    Detail = $"엔진 {info.EngineVersion} · {dimension}",
                });
            }
            else
            {
                _projectRows.Add(new ProjectRow
                {
                    Name = string.IsNullOrEmpty(entry.DisplayName)
                        ? Path.GetFileNameWithoutExtension(entry.ProjectFilePath)
                        : entry.DisplayName,
                    Path = entry.ProjectFilePath,
                    Detail = error.Message,
                });
            }
        }

        _engineRows.Clear();
        foreach (EngineEntry engine in _catalog.Engines)
        {
            _engineRows.Add(new EngineRow { Version = engine.Version, Directory = engine.InstallDirectory });
        }
    }

    private void Say(string message, InfoBarSeverity severity)
    {
        Notice.Message = message;
        Notice.Severity = severity;
        Notice.IsOpen = true;
    }

    private nint WindowHandle => WinRT.Interop.WindowNative.GetWindowHandle(this);

    private async void OnAddProject(object sender, RoutedEventArgs args)
    {
        var picker = new FileOpenPicker();
        WinRT.Interop.InitializeWithWindow.Initialize(picker, WindowHandle);
        picker.FileTypeFilter.Add(".jproject");
        StorageFile? file = await picker.PickSingleFileAsync();
        if (file is null)
        {
            return;
        }

        // 읽어 보고 넣는다. 프로젝트가 아닌 파일을 목록에 올려 두면 그때부터 매번
        // 빨간 줄로 남는다 - 이유를 지금 알려 주는 편이 낫다.
        if (false == ProjectFileReader.TryRead(file.Path, out ProjectFileInfo info, out ProjectFileError error))
        {
            Say($"프로젝트로 읽지 못했습니다: {error.Message}", InfoBarSeverity.Error);
            return;
        }
        if (_catalog.Projects.Any(entry => string.Equals(entry.ProjectFilePath, file.Path, StringComparison.OrdinalIgnoreCase)))
        {
            Say("이미 목록에 있는 프로젝트입니다.", InfoBarSeverity.Informational);
            return;
        }

        _catalog.Projects.Add(new ProjectEntry
        {
            ProjectFilePath = file.Path,
            DisplayName = string.IsNullOrEmpty(info.ProductName)
                ? Path.GetFileNameWithoutExtension(file.Path)
                : info.ProductName,
            EngineVersion = info.EngineVersion,
        });
        SaveAndRefresh();
    }

    private void OnRemoveProject(object sender, RoutedEventArgs args)
    {
        if (ProjectList.SelectedItem is not ProjectRow row)
        {
            Say("목록에서 프로젝트를 먼저 고르세요.", InfoBarSeverity.Informational);
            return;
        }
        // **목록에서만 뺀다.** 프로젝트 폴더는 건드리지 않는다.
        _catalog.Projects.RemoveAll(entry =>
            string.Equals(entry.ProjectFilePath, row.Path, StringComparison.OrdinalIgnoreCase));
        SaveAndRefresh();
    }

    private void OnOpenProject(object sender, RoutedEventArgs args)
    {
        if (ProjectList.SelectedItem is not ProjectRow row)
        {
            Say("열 프로젝트를 먼저 고르세요.", InfoBarSeverity.Informational);
            return;
        }
        if (false == ProjectFileReader.TryRead(row.Path, out ProjectFileInfo info, out ProjectFileError error))
        {
            Say($"프로젝트를 읽지 못했습니다: {error.Message}", InfoBarSeverity.Error);
            return;
        }

        EngineEntry? engine = _catalog.Engines.FirstOrDefault(
            candidate => string.Equals(candidate.Version, info.EngineVersion, StringComparison.OrdinalIgnoreCase));
        if (engine is null)
        {
            Say($"이 프로젝트가 쓰는 엔진 {info.EngineVersion} 이(가) 등록돼 있지 않습니다. 엔진 탭에서 폴더를 등록하세요.",
                InfoBarSeverity.Warning);
            return;
        }

        EditorStartResult result = EditorProcess.Start(engine, row.Path);
        if (false == result.Started)
        {
            Say(result.Message, InfoBarSeverity.Error);
            return;
        }

        ProjectEntry? entry = _catalog.Projects.FirstOrDefault(
            candidate => string.Equals(candidate.ProjectFilePath, row.Path, StringComparison.OrdinalIgnoreCase));
        if (entry is not null)
        {
            entry.LastOpenedAt = DateTimeOffset.Now;
            _catalog.Save();
        }
        Say($"{row.Name} 을(를) 엔진 {info.EngineVersion} 으로 열었습니다.", InfoBarSeverity.Success);
        WatchForFailure(result.Process!, row.Name);
    }

    /// <summary>
    /// 에디터가 실패로 끝나면 알려 준다. 창이 잠깐 떴다 사라지면 사용자는 아무것도 모른다.
    /// 정상 종료는 알리지 않는다 - 닫은 것은 사용자가 한 일이다.
    /// </summary>
    private async void WatchForFailure(Process process, string name)
    {
        try
        {
            await process.WaitForExitAsync();
        }
        catch (Exception exception) when (exception is InvalidOperationException or SystemException)
        {
            return;
        }
        int exitCode = process.ExitCode;
        if (exitCode == 0)
        {
            return;
        }
        DispatcherQueue.TryEnqueue(() =>
            Say($"{name}: {EditorProcess.DescribeExitCode(exitCode)}", InfoBarSeverity.Error));
    }

    private async void OnAddEngine(object sender, RoutedEventArgs args)
    {
        var picker = new FolderPicker();
        WinRT.Interop.InitializeWithWindow.Initialize(picker, WindowHandle);
        picker.FileTypeFilter.Add("*");
        StorageFolder? folder = await picker.PickSingleFolderAsync();
        if (folder is null)
        {
            return;
        }

        string editorPath = Path.Combine(folder.Path, "JBroEditorHost.exe");
        if (false == File.Exists(editorPath))
        {
            Say("이 폴더에는 JBroEditorHost.exe 가 없습니다.", InfoBarSeverity.Error);
            return;
        }

        if (false == EngineVersionReader.TryRead(editorPath, out string version))
        {
            Say("이 엔진은 자기 버전을 말하지 않습니다. 버전 리소스가 없는 빌드입니다.", InfoBarSeverity.Error);
            return;
        }
        if (_catalog.Engines.Any(entry => string.Equals(entry.InstallDirectory, folder.Path, StringComparison.OrdinalIgnoreCase)))
        {
            Say("이미 등록된 폴더입니다.", InfoBarSeverity.Informational);
            return;
        }
        _catalog.Engines.Add(new EngineEntry { Version = version, InstallDirectory = folder.Path });
        SaveAndRefresh();
        Say($"엔진 {version} 을(를) 등록했습니다.", InfoBarSeverity.Success);
    }

    private void OnRemoveEngine(object sender, RoutedEventArgs args)
    {
        if (EngineList.SelectedItem is not EngineRow row)
        {
            Say("목록에서 엔진을 먼저 고르세요.", InfoBarSeverity.Informational);
            return;
        }
        // 등록만 지운다. 설치 폴더는 건드리지 않는다.
        _catalog.Engines.RemoveAll(entry =>
            string.Equals(entry.InstallDirectory, row.Directory, StringComparison.OrdinalIgnoreCase));
        SaveAndRefresh();
    }

    private void SaveAndRefresh()
    {
        try
        {
            _catalog.Save();
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            Say($"목록을 저장하지 못했습니다: {exception.Message}", InfoBarSeverity.Error);
        }
        RefreshRows();
    }
}

#include <JBro/Editor/EditorApplication.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Runtime/GameObject.h>

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    void TestEditorProjectSessions()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 96;
        config.windowHeight = 64;
        JBro::EditorApplicationConfig unsupportedConfig = config;
        unsupportedConfig.graphicsApi = JBro::GraphicsApi::Vulkan;
        Check(false == editor.Initialize(unsupportedConfig),
            "editor must reject an unavailable graphics backend without changing state");
        Check(editor.Initialize(config), "editor process must initialize without a project");
        Check(editor.IsInitialized() && false == editor.HasOpenProject(),
            "initialized editor must begin without a project");

        JBro::ProjectDescriptor project;
        project.framework = JBro::FrameworkKind::Framework2D;
        project.graphicsApi = JBro::GraphicsApi::D3D12;
        Check(editor.OpenProject(project), "editor must open a 2D project");
        Check(false == editor.OpenProject(project), "editor must reject opening over a live project");
        Check(editor.Tick(1.0f / 60.0f), "editor must tick its project through EngineInstance");
        // 카메라 없는 빈 프로젝트는 제출할 것이 없다. 실패가 아니라 버려진 프레임이다(D-49).
        Check(editor.GetLastFrameStatus() == JBro::FrameStatus::Skipped,
            "a 2D project without a camera must report a skipped frame, not a presented one");
        editor.CloseProject();
        Check(editor.IsInitialized() && false == editor.HasOpenProject(),
            "project close must preserve the editor process");
        Check(editor.Tick(1.0f / 60.0f), "projectless editor must keep pumping its process");
        Check(editor.GetLastFrameStatus() == JBro::FrameStatus::Skipped,
            "projectless editor tick must skip GPU submission");
        Check(editor.OpenProject(project) && editor.Tick(1.0f / 60.0f),
            "editor must reopen a project on its live process resources");
        editor.CloseProject();

        // 3D 백엔드는 아직 그리지 않지만, 그것이 호스트를 끝내는 이유가 되어서는 안 된다(F-7).
        JBro::ProjectDescriptor project3D;
        project3D.framework = JBro::FrameworkKind::Framework3D;
        project3D.graphicsApi = JBro::GraphicsApi::D3D12;
        Check(editor.OpenProject(project3D), "editor must open a 3D project");
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(1.0f / 60.0f),
                "a 3D project must keep ticking even though its renderer submits nothing");
            Check(editor.GetLastFrameStatus() == JBro::FrameStatus::Skipped,
                "a non-submitting 3D frame must be skipped, not an invalid state");
        }
        Check(editor.IsInitialized() && editor.HasOpenProject(),
            "repeated empty 3D frames must leave the editor process and project alive");
        editor.Shutdown();
        Check(false == editor.IsInitialized() && false == editor.HasOpenProject(),
            "editor shutdown must release project and process resources");
        editor.Shutdown();
    }

    // 임시 파일 하나를 만들고 지운다. 여기서 만드는 `.jproject` 와 `.jcanvas` 는
    // 리포에 남기지 않는다 — 테스트가 만든 것이 소스 트리에 쌓이면 안 된다.
    JBro::String TempPath(const char* name)
    {
        char buffer[MAX_PATH] = {};
        const DWORD length = GetTempPathA(static_cast<DWORD>(sizeof(buffer)), buffer);
        JBro::String path(length > 0 ? buffer : ".");
        if (path.empty() || (path.back() != '/' && path.back() != '\\'))
        {
            path.append("\\");
        }
        path.append(name);
        return path;
    }

    bool WriteTextFile(const JBro::String& path, const char* text)
    {
        std::FILE* file = nullptr;
        if (fopen_s(&file, path.c_str(), "wb") != 0 || file == nullptr)
        {
            return false;
        }
        const std::size_t length = std::strlen(text);
        const std::size_t written = std::fwrite(text, 1, length, file);
        std::fclose(file);
        return written == length;
    }

    void TestEditorOpensAProjectFile()
    {
        // 스크립트 경로를 비워 둔다. 이 테스트가 보려는 것은 `.jproject` 를 읽는 길이지
        // DLL 을 싣는 길이 아니다(그쪽은 ScriptDLLLoaderTests 가 본다).
        const JBro::String projectPath = TempPath("JBroEditorTest.jproject");
        Check(WriteTextFile(projectPath,
            "Version: 1\n"
            "RootPath: .\n"
            "ResolutionWidth: 1280\n"
            "ResolutionHeight: 720\n"
            "PixelsPerUnit: 100\n"
            "ScriptOutputLibraryPath: \"\"\n"
            "LastOpenedCanvasPath: Scenes/Opening.jcanvas\n"
            "Build:\n"
            "  ProductName: EditorTest\n"
            "  StartupCanvas: Scenes/Opening.jcanvas\n"),
            "the test must be able to write its own project file");

        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        JBro::EditorApplication editor;
        Check(editor.Initialize(config), "the editor must initialize");

        JBro::ProjectFileError error;
        if (false == editor.OpenProjectFile(
            projectPath.c_str(), JBro::FrameworkKind::Framework2D, error))
        {
            std::cout << "  open failed at line " << error.line
                << ": " << error.message.c_str() << std::endl;
            Check(false, "the editor must open a project from its file");
        }
        Check(editor.HasOpenProject(), "the project must be open afterwards");

        // 파일의 내용이 실제로 실렸는지 본다. 열리기만 하고 값이 비면 소용이 없다.
        const JBro::ProjectFile& project = editor.GetProjectFile();
        Check(project.resolutionWidth == 1280 && project.resolutionHeight == 720,
            "what the project file said must be readable through the editor");
        Check(project.lastOpenedCanvasPath == "Scenes/Opening.jcanvas",
            "the canvas the project remembers must come through");

        // 그 안의 경로는 전부 프로젝트 폴더 기준이다.
        const JBro::String resolved =
            editor.ResolveProjectPath(project.lastOpenedCanvasPath.c_str());
        Check(resolved != project.lastOpenedCanvasPath,
            "a relative path must be joined to the project folder");
        Check(resolved.find("Scenes/Opening.jcanvas") != JBro::String::npos,
            "and must still end with what it named");
        Check(editor.ResolveProjectPath("C:/elsewhere/Other.jcanvas")
            == "C:/elsewhere/Other.jcanvas",
            "an absolute path must be left alone");

        editor.CloseProject();
        Check(false == editor.HasOpenProject(), "closing must leave the process running");
        // 프로젝트가 없으면 기준도 없어야 한다. 남아 있으면 다음 프로젝트의 경로가
        // 옛 폴더를 기준으로 풀린다.
        Check(editor.ResolveProjectPath("Scenes/Opening.jcanvas") == "Scenes/Opening.jcanvas",
            "with no project open there is nothing to resolve against");

        editor.Shutdown();
        std::remove(projectPath.c_str());
    }

    void TestEditorSavesAndOpensACanvas()
    {
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        JBro::EditorApplication editor;
        Check(editor.Initialize(config), "the editor must initialize");

        JBro::ProjectDescriptor project;
        project.name = {"EditorTest", 10};
        Check(editor.OpenProject(project), "the editor must open a 2D project");

        JBro::Canvas* canvas = editor.GetCanvas();
        Check(canvas != nullptr, "an open project must have a canvas");

        JBro::GameObject* object = canvas->CreateObject("Saved");
        auto* transform = canvas->AttachComponent<JBro::Component::Transform2D>(object);
        transform->position = { 4.5f, -1.25f };

        const JBro::String canvasPath = TempPath("JBroEditorTest.jcanvas");
        JBro::CanvasFileError error;
        if (false == editor.SaveCanvas(canvasPath.c_str(), error))
        {
            std::cout << "  save failed: " << error.message.c_str() << std::endl;
            Check(false, "the editor must save its canvas to a file");
        }

        // 닫고 다시 열면 빈 캔버스다. 거기에 읽어 넣는다.
        editor.CloseProject();
        Check(editor.OpenProject(project), "the editor must open a project again");
        JBro::Canvas* reopened = editor.GetCanvas();
        Check(reopened != nullptr, "a new project session must bring a canvas");
        // 주소가 다른지는 묻지 않는다. 앞의 것이 해제된 자리에 다시 잡힐 수 있고,
        // 그것은 틀린 것이 아니다. 중요한 것은 내용이 비어 있다는 쪽이다.
        Check(reopened->GetObjectCount() == 0, "and that canvas must start empty");

        if (false == editor.LoadCanvas(canvasPath.c_str(), error))
        {
            std::cout << "  load failed: " << error.message.c_str()
                << " (object " << error.objectName.c_str()
                << ", type " << error.typeName.c_str() << ")" << std::endl;
            Check(false, "the editor must read a canvas it wrote");
        }
        Check(reopened->GetObjectCount() == 1, "the object must come back");

        JBro::GameObject* loaded = nullptr;
        reopened->ForEachObject([&loaded](JBro::GameObject& found) { loaded = &found; });
        Check(loaded != nullptr && std::strcmp(loaded->GetTag(), "Saved") == 0,
            "and come back under its own name");
        auto* loadedTransform = reopened->FindComponentRaw<JBro::Component::Transform2D>(loaded);
        Check(loadedTransform != nullptr
            && loadedTransform->position.x == 4.5f
            && loadedTransform->position.y == -1.25f,
            "with the values it was saved with");

        // 이미 내용이 있는 캔버스에 또 읽으면 거절해야 한다.
        Check(false == editor.LoadCanvas(canvasPath.c_str(), error),
            "reading into a canvas that already holds something must be refused");

        editor.Shutdown();
        std::remove(canvasPath.c_str());
    }

    void TestCanvasWorkNeedsAnOpenProject()
    {
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        JBro::EditorApplication editor;
        Check(editor.Initialize(config), "the editor must initialize");

        Check(editor.GetCanvas() == nullptr, "there is no canvas without a project");
        JBro::CanvasFileError error;
        Check(false == editor.SaveCanvas(TempPath("never.jcanvas").c_str(), error),
            "saving with no project open must be refused");
        Check(false == error.message.empty(), "and must say why");
        Check(false == editor.LoadCanvas(TempPath("never.jcanvas").c_str(), error),
            "loading with no project open must be refused");

        editor.Shutdown();
    }

    void TestAFailedOpenSaysWhy()
    {
        // 파일은 멀쩡히 읽혔는데 여는 데 실패하는 경우다. 여기서 아무 말도 하지 않으면
        // 부르는 쪽은 빈 오류를 받고 무엇이 잘못됐는지 알 길이 없다.
        const JBro::String projectPath = TempPath("JBroEditorMissingDll.jproject");
        Check(WriteTextFile(projectPath,
            "Version: 1\n"
            "ScriptOutputLibraryPath: NoSuchScriptModule.dll\n"),
            "the test must be able to write its own project file");

        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        JBro::EditorApplication editor;
        Check(editor.Initialize(config), "the editor must initialize");

        JBro::ProjectFileError error;
        Check(false == editor.OpenProjectFile(
            projectPath.c_str(), JBro::FrameworkKind::Framework2D, error),
            "a project whose script module is missing must not open");
        Check(false == error.message.empty(),
            "and the refusal must say something rather than come back blank");
        Check(false == editor.HasOpenProject(),
            "nothing may be left half open behind a refusal");

        // 실패한 뒤에도 다시 열 수 있어야 한다. 프레임워크가 남아 있으면 막힌다.
        Check(WriteTextFile(projectPath,
            "Version: 1\n"
            "ScriptOutputLibraryPath: \"\"\n"),
            "the test must be able to rewrite its project file");
        Check(editor.OpenProjectFile(
            projectPath.c_str(), JBro::FrameworkKind::Framework2D, error),
            "the editor must still be usable after a refused open");

        editor.Shutdown();
        std::remove(projectPath.c_str());
    }
}

int RunEditorApplicationTests()
{
    TestEditorProjectSessions();
    TestEditorOpensAProjectFile();
    TestEditorSavesAndOpensACanvas();
    TestCanvasWorkNeedsAnOpenProject();
    TestAFailedOpenSaysWhy();
    std::cout << "Editor application tests passed.\n";
    return 0;
}

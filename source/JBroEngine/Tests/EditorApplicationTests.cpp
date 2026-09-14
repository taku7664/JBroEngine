#include <JBro/Editor/EditorApplication.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Types/Array.h>
#include <JBro/Framework2D/Component/Camera2D.h>
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

    // **에디터 화면이 실제로 나오는가.** 게임은 텍스처로 가고 백버퍼에는 UI 만 남는다 -
    // 그 프레임은 "게임이 낼 것이 없는" 프레임이기도 해서, 배선이 하나라도 어긋나면
    // 화면이 통째로 검게 남는다. 픽셀을 되읽지 않으면 알 수 없다(D-63).
    void TestTheEditorPaintsItsOwnScreen()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 320;
        config.windowHeight = 240;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the editor screen not verified"
                << std::endl;
            return;
        }

        JBro::ProjectDescriptor project;
        constexpr char name[] = "EditorScreenProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");

        Check(false == editor.IsEditorUiEnabled(), "the UI starts off");
        Check(false == editor.EnableEditorUi({0, 0}), "a game view with no size is refused");

        // 게임 뷰는 창과 다른 크기다. 4:3 을 320x240 패널에 넣으면 위아래가 남는다.
        constexpr std::uint32_t GameWidth = 64;
        constexpr std::uint32_t GameHeight = 48;
        Check(editor.EnableEditorUi({GameWidth, GameHeight}), "the editor UI must turn on");
        Check(editor.IsEditorUiEnabled(), "and say so");
        Check(editor.GetGameViewTexture().IsValid(), "with a game view to draw into");
        Check(false == editor.EnableEditorUi({GameWidth, GameHeight}),
            "turning it on twice must be refused");

        // **카메라를 하나 놓는다.** 그래야 게임이 텍스처에 실제로 무언가를 그리고,
        // 그 픽셀이 패널까지 오는지 볼 수 있다. 카메라가 없으면 텍스처는 한 번도
        // 그려지지 않은 채 패널에 붙고, 그래도 화면은 그럴듯하게 나온다.
        JBro::Canvas* canvas = editor.GetCanvas();
        Check(canvas != nullptr, "the probe project must have a canvas");
        JBro::GameObject* eye = canvas->CreateObject("Eye");
        Check(canvas->AttachComponent<JBro::Component::Transform2D>(eye) != nullptr,
            "the camera needs a transform");
        auto* camera = canvas->AttachComponent<JBro::Component::Camera2D>(eye);
        Check(camera != nullptr, "the probe camera must attach");
        camera->primary = true;
        // 창의 어느 색과도 겹치지 않는 색이다. 이 색이 화면에 있으면 게임 화면이
        // 텍스처를 거쳐 패널까지 온 것이다.
        camera->clearColor = {0.0f, 0.85f, 0.35f, 1.0f};

        // 새 창은 ImGui 가 크기를 재는 동안 감춰진다. 몇 프레임 돌린 뒤에 본다.
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(1.0f / 60.0f), "the editor must keep ticking with its UI on");
        }

        JBro::Renderer* renderer = editor.GetRenderer();
        Check(renderer != nullptr, "the editor must expose its renderer");
        JBro::Array<std::byte> image;
        image.Resize(320 * 240 * 4);
        JBro::TextureReadback readback;
        Check(renderer->ReadBackBuffer(image.Data(), image.Size(), readback),
            "the editor window must read back");

        // 오버레이가 백버퍼를 지우는 색이다(0.09, 0.09, 0.11). 패널이 덮은 자리는
        // 이 색이 아니다.
        constexpr int ClearRed = 23;
        constexpr int ClearGreen = 23;
        constexpr int ClearBlue = 28;
        const auto Differs = [](unsigned char got, int want) {
            const int gap = static_cast<int>(got) - want;
            return gap > 4 || gap < -4;
        };

        std::size_t painted = 0;
        std::size_t bright = 0;
        for (std::uint32_t y = 0; y < 240; ++y)
        {
            for (std::uint32_t x = 0; x < 320; ++x)
            {
                const std::size_t offset = static_cast<std::size_t>(y) * readback.rowPitch
                    + static_cast<std::size_t>(x) * 4;
                const auto* pixel =
                    reinterpret_cast<const unsigned char*>(image.Data() + offset);
                // **지움색과 다른지를 본다.** 밝기로 재면 안 된다 - ImGui 의 창
                // 배경은 오버레이가 지운 색보다 오히려 어둡다.
                if (Differs(pixel[0], ClearBlue)
                    || Differs(pixel[1], ClearGreen)
                    || Differs(pixel[2], ClearRed))
                {
                    ++painted;
                }
                if (pixel[0] > 200 && pixel[1] > 200 && pixel[2] > 200)
                {
                    ++bright;
                }
            }
        }
        // 카메라가 지운 초록이 화면에 있어야 한다. 게임 -> 텍스처 -> 패널로
        // 이어지는 길 어디가 끊겨도 이 숫자가 0 이 된다.
        std::size_t gamePixels = 0;
        for (std::uint32_t y = 0; y < 240; ++y)
        {
            for (std::uint32_t x = 0; x < 320; ++x)
            {
                const std::size_t offset = static_cast<std::size_t>(y) * readback.rowPitch
                    + static_cast<std::size_t>(x) * 4;
                const auto* pixel =
                    reinterpret_cast<const unsigned char*>(image.Data() + offset);
                if (pixel[2] < 40 && pixel[1] > 180 && pixel[0] > 60 && pixel[0] < 120)
                {
                    ++gamePixels;
                }
            }
        }

        std::cout << "  the editor painted " << painted << " pixels (" << bright
            << " bright, " << gamePixels << " from the game) on its window" << std::endl;
        // 창을 채우는 패널이 하나 있으므로 화면 대부분이 패널 색이다.
        Check(painted > (320 * 240) / 2,
            "the editor panel must cover the window");
        // 패널 제목이 글자로 나온다. 폰트 아틀라스가 안 올라가면 여기서 걸린다.
        Check(bright > 50, "and its text must be on screen");
        // 게임 뷰는 4:3 이고 패널은 그보다 넓으므로 좌우가 남는다. 그래도 화면의
        // 상당 부분이 게임 화면이어야 한다.
        Check(gamePixels > (320 * 240) / 4,
            "the game must reach the panel through its texture");

        // 꺼지면 게임이 다시 백버퍼로 간다. 남은 GPU 리소스도 함께 놓는다.
        editor.DisableEditorUi();
        Check(false == editor.IsEditorUiEnabled(), "the UI must turn off");
        Check(false == editor.GetGameViewTexture().IsValid(),
            "and give its game view texture back");
        Check(editor.Tick(1.0f / 60.0f), "the editor must keep ticking without its UI");

        editor.Shutdown();
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
    TestTheEditorPaintsItsOwnScreen();
    TestEditorOpensAProjectFile();
    TestEditorSavesAndOpensACanvas();
    TestCanvasWorkNeedsAnOpenProject();
    TestAFailedOpenSaysWhy();
    std::cout << "Editor application tests passed.\n";
    return 0;
}

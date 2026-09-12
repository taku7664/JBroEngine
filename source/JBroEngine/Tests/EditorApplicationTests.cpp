#include <JBro/Editor/EditorApplication.h>

#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
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
}

int RunEditorApplicationTests()
{
    TestEditorProjectSessions();
    std::cout << "Editor application tests passed.\n";
    return 0;
}

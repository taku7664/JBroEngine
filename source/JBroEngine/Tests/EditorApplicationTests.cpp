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
        Check(editor.GetLastFrameStatus() == JBro::FrameStatus::Ready,
            "editor must expose the engine frame result");
        editor.CloseProject();
        Check(editor.IsInitialized() && false == editor.HasOpenProject(),
            "project close must preserve the editor process");
        Check(editor.Tick(1.0f / 60.0f), "projectless editor must keep pumping its process");
        Check(editor.GetLastFrameStatus() == JBro::FrameStatus::Skipped,
            "projectless editor tick must skip GPU submission");
        Check(editor.OpenProject(project) && editor.Tick(1.0f / 60.0f),
            "editor must reopen a project on its live process resources");
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

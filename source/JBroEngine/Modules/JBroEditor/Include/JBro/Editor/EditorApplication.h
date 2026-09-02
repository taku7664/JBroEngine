#pragma once

#include <JBro/Runtime/IFramework.h>
#include <JBro/Platform/Platform.h>
#include <JBro/RHI/RHI.h>

namespace JBro::Engine
{
    enum class FrameworkKind : std::uint8_t
    {
        Framework2D,
        Framework3D
    };

    struct ProjectDescriptor
    {
        JStringView name;
        JStringView projectGuid;
        JStringView engineVersion;
        FrameworkKind framework = FrameworkKind::Framework2D;
        GraphicsApi graphicsApi = GraphicsApi::D3D12;
    };

    class EditorApplication
    {
    public:
        bool OpenProject(const ProjectDescriptor& project);
        int Run();
        void CloseProject();

    private:
        bool CreatePlatformAndWindow();
        bool CreateGraphicsDevice();
        bool CreateSelectedFramework();
        void DestroySelectedFramework();

        IPlatform* mPlatform = nullptr;
        IRHIModule* mRhiModule = nullptr;
        IRHIDevice* mDevice = nullptr;
        IFramework* mFramework = nullptr;
        WindowHandle mWindow;
    };
}

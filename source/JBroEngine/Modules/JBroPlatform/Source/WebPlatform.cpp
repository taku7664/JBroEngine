#include <JBro/Platform/WebPlatform.h>

#include <JBro/Network/Socket.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/Bool.h>
#if defined(__EMSCRIPTEN__)
#include "MiniaudioAudioOutput.h"
#endif
#if defined(__EMSCRIPTEN__)
#include <JBro/Network/Web/WebSocketProvider.h>
#endif

namespace JBro
{
    Bool WebPlatform::Initialize(const JMemoryContext&)
    {
        return true;
    }

    void WebPlatform::Shutdown()
    {
    }

    WindowHandle WebPlatform::OpenPlatformWindow(const WindowDesc&)
    {
        return {};
    }

    void WebPlatform::ClosePlatformWindow(WindowHandle)
    {
    }

    SurfaceHandle WebPlatform::CreateSurface(WindowHandle window)
    {
        return {window.value};
    }

    void WebPlatform::PumpEvents()
    {
    }

    JArrayView<InputEvent> WebPlatform::GetInputEvents() const
    {
        // 이 플랫폼은 아직 입력을 모으지 않는다.
        return {};
    }

    void WebPlatform::ClearInputEvents()
    {
        // 모으지 않으니 비울 것도 없다.
    }

    void WebPlatform::WaitForEvents(UInt32)
    {
        // Browser hosts must yield through their external event loop.
    }

    Bool WebPlatform::ShouldClose(WindowHandle) const
    {
        return false;
    }

    DynamicLibrary WebPlatform::LoadDynamicLibrary(const char*)
    {
        return {};
    }

    Bool WebPlatform::GetWindowState(WindowHandle, WindowState& state) const
    {
        state = {};
        return false;
    }

    void* WebPlatform::GetSymbol(DynamicLibrary, const char*)
    {
        return nullptr;
    }

    void WebPlatform::UnloadDynamicLibrary(DynamicLibrary)
    {
    }

    Bool WebPlatform::ReadWholeFile(const char*, Array<std::byte>&)
    {
        return false;
    }

    Bool WebPlatform::WriteWholeFile(const char*, JArrayView<std::byte>)
    {
        return false;
    }

    Bool WebPlatform::FileExists(const char*) const
    {
        return false;
    }

    Bool WebPlatform::DirectoryExists(const char*) const
    {
        return false;
    }

    Bool WebPlatform::EnumerateDirectory(const char*, DirectoryVisitor, void*)
    {
        return false;
    }

    OwnerPtr<IAudioOutput> WebPlatform::CreateAudioOutput(const AudioOutputDesc& desc)
    {
#if defined(__EMSCRIPTEN__)
        return Internal::CreateMiniaudioOutput(desc);
#else
        // 이 저장소의 Windows 빌드에서 이 플랫폼은 미리보기다. 장치도 없다.
        (void)desc;
        return nullptr;
#endif
    }

    UInt32 WebPlatform::EnumerateAudioOutputs(AudioDeviceInfo* devices, UInt32 capacity)
    {
#if defined(__EMSCRIPTEN__)
        return Internal::EnumerateMiniaudioOutputs(devices, capacity);
#else
        (void)devices;
        (void)capacity;
        return 0;
#endif
    }

    OwnerPtr<Network::ISocketProvider> WebPlatform::CreateSocketProvider()
    {
#if defined(__EMSCRIPTEN__)
        return MakeOwnerPtr<Network::Web::WebSocketProvider>();
#else
        // 이 저장소의 Windows 빌드에서 이 플랫폼은 미리보기다. 소켓도 없다.
        return nullptr;
#endif
    }
}

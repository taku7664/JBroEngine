#pragma once

#include <cstdint>

namespace JBro::System
{
    class Camera2DSystem;
    class Physics2DSystem;
    class ScriptSystem;
    class SpriteRender2DSystem;
    class Transform2DSystem;
}

namespace JBro
{
    inline constexpr std::uint32_t SystemContextAbiVersion = 1;

    struct SystemContext
    {
        std::uint32_t AbiVersion = SystemContextAbiVersion;
        System::Transform2DSystem* Transform2D = nullptr;
        System::SpriteRender2DSystem* SpriteRender2D = nullptr;
        System::Camera2DSystem* Camera2D = nullptr;
        System::Physics2DSystem* Physics2D = nullptr;
        System::ScriptSystem* Script = nullptr;
    };

    void BindSystemContext(const SystemContext& context);
    const SystemContext& GetSystemContext();
}

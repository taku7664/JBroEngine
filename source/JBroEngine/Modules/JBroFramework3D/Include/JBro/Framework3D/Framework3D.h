#pragma once

#include <JBro/Asset/Asset.h>
#include <JBro/Runtime/IFramework.h>

namespace JBro
{
    struct Vec3       { float x = 0.0f; float y = 0.0f; float z = 0.0f; };
    struct Quaternion { float x = 0.0f; float y = 0.0f; float z = 0.0f; float w = 1.0f; };
}

namespace JBro::Component
{
    struct Transform3D  { JBro::Vec3 position; JBro::Quaternion rotation; JBro::Vec3 scale{ 1.0f, 1.0f, 1.0f }; };
    struct Camera3D     { float verticalFieldOfView = 60.0f; };
    struct MeshRenderer { AssetHandle mesh; AssetHandle material; };
    struct Rigidbody3D  { JBro::Vec3 velocity; float mass = 1.0f; };
    struct Collider3D   { JBro::Vec3 size{ 1.0f, 1.0f, 1.0f }; };
}

namespace JBro
{
    class Framework3D final : public IFramework
    {
    public:
        bool Initialize(const FrameworkContext& context) override;
        bool BindScriptContexts() noexcept override;
        void UnbindScriptContexts() noexcept override;
        void Update(float deltaTime) override;
        bool Render() override;
        void Shutdown() override;
    };

    IFramework* CreateFramework3D();
    void        DestroyFramework3D(IFramework* framework);
}

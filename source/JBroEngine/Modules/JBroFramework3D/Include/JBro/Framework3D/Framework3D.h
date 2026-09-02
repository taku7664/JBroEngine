#pragma once

#include <JBro/Runtime/IFramework.h>

namespace JBro::Engine
{
    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct Quaternion
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;
    };

    struct Transform3DComponent
    {
        Vec3 position;
        Quaternion rotation;
        Vec3 scale{ 1.0f, 1.0f, 1.0f };
    };

    struct Camera3DComponent
    {
        float verticalFieldOfView = 60.0f;
    };

    struct MeshRendererComponent
    {
        AssetHandle mesh;
        AssetHandle material;
    };

    struct Rigidbody3DComponent
    {
        Vec3 velocity;
        float mass = 1.0f;
    };

    struct Collider3DComponent
    {
        Vec3 size{ 1.0f, 1.0f, 1.0f };
    };

    class Framework3D final : public IFramework
    {
    public:
        bool Initialize(const FrameworkContext& context) override;
        void Update(float deltaTime) override;
        void Shutdown() override;

        void RegisterComponents(ComponentRegistry& registry);
        void RenderMeshes();
        void StepPhysics(float deltaTime);
    };

    IFramework* CreateFramework3D();
    void DestroyFramework3D(IFramework* framework);
}

#pragma once

#include <JBro/Asset/Asset.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Host/IFramework.h>

namespace JBro
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
}

namespace JBro::Component
{
    class Transform3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Transform3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBro::Vec3 position;
        JBro::Quaternion rotation;
        JBro::Vec3 scale{ 1.0f, 1.0f, 1.0f };
    };

    class Camera3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Camera3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        float verticalFieldOfView = 60.0f;
    };

    class MeshRenderer final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::MeshRenderer";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        AssetHandle mesh;
        AssetHandle material;
    };

    class Rigidbody3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Rigidbody3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBro::Vec3 velocity;
        float mass = 1.0f;
    };

    class Collider3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Collider3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBro::Vec3 size{ 1.0f, 1.0f, 1.0f };
    };
}

namespace JBro
{
    class Framework3D final : public IFramework
    {
    public:
        ~Framework3D() override;
        bool Initialize(const FrameworkContext& context) override;
        bool BindScriptContexts() noexcept override;
        void UnbindScriptContexts() noexcept override;
        void Update(float deltaTime) override;
        bool Render() override;
        void Shutdown() override;

        Canvas* GetCanvas();

    private:
        void RunFixedSteps(float deltaTime);

        FrameworkContext m_context;
        OwnerPtr<Canvas> m_canvas;
        double m_fixedAccumulator = 0.0;
        bool m_initialized = false;
    };

    IFramework* CreateFramework3D();
    void        DestroyFramework3D(IFramework* framework);
}

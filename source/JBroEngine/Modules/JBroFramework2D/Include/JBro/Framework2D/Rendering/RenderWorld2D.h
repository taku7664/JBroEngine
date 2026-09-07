#pragma once

#include <JBro/Asset/Asset.h>
#include <JBro/Core/Core.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Math2D.h>
#include <JBro/Types/Array.h>

#include <cstdint>

namespace JBro
{
    class GameObject;

    struct RenderCamera2D
    {
        GameObject* owner            = nullptr;
        Matrix3x2   view;
        float       orthographicSize = 10.0f;
        Component::CameraProjection2D projection = Component::CameraProjection2D::Orthographic;
        float       nearPlane = -100.0f;
        float       farPlane = 100.0f;
        Color       clearColor;
    };

    struct SpriteRenderItem
    {
        GameObject*   owner = nullptr;
        InstanceId    sourceId = InvalidInstanceId;
        Matrix3x2     world;
        AssetHandle   sprite;
        AssetHandle   material;
        Color         tint;
        Vec2          pivot;
        Vec2          size;
        std::int32_t  renderOrder = 0;
    };

    class RenderWorld2D
    {
    public:
        bool ReserveSprites(std::size_t capacity);
        void BeginFrame();
        void SetCamera(const RenderCamera2D& camera);
        // Reserve outside frame processing. Full storage rejects submissions without allocation.
        bool SubmitSprite(const SpriteRenderItem& item);
        void Sort();
        void EndFrame();

        const RenderCamera2D*   GetCamera()      const;
        std::size_t             GetSpriteCount() const;
        std::size_t             GetSpriteCapacity() const;
        std::size_t             GetDroppedSpriteCount() const;
        const SpriteRenderItem* GetSprites()     const;

    private:
        RenderCamera2D          m_camera;
        bool                    m_hasCamera = false;
        Array<SpriteRenderItem> m_sprites;
        std::size_t             m_droppedSpriteCount = 0;
    };
}

#pragma once

#include <JBro/Asset/Asset.h>
#include <JBro/Core/Core.h>
#include <JBro/Framework2D/Math2D.h>

#include <cstdint>
#include <vector>

namespace JBro
{
    class GameObject;

    struct RenderCamera2D
    {
        GameObject* owner            = nullptr;
        Matrix3x2   view;
        float       orthographicSize = 10.0f;
        Color       clearColor;
    };

    struct SpriteRenderItem
    {
        GameObject*   owner = nullptr;
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
        void BeginFrame();
        void SetCamera(const RenderCamera2D& camera);
        void SubmitSprite(const SpriteRenderItem& item);
        void Sort();
        void EndFrame();

        const RenderCamera2D*   GetCamera()      const;
        std::size_t             GetSpriteCount() const;
        const SpriteRenderItem* GetSprites()     const;

    private:
        RenderCamera2D                m_camera;
        bool                          m_hasCamera = false;
        std::vector<SpriteRenderItem> m_sprites;
    };
}

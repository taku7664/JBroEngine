#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
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
        Color       clearColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    struct SpriteRenderItem
    {
        GameObject*   owner = nullptr;
        InstanceId    sourceId = InvalidInstanceId;
        // 레이어 합성 순서. 정렬 키의 최상위다(D-46).
        std::uint16_t layerOrder = 0;
        Matrix3x2     world;
        // 렌더러가 발급한 텍스처와 그 안의 칸이다(D-113). 추출 단계에서 `SpriteLibrary` 가 스프라이트 에셋에서 풀어 넣는다.
        // 텍스처가 비어 있으면 흰색이라 틴트만 보인다.
        AssetHandle   texture;
        float         uvRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
        AssetHandle   material;
        Color         tint{ 1.0f, 1.0f, 1.0f, 1.0f };
        Vec2          pivot;
        Vec2          size;
        std::int32_t  renderOrder = 0;
    };

    // 정렬은 100B 넘는 아이템이 아니라 이 16B 항목을 움직인다(P-5).
    struct SpriteSortKey
    {
        // [63:48] layerOrder | [47:16] 부호 없는 순서로 옮긴 renderOrder | [15:0] 예약
        std::uint64_t key = 0;
        std::uint32_t index = 0;
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

        // 그리는 순서다. Sort 가 만든 순열을 거쳐 나간다.
        const SpriteRenderItem& GetSprite(std::size_t drawIndex) const;
        // 제출된 순서 그대로다. 정렬 결과가 아니므로 렌더 제출에 쓰지 않는다.
        const SpriteRenderItem* GetSubmittedSprites() const;

    private:
        static std::uint64_t MakeSortKey(const SpriteRenderItem& item);

        RenderCamera2D          m_camera;
        bool                    m_hasCamera = false;
        Array<SpriteRenderItem> m_sprites;
        Array<SpriteSortKey>    m_order;
        std::size_t             m_droppedSpriteCount = 0;
    };
}

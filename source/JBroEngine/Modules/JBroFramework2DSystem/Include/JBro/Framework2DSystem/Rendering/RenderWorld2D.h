#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Canvas/ScreenSpace.h>
#include <JBro/Core/Core.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Math2D.h>

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
        // 텍스처의 샘플러다(D-117). 텍스처가 없으면 뜻이 없다.
        TextureFilter filter = TextureFilter::Nearest;
        AssetHandle   material;
        Color         tint{ 1.0f, 1.0f, 1.0f, 1.0f };
        Vec2          pivot;
        Vec2          size;
        std::int32_t  renderOrder = 0;
        // 텍스처 알파를 거리장으로 읽는 SDF 글자다(4 단계). 참이면 외곽선 색과 외곽선이 끝나는 거리값을 쓴다. 렌더러 패킷과 같은 정규화
        // 정수다(`SpriteSubmit`) - 아이템을 정렬 뒤 옮기는 비용을 스프라이트에 물리지 않는다.
        bool          sdfText = false;
        std::uint16_t outlineEdge = 32768; // 0..1 을 65535 로. 32768 은 0.5(외곽선 없음)
        std::uint8_t  outlineColor[4] = { 0, 0, 0, 0 };
        // 화면 레이어의 것이다(D-237). 참이면 좌표는 기준 해상도의 픽셀이고 월드 뷰 뒤의 화면 뷰에 그려진다.
        bool          screenSpace = false;
        ScreenScaleMode scaleMode = ScreenScaleMode::FixedHeight;
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
        // 화면 레이어의 기준이다(D-237). 프레임을 넘어 남는다 - 프레임워크가 바뀔 때 넣는다.
        void SetScreenSpace(const ScreenSpaceFrame& frame);
        const ScreenSpaceFrame& GetScreenSpace() const;
        std::size_t GetScreenSpriteCount() const;
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
        ScreenSpaceFrame        m_screen;
        std::size_t             m_screenSprites = 0;
        Array<SpriteRenderItem> m_sprites;
        Array<SpriteSortKey>    m_order;
        std::size_t             m_droppedSpriteCount = 0;
    };
}

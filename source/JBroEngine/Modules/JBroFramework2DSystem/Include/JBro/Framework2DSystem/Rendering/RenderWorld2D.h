#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Canvas/ScreenSpace.h>
#include <JBro/Core/Core.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Light2D.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Math2D.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    class GameObject;

    struct RenderCamera2D
    {
        GameObject* owner            = nullptr;
        Matrix3x2   view;
        Float       orthographicSize = 10.0f;
        Float       pixelsPerUnit = 100.0f;
        Component::CameraProjection2D projection = Component::CameraProjection2D::Orthographic;
        Float       nearPlane = -100.0f;
        Float       farPlane = 100.0f;
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
        Float         uvRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
        // 텍스처의 샘플러다(D-117). 텍스처가 없으면 뜻이 없다.
        TextureFilter filter = TextureFilter::Nearest;
        AssetHandle   material;
        Color         tint{ 1.0f, 1.0f, 1.0f, 1.0f };
        Vector2          pivot;
        Vector2          size;
        Int32  renderOrder = 0;
        // 텍스처 알파를 거리장으로 읽는 SDF 글자다(4 단계). 참이면 외곽선 색과 외곽선이 끝나는 거리값을 쓴다. 렌더러 패킷과 같은 정규화
        // 정수다(`SpriteSubmit`) - 아이템을 정렬 뒤 옮기는 비용을 스프라이트에 물리지 않는다.
        Bool          sdfText = false;
        std::uint16_t outlineEdge = 32768; // 0..1 을 65535 로. 32768 은 0.5(외곽선 없음)
        std::uint8_t  outlineColor[4] = { 0, 0, 0, 0 };
        // 화면 레이어의 것이다(D-237). 참이면 좌표는 기준 해상도의 픽셀이고 월드 뷰 뒤의 화면 뷰에 그려진다.
        Bool          screenSpace = false;
        ScreenScaleMode scaleMode = ScreenScaleMode::FixedHeight;
        // 레이어를 얹는 방식이다(D-279). `Normal` 이 아니거나 불투명도가 1 보다 작으면 브리지가 그 레이어를 렌더러의 묶음으로 낸다.
        LayerBlend    layerBlend = LayerBlend::Normal;
        Float         layerOpacity = 1.0f;
        // 레이어의 패럴랙스 계수다(D-286). 게임 화면을 그릴 때 브리지가 이 아이템을 `카메라 위치 x (1 - 계수)` 만큼 옮긴다.
        Float         layerParallax = 1.0f;
        // 레이어가 빛을 받는가(D-291). 브리지가 이 아이템을 렌더러의 빛을 받는 구간에 낸다. 화면 레이어는 보지 않는다.
        Bool          layerLit = true;
    };

    // 2D 라이트 하나다(D-291). 위치·방향은 월드다. 브리지가 렌더러의 `Light2DSubmit` 으로 옮긴다.
    struct Light2DRenderItem
    {
        GameObject* owner = nullptr;
        Component::Light2DType type = Component::Light2DType::Point;
        Vector2 position;
        // 오브젝트의 +x 다(길이 1). `Spot` 만 본다.
        Vector2 direction{ 1.0f, 0.0f };
        Color color{ 1.0f, 1.0f, 1.0f, 1.0f };
        Float intensity = 1.0f;
        Float innerRadius = 0.0f;
        Float outerRadius = 5.0f;
        Degree innerAngle = 30.0f;
        Degree outerAngle = 60.0f;
        // 라이트가 놓인 레이어의 패럴랙스다(D-286) - 그 레이어의 스프라이트와 함께 옮겨야 제 자리를 비춘다.
        Float layerParallax = 1.0f;
    };

    // 정렬은 100B 넘는 아이템이 아니라 이 16B 항목을 움직인다(P-5).
    struct SpriteSortKey
    {
        // [63:48] layerOrder | [47:16] 부호 없는 순서로 옮긴 renderOrder | [15:0] 예약
        UInt64 key = 0;
        UInt32 index = 0;
    };

    class RenderWorld2D
    {
    public:
        Bool ReserveSprites(std::size_t capacity);
        void BeginFrame();
        void SetCamera(const RenderCamera2D& camera);
        // 켜져 있지만 값이 잘못되어 건너뛴 카메라 수다(D-239). 게임 뷰가 "카메라 없음" 과 가려 까닭을 말한다.
        void SetUnusableCameraCount(UInt32 count);
        UInt32 GetUnusableCameraCount() const;
        // 화면 레이어의 기준이다(D-237). 프레임을 넘어 남는다 - 프레임워크가 바뀔 때 넣는다.
        void SetScreenSpace(const ScreenSpaceFrame& frame);
        const ScreenSpaceFrame& GetScreenSpace() const;
        std::size_t GetScreenSpriteCount() const;
        // Reserve outside frame processing. Full storage rejects submissions without allocation.
        Bool SubmitSprite(const SpriteRenderItem& item);
        // 라이트 저장소다(D-291). 스프라이트처럼 프레임 밖에서 잡고, 차면 버리고 센다.
        Bool ReserveLights(std::size_t capacity);
        Bool SubmitLight(const Light2DRenderItem& item);
        std::size_t GetLightCount() const;
        std::size_t GetDroppedLightCount() const;
        const Light2DRenderItem& GetLight(std::size_t index) const;
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
        static UInt64 MakeSortKey(const SpriteRenderItem& item);

        RenderCamera2D          m_camera;
        Bool                    m_hasCamera = false;
        UInt32           m_unusableCameras = 0;
        ScreenSpaceFrame        m_screen;
        std::size_t             m_screenSprites = 0;
        Array<SpriteRenderItem> m_sprites;
        Array<SpriteSortKey>    m_order;
        std::size_t             m_droppedSpriteCount = 0;
        Array<Light2DRenderItem> m_lights;
        std::size_t             m_droppedLightCount = 0;
    };
}

#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/RHI/RHI.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Matrix4x4.h>

#include <cstddef>
#include <JBro/Types/Angle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    struct RendererConfig
    {
        GraphicsApi api = GraphicsApi::D3D12;
        SurfaceHandle surface;
        Extent2D surfaceExtent{1280, 720};
        TextureFormat backBufferFormat = TextureFormat::BGRA8Unorm;
        PresentMode presentMode = PresentMode::VSync;
        std::uint8_t backBufferCount = 3;
        std::uint8_t maxFramesInFlight = 2;
        // 3D 는 그릴 것이 있는 레이어마다 뷰 하나다(D-280). 에디터는 게임 뷰와 캔버스 뷰가 함께 쓴다.
        UInt32 maxViews = 64;
        UInt32 maxSpriteSubmissions = 65536;
        UInt32 maxMeshSubmissions = 16384;
        // 월드 텍스트(3D 뷰의 글자 사각형) 제출 상한이다. 0 이면 월드 텍스트를 받지 않는다(D-222).
        UInt32 maxWorldTextSubmissions = 16384;
        // 한 프레임의 레이어 묶음(`BeginLayer`) 상한이다(D-279). 뷰마다 합성하는 레이어 수의 합이다.
        // 빛을 받는 구간(`SetSpriteLighting`)도 같은 상한이다 - 둘 다 레이어마다 하나다.
        UInt32 maxLayerGroups = 256;
        // 한 프레임의 2D 라이트(`SubmitLight2D` 의 `Point`·`Spot`) 상한이다(D-291). 0 이면 라이팅을 만들지 않는다. `Global` 은 세지 않는다.
        UInt32 maxLights2D = 1024;
        // 한 프레임의 그림자 변(`SubmitShadowEdges2D`) 상한이다(D-291). 0 이면 그림자를 그리지 않는다.
        UInt32 maxShadowEdges2D = 16384;
        Bool validation = false;
    };

    // 이번 프레임의 뷰를 어디에 그릴지다. 비워 두면 스왑체인 백버퍼다.
    //
    // **렌더러의 모드가 아니라 인자다.** 에디터는 게임 화면을 자기 패널 안에 붙여야 하므로
    // 같은 렌더러를 텍스처로 한 번 부르고, 게임 실행은 백버퍼로 부른다. 그 차이가 전부이고
    // 두 개의 렌더 경로가 되어서는 안 된다.
    struct FrameTarget
    {
        // 비어 있으면 백버퍼다.
        TextureHandle texture;
        // 텍스처를 줄 때는 그 크기도 줘야 한다. 뷰포트가 타깃 안에 있는지 재는 기준이
        // 그것이고, 백버퍼일 때와 달리 렌더러가 알 길이 없다.
        Extent2D extent;
        // 거짓이면 이 프레임의 뷰를 기록하지 않는다. 게임 뷰 렌더는 **매 프레임 opt-in**
        // 이다(D-63) - 에디터의 게임 뷰 패널이 그려지지 않는 프레임에는 텍스처를 건드리지
        // 않고, 다시 보일 때 마지막 그림에서 이어진다. 제출된 뷰는 `skippedViewCount` 로 센다.
        Bool recordViews = true;
    };

    // 뷰를 모두 기록한 뒤, 프레임을 닫기 전에 불린다. 에디터 UI 가 백버퍼에
    // 그리는 자리다.
    //
    // **렌더러가 프레임과 커맨드 컨텍스트를 쥔 채로 불러들인다.** 그것들을 밖으로
    // 꺼내 주면 프레임을 누가 여닫는지가 둘로 갈린다. false 를 돌려주면 프레임을
    // 버린다 - UI 가 반쯤 그려진 화면을 내보내지 않는다.
    // `frameSlot` 은 이 프레임이 쓰는 슬롯이다. 매 프레임 덮어쓰는 자원을
    // 가진 쪽은 이것으로 갈라 써야 한다 - 그 슬롯의 지난 프레임이 끝났다는 것은
    // 이미 보장되어 있고, 하나로 두면 아직 읽는 중인 것을 덮어쓴다.
    using FrameOverlay = Bool (*)(
        IRHICommandContext& commands,
        TextureHandle backBuffer,
        UInt32 frameSlot,
        void* user);

    // 레이어를 아래에 얹는 방식이다(D-279). `Renderer::BeginLayer` 와 `CameraParams::composite` 가 받는다. 캔버스의 `LayerBlend` 와 같은 넷이고,
    // 렌더러는 캔버스를 모르므로 제 이름을 둔다(`SpriteFilter` 와 에셋의 `TextureFilter` 처럼).
    // 앞의 넷(`Screen` 까지)은 하드웨어 블렌드 계수로 얹고, 뒤의 아홉은 대상의 아래 그림을 복사해 셰이더가 섞는다(D-283) - 레이어마다
    // 대상 크기의 복사가 한 번 든다.
    enum class CompositeBlend : std::uint8_t
    {
        Normal,
        Additive,
        Multiply,
        Screen,
        Subtract,
        Lighten,
        Darken,
        Overlay,
        SoftLight,
        HardLight,
        ColorDodge,
        ColorBurn,
        Difference,
    };
    inline constexpr UInt32 CompositeBlendCount = 13;
    // 이 차례부터는 아래 그림을 읽는다.
    inline constexpr UInt32 FirstBackdropBlend = 4;

    struct CameraParams
    {
        Matrix4x4 view;
        Matrix4x4 projection;
        Float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        Viewport viewport;
        AssetHandle postProcessProfile;
        // **이 뷰만 다른 곳에 그린다**(D-130). 비어 있으면 프레임의 타깃이다.
        //
        // 에디터가 같은 장면을 두 번 그리는 자리다 - 게임 뷰는 게임의 카메라가 보는 것이고,
        // 캔버스 뷰는 편집 카메라가 보는 것이라 한 프레임에 둘 다 있어야 한다. 프레임 타깃
        // 하나로는 둘 중 하나만 살아남는다.
        TextureHandle target;
        // 그 텍스처의 크기다. 뷰포트가 타깃 안에 있는지 재는 기준이고, 프레임 타깃과 달리
        // 렌더러가 알 길이 없다. `target` 을 줬는데 이것이 0 이면 그 뷰는 거절된다.
        Extent2D targetExtent;

        // **이 뷰를 다 그린 뒤 마스크의 둘레를 덧그린다**(D-276, 에디터의 선택 외곽선, 기존 `COutlineRenderer2D`).
        // `outlineMask` 는 이 뷰보다 **먼저 낸** 뷰가 그린 텍스처이고(선택된 스프라이트만 그린 것), `outlineScratch` 는
        // 가로로 키운 마스크를 담을 자리다. 둘 다 이 뷰의 타깃과 같은 크기·백버퍼 포맷이고 `RenderTarget | Sampled` 다.
        // 마스크를 `outlineWidth` 픽셀만큼 가로·세로로 키우고, 키운 곳이면서 마스크 밖인 픽셀만 `outlineColor` 로 칠한다 -
        // 회전·텍스처 알파·틴트를 가리지 않고 그림의 실제 픽셀 바로 바깥이다. 셋 중 하나라도 비면 덧그리지 않는다.
        TextureHandle outlineMask;
        TextureHandle outlineScratch;
        Float outlineColor[4] = {1.0f, 1.0f, 0.0f, 1.0f};
        UInt32 outlineWidth = 0;

        // **이 뷰 전체를 제 텍스처에 그려 얹는다**(D-280, 3D 레이어). `Normal` 이고 1 이면 타깃에 바로 그린다. 텍스처는 투명하게 지우고,
        // 다 그린 뒤 `BeginLayer` 의 묶음과 같은 `Layer*` 블렌드와 불투명도로 타깃에 얹는다. 3D 레이어는 레이어마다 뷰 하나다 - 메시·월드
        // 텍스트는 깊이 패스 안에 있어 뷰 중간에 묶음을 끊을 수 없고, 깊이는 뷰마다 지우므로 뒤에 낸 레이어가 늘 위다(포토샵의 레이어).
        // 이 뷰 안의 스프라이트 묶음은 보지 않는다.
        CompositeBlend composite = CompositeBlend::Normal;
        Float compositeOpacity = 1.0f;
    };

    // 2D 스프라이트의 월드 변환이다. 열 벡터 규약의 2x3 아핀 여섯 값과 깊이 하나를 담는다(D-54).
    //   x' = linear[0]*x + linear[1]*y + translation[0]
    //   y' = linear[2]*x + linear[3]*y + translation[1]
    // 4x4 로 넘길 때 사라지던 것은 항상 같던 z 행과 w 행뿐이다.
    struct SpriteTransform2D
    {
        Float linear[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        Float translation[2] = {0.0f, 0.0f};
        Float depth = 0.0f;
    };

    enum class SpriteFilter : std::uint8_t
    {
        // 텍셀 하나를 그대로 집는다. 픽셀 아트의 기본이다.
        Nearest,
        Linear
    };

    // 스프라이트를 무엇으로 칠하나(text-plan §4.5). `SdfText` 는 텍스처의 알파를 거리장으로 읽고 채우기와 외곽선을 한 번에
    // 합성한다 - 제 파이프라인과 제 인스턴스 버퍼가 있어 보통 스프라이트의 인스턴스(40 B)는 그대로다.
    enum class SpriteShading : std::uint8_t
    {
        Sprite,
        SdfText
    };

    // 정렬과 레이어 합성은 프레임워크가 제출 전에 끝낸다.
    // 렌더러는 받은 순서대로 그린다(D-53). 텍스처가 같은 연속 구간이 드로우 하나다(D-113).
    struct SpriteSubmit
    {
        SpriteTransform2D world;
        // 렌더러가 `RegisterTexture` 로 발급한 텍스처다. 비어 있으면 1x1 흰색이라 틴트만 보인다.
        AssetHandle texture;
        AssetHandle material;
        Float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        // 텍스처의 어느 부분인가: uMin, vMin, uScale, vScale. 기본은 전체다. 시트의 한 칸이 이것으로 온다(D-113).
        Float uvRect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        SpriteFilter filter = SpriteFilter::Nearest;
        SpriteShading shading = SpriteShading::Sprite;
        // `SdfText` 에서만 읽는다. 외곽선이 끝나는 거리값(0..1 을 65535 로, 32768 은 0.5 라 외곽선이 없다)과 외곽선 색(0..255)이다.
        // 거리값은 부르는 쪽이 한 칸 이상 0 위로 둔다. **인스턴스와 같은 정규화 정수로 담는다** - float 다섯으로 두면 패킷이 80 → 100 바이트가
        // 되어 스프라이트 6 만 개의 제출이 0.1~0.2 ms 늘었다(D3D12·D3D11 A/B). 이렇게 두면 앞의 두 바이트는 기존 패딩 자리라 84 바이트다.
        std::uint16_t outlineEdge = 32768;
        std::uint8_t outlineColor[4] = {0, 0, 0, 0};
    };

    // 제출 패킷의 크기는 스프라이트 제출 비용이다(위 주석). 늘리기 전에 벤치마크(`JBRO_BENCH`)로 잰다.
    static_assert(sizeof(SpriteSubmit) == 84, "the sprite packet size is measured - see the outline fields");

    // 2D 라이트의 종류다(D-291). 캔버스의 `Light2DType` 과 같은 셋이고, 렌더러는 캔버스를 모르므로 제 이름을 둔다(`CompositeBlend` 처럼).
    enum class Light2DKind : std::uint8_t
    {
        // 뷰 전체에 같은 빛이다(환경광). 라이트맵을 지우는 색에 더해진다.
        Global,
        Point,
        Spot,
    };

    // **2D 라이트 하나**(D-291, tasks/lighting2d-plan.md §2.2). 뷰 안에서 낸다. 값은 월드 좌표다 - 화면 자리는 렌더러가 뷰의 카메라로 셈한다.
    // 라이트가 하나라도 있는 뷰는 빛을 받는 스프라이트(`SetSpriteLighting`)를 라이트맵에 곱해 그린다. 라이트가 없는 뷰는 그대로 그린다.
    struct Light2DSubmit
    {
        Light2DKind kind = Light2DKind::Point;
        Float position[2] = {0.0f, 0.0f};
        // `Spot` 의 축이다(월드, 길이 1). 다른 종류는 보지 않는다.
        Float direction[2] = {1.0f, 0.0f};
        // 색에 세기를 곱한 값이다. 1 을 넘어도 된다 - 라이트맵은 16 비트 실수다.
        Float color[3] = {1.0f, 1.0f, 1.0f};
        // 이 안은 빛이 다 닿고, `outerRadius` 에서 0 이 된다. `Global` 은 보지 않는다.
        Float innerRadius = 0.0f;
        Float outerRadius = 1.0f;
        // `Spot` 의 원뿔 **전체** 각이다. 안쪽 각 안은 빛이 다 닿고 바깥 각에서 0 이 된다.
        Radian innerAngle = Radian(0.0f);
        Radian outerAngle = Radian(1.5707964f);
        // 참이면 이 뷰의 그림자 변(`SubmitShadowEdges2D`)에 가려진 곳에는 빛이 닿지 않는다. `Global` 은 보지 않는다.
        // 그림자를 드리우는 라이트는 하나씩 그려진다(그림자 마스크 하나와 라이트맵 패스 하나) - 많이 켜면 비싸다.
        Bool castShadows = false;
        // 그림자를 드리울 때 빛을 이 반지름(월드)의 원판으로 본다 - 가림막에서 멀어질수록 그림자 가장자리가 넓게 번진다(반그림자, 4 단계).
        // 0 이면 점 빛이라 가장자리가 단단하다. `castShadows` 가 거짓이면 보지 않는다.
        Float shadowSoftness = 0.0f;
    };

    // **그림자를 드리우는 변 하나**(D-291, tasks/lighting2d-plan.md 3 단계). 월드 좌표다. 닫힌 모양은 **시계 반대 방향**으로 감아 낸다 - 변의
    // 바깥 법선이 오른쪽(`(dy, -dx)`)이다. 라이트를 향한 변은 밀어내지 않아 가림막의 안쪽은 밝다. `selfShadow` 면 그 변도 밀어내 가림막 자체가
    // 제 그늘에 든다 - 두께 없는 선(체인)은 늘 이렇게 낸다.
    struct ShadowEdge2D
    {
        Float from[2] = {0.0f, 0.0f};
        Float to[2] = {0.0f, 0.0f};
        Bool selfShadow = false;
    };

    struct MeshSubmit
    {
        Matrix4x4 world;
        AssetHandle mesh;
        AssetHandle material;
        Float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    };

    // **월드 텍스트**(D-222). 3D 뷰에 놓는 글자 사각형 하나다. 스프라이트와 같은 단위 쿼드(-0.5..0.5)를 `world`(행 우선 4x4, 열 벡터)로
    // 월드에 놓으므로 어느 방향이든 향한다. 렌더러는 그 뷰의 **메시 뒤에** 깊이를 보되 쓰지 않고 알파로 그린다 - 메시에 가려지고,
    // 글자끼리는 가리지 않는다. 겹치는 반투명 글자의 뒤→앞 정렬은 프레임워크가 제출 전에 한다(D-53). 텍스처는 `RegisterTexture` 의 것이다.
    struct WorldTextSubmit
    {
        Matrix4x4 world;
        AssetHandle texture;
        Float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        Float uvRect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        SpriteFilter filter = SpriteFilter::Linear;
        // 참이면 텍스처 알파를 거리장으로 읽고 채우기와 외곽선을 한 번에 합성한다(`SpriteShading::SdfText` 와 같은 셈).
        Bool sdf = false;
        std::uint16_t outlineEdge = 32768;
        std::uint8_t outlineColor[4] = {0, 0, 0, 0};
    };

    // 렌더러가 GPU 에 올리는 메시 정점이다. 위치와 법선만 있다 - 재질이 생기면 UV 가 붙는다
    // (framework3d-plan §2.4). 셰이더 ABI 라 크기와 자리를 아래에서 단언한다.
    struct MeshVertex
    {
        Float position[3] = {0.0f, 0.0f, 0.0f};
        Float normal[3] = {0.0f, 0.0f, 1.0f};
    };

    struct RendererFrameStats
    {
        UInt32 viewCount = 0;
        UInt32 spriteCount = 0;
        UInt32 meshCount = 0;
        UInt32 droppedViewCount = 0;
        // 타깃이 뷰 기록을 끈 프레임에 제출된 뷰다. 버린 것이 아니라 그리지 않기로 한 것이다.
        UInt32 skippedViewCount = 0;
        UInt32 droppedSpriteCount = 0;
        UInt32 droppedMeshCount = 0;
        UInt32 worldTextCount = 0;
        UInt32 droppedWorldTextCount = 0;
        // 유효하지 않은(빈 것이 아니라 죽은) 텍스처 핸들을 든 스프라이트다. 흰색으로 그리고 센다.
        UInt32 staleTextureSpriteCount = 0;
        // 제 텍스처에 그려 얹은 레이어 묶음이다(D-279).
        UInt32 compositedLayerCount = 0;
        // 얹을 텍스처가 아직 없어 그대로 그린 묶음이다. 그 크기의 텍스처는 다음 프레임을 열 때 생긴다 - 디바이스는 프레임 안에서
        // 자원을 만들지 않는다. 깊이가 달린 뷰(3D)의 묶음도 여기 든다.
        UInt32 uncompositedLayerCount = 0;
        // 묶음 상한(`RendererConfig::maxLayerGroups`)을 넘어 묶지 못한 것이다. 그 스프라이트는 그대로 그려진다.
        UInt32 droppedLayerCount = 0;
        // 깊이 텍스처가 아직 없어 메시·월드 텍스트를 빼고 그린 뷰다(D-288). 처음 보는 크기의 뷰(편집 화면·레이어 썸네일)이고, 그 크기의 깊이는
        // 다음 프레임을 열 때 생긴다.
        UInt32 viewsWithoutDepthCount = 0;
        // 받은 2D 라이트다(`Global` 포함, D-291).
        UInt32 light2DCount = 0;
        // 상한(`RendererConfig::maxLights2D`)을 넘어 버린 라이트다.
        UInt32 droppedLight2DCount = 0;
        // 라이트맵을 그린 뷰다.
        UInt32 litViewCount = 0;
        // 받은 그림자 변과 상한을 넘어 버린 변이다.
        UInt32 shadowEdge2DCount = 0;
        UInt32 droppedShadowEdge2DCount = 0;
        // 그림자를 그려 깎은 라이트다. 그림자 마스크가 아직 없어 그림자 없이 그린 라이트는 `unshadowedLight2DCount` 다.
        UInt32 shadowedLight2DCount = 0;
        UInt32 unshadowedLight2DCount = 0;
        // 라이트맵이 아직 없어 빛을 받는 스프라이트를 빛 없이 그린 뷰다. 그 크기의 라이트맵은 다음 프레임을 열 때 생긴다.
        // 깊이가 달린 뷰(3D)도 여기 든다 - 그 뷰는 라이팅을 보지 않는다.
        UInt32 viewsWithoutLightMapCount = 0;
    };

    class Renderer final
    {
    public:
        Renderer() = default;
        ~Renderer();
        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;
        Renderer(Renderer&&) = delete;
        Renderer& operator=(Renderer&&) = delete;

        Bool Initialize(IRHIModule& rhi, const RendererConfig& config);
        void Shutdown();

        // 타깃을 비우면 백버퍼에 그린다.
        FrameStatus BeginFrame(const FrameTarget& target = {});
        Bool BeginView(const CameraParams& camera);
        Bool SubmitSprite(const SpriteSubmit& item);
        Bool SubmitSprites(JArrayView<SpriteSubmit> items);
        Bool SubmitMesh(const MeshSubmit& item);
        Bool SubmitMeshes(JArrayView<MeshSubmit> items);
        Bool SubmitWorldText(const WorldTextSubmit& item);
        Bool SubmitWorldTexts(JArrayView<WorldTextSubmit> items);
        // **레이어 하나를 제 텍스처에 그려 얹는다**(D-279, 기존 `Render2DPipeline` 의 레이어 경로). 이 뒤로 `EndLayer` 까지 낸
        // 스프라이트는 투명하게 지운 텍스처에 보통 알파로 그려지고, 그 텍스처가 한 장으로 `blend` 와 `opacity` 로 뷰의 타깃에 얹힌다 -
        // 레이어 안의 스프라이트끼리는 서로 비치지 않고 레이어 전체가 한 번에 옅어진다. 뷰 안에서만 열고, 겹쳐 열지 않는다.
        // `Normal` 이고 불투명도 1 인 레이어는 열 필요가 없다(열어도 그림은 같고 텍스처만 든다). `EndView` 는 열린 것을 닫는다.
        // 깊이가 달린 뷰(메시·월드 텍스트가 있는 3D 뷰)에서는 묶음을 보지 않고 그대로 그린다.
        Bool BeginLayer(CompositeBlend blend, Float opacity);
        Bool EndLayer();
        // **2D 라이트를 낸다**(D-291). 뷰 안에서만 받는다. `Global` 은 색을 뷰의 환경광에 더하고, `Point`·`Spot` 은 라이트맵에 그린다.
        Bool SubmitLight2D(const Light2DSubmit& light);
        Bool SubmitLights2D(JArrayView<Light2DSubmit> lights);
        // **그림자 변을 낸다**(D-291). 뷰 안에서만 받는다. 그 뷰의 그림자를 드리우는 라이트 모두가 같은 변을 본다. 넘치면 그 묶음을 버리고 센다.
        Bool SubmitShadowEdges2D(JArrayView<ShadowEdge2D> edges);
        // **이 뒤로 낸 스프라이트가 빛을 받는가**(D-291). 참이면 `SetSpriteLighting(false)` 나 `EndView` 까지 낸 스프라이트는 이 뷰의 라이트맵을
        // 곱해 그린다 - 빛을 받는 레이어의 구간이다. 뷰마다 거짓으로 시작한다. 레이어 묶음(`BeginLayer`)과 겹쳐도 된다.
        // 깊이가 달린 뷰(3D)에서는 보지 않는다.
        Bool SetSpriteLighting(Bool lit);

        // 메시 지오메트리를 GPU 에 올리고 `MeshSubmit::mesh` 에 넣을 핸들을 준다. 프레임 밖에서만
        // 부른다. 빈 배열·너무 큰 배열·프레임 안이면 빈 핸들이다.
        // **핸들 모양이 `AssetHandle` 인 것은 `MeshSubmit` 이 그 타입이기 때문이다.** 발급자가 렌더러라는
        // 것은 `MeshLibrary`(Framework3DSystem)만 안다 - `AssetSystem` 이 실제로 로드하게 되면 그쪽이
        // 발급한다(`[가정]`, framework3d-plan §2.3).
        AssetHandle RegisterMesh(JArrayView<MeshVertex> vertices, JArrayView<UInt32> indices);
        void UnregisterMesh(AssetHandle mesh);
        UInt32 GetMeshCount() const;
        // RGBA8 픽셀(왼쪽 위 원점, 행마다 `width * 4` 바이트)을 GPU 에 올리고 `SpriteSubmit::texture` 에 넣을 핸들을
        // 준다. 프레임 밖에서만 부른다. 크기가 0 이거나 바이트 수가 맞지 않거나 프레임 안이면 빈 핸들이다(D-113).
        // 발급자가 렌더러라는 것은 `SpriteLibrary`(Framework2DSystem)만 안다 - `MeshLibrary` 와 같다.
        AssetHandle RegisterTexture(const Extent2D& extent, JArrayView<std::byte> rgba8);
        // 같은 크기의 새 픽셀로 갈아 끼운다(에셋의 in-place 재로드). 크기가 다르면 거짓이다 - 새로 등록한다.
        Bool UpdateTexture(AssetHandle texture, JArrayView<std::byte> rgba8);
        // 사각형 하나만 갈아 끼운다(글리프 아틀라스의 새 칸). rgba8 은 사각형 왼쪽 위 텍셀부터이고 행 간격은 rowPitch 바이트다.
        // 백엔드가 못 하면 거짓이다 - 부르는 쪽은 `UpdateTexture` 로 전체를 올린다.
        Bool UpdateTextureRegion(AssetHandle texture, UInt32 x, UInt32 y, UInt32 width, UInt32 height,
            JArrayView<std::byte> rgba8, UInt32 rowPitch);
        void UnregisterTexture(AssetHandle texture);
        UInt32 GetTextureCount() const;
        Bool EndView();
        FrameStatus EndFrame();
        void AbortFrame();

        Bool ResizeSurface(const Extent2D& extent);
        RendererFrameStats GetLastFrameStats() const;
        // 마지막으로 기록된 프레임의 첫 뷰 카메라다. 에디터의 기즈모가 화면과 월드를 잇는 데 쓴다(D-109) -
        // UI 는 엔진 프레임보다 먼저 만들어지므로 한 프레임 전의 카메라다. 뷰를 기록한 프레임이 아직 없으면 거짓이다.
        Bool GetLastViewCamera(CameraParams& camera) const;
        // 마지막으로 기록된 **편집 뷰**(자기 타깃을 든 뷰)의 카메라다(D-140).
        //
        // 3D 의 기즈모가 화면과 월드를 이으려면 이번 프레임의 뷰-투영이 필요한데, 2D 처럼
        // 나눗셈 하나로 되지 않는다. **에디터가 같은 행렬을 한 번 더 세우면 둘로 갈려**,
        // 한쪽만 고쳐졌을 때 손잡이가 그림과 다른 자리에 선다 - 그리는 쪽이 쓴 것을 그대로 내준다.
        // 편집 뷰를 기록한 프레임이 아직 없으면 거짓이다.
        Bool GetLastEditorViewCamera(CameraParams& camera) const;

        // 프레임을 닫기 전에 부를 것을 건다. **프레임 밖에서만 바꾼다** -
        // 프레임 중간에 바뀌면 이미 기록한 것과 어긋난다. nullptr 이면 뗀다.
        Bool SetFrameOverlay(FrameOverlay overlay, void* user);
        Bool HasFrameOverlay() const;

        // 렌더러가 만든 디바이스다. **리소스를 만들고 지우는 데만 쓴다** -
        // 에디터가 게임 뷰 텍스처와 자기 UI 파이프라인을 만들려면 이것이 필요하다.
        // 프레임을 여닫는 것은 여전히 렌더러의 일이다.
        IRHIDevice* GetDevice() const;

        Bool IsDeviceLost() const;
        Bool IsInitialized() const;
        // 창(스왑체인)의 크기다. 프레임이 텍스처로 가는 동안에도 이것은 창이다.
        Extent2D GetSurfaceExtent() const;
        // **이번 프레임이 실제로 그려지는 크기다.** 타깃을 준 프레임은 그
        // 텍스처의 크기이고, 아니면 창 크기다.
        //
        // 카메라는 창이 아니라 이것으로 화면을 잡아야 한다. 창으로 잡으면
        // 에디터에서 게임 화면이 에디터 창 모양을 따라가고, 게임 뷰가 창보다
        // 작으면 뷰포트가 타깃 밖으로 나가 프레임이 통째로 거절된다.
        Extent2D GetFrameExtent() const;
        // 백버퍼 포맷이다. 백버퍼에 얹혀 그리는 파이프라인은 이것과 같아야
        // 만들어진다 - 렌더 타깃 포맷은 파이프라인을 만들 때 굳는다.
        TextureFormat GetBackBufferFormat() const;
        UInt32 GetSpriteSubmissionLimit() const;
        // 한 프레임에 받는 2D 라이트(`Point`·`Spot`)의 상한이다(D-291). 프레임워크가 제 저장소를 이만큼 잡는다.
        UInt32 GetLight2DLimit() const;
        // 한 프레임에 받는 그림자 변의 상한이다(D-291).
        UInt32 GetShadowEdge2DLimit() const;
        // 마지막으로 제시한 백버퍼를 CPU 로 읽는다. **진단과 테스트 경로다** —
        // GPU 를 기다리므로 프레임 안에서 부를 수 없고 매 프레임 경로도 아니다.
        Bool ReadBackBuffer(std::byte* destination, std::size_t destinationSize, TextureReadback& result);

    private:
        struct ViewPacket
        {
            CameraParams camera;
            UInt32 spriteOffset = 0;
            UInt32 spriteCount = 0;
            UInt32 meshOffset = 0;
            UInt32 meshCount = 0;
            // 이 뷰의 메시 드로우 묶음(`m_meshRuns`)이 시작하는 자리와 개수. 업로드가 채운다.
            UInt32 runOffset = 0;
            UInt32 runCount = 0;
            // 이 뷰의 스프라이트 드로우 묶음(`m_spriteRuns`). 텍스처·샘플러가 같은 연속 구간 하나가 묶음 하나다.
            UInt32 spriteRunOffset = 0;
            UInt32 spriteRunCount = 0;
            // 이 뷰의 월드 텍스트(`m_worldTexts`)와 그 드로우 묶음(`m_worldTextRuns`). 묶음은 텍스처·샘플러가 같은 연속 구간이다.
            UInt32 worldTextOffset = 0;
            UInt32 worldTextCount = 0;
            UInt32 worldTextRunOffset = 0;
            UInt32 worldTextRunCount = 0;
            // 이 뷰의 레이어 묶음(`m_layerGroups`)이다. 스프라이트 번호 순이다.
            UInt32 layerGroupOffset = 0;
            UInt32 layerGroupCount = 0;
            // 이 뷰의 2D 라이트(`m_lights`, `Point`·`Spot`)와 빛을 받는 구간(`m_litRanges`)이다(D-291).
            UInt32 lightOffset = 0;
            UInt32 lightCount = 0;
            // 업로드가 라이트를 뷰마다 그림자 없는 것 먼저, 그림자를 드리우는 것 뒤로 놓는다. 앞쪽 수다.
            UInt32 plainLightCount = 0;
            // 이 뷰의 그림자 변(`m_shadowEdges`)이다.
            UInt32 shadowEdgeOffset = 0;
            UInt32 shadowEdgeCount = 0;
            UInt32 litRangeOffset = 0;
            UInt32 litRangeCount = 0;
            // `Global` 라이트의 합이다. 라이트맵을 지우는 색이다.
            Float ambient[3] = {0.0f, 0.0f, 0.0f};
            // 라이트를 하나라도 받았는가(`Global` 포함). 거짓이면 빛을 받는 구간도 그대로 그린다.
            Bool lighting = false;
            // 빛을 받는 스프라이트 구간이 있는가. 업로드가 채운다.
            Bool hasLitRun = false;
        };

        // `SetSpriteLighting(true)` 와 `false` 사이에 낸 스프라이트 번호 구간 [first, end) 다(D-291).
        struct LitRange
        {
            UInt32 firstSprite = 0;
            UInt32 endSprite = 0;
        };

        // 라이트 하나의 인스턴스다. `BuiltinLight2D.hlsl` 의 ATTRIBUTE1..3 이 읽는다.
        struct GpuLight2DInstance
        {
            // 중심 xy, 바깥 반지름, 안쪽 반지름.
            Float shape[4] = {0.0f, 0.0f, 1.0f, 0.0f};
            // 색 × 세기. w 는 비운다.
            Float color[4] = {1.0f, 1.0f, 1.0f, 0.0f};
            // 스포트 축 xy, 안쪽 반각, 바깥 반각(라디안). 점 라이트는 4·5 라 모든 방향이 안쪽이다.
            Float cone[4] = {1.0f, 0.0f, 4.0f, 5.0f};
        };
        static_assert(sizeof(GpuLight2DInstance) == 48, "light instance stride is part of the shader ABI");

        // 그림자 변 하나의 인스턴스다. `BuiltinShadow2D.hlsl` 의 ATTRIBUTE1..2 가 읽는다.
        struct GpuShadowEdgeInstance
        {
            // from xy, to xy.
            Float edge[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            // x: 라이트를 향해도 밀어내는가(1/0). 나머지는 비운다.
            Float flags[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        };
        static_assert(sizeof(GpuShadowEdgeInstance) == 32, "shadow edge instance stride is part of the shader ABI");
        static_assert(offsetof(GpuShadowEdgeInstance, flags) == 16, "shadow attribute 2 reads the flags from offset 16");
        // 그림자를 드리우는 라이트 하나의 자리다. 업로드가 채우고 라이트맵 기록이 읽는다.
        struct ShadowedLight
        {
            UInt32 instance = 0;
            Float position[2] = {0.0f, 0.0f};
            Float reach = 0.0f;
            Float softness = 0.0f;
        };
        static_assert(offsetof(GpuLight2DInstance, color) == 16, "light attribute 2 reads the colour from offset 16");
        static_assert(offsetof(GpuLight2DInstance, cone) == 32, "light attribute 3 reads the cone from offset 32");

        static constexpr UInt32 NoLayerGroup = 0xFFFFFFFFu;

        // `BeginLayer`·`EndLayer` 사이에 낸 스프라이트 번호 구간 [first, end) 과 얹는 방식이다.
        struct LayerGroup
        {
            UInt32 firstSprite = 0;
            UInt32 endSprite = 0;
            CompositeBlend blend = CompositeBlend::Normal;
            Float opacity = 1.0f;
        };

        // 레이어를 그려 둘 텍스처다. 뷰의 타깃 크기마다 하나이고 백버퍼 포맷이다. 프레임 안에서는 만들 수 없으므로, 기록 중에
        // 없는 크기를 만나면 바라는 크기로 적어 두고 다음 `BeginFrame` 이 프레임을 열기 전에 만든다. 오래 안 쓰면 놓는다.
        // 쓰임은 둘이다: 레이어를 그리는 자리와, 아래 그림을 읽는 블렌드가 대상을 복사해 두는 자리(D-283). 같은 크기라도 따로 든다.
        // `LightMap` 은 2D 라이트맵이다(D-291) - 백버퍼 포맷이 아니라 RGBA16F 다. `ShadowMask` 는 그림자를 드리우는 라이트 하나의 가림막(RGBA16F)이다.
        enum class LayerTargetRole : std::uint8_t
        {
            Layer,
            Backdrop,
            LightMap,
            ShadowMask
        };
        struct LayerTarget
        {
            TextureHandle texture;
            Extent2D extent;
            LayerTargetRole role = LayerTargetRole::Layer;
            UInt32 idleFrames = 0;
        };
        struct LayerTargetWant
        {
            Extent2D extent;
            LayerTargetRole role = LayerTargetRole::Layer;
        };
        static constexpr std::size_t MaxLayerTargets = 8;
        static constexpr UInt32 LayerTargetIdleFrames = 300;

        // 같은 텍스처와 샘플러로 그리는 스프라이트의 연속 구간이다(D-113). 순서는 제출 순서 그대로다 - 정렬은
        // 프레임워크의 일이고, 여기서는 이웃이 같으면 묶는 것만 한다.
        struct SpriteRun
        {
            TextureHandle texture;
            SamplerHandle sampler;
            UInt32 firstInstance = 0;
            UInt32 instanceCount = 0;
            // SDF 텍스트 구간이면 참이다. 인스턴스는 텍스트 버퍼의 같은 번호에 있다.
            Bool sdf = false;
            // 이 구간이 든 레이어 묶음(`m_layerGroups` 의 자리)이다. 묶음 경계에서 구간이 끊긴다.
            UInt32 layerGroup = NoLayerGroup;
            // 빛을 받는 구간이면 참이다(D-291). 빛을 받는 구간의 경계에서 끊긴다.
            Bool lit = false;
        };

        // 같은 메시를 그리는 인스턴스들의 연속 구간이다(D-110). 업로드가 뷰 안에서 메시별로 모아 놓으므로
        // 드로우 하나가 구간 하나다 - 메시마다 드로우를 내던 것에서 메시 **종류**마다 드로우를 내는 것으로.
        struct MeshRun
        {
            AssetHandle mesh;
            UInt32 firstInstance = 0;
            UInt32 instanceCount = 0;
        };

        // 이 멤버 순서가 정점 속성 오프셋이고 BuiltinSprite.hlsl 의 ABI 다.
        // 크기나 순서를 바꾸면 셰이더도 함께 다시 만든다(Shaders/Compile.ps1).
        // **40 바이트다**(D-114). 틴트는 바이트 넷(`UByte4Norm`), UV 사각형은 16 비트 정규화 넷(`UShort4Norm`)이라
        // 셰이더는 둘 다 0..1 의 float4 로 받는다 - HLSL 은 바뀌지 않는다. 60000 개에서 인스턴스 업로드가 3.6MB 에서
        // 2.4MB 로 준다. 틴트는 0..1 로 잘리고 UV 도 0..1 로 잘린다(감싸기 없음).
        struct GpuSpriteInstance
        {
            SpriteTransform2D world;
            std::uint8_t tint[4] = {255, 255, 255, 255};
            // ATTRIBUTE4. 셰이더는 uv = uv * zw + xy 다(D-113).
            std::uint16_t uvRect[4] = {0, 0, 65535, 65535};
        };

        static_assert(sizeof(SpriteTransform2D) == 28,
            "sprite transform layout is part of the shader ABI");
        static_assert(sizeof(GpuSpriteInstance) == 40,
            "sprite instance stride is part of the shader ABI");
        static_assert(offsetof(GpuSpriteInstance, uvRect) == 32,
            "instance attribute 4 reads the uv rectangle from offset 32");
        static_assert(offsetof(GpuSpriteInstance, world) == 0,
            "instance attribute 1 and 2 read the transform from offset 0");
        static_assert(offsetof(SpriteTransform2D, translation) == 16,
            "instance attribute 2 reads the translation and depth from offset 16");
        static_assert(offsetof(GpuSpriteInstance, tint) == 28,
            "instance attribute 3 reads the tint from offset 28");

        // SDF 텍스트의 인스턴스다. `BuiltinSdfText.hlsl` 의 ATTRIBUTE1..6 이 읽는다. 앞 40 바이트는 스프라이트와 같은 자리다.
        // 제출 번호와 같은 칸에 쓴다(스프라이트 칸과 짝) - 구간이 스프라이트와 텍스트를 섞어도 각자의 버퍼에서 연속이다.
        struct GpuTextInstance
        {
            SpriteTransform2D world;
            std::uint8_t fill[4] = {255, 255, 255, 255};
            std::uint16_t uvRect[4] = {0, 0, 65535, 65535};
            std::uint8_t outline[4] = {0, 0, 0, 0};
            // x 는 외곽선이 끝나는 거리값이다. 나머지는 비워 둔다 - 정점 형식에 16 비트 둘짜리가 없다.
            std::uint16_t params[4] = {32768, 0, 0, 0};
        };
        static_assert(sizeof(GpuTextInstance) == 52, "text instance stride is part of the shader ABI");
        static_assert(offsetof(GpuTextInstance, fill) == 28, "text attribute 3 reads the fill colour from offset 28");
        static_assert(offsetof(GpuTextInstance, uvRect) == 32, "text attribute 4 reads the uv rectangle from offset 32");
        static_assert(offsetof(GpuTextInstance, outline) == 40, "text attribute 5 reads the outline colour from offset 40");
        static_assert(offsetof(GpuTextInstance, params) == 44, "text attribute 6 reads the params from offset 44");

        // 메시 인스턴스 하나. 월드 4x4(행 넷)와 tint. `BuiltinMesh.hlsl` 의 ATTRIBUTE2..6 이 이것을 읽는다.
        struct GpuMeshInstance
        {
            Matrix4x4 world;
            Float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        };
        static_assert(sizeof(MeshVertex) == 24, "mesh vertex stride is part of the shader ABI");
        static_assert(offsetof(MeshVertex, normal) == 12, "attribute 1 reads the normal from offset 12");
        static_assert(sizeof(GpuMeshInstance) == 80, "mesh instance stride is part of the shader ABI");
        static_assert(offsetof(GpuMeshInstance, tint) == 64, "attribute 6 reads the tint from offset 64");

        // 월드 텍스트 인스턴스다. `BuiltinWorldText.hlsl` 의 ATTRIBUTE1..8 이 읽는다. 월드는 메시처럼 행 넷이다.
        struct GpuWorldTextInstance
        {
            Matrix4x4 world;
            std::uint8_t fill[4] = {255, 255, 255, 255};
            std::uint16_t uvRect[4] = {0, 0, 65535, 65535};
            std::uint8_t outline[4] = {0, 0, 0, 0};
            // x 는 외곽선이 끝나는 거리값, y 는 거리장이면 65535(셰이더에서 1)다.
            std::uint16_t params[4] = {32768, 0, 0, 0};
        };
        static_assert(sizeof(GpuWorldTextInstance) == 88, "world text instance stride is part of the shader ABI");
        static_assert(offsetof(GpuWorldTextInstance, fill) == 64, "world text attribute 5 reads the fill colour from offset 64");
        static_assert(offsetof(GpuWorldTextInstance, uvRect) == 68, "world text attribute 6 reads the uv rectangle from offset 68");
        static_assert(offsetof(GpuWorldTextInstance, outline) == 76, "world text attribute 7 reads the outline colour from offset 76");
        static_assert(offsetof(GpuWorldTextInstance, params) == 80, "world text attribute 8 reads the params from offset 80");

        // 올라간 메시 하나. 핸들의 index 가 이 배열의 자리고 generation 이 재사용을 가른다.
        struct MeshResource
        {
            BufferHandle vertexBuffer;
            BufferHandle indexBuffer;
            UInt32 indexCount = 0;
            UInt32 generation = 1;
            Bool occupied = false;
        };

        // 올라간 텍스처 하나. 핸들의 index 가 이 배열의 자리고 generation 이 재사용을 가른다(`MeshResource` 와 같다).
        struct TextureResource
        {
            TextureHandle texture;
            Extent2D extent;
            UInt32 generation = 1;
            Bool occupied = false;
        };

        // 깊이 텍스처 하나. **크기마다 하나**다 - 한 프레임에 크기가 다른 뷰(게임 뷰·편집 화면·레이어 썸네일, D-288)가 섞여도 서로 밀어내지 않는다.
        // 오래 안 쓴 것은 놓는다.
        struct DepthTarget
        {
            TextureHandle texture;
            Extent2D extent;
            UInt32 idleFrames = 0;
        };
        static constexpr std::size_t MaxDepthTargets = 4;
        static constexpr UInt32 DepthTargetIdleFrames = 300;

        static constexpr UInt32 InvalidViewIndex = 0xFFFFFFFFu;
        static constexpr UInt32 MaxFrameSlots = 3;

        Bool CreateBuiltinSpriteResources();
        void DestroyBuiltinSpriteResources();
        Bool UploadSpriteInstances();
        Bool CreateBuiltinMeshResources();
        void DestroyBuiltinMeshResources();
        Bool UploadMeshInstances();
        Bool CreateBuiltinWorldTextResources();
        void DestroyBuiltinWorldTextResources();
        Bool UploadWorldTextInstances();
        void DestroyMeshResources();
        // `extent` 크기의 깊이 텍스처를 준다. 없으면 만든다 - 프레임 밖에서만 부른다.
        Bool AcquireDepthTarget(const Extent2D& extent, TextureHandle& depth);
        // 프레임 안에서 `extent` 크기의 깊이 텍스처를 찾는다. 없으면 바람으로 적어 두고(다음 `BeginFrame` 이 만든다) 빈 핸들을 준다.
        TextureHandle FindDepthTarget(const Extent2D& extent);
        // 프레임을 열기 전에 지난 프레임이 바란 깊이 텍스처를 만들고 오래 안 쓴 것을 놓는다.
        void PrepareDepthTargets();
        void DestroyDepthTargets();
        const MeshResource* FindMesh(AssetHandle mesh) const;
        const TextureResource* FindTexture(AssetHandle texture) const;
        void DestroyTextureResources();
        Bool RecordViews();
        void ResetSubmissionStorage();

        RendererConfig m_config;
        IRHIModule* m_rhi = nullptr;
        IRHIDevice* m_device = nullptr;
        SwapchainHandle m_swapchain;
        FrameContext m_frame;
        FrameTarget m_frameTarget;
        FrameOverlay m_frameOverlay = nullptr;
        void* m_frameOverlayUser = nullptr;
        Array<ViewPacket> m_views;
        Array<SpriteSubmit> m_sprites;
        Array<MeshSubmit> m_meshes;
        Array<WorldTextSubmit> m_worldTexts;
        Array<GpuWorldTextInstance> m_gpuWorldTextInstances;
        Array<SpriteRun> m_worldTextRuns;
        std::size_t m_gpuWorldTextCount = 0;
        BufferHandle m_worldTextInstanceBuffers[MaxFrameSlots];
        GraphicsPipelineHandle m_worldTextPipeline;
        // 인스턴스 배열은 초기화 때 상한 크기로 한 번 잡고 프레임마다 앞에서부터 채운다 - `Resize` 는 매 프레임
        // 값 초기화(memset)를 하고, 그 비용이 자료를 옮기는 것보다 컸다(D-110 리뷰).
        Array<GpuSpriteInstance> m_gpuSpriteInstances;
        // SDF 텍스트 인스턴스다. 스프라이트 배열과 같은 크기로 한 번 잡고, 이번 프레임에 쓴 번호 구간만 올린다.
        Array<GpuTextInstance> m_gpuTextInstances;
        std::size_t m_gpuTextFirst = 0;
        std::size_t m_gpuTextEnd = 0;
        std::size_t m_gpuSpriteCount = 0;
        std::size_t m_gpuMeshCount = 0;
        BufferHandle m_spriteVertexBuffer;
        BufferHandle m_spriteIndexBuffer;
        BufferHandle m_spriteInstanceBuffers[MaxFrameSlots];
        BufferHandle m_textInstanceBuffers[MaxFrameSlots];
        GraphicsPipelineHandle m_sdfTextPipeline;
        GraphicsPipelineHandle m_sdfTextOverDepthPipeline;
        // 선택 외곽선의 두 패스다(D-276). 가로로 키우기(덮어쓰기)와 세로로 키워 둘레만 칠하기(알파 섞기).
        GraphicsPipelineHandle m_outlineGrowPipeline;
        GraphicsPipelineHandle m_outlineCompositePipeline;
        Bool RecordOutline(const CameraParams& camera, TextureHandle target, const Extent2D& extent);
        // 레이어 텍스처를 타깃에 얹는다(D-279). 타깃의 패스를 `Load` 로 열고 열린 채로 돌려준다 - 뒤의 구간이 이어 그린다.
        // 아래 그림을 읽는 블렌드(D-283)는 먼저 타깃을 복사해 둔다 - 부를 때 패스가 닫혀 있어야 한다. 복사할 자리가 아직 없으면 표준으로 얹는다.
        Bool RecordLayerComposite(const LayerGroup& group, TextureHandle layer, TextureHandle target, const Extent2D& extent,
            const Viewport& viewport, const ScissorRect& scissor);
        // 이 크기·쓰임의 텍스처다. 없으면 바라는 것으로 적고 빈 핸들이다.
        TextureHandle FindLayerTarget(const Extent2D& extent, LayerTargetRole role = LayerTargetRole::Layer);
        // 프레임을 열기 전에 바라던 크기의 텍스처를 만들고, 오래 안 쓴 것을 놓는다.
        void PrepareLayerTargets();
        void DestroyLayerTargets();
        // `Normal`·`Additive`·`Multiply`·`Screen` 차례다.
        GraphicsPipelineHandle m_layerCompositePipelines[4];
        // 아래 그림을 읽는 블렌드 아홉이 함께 쓰는 하나다(덮어쓰기, D-283). 어느 식인지는 상수가 고른다.
        GraphicsPipelineHandle m_layerBackdropPipeline;
        // **2D 라이팅**(D-291). 라이트맵에 라이트를 더하는 것(RGBA16F, `One·One`)과, 라이트맵을 곱해 그리는 스프라이트·SDF 텍스트다.
        Bool CreateBuiltinLightResources();
        void DestroyBuiltinLightResources();
        Bool UploadLightInstances();
        Bool UploadShadowEdges();
        void WriteLightInstance(const Light2DSubmit& light, GpuLight2DInstance& instance);
        // 뷰의 라이트맵을 그린다 - 환경광으로 지우고 `Point`·`Spot` 을 더하고, 그림자를 드리우는 라이트는 하나씩 마스크로 깎아 더한다. 패스를 열고 닫는다.
        Bool RecordLightMap(const ViewPacket& view, TextureHandle lightMap, const Extent2D& extent, const Viewport& viewport,
            const ScissorRect& scissor);
        GraphicsPipelineHandle m_light2DPipeline;
        // 그림자(D-291 3 단계): 변을 밀어내 마스크에 칠하는 것과, 마스크로 깎으며 라이트 하나를 더하는 것이다.
        GraphicsPipelineHandle m_shadowMaskPipeline;
        GraphicsPipelineHandle m_shadowedLight2DPipeline;
        BufferHandle m_shadowEdgeBuffers[MaxFrameSlots];
        Array<ShadowEdge2D> m_shadowEdges;
        Array<GpuShadowEdgeInstance> m_gpuShadowEdges;
        Array<ShadowedLight> m_shadowedLights;
        GraphicsPipelineHandle m_litSpritePipeline;
        GraphicsPipelineHandle m_litSdfTextPipeline;
        BufferHandle m_lightInstanceBuffers[MaxFrameSlots];
        Array<Light2DSubmit> m_lights;
        Array<GpuLight2DInstance> m_gpuLightInstances;
        Array<LitRange> m_litRanges;
        // 열린 빛을 받는 구간(`m_litRanges` 의 자리)이다. 없으면 `NoLayerGroup` 이다.
        UInt32 m_openLitRange = NoLayerGroup;
        Array<LayerGroup> m_layerGroups;
        UInt32 m_openLayerGroup = NoLayerGroup;
        LayerTarget m_layerTargets[MaxLayerTargets];
        LayerTargetWant m_layerTargetWants[MaxLayerTargets];
        std::size_t m_layerTargetWantCount = 0;
        GraphicsPipelineHandle m_spritePipeline;
        // 깊이가 달린 패스(메시가 있는 뷰) 위에 스프라이트를 얹을 때 쓰는 쌍둥이다. 포맷만 같고 깊이는 보지도
        // 쓰지도 않는다 - 파이프라인의 깊이 포맷은 패스의 첨부와 같아야 하기 때문에 둘이 필요하다.
        GraphicsPipelineHandle m_spriteOverDepthPipeline;
        // 텍스처가 없는 스프라이트가 샘플링하는 1x1 흰색이다. 틴트가 그대로 나온다.
        TextureHandle m_whiteTexture;
        SamplerHandle m_nearestSampler;
        SamplerHandle m_linearSampler;
        Array<TextureResource> m_textureResources;
        Array<SpriteRun> m_spriteRuns;
        Array<MeshResource> m_meshResources;
        Array<GpuMeshInstance> m_gpuMeshInstances;
        Array<MeshRun> m_meshRuns;
        // 이번 프레임에 이미 지운 타깃들(D-130). 뷰마다 타깃이 다를 수 있으므로
        // "첫 뷰" 가 아니라 "그 타깃의 첫 뷰" 에서 지운다.
        //
        // **고정 길이다.** 이 경로는 매 프레임 돌고 힙 할당이 금지되어 있다
        // (`RendererContractTests` 가 잰다). 한 프레임의 타깃은 백버퍼·게임 화면·
        // 편집 화면 정도라 여덟이면 넉넉하고, 넘치면 그 뒤의 타깃은 처음 만난 것으로
        // 쳐서 지운다 - 지우지 않는 쪽으로 넘기면 지난 프레임이 비쳐 남는다.
        static constexpr std::size_t MaxClearedTargets = 8;
        TextureHandle m_clearedTargets[MaxClearedTargets];
        std::size_t m_clearedTargetCount = 0;
        // 뷰마다 메시 슬롯별 개수를 세는 작업 배열. 크기는 등록된 메시 슬롯 수다.
        Array<UInt32> m_meshHistogram;
        BufferHandle m_meshInstanceBuffers[MaxFrameSlots];
        GraphicsPipelineHandle m_meshPipeline;
        DepthTarget m_depthTargets[MaxDepthTargets];
        Extent2D m_depthTargetWants[MaxDepthTargets];
        std::size_t m_depthTargetWantCount = 0;
        RendererFrameStats m_currentStats;
        RendererFrameStats m_lastStats;
        CameraParams m_lastViewCamera;
        CameraParams m_lastEditorViewCamera;
        Bool m_hasLastEditorViewCamera = false;
        Bool m_hasLastViewCamera = false;
        // EndFrame 이 프레임 컨텍스트를 비우므로 읽기 경로를 위해 따로 기억한다.
        TextureHandle m_lastPresentedBackBuffer;
        UInt32 m_activeView = InvalidViewIndex;
        Bool m_frameActive = false;
    };
}

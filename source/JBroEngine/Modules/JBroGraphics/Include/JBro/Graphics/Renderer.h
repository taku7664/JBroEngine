#pragma once

#include <JBro/Asset/MaterialShader.h>
#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Graphics/PostProcess.h>
#include <JBro/Graphics/RenderGraph.h>
#include <JBro/RHI/ConstantRing.h>
#include <JBro/RHI/RHI.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Matrix4x4.h>

#include <cstddef>
#include <JBro/Types/Angle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
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
        // 3D 는 그릴 것이 있는 레이어마다 뷰 하나다(D-280). 에디터는 시뮬레이션 뷰와 캔버스 뷰가 함께 쓴다.
        UInt32 maxViews = 64;
        UInt32 maxSpriteSubmissions = 65536;
        // 한 프레임에 모든 뷰가 낸 후처리 효과의 합과, 그 파라미터 블록의 바이트다(D-342). 넘치는 효과는 버리고 센다.
        UInt32 maxPostProcessEntries = 64;
        UInt32 postProcessBlockBytes = 4096;
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
        // 거짓이면 이 프레임의 뷰를 기록하지 않는다. 시뮬레이션 뷰 렌더는 **매 프레임 opt-in**
        // 이다(D-63) - 에디터의 시뮬레이션 뷰 패널이 그려지지 않는 프레임에는 텍스처를 건드리지
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
        // **이 뷰의 후처리**(D-342, postprocess-plan §2.5). 비어 있으면 지금처럼 타깃에 바로 그린다. 효과가 있으면 장면을 중간 텍스처(`SceneColor`)에
        // 그리고 효과를 지점 차례로 건 뒤 타깃의 뷰포트 안에 쓴다 - HDR 효과가 있으면 장면은 `RGBA16Float` 이고 톤맵에서 내린다.
        // `BeginView` 가 블록을 복사하므로 가리키는 곳은 그 뒤 버려도 된다. 통째로 얹는 뷰(`composite`)는 장면의 후처리를 받지 않는다 -
        // 3D 는 레이어를 다 얹은 뒤의 빈 뷰 하나에 건다(D-344). 그런 뷰의 레이어 효과는 아래 `layerPostProcess` 다.
        PostProcessStack postProcess;
        // **이 뷰를 한 장의 레이어로 보고 거는 효과**(D-345, 3D 레이어의 프로필). 비어 있지 않으면 `composite` 가 `Normal` 이고 1 이어도 뷰를
        // 제 텍스처에 그려 얹고, 얹기 전에 효과(`PostProcessEffectType::onLayer`)를 낸 차례로 건다. 레이어에 걸리지 않는 종류는 버리고 센다.
        PostProcessStack layerPostProcess;
        // **이 뷰만 다른 곳에 그린다**(D-130). 비어 있으면 프레임의 타깃이다.
        //
        // 에디터가 같은 장면을 두 번 그리는 자리다 - 시뮬레이션 뷰는 게임의 카메라가 보는 것이고,
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
        // 렌더러가 `RegisterMaterial` 로 발급한 스프라이트 자리의 머티리얼이다(D-347). 비어 있으면 빌트인 스프라이트 셰이더다. 머티리얼 스프라이트는
        // 빛을 받지 않는다(라이트맵을 곱하지 않는다). SDF 텍스트는 보지 않는다.
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

    // 3D 라이트의 종류다(D-366). 캔버스의 `Light3DType` 과 같은 셋이고, 렌더러는 컴포넌트를 모르므로 제 이름을 둔다.
    enum class Light3DKind : std::uint8_t
    {
        Directional,
        Point,
        Ambient,
    };

    // 열린 뷰에 내는 3D 라이트 하나다(D-366). `Directional` 은 `direction`(빛이 가는 쪽)으로, `Point` 는 `position` 에서 `range` 까지 비춘다.
    // 라이트를 하나라도 낸 뷰의 메시는 이 라이트들로 비추고, 안 낸 뷰는 고정 방향광이다. 그림자는 없다.
    struct Light3DSubmit
    {
        Light3DKind kind = Light3DKind::Directional;
        Float position[3] = {0.0f, 0.0f, 0.0f};
        Float direction[3] = {0.0f, 0.0f, -1.0f};
        // 색 x 세기다. 1 을 넘어도 된다.
        Float color[3] = {1.0f, 1.0f, 1.0f};
        Float range = 10.0f;
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

    // 렌더러가 GPU 에 올리는 메시 정점이다. 위치·법선·UV 다 - UV 는 메시 머티리얼(D-348)이 읽는다(빌트인 셰이더는 보지 않는다).
    // 셰이더 ABI 라 크기와 자리를 아래에서 단언한다.
    struct MeshVertex
    {
        Float position[3] = {0.0f, 0.0f, 0.0f};
        Float normal[3] = {0.0f, 0.0f, 1.0f};
        Float uv[2] = {0.0f, 0.0f};
    };

    // **머티리얼 하나를 올릴 것**(D-347, `Renderer::RegisterMaterial`). 셰이더는 자리(`shader->domain`)로 구운 것이고, 렌더러의 API 에 맞는 바이트코드가
    // 있어야 한다. 파라미터는 `shader->parameterBytes` 바이트(셰이더의 `cbuffer Material` 그대로의 배치)다. 텍스처는 t 자리 - 1 의 칸에 렌더러가
    // 발급한 텍스처(`RegisterTexture`)다. 비면 1x1 흰색이다.
    struct MaterialDesc
    {
        const CompiledMaterialShader* shader = nullptr;
        JArrayView<std::byte> parameters;
        AssetHandle textures[MaxMaterialTextures];
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
        // 후처리를 건 뷰다(D-342).
        UInt32 postProcessedViewCount = 0;
        // 후처리할 자리(HDR 파이프라인·장면 텍스처·중간 텍스처)가 아직 없어 후처리 없이 그린 뷰다. 다음 프레임을 열 때 선다.
        UInt32 postProcessWaitingViewCount = 0;
        // 상한을 넘었거나 종류·블록이 맞지 않아 버린 효과다.
        UInt32 droppedPostProcessCount = 0;
        // 통째로 얹는 뷰라 장면의 후처리를 받지 않은 뷰다. 그런 뷰의 레이어 효과는 `layerPostProcess` 로 낸다(D-345).
        UInt32 unpostprocessedCompositeViewCount = 0;
        // 레이어 효과를 건 레이어(2D 의 레이어 묶음과 통째로 얹는 뷰)다(D-345).
        UInt32 layerPostProcessedCount = 0;
        // 레이어 효과를 걸 자리(파이프라인·중간 텍스처)가 아직 없어 효과 없이 얹은 레이어다. 다음 프레임을 열 때 선다.
        UInt32 layerPostProcessWaitingCount = 0;
        // 머티리얼로 그린 스프라이트다(D-347).
        UInt32 materialSpriteCount = 0;
        // 머티리얼로 그린 메시다(D-348).
        UInt32 materialMeshCount = 0;
        // 뷰에 낸 3D 라이트(환경광 포함)와 뷰의 상한을 넘어 버린 것이다(D-366).
        UInt32 light3DCount = 0;
        UInt32 droppedLight3DCount = 0;
        // 죽은(빈 것이 아니라 지워진) 머티리얼을 든 스프라이트·효과다. 빌트인 셰이더(효과는 그대로 옮기기)로 그리고 센다.
        UInt32 staleMaterialCount = 0;
    };

    // **렌더 그래프 창이 보는 뷰 하나다**(D-339, 진단). 뷰 번호가 그 뷰 패스의 묶음 번호(`RenderGraph::GetPassGroup`)다.
    struct RendererViewDebugInfo
    {
        // 그 뷰가 그린 곳이다. 프레임 타깃으로 간 뷰는 프레임의 텍스처(타깃을 안 준 프레임은 백버퍼)다.
        TextureHandle target;
        Extent2D extent;
        // 자기 타깃을 든 뷰다(캔버스 뷰·레이어 썸네일 같은 편집 화면).
        Bool ownTarget = false;
        // 이 프레임에 기록했는가. 프레임 타깃이 뷰를 원하지 않은 프레임(`FrameTarget::recordViews`)의 뷰는 거짓이고 패스가 없다.
        Bool recorded = false;
        Bool hasMeshes = false;
        Bool lit = false;
        UInt32 layerGroupCount = 0;
    };

    // **패스 하나의 GPU 시간이다**(D-341, 렌더 그래프 창). 이름은 그래프의 정적 이름 그대로라 포인터로 견줄 수 있다.
    struct RenderGraphPassTiming
    {
        const char* name = nullptr;
        // 뷰 번호(패스 묶음)와 그 프레임 그래프에서의 패스 번호다.
        UInt32 group = 0;
        UInt32 pass = 0;
        Float milliseconds = 0.0f;
    };

    // **패스 직후 미리보기 요청이다**(D-341). 그래프는 프레임마다 다시 지으므로 번호가 아니라 뷰·이름·그 뷰에서 그 이름의 몇 번째인지로 패스를
    // 가리킨다. 이름은 `RenderGraph::GetPassName` 이 준 포인터 그대로여야 한다(포인터로 견준다 - 프레임 경로에서 글자를 견주지 않는다).
    struct RenderGraphCaptureRequest
    {
        const char* passName = nullptr;
        UInt32 group = 0;
        UInt32 occurrence = 0;
        // 그 패스의 접근 번호(`RenderGraph::GetPassAccess`)다. 그 텍스처를 뜬다.
        UInt32 access = 0;
    };

    enum class RenderGraphCaptureStatus : std::uint8_t
    {
        // 요청이 없다.
        None,
        // 아직 이 요청의 사본이 없다 - 요청이 처음이거나 바뀌었고, 모양이 바뀌었으면 사본 텍스처를 다음 프레임 전에 만든다.
        Waiting,
        Captured,
        // 요청한 패스·텍스처가 이번 그래프에 없거나 그 패스가 돌지 않았다.
        NotFound,
        // 깊이는 복사하지 못한다.
        Unsupported
    };

    class Renderer final
    {
        friend class PostProcessBuilder;

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
        // 열린 뷰에 3D 라이트를 낸다(D-366). 뷰마다 `MaxLights3D` 개까지이고(`Ambient` 는 세지 않는다) 넘치면 버리고 센다.
        static constexpr UInt32 MaxLights3D = 16;
        Bool SubmitLight3D(const Light3DSubmit& light);
        Bool SubmitMeshes(JArrayView<MeshSubmit> items);
        Bool SubmitWorldText(const WorldTextSubmit& item);
        Bool SubmitWorldTexts(JArrayView<WorldTextSubmit> items);
        // **레이어 하나를 제 텍스처에 그려 얹는다**(D-279, 기존 `Render2DPipeline` 의 레이어 경로). 이 뒤로 `EndLayer` 까지 낸
        // 스프라이트는 투명하게 지운 텍스처에 보통 알파로 그려지고, 그 텍스처가 한 장으로 `blend` 와 `opacity` 로 뷰의 타깃에 얹힌다 -
        // 레이어 안의 스프라이트끼리는 서로 비치지 않고 레이어 전체가 한 번에 옅어진다. 뷰 안에서만 열고, 겹쳐 열지 않는다.
        // `Normal` 이고 불투명도 1 인 레이어는 열 필요가 없다(열어도 그림은 같고 텍스처만 든다). `EndView` 는 열린 것을 닫는다.
        // 깊이가 달린 뷰(메시·월드 텍스트가 있는 3D 뷰)에서는 묶음을 보지 않고 그대로 그린다.
        // `post` 는 그 레이어의 효과다(D-345, 레이어의 프로필). 얹기 전에 레이어 텍스처에 낸 차례로 걸고, 레이어에 걸리지 않는 종류는 버리고 센다.
        // 블록은 여기서 복사하므로 가리키는 곳은 돌아온 뒤 버려도 된다. 효과가 있으면 `Normal`·1 이어도 열어야 걸린다.
        Bool BeginLayer(CompositeBlend blend, Float opacity, const PostProcessStack& post = {});
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
        // **이 뒤로 낸 스프라이트를 사각형 안에서만 그린다**(D-351). `rect` 는 제출한 월드와 같은 공간의 {minX, minY, maxX, maxY} 이고
        // nullptr 이면 끈다. 뷰마다 꺼진 채 시작한다. 그릴 때 뷰의 투영으로 픽셀 시저가 되고(돌린 뷰는 둘러싼 사각형), 뷰의 시저와 교집합이다.
        // 구간 상한은 레이어 묶음과 같다(`maxLayerGroups`) - 넘치면 자르지 않고 센다.
        Bool SetSpriteClip(const Float* rect);

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
        // **3D 텍스처를 올린다**(D-343, 컬러 그레이딩의 LUT). `RGBA16Float` 텍셀(채널마다 반정밀 실수 넷, x 가 가장 빨리 돈다)이고 깊이 조각을
        // 차례로 이어 준다. 프레임 밖에서만 부른다. 크기가 0 이거나 장치 상한을 넘거나 바이트 수가 맞지 않으면 빈 핸들이다.
        // 지우기는 `UnregisterTexture` 다. `UpdateTexture`·`UpdateTextureRegion` 은 3D 를 받지 않는다.
        AssetHandle RegisterVolumeTexture(UInt32 width, UInt32 height, UInt32 depth, JArrayView<std::byte> rgba16f);
        // 같은 크기의 새 픽셀로 갈아 끼운다(에셋의 in-place 재로드). 크기가 다르면 거짓이다 - 새로 등록한다.
        Bool UpdateTexture(AssetHandle texture, JArrayView<std::byte> rgba8);
        // 사각형 하나만 갈아 끼운다(글리프 아틀라스의 새 칸). rgba8 은 사각형 왼쪽 위 텍셀부터이고 행 간격은 rowPitch 바이트다.
        // 백엔드가 못 하면 거짓이다 - 부르는 쪽은 `UpdateTexture` 로 전체를 올린다.
        Bool UpdateTextureRegion(AssetHandle texture, UInt32 x, UInt32 y, UInt32 width, UInt32 height,
            JArrayView<std::byte> rgba8, UInt32 rowPitch);
        void UnregisterTexture(AssetHandle texture);
        UInt32 GetTextureCount() const;
        // **렌더 타깃 텍스처**(D-362, 렌더 텍스처). 백버퍼 포맷이고 그리기·샘플링 둘 다 된다. 스프라이트 텍스처로 쓰는 핸들(`SpriteSubmit::texture`)을 주고,
        // `GetRenderTargetTexture` 가 뷰의 `CameraParams::target` 이다. 프레임 밖에서만 부른다. 지우기는 `UnregisterTexture` 다. 크기가 0 이거나 장치가 거절하면 빈 핸들이다.
        AssetHandle RegisterRenderTarget(const Extent2D& extent);
        // `RegisterRenderTarget` 이 준 핸들의 텍스처다. 렌더 타깃이 아니거나 풀린 핸들이면 빈 핸들이다.
        TextureHandle GetRenderTargetTexture(AssetHandle texture) const;
        // **머티리얼을 올린다**(D-347). 자리마다 파이프라인을 장면 포맷(출력·HDR)마다 미리 만든다 - 스프라이트는 깊이가 달린 패스의 쌍둥이까지
        // 넷, 후처리는 둘이다. 프레임 밖에서만 부른다. 셰이더가 없거나 이 API 의 바이트코드가 없거나 파라미터 크기가 맞지 않거나 파이프라인을
        // 만들지 못하면 빈 핸들이다. `SpriteSubmit::material`·`PostProcessPassDesc::material` 에 넣는다.
        AssetHandle RegisterMaterial(const MaterialDesc& desc);
        // 같은 셰이더의 파라미터·텍스처만 갈아 끼운다(머티리얼 파일의 재로드). 셰이더가 바뀌면 지우고 다시 올린다. 프레임 밖에서만 부른다.
        Bool UpdateMaterial(AssetHandle material, const MaterialDesc& desc);
        void UnregisterMaterial(AssetHandle material);
        UInt32 GetMaterialCount() const;
        // 머티리얼 셰이더가 읽는 시각(`JBroTime`, 초)이다. 부르지 않으면 렌더러를 연 뒤로 흐른 실제 시간이다. 프레임 밖에서 부른다.
        void SetMaterialTime(Float seconds, Float delta);
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

        // ── 렌더 그래프 진단(D-339, 렌더 그래프 창) ──
        // **지난 프레임의 렌더 그래프다.** 다음 `EndFrame` 이 다시 짓기 전까지 그대로다 - 에디터는 UI 를 엔진 프레임보다 먼저 지으므로
        // 그때 읽으면 지난 프레임의 것이다. 컴파일한 그래프가 아직 없거나 짓다가 멈춘 프레임이면 nullptr 이다.
        const RenderGraph* GetLastRenderGraph() const;
        // 그래프를 컴파일한 프레임마다 하나씩 는다. 창이 같은 그래프를 다시 읽지 않게, 멈춰 둔 것과 새것을 가르는 데 쓴다.
        UInt64 GetLastRenderGraphSerial() const;
        UInt32 GetLastRenderGraphViewCount() const;
        Bool GetLastRenderGraphView(UInt32 index, RendererViewDebugInfo& info) const;
        // 그래프가 만드는 중간 텍스처의 풀이다. 바라는 모양은 다음 `BeginFrame` 이 만들고 비운다.
        const RenderTargetPool& GetRenderTargetPool() const;
        // **패스마다 GPU 시간을 잰다**(D-341). 켜 둔 동안만 패스 앞뒤에 시각을 적는다 - 렌더 그래프 창이 열려 있을 때다.
        // 시각은 GPU 가 프레임을 끝낸 뒤 오므로 떠 있는 프레임 수만큼 늦다. 장치가 시각을 못 적으면 켜도 아무 일이 없다.
        void SetRenderGraphTimingEnabled(Bool enabled);
        Bool IsRenderGraphTimingSupported() const;
        // 가장 최근에 시간이 온 그래프의 번호(`GetLastRenderGraphSerial` 과 같은 셈)다. 아직 없으면 0 이다.
        UInt64 GetRenderGraphTimingSerial() const;
        UInt32 GetRenderGraphTimingCount() const;
        Bool GetRenderGraphTiming(UInt32 index, RenderGraphPassTiming& timing) const;
        // 그 프레임의 첫 시각부터 끝 시각까지다(잰 패스들이 GPU 에서 걸린 전체).
        Float GetRenderGraphGpuMilliseconds() const;
        // **패스 직후의 텍스처를 뜬다**(D-341). 요청은 지울 때까지 남아 프레임마다 다시 뜬다. 사본은 렌더러가 들고, 크기·포맷이 바뀌면
        // 다음 프레임 전에 다시 만든다.
        void RequestRenderGraphCapture(const RenderGraphCaptureRequest& request);
        void ClearRenderGraphCapture();
        RenderGraphCaptureStatus GetRenderGraphCaptureStatus() const;
        TextureHandle GetRenderGraphCaptureTexture() const;
        Extent2D GetRenderGraphCaptureExtent() const;
        // 사본을 뜬 그래프의 번호다.
        UInt64 GetRenderGraphCaptureSerial() const;

        // 프레임을 닫기 전에 부를 것을 건다. **프레임 밖에서만 바꾼다** -
        // 프레임 중간에 바뀌면 이미 기록한 것과 어긋난다. nullptr 이면 뗀다.
        Bool SetFrameOverlay(FrameOverlay overlay, void* user);
        Bool HasFrameOverlay() const;

        // 렌더러가 만든 디바이스다. **리소스를 만들고 지우는 데만 쓴다** -
        // 에디터가 시뮬레이션 뷰 텍스처와 자기 UI 파이프라인을 만들려면 이것이 필요하다.
        // 프레임을 여닫는 것은 여전히 렌더러의 일이다.
        IRHIDevice* GetDevice() const;

        Bool IsDeviceLost() const;
        Bool IsInitialized() const;
        // 후처리할 파이프라인(HDR 장면 묶음과 전체 화면 패스)이 섰는가(D-342). 처음 후처리를 바란 프레임의 다음 `BeginFrame` 에 선다.
        Bool IsPostProcessReady() const;
        // 창(스왑체인)의 크기다. 프레임이 텍스처로 가는 동안에도 이것은 창이다.
        Extent2D GetSurfaceExtent() const;
        // **이번 프레임이 실제로 그려지는 크기다.** 타깃을 준 프레임은 그
        // 텍스처의 크기이고, 아니면 창 크기다.
        //
        // 카메라는 창이 아니라 이것으로 화면을 잡아야 한다. 창으로 잡으면
        // 에디터에서 게임 화면이 에디터 창 모양을 따라가고, 시뮬레이션 뷰가 창보다
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
            // 이 뷰의 클립 구간(`m_clipRanges`)이다(D-351).
            UInt32 clipRangeOffset = 0;
            UInt32 clipRangeCount = 0;
            // `Global` 라이트의 합이다. 라이트맵을 지우는 색이다.
            Float ambient[3] = {0.0f, 0.0f, 0.0f};
            // 라이트를 하나라도 받았는가(`Global` 포함). 거짓이면 빛을 받는 구간도 그대로 그린다.
            Bool lighting = false;
            // 빛을 받는 스프라이트 구간이 있는가. 업로드가 채운다.
            Bool hasLitRun = false;
            // 이 뷰의 후처리 효과(`m_postEntries`)다(D-342).
            UInt32 postOffset = 0;
            UInt32 postCount = 0;
            // 통째로 얹을 때 거는 레이어 효과(`CameraParams::layerPostProcess`, D-345)다.
            UInt32 layerPostOffset = 0;
            UInt32 layerPostCount = 0;
        };

        // **장면 포맷 하나에 매인 파이프라인 묶음**(D-342). 장면(뷰의 타깃·레이어 텍스처)에 그리는 것은 모두 그 색 포맷으로 만든다 - D3D12·Vulkan 은
        // 파이프라인의 포맷이 첨부와 같아야 한다. 0 번은 백버퍼 포맷이고 처음부터 서며, 1 번은 HDR(`PostProcessHdrFormat`)이고 처음 바랄 때 선다.
        struct ScenePipelines
        {
            GraphicsPipelineHandle sprite;
            // 깊이가 달린 패스(메시가 있는 뷰) 위에 스프라이트를 얹을 때 쓰는 쌍둥이다. 포맷만 같고 깊이는 보지도
            // 쓰지도 않는다 - 파이프라인의 깊이 포맷은 패스의 첨부와 같아야 하기 때문에 둘이 필요하다.
            GraphicsPipelineHandle spriteOverDepth;
            GraphicsPipelineHandle sdfText;
            GraphicsPipelineHandle sdfTextOverDepth;
            // 라이트맵을 곱하는 스프라이트·SDF 텍스트다(D-291).
            GraphicsPipelineHandle litSprite;
            GraphicsPipelineHandle litSdfText;
            GraphicsPipelineHandle worldText;
            GraphicsPipelineHandle mesh;
            // `Normal`·`Additive`·`Multiply`·`Screen` 차례다.
            GraphicsPipelineHandle layerComposite[4];
            // 아래 그림을 읽는 블렌드 아홉이 함께 쓰는 하나다(덮어쓰기, D-283). 어느 식인지는 상수가 고른다.
            GraphicsPipelineHandle layerBackdrop;
        };
        static constexpr UInt32 SceneFormatCount = 2;
        static constexpr UInt32 LdrScene = 0;
        static constexpr UInt32 HdrScene = 1;

        // `SetSpriteLighting(true)` 와 `false` 사이에 낸 스프라이트 번호 구간 [first, end) 다(D-291).
        struct LitRange
        {
            UInt32 firstSprite = 0;
            UInt32 endSprite = 0;
        };

        // `SetSpriteClip(rect)` 와 `SetSpriteClip(nullptr)` 사이에 낸 스프라이트 번호 구간 [first, end) 과 그 사각형이다(D-351).
        struct ClipRange
        {
            UInt32 firstSprite = 0;
            UInt32 endSprite = 0;
            Float rect[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
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
            // 얹기 전에 거는 효과(`m_postEntries`)다(D-345).
            UInt32 postOffset = 0;
            UInt32 postCount = 0;
        };

        // ── 렌더 그래프의 패스 자료(D-339) ──
        // 뷰의 중간 텍스처(레이어·아래 그림 사본·라이트맵·그림자 마스크·깊이)는 그래프가 만들고 풀(`m_targetPool`)에서 나눠 쓴다.
        // 실행 함수는 렌더러의 정적 함수이고, 자료는 그래프의 프레임 저장소에 복사된다 - 그래서 아래는 모두 복사해도 되는 값이다.

        // 스프라이트 구간 [firstRun, endRun) 을 그리는 패스다. 뷰의 본 패스·레이어 묶음·대신 그리기가 모두 이것이다.
        struct SpritePassData
        {
            Renderer* renderer = nullptr;
            Viewport viewport;
            ScissorRect scissor;
            UInt32 view = 0;
            UInt32 firstRun = 0;
            UInt32 endRun = 0;
            // 선택으로 읽는 라이트맵과 선택으로 다는 깊이다. 없으면 빛 없이·깊이 없이 그린다.
            RenderGraphTexture lightMap;
            RenderGraphTexture depth;
            // 메시와 월드 텍스트도 그린다(깊이가 달린 뷰의 본 패스).
            Bool drawDepthWork = false;
            // 그리는 곳의 포맷 묶음(`m_scene` 의 자리)이다.
            UInt32 scene = LdrScene;
            // 그리는 곳의 크기다. 머티리얼 셰이더의 `JBroTarget` 이다.
            Extent2D targetExtent;
            // 뷰마다 한 번 세는 것(빛·깊이가 없던 뷰, 보지 않은 레이어 묶음)을 이 패스가 센다.
            Bool countView = false;
            // 레이어 텍스처가 없어 대신 그리는 패스다. 얹지 못한 것으로 센다.
            Bool uncomposited = false;
        };
        // 레이어 텍스처를 타깃에 얹는 패스다. 아래 그림을 읽는 블렌드는 앞의 복사 패스가 채운 사본을 읽는다(D-283).
        struct CompositePassData
        {
            Renderer* renderer = nullptr;
            Viewport viewport;
            ScissorRect scissor;
            RenderGraphTexture layer;
            RenderGraphTexture backdrop;
            CompositeBlend blend = CompositeBlend::Normal;
            Float opacity = 1.0f;
            UInt32 scene = LdrScene;
        };
        // 후처리의 전체 화면 패스 하나다(D-342). 상수는 패스 자료에 복사해 두고 기록할 때 상수 링에 올린다.
        struct PostPassData
        {
            Renderer* renderer = nullptr;
            Viewport viewport;
            ScissorRect scissor;
            RenderGraphTexture inputs[PostProcessPassDesc::MaxInputs];
            UInt32 inputCount = 0;
            // 그래프 밖의 텍스처(LUT)다. 그래프 입력 다음 자리에 묶는다.
            AssetHandle external;
            PostProcessProgram program = PostProcessProgram::Copy;
            UInt32 scene = LdrScene;
            UInt32 constantBytes = 0;
            std::byte constants[PostProcessBuilder::MaxConstantBytes] = {};
            // 머티리얼로 그리는 패스면 그 자리 + 1 이다(D-347). 입력의 크기와 곱한 알파 표시는 셰이더의 엔진 값이다.
            UInt32 material = 0;
            // 볼륨이 섞은 파라미터 블록이다(D-348). 프레임 동안 사는 효과 블록 사본(`m_postBlocks`) 안을 가리킨다. 없으면 머티리얼의 블록이다.
            const std::byte* materialParameters = nullptr;
            Extent2D targetExtent;
            Bool premultiplied = false;
        };
        // **올라간 머티리얼 하나**(D-347). 핸들의 index 가 자리이고 generation 이 재사용을 가른다(`TextureResource` 와 같다).
        // 파이프라인은 후처리면 [장면 포맷], 스프라이트면 [장면 포맷 * 2 + 깊이 쌍둥이] 다.
        struct MaterialResource
        {
            MaterialDomain domain = MaterialDomain::PostProcess;
            GraphicsPipelineHandle pipelines[SceneFormatCount * 2];
            // 셰이더가 선언한 텍스처 자리 수(t0 포함)다. 그만큼 묶어야 그린다(D-325).
            UInt32 textureSlots = 1;
            AssetHandle textures[MaxMaterialTextures];
            UInt32 parameterBytes = 0;
            std::byte parameters[MaxMaterialParameterBytes] = {};
            UInt32 generation = 1;
            Bool occupied = false;
        };
        // 머티리얼 셰이더의 엔진 값(b0, `MaterialPrelude` 의 `JBroEngine`)이다. 스프라이트는 앞에 뷰-투영 행렬이 더 붙는다.
        struct MaterialEngineValues
        {
            Float viewport[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            Float target[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            Float time[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            Float flags[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        };
        static_assert(sizeof(MaterialEngineValues) == 64, "the material engine values are the prelude's cbuffer JBroEngine");
        // 뷰에 낸 효과 하나다. 블록은 `m_postBlocks` 에 복사해 두었다.
        struct PostEntrySlot
        {
            UInt32 type = 0;
            UInt32 blockOffset = 0;
        };
        // 뷰 하나가 받는 효과의 상한이다. 넘는 것은 `BeginView` 가 버리고 센다.
        static constexpr UInt32 MaxPostProcessPerView = 32;
        // 뷰 하나의 효과를 건 차례다. 마지막 칸 하나는 넣어 준 톤맵의 자리다.
        struct PostPlanStep
        {
            const PostProcessEffectType* type = nullptr;
            const void* block = nullptr;
            TextureFormat output = TextureFormat::Unknown;
        };
        struct PostPlan
        {
            PostPlanStep steps[MaxPostProcessPerView + 1];
            UInt32 count = 0;
            Bool hdr = false;
        };
        struct CopyPassData
        {
            RenderGraphTexture source;
            RenderGraphTexture destination;
        };
        // 라이트맵의 패스들이다. `shadowedLight` 는 `m_shadowedLights` 의 자리이고, 그림자 없는 라이트를 한 번에 더하는 패스는 보지 않는다.
        struct LightPassData
        {
            Renderer* renderer = nullptr;
            Viewport viewport;
            ScissorRect scissor;
            UInt32 view = 0;
            UInt32 shadowedLight = 0;
            RenderGraphTexture mask;
        };
        struct OutlinePassData
        {
            Renderer* renderer = nullptr;
            Float color[4] = {1.0f, 1.0f, 0.0f, 1.0f};
            UInt32 width = 0;
            Extent2D extent;
            RenderGraphTexture first;
            RenderGraphTexture second;
            Bool composite = false;
        };

        // 같은 텍스처와 샘플러로 그리는 스프라이트의 연속 구간이다(D-113). 순서는 제출 순서 그대로다 - 정렬은
        // 프레임워크의 일이고, 여기서는 이웃이 같으면 묶는 것만 한다.
        struct SpriteRun
        {
            TextureHandle texture;
            SamplerHandle sampler;
            // 머티리얼 스프라이트의 구간이면 그 머티리얼(`m_materials` 의 자리 + 1)이다. 0 이면 빌트인이다.
            UInt32 material = 0;
            UInt32 firstInstance = 0;
            UInt32 instanceCount = 0;
            // SDF 텍스트 구간이면 참이다. 인스턴스는 텍스트 버퍼의 같은 번호에 있다.
            Bool sdf = false;
            // 이 구간이 든 레이어 묶음(`m_layerGroups` 의 자리)이다. 묶음 경계에서 구간이 끊긴다.
            UInt32 layerGroup = NoLayerGroup;
            // 빛을 받는 구간이면 참이다(D-291). 빛을 받는 구간의 경계에서 끊긴다.
            Bool lit = false;
            // 이 구간이 든 클립 구간(`m_clipRanges` 의 자리)이다(D-351). 경계에서 끊기고, 그릴 때 그 사각형이 시저가 된다.
            UInt32 clip = NoLayerGroup;
        };

        // 같은 메시를 그리는 인스턴스들의 연속 구간이다(D-110). 업로드가 뷰 안에서 메시별로 모아 놓으므로
        // 드로우 하나가 구간 하나다 - 메시마다 드로우를 내던 것에서 메시 **종류**마다 드로우를 내는 것으로.
        struct MeshRun
        {
            AssetHandle mesh;
            // 렌더러 머티리얼 자리 + 1 이다. 0 이면 빌트인 셰이더다(D-348).
            UInt32 material = 0;
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

        // 뷰 하나의 3D 라이트 블록이다(D-366). `BuiltinMesh.hlsl` 의 `MeshLights`(b1)·머리글의 `JBroLights`(b2)와 같은 배치다.
        struct GpuMeshLights
        {
            Float count[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            Float ambient[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            Float position[MaxLights3D.Get()][4] = {};
            Float color[MaxLights3D.Get()][4] = {};
        };
        static_assert(sizeof(GpuMeshLights) == 544, "the light block is part of the shader ABI");
        // 메시 인스턴스 하나. 월드 4x4(행 넷)와 tint. `BuiltinMesh.hlsl` 의 ATTRIBUTE2..6 이 이것을 읽는다.
        struct GpuMeshInstance
        {
            Matrix4x4 world;
            Float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        };
        static_assert(sizeof(MeshVertex) == 32, "mesh vertex stride is part of the shader ABI");
        static_assert(offsetof(MeshVertex, normal) == 12, "attribute 1 reads the normal from offset 12");
        static_assert(offsetof(MeshVertex, uv) == 24, "attribute 7 reads the uv from offset 24");
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
            // 3D 텍스처(LUT)의 깊이다. 2D 는 1 이다.
            UInt32 depth = 1;
            UInt32 generation = 1;
            Bool occupied = false;
            // 뷰의 대상으로 쓸 수 있는가(D-362).
            Bool renderTarget = false;
        };

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
        const MeshResource* FindMesh(AssetHandle mesh) const;
        const TextureResource* FindTexture(AssetHandle texture) const;
        void DestroyTextureResources();
        // 핸들의 머티리얼 자리 + 1 이다. 빈 핸들·죽은 핸들은 0 이다.
        UInt32 FindMaterialSlot(AssetHandle material) const;
        Bool CreateMaterialPipelines(MaterialResource& resource, const CompiledMaterialShader& shader);
        void DestroyMaterialPipelines(MaterialResource& resource);
        void DestroyMaterialResources();
        MaterialEngineValues MakeMaterialEngineValues(const Viewport& viewport, const Extent2D& target, Bool premultiplied) const;
        // 스프라이트 인스턴스의 정점 배치다. 빌트인 스프라이트와 머티리얼 스프라이트가 같은 것을 쓴다.
        struct SpriteVertexLayout
        {
            VertexAttributeDesc quad[1];
            VertexAttributeDesc instance[4];
            VertexBufferLayoutDesc buffers[2];
        };
        static void MakeSpriteVertexLayout(SpriteVertexLayout& layout);
        // 메시의 정점 배치다. 빌트인 메시와 머티리얼 메시가 같은 것을 쓴다.
        struct MeshVertexLayout
        {
            VertexAttributeDesc vertex[3];
            VertexAttributeDesc instance[5];
            VertexBufferLayoutDesc buffers[2];
        };
        static void MakeMeshVertexLayout(MeshVertexLayout& layout);
        // 머티리얼의 텍스처(t1..)·샘플러 넷·파라미터(b1)를 묶는다. t0 은 부르는 쪽이 묶는다.
        // `parameters` 를 주면 머티리얼의 블록 대신 그것(같은 크기)을 올린다(볼륨이 섞은 것, D-348).
        Bool BindMaterialResources(IRHICommandContext& commands, const MaterialResource& material, SamplerHandle sourceSampler,
            const std::byte* parameters = nullptr);
        // 뷰를 그래프의 패스로 짓고, 컴파일해 기록한다(D-339).
        Bool RecordViews();
        // 뷰 하나의 패스를 더한다. 뷰가 잘못되었으면(타깃 크기를 모름·뷰포트가 타깃 밖) 거짓이다.
        Bool AddViewPasses(UInt32 viewIndex, RenderGraphTexture frameTarget, TextureHandle frameTexture, const Extent2D& frameExtent);
        // 그 뷰의 라이트맵을 그리는 패스들이다 - 환경광으로 지우고 그림자 없는 라이트를 더하고, 그림자를 드리우는 라이트는 하나씩 마스크로 깎아 더한다.
        Bool AddLightMapPasses(UInt32 viewIndex, RenderGraphTexture lightMap, const Extent2D& extent, const Viewport& viewport,
            const ScissorRect& scissor);
        // 레이어 텍스처를 타깃에 얹는 패스들이다. 아래 그림을 읽는 블렌드면 그 앞에 타깃을 사본에 복사하는 패스가 붙는다.
        // `scene` 은 `target` 의 포맷 묶음이다.
        Bool AddCompositePasses(UInt32 viewIndex, RenderGraphTexture layer, RenderGraphTexture backdrop, RenderGraphTexture target,
            CompositeBlend blend, Float opacity, const Viewport& viewport, const ScissorRect& scissor, UInt32 scene);
        // 스프라이트 구간을 그린다. 렌더 패스는 열려 있어야 한다.
        Bool DrawSpriteRuns(IRHICommandContext& commands, UInt32 scene, const ViewPacket& view, UInt32 firstRun, UInt32 endRun,
            TextureHandle lightMap, Bool overDepth, const Viewport& viewport, const Extent2D& target, const ScissorRect& viewScissor);
        // 장면에 그리는 파이프라인을 그 포맷으로 만든다. 하나라도 못 만들면 거짓이고, 만든 것은 `DestroyScenePipelines` 가 지운다.
        Bool CreateScenePipelines(TextureFormat format, ScenePipelines& pipelines);
        void DestroyScenePipelines(ScenePipelines& pipelines);
        // ── 후처리(D-342) ──
        // HDR 장면 묶음·전체 화면 패스·상수 링을 만든다. 프레임 밖에서, 처음 후처리를 바란 프레임의 다음 `BeginFrame` 이 부른다.
        Bool CreatePostProcessResources();
        void DestroyPostProcessResources();
        // 뷰 하나의 효과를 지점 차례로 늘어놓는다. 톤맵은 앞의 하나만 쓰고, HDR 인데 톤맵이 없으면 `None` 톤맵을 넣는다.
        // 받지 않는 지점(`AfterLayer`·`AfterScreenLayers`)의 효과는 버리고 센다. 걸 것이 없으면 거짓이다.
        Bool PlanPostProcess(const ViewPacket& view, PostPlan& plan);
        // 낸 효과를 프레임 저장소(`m_postEntries`·`m_postBlocks`)에 옮긴다. 꺼진 효과는 옮기지 않고, 종류를 모르거나 블록이 배열 밖이거나
        // 상한을 넘는 효과는 버리고 센다.
        void CopyPostEntries(const PostProcessStack& stack, UInt32& offset, UInt32& count);
        // **레이어 효과**(D-345). 레이어에 걸리는 종류만 낸 차례로 늘어놓고 나머지는 버리고 센다. 모든 효과가 `format` 에 쓴다.
        Bool PlanLayerPostProcess(UInt32 offset, UInt32 count, TextureFormat format, PostPlan& plan);
        // 레이어 텍스처에 효과를 걸고 `layer` 를 마지막 효과의 텍스처로 바꾼다. 효과는 텍스처 전체를 덮는다 - 다음 효과가 뷰포트 둘레를
        // 읽어도 비어 있지 않다. 걸 자리(파이프라인·텍스처)가 없으면 바라 두고 `layer` 를 그대로 둔다. 그래프에 더하다 실패하면 거짓이다.
        Bool AddLayerPostPasses(UInt32 viewIndex, UInt32 offset, UInt32 count, TextureFormat format, const Extent2D& extent,
            RenderGraphTexture& layer);
        // 계획한 효과를 건다. `scene` 은 그려 둔 장면이고 `output` 은 뷰의 타깃이다 - 마지막 효과가 그 뷰포트 안에 쓴다.
        Bool AddPostProcessPasses(UInt32 viewIndex, const PostPlan& plan, RenderGraphTexture scene, RenderGraphTexture output,
            LoadOperation outputLoad, const ClearColor& outputClear, const Extent2D& extent, const Viewport& viewport,
            const ScissorRect& scissor);
        Bool AddPostProcessPass(PostProcessBuilder& builder, const PostProcessPassDesc& desc);
        // 계획한 효과가 만들 텍스처를 센다. 효과 사이의 중간 텍스처(포맷마다 많아야 둘)와, 패스 함수를 셈으로만 돌려 얻은 효과 안의
        // 텍스처다. 셀 모양이 너무 많거나 패스 함수가 거절하면 거짓이다.
        // 뷰는 마지막 효과가 타깃에 쓰고(`finalIsTexture` 거짓), 레이어는 마지막 효과도 텍스처에 쓴다.
        Bool CountPostProcessTextures(UInt32 viewIndex, const PostPlan& plan, TextureFormat inputFormat, TextureFormat finalFormat,
            Bool finalIsTexture, Bool premultiplied, const Extent2D& extent, const Viewport& viewport, const ScissorRect& scissor,
            PostProcessBuilder::TextureCount* counts, UInt32& countSize);
        // 그 텍스처 자리가 있는가(렌더 그래프 밖의 텍스처, 빌더가 묻는다).
        Bool GetRegisteredTextureSize(AssetHandle texture, UInt32& width, UInt32& height, UInt32& depth) const;
        static Bool ExecutePostProcess(RenderGraphContext& context, const void* data);
        static Bool ExecuteNothing(RenderGraphContext& context, const void* data);
        static Bool ExecuteSprites(RenderGraphContext& context, const void* data);
        static Bool ExecuteComposite(RenderGraphContext& context, const void* data);
        static Bool ExecuteCopy(RenderGraphContext& context, const void* data);
        static Bool ExecuteLightMap(RenderGraphContext& context, const void* data);
        static Bool ExecuteShadowMask(RenderGraphContext& context, const void* data);
        static Bool ExecuteShadowedLight(RenderGraphContext& context, const void* data);
        static Bool ExecuteUnshadowedLight(RenderGraphContext& context, const void* data);
        static Bool ExecuteOutline(RenderGraphContext& context, const void* data);
        void ResetSubmissionStorage();
        static UInt32 SceneIndexOf(TextureFormat format) noexcept
        {
            return format == PostProcessHdrFormat ? HdrScene : LdrScene;
        }

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
        // 장면 포맷마다의 파이프라인 묶음이다(D-342).
        ScenePipelines m_scene[SceneFormatCount];
        // 선택 외곽선의 두 패스다(D-276). 가로로 키우기(덮어쓰기)와 세로로 키워 둘레만 칠하기(알파 섞기).
        GraphicsPipelineHandle m_outlineGrowPipeline;
        GraphicsPipelineHandle m_outlineCompositePipeline;
        // **2D 라이팅**(D-291). 라이트맵에 라이트를 더하는 것(RGBA16F, `One·One`)과, 라이트맵을 곱해 그리는 스프라이트·SDF 텍스트다.
        Bool CreateBuiltinLightResources();
        void DestroyBuiltinLightResources();
        Bool UploadLightInstances();
        Bool UploadShadowEdges();
        void WriteLightInstance(const Light2DSubmit& light, GpuLight2DInstance& instance);
        GraphicsPipelineHandle m_light2DPipeline;
        // 그림자(D-291 3 단계): 변을 밀어내 마스크에 칠하는 것과, 마스크로 깎으며 라이트 하나를 더하는 것이다.
        GraphicsPipelineHandle m_shadowMaskPipeline;
        GraphicsPipelineHandle m_shadowedLight2DPipeline;
        BufferHandle m_shadowEdgeBuffers[MaxFrameSlots];
        Array<ShadowEdge2D> m_shadowEdges;
        Array<GpuShadowEdgeInstance> m_gpuShadowEdges;
        Array<ShadowedLight> m_shadowedLights;
        BufferHandle m_lightInstanceBuffers[MaxFrameSlots];
        Array<Light2DSubmit> m_lights;
        Array<GpuLight2DInstance> m_gpuLightInstances;
        Array<LitRange> m_litRanges;
        // 열린 빛을 받는 구간(`m_litRanges` 의 자리)이다. 없으면 `NoLayerGroup` 이다.
        UInt32 m_openLitRange = NoLayerGroup;
        Array<ClipRange> m_clipRanges;
        UInt32 m_openClipRange = NoLayerGroup;
        Array<LayerGroup> m_layerGroups;
        UInt32 m_openLayerGroup = NoLayerGroup;
        // 프레임마다 처음부터 짓는 렌더 그래프와, 그래프가 만드는 중간 텍스처의 풀이다(D-339). 풀은 지난 프레임이 바란 모양을
        // 다음 `BeginFrame` 이 프레임을 열기 전에 만든다 - 디바이스는 프레임 안에서 자원을 만들지 않는다.
        RenderGraph m_graph;
        RenderTargetPool m_targetPool;
        // 지난 그래프의 뷰들이다(렌더 그래프 창). 뷰 상한만큼 잡아 두고 그래프를 지을 때 채운다.
        Array<RendererViewDebugInfo> m_graphViews;
        UInt64 m_graphSerial = 0;
        // ── 패스별 GPU 시간(D-341) ── 프레임마다 잰 패스의 이름·뷰·시각 자리를 RHI 프레임 번호와 함께 고리에 적어 두고, 그 프레임의 시각이
        // 오면 ms 로 바꾼다. 고리는 떠 있는 프레임보다 하나 크다(D3D11 은 시각 묶음 셋을 돈다).
        static constexpr UInt32 TimedPassCapacity = MaxTimestampsPerFrame / 2;
        static constexpr UInt32 TimingRingSize = 4;
        struct TimedPass
        {
            const char* name = nullptr;
            UInt32 group = 0;
            UInt32 pass = 0;
            UInt32 timestamp = 0;
        };
        struct TimingFrame
        {
            UInt64 rhiSerial = 0;
            UInt64 graphSerial = 0;
            UInt32 count = 0;
            TimedPass passes[TimedPassCapacity];
        };
        void RecordGraphTiming();
        void CollectGraphTiming();
        TimingFrame m_timingRing[TimingRingSize];
        UInt32 m_timingRingNext = 0;
        UInt64 m_timestampScratch[MaxTimestampsPerFrame] = {};
        RenderGraphPassTiming m_timings[TimedPassCapacity];
        UInt32 m_timingCount = 0;
        UInt64 m_timingSerial = 0;
        UInt64 m_timingRhiSerial = 0;
        Float m_timingGpuMilliseconds = 0.0f;
        Bool m_timingEnabled = false;
        // ── 패스 직후 미리보기(D-341) ──
        void PrepareCapture();
        RenderGraphCaptureRequest m_captureRequest;
        Bool m_captureRequested = false;
        RenderGraphCaptureStatus m_captureStatus = RenderGraphCaptureStatus::None;
        TextureHandle m_captureTexture;
        TransientTextureDesc m_captureDesc;
        TransientTextureDesc m_captureWant;
        Bool m_captureWanted = false;
        UInt64 m_captureSerial = 0;
        // ── 후처리(D-342) ── 뷰가 낸 효과와 블록은 프레임마다 앞에서부터 채운다(상한만큼 `Initialize` 가 잡는다).
        // 파이프라인은 장면 포맷마다(`SceneIndexOf`) 프로그램마다 하나다. 처음 바란 프레임에는 후처리 없이 그리고 다음 `BeginFrame` 에 만든다.
        Array<PostEntrySlot> m_postEntries;
        Array<std::byte> m_postBlocks;
        UInt32 m_postBlockUsed = 0;
        GraphicsPipelineHandle m_postPipelines[SceneFormatCount][PostProcessProgramCount];
        ConstantRing m_postConstants;
        Bool m_postReady = false;
        Bool m_postWanted = false;
        // 만들다 실패했다. 다시 만들지 않는다 - 그 뒤의 후처리는 늘 기다리는 것으로 센다.
        Bool m_postFailed = false;
        // 텍스처가 없는 스프라이트가 샘플링하는 1x1 흰색이다. 틴트가 그대로 나온다.
        TextureHandle m_whiteTexture;
        SamplerHandle m_nearestSampler;
        SamplerHandle m_linearSampler;
        // 머티리얼 셰이더의 s3 다(D-347).
        SamplerHandle m_linearRepeatSampler;
        Array<TextureResource> m_textureResources;
        Array<MaterialResource> m_materials;
        // 머티리얼 셰이더의 시각이다. `SetMaterialTime` 을 부르지 않았으면 `BeginFrame` 이 실제 시간으로 채운다.
        Float m_materialTime = 0.0f;
        Float m_materialDelta = 0.0f;
        Bool m_materialTimeSet = false;
        // 실제 시간의 시작(steady_clock, 나노초)이다. 0 이면 아직 재지 않았다.
        Int64 m_materialClockStart = 0;
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
        // 제출 하나마다 푼 머티리얼 자리(+1, 0 은 빌트인)와, 뷰에 나온 머티리얼의 차례다(D-348). 제출 상한만큼 미리 잡는다.
        Array<UInt32> m_meshMaterialOf;
        Array<UInt32> m_meshMaterialOrder;
        // 뷰마다 하나다. `BeginView` 가 비우고 `SubmitLight3D` 가 채운다.
        Array<GpuMeshLights> m_viewLights3D;
        BufferHandle m_meshInstanceBuffers[MaxFrameSlots];
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

#pragma once

#include <JBro/Graphics/RenderGraph.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Reflection/Field.h>
#include <JBro/RHI/RHI.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    class Renderer;

    // **후처리의 주입 지점**(D-342, postprocess-plan §2.3). 효과는 종류마다 지점 하나에 들어가고, 뷰의 효과는 지점 차례로, 같은 지점 안에서는
    // 낸 차례로 돈다. 선택 외곽선과 에디터 표시는 모든 지점 뒤다.
    enum class PostProcessStage : std::uint8_t
    {
        // HDR 장면과 깊이(3D 의 SSAO·포그). 지금은 장면을 다 그린 뒤라 `BeforeTonemap` 과 같은 자리다.
        AfterOpaque,
        // 레이어마다의 프로필(9 단계). 지금은 받지 않는다.
        AfterLayer,
        // HDR 장면(블룸·DoF·노출).
        BeforeTonemap,
        // HDR 을 출력 포맷으로 내리는 톤맵 하나.
        Tonemap,
        // 출력 포맷(컬러 그레이딩·비네트·FXAA·픽셀화).
        AfterTonemap,
        // 화면 레이어까지 얹은 출력(CRT). 지금은 받지 않는다 - 화면 레이어는 카메라 뒤의 다른 뷰다.
        AfterScreenLayers,
    };
    inline constexpr UInt32 PostProcessStageCount = 6;

    // 효과가 뷰에 바라는 것이다. `Hdr` 를 바라는 효과가 하나라도 있으면 그 뷰는 장면을 `RGBA16Float` 로 그리고 `Tonemap` 지점에서 출력 포맷으로 내린다.
    enum class PostProcessNeeds : std::uint8_t
    {
        None = 0,
        Hdr = 1 << 0,
        Depth = 1 << 1,
        History = 1 << 2,
    };
    constexpr PostProcessNeeds operator|(PostProcessNeeds a, PostProcessNeeds b) noexcept
    {
        return static_cast<PostProcessNeeds>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
    }
    constexpr Bool HasNeed(PostProcessNeeds needs, PostProcessNeeds need) noexcept
    {
        return (static_cast<std::uint8_t>(needs) & static_cast<std::uint8_t>(need)) != 0;
    }

    // 장면을 HDR 로 그릴 때의 포맷이다(D-342, 사용자 결정 "RGBA16Float"). 레이어 텍스처와 아래 그림 사본도 이 포맷이라 알파가 필요하다.
    inline constexpr TextureFormat PostProcessHdrFormat = TextureFormat::RGBA16Float;

    // 효과의 패스가 쓰는 빌트인 전체 화면 프로그램이다. 입력 하나를 `Load` 로 읽어 출력의 뷰포트 안을 덮어쓴다.
    enum class PostProcessProgram : std::uint8_t
    {
        // 그대로 옮긴다(포맷을 바꾸는 자리).
        Copy,
        // 색에 상수 x 를 곱한다.
        Exposure,
        // 톤 곡선. 상수 x 가 곡선 번호다(`Tonemapper`).
        Tonemap,
    };
    inline constexpr UInt32 PostProcessProgramCount = 3;

    // **효과 하나의 패스를 짓는 자리**(D-342). 효과 종류의 `addPasses` 가 받는다. 입력은 앞 효과(또는 장면)의 결과이고, 효과는 출력의 뷰포트 안
    // 모든 픽셀을 써야 한다 - 마지막 효과의 출력은 뷰의 타깃이라 뷰포트 밖은 그대로 남아야 하므로(`GetOutputLoad`) 덮어쓰지 않는다.
    // 프레임 안에서만 산다. 렌더러가 만든다.
    class PostProcessBuilder
    {
    public:
        RenderGraphTexture GetInput() const noexcept
        {
            return m_input;
        }
        RenderGraphTexture GetOutput() const noexcept
        {
            return m_output;
        }
        TextureFormat GetInputFormat() const noexcept
        {
            return m_inputFormat;
        }
        TextureFormat GetOutputFormat() const noexcept
        {
            return m_outputFormat;
        }
        // 출력이 뷰의 타깃이면 `Load` 다(뷰포트 밖을 남긴다). 중간 텍스처면 `Discard` 다.
        LoadOperation GetOutputLoad() const noexcept
        {
            return m_outputLoad;
        }
        Extent2D GetExtent() const noexcept
        {
            return m_extent;
        }

        // 입력을 읽어 출력에 쓰는 전체 화면 패스 하나를 더한다. `constants` 는 b1 에 올라가는 상수이고 `MaxConstantBytes` 까지다.
        Bool AddFullscreenPass(const char* name, PostProcessProgram program, JArrayView<std::byte> constants);
        static constexpr UInt32 MaxConstantBytes = 64;

    private:
        friend class Renderer;
        Renderer* m_renderer = nullptr;
        UInt32 m_view = 0;
        RenderGraphTexture m_input;
        RenderGraphTexture m_output;
        TextureFormat m_inputFormat = TextureFormat::Unknown;
        TextureFormat m_outputFormat = TextureFormat::Unknown;
        LoadOperation m_outputLoad = LoadOperation::Discard;
        ClearColor m_outputClear;
        Extent2D m_extent;
        Viewport m_viewport;
        ScissorRect m_scissor;
    };

    // 효과의 패스를 짓는다. `parameters` 는 그 종류의 파라미터 블록(`blockSize` 바이트)이다. 거짓이면 그 뷰의 그래프를 버린다.
    using PostProcessAddPasses = Bool (*)(PostProcessBuilder& builder, const void* parameters);

    // **효과 종류 하나**(D-342). 빌트인 효과와 나중의 셰이더 그래프 효과가 같은 모양으로 들어온다(D-33).
    struct PostProcessEffectType
    {
        // 종류 이름이다. 프로필 에셋의 `Type:` 이 이것이다(8 단계). 정적 글자여야 한다.
        const char* name = nullptr;
        PostProcessStage stage = PostProcessStage::BeforeTonemap;
        PostProcessNeeds needs = PostProcessNeeds::None;
        // 파라미터 블록의 리플렉션 표다. 인스펙터·저장·섞기가 이것을 본다. 블록이 없으면 nullptr 이다.
        const PropertyTable* parameters = nullptr;
        UInt32 blockSize = 0;
        // 블록의 기본값(`blockSize` 바이트)이다. 블록이 없으면 nullptr 이다.
        const void* defaults = nullptr;
        PostProcessAddPasses addPasses = nullptr;
    };

    // **효과 종류 표**(D-342). 프로세스에 하나이고 빌트인(`Exposure`·`Tonemap`)이 먼저 든다. 번호가 곧 종류의 아이디다 - 뷰에 낼 때
    // `PostProcessEntry::type` 이 이것이다. 메인 스레드 전용이다.
    class PostProcessEffectRegistry
    {
    public:
        static constexpr UInt32 Capacity = 64;
        static constexpr UInt32 NotFound = 0xFFFFFFFFu;

        // 새 종류를 들이고 번호를 준다. 이름이 비었거나 이미 있거나, 패스 함수가 없거나, 블록과 기본값이 맞지 않거나, 표가 차면 `NotFound` 다.
        static UInt32 Register(const PostProcessEffectType& type);
        static const PostProcessEffectType* Get(UInt32 id);
        static UInt32 Find(const char* name);
        static UInt32 GetCount();
    };

    // 빌트인 효과의 번호다. 표가 처음 열릴 때 이 차례로 든다.
    namespace PostProcessEffects
    {
        inline constexpr UInt32 Exposure = 0;
        inline constexpr UInt32 Tonemap = 1;
    }

    // 톤 곡선이다(D-342, 사용자 결정 "고점 높은 것으로" - 넷을 모두 둔다). 기본은 `None` 이다.
    enum class Tonemapper : std::uint8_t
    {
        // 1 을 넘는 값만 자른다. 빛 1 아래의 흰색은 흰색이다(L4).
        None,
        // x / (1 + x). 기존 엔진의 것이다.
        Reinhard,
        // Narkowicz 의 ACES 근사.
        Aces,
        // Wrensch 의 최소 AgX(Blender 의 기본 곡선). 밝은 색이 흰색 쪽으로 빠지고 색이 덜 틀어진다.
        AgX,
    };

    JBRO_DEFINE_ENUM_TYPE(Tonemapper, "JBro.Tonemapper",
        { Tonemapper::None,     "None" },
        { Tonemapper::Reinhard, "Reinhard" },
        { Tonemapper::Aces,     "Aces" },
        { Tonemapper::AgX,      "AgX" });

    // **노출**(`BeforeTonemap`, HDR). 빛을 2^exposure 배 한다.
    struct ExposureEffect
    {
        JBRO_REFLECT_BODY(ExposureEffect)

        JBRO_FIELD(Float, exposure, Range(-16.0f, 16.0f)) = 0.0f;
    };

    // **톤맵**(`Tonemap`, HDR). 뷰에 톤맵이 둘이면 앞의 것만 쓴다. HDR 뷰에 톤맵이 없으면 `None` 으로 내린다.
    struct TonemapEffect
    {
        JBRO_REFLECT_BODY(TonemapEffect)

        JBRO_FIELD(Tonemapper, curve) = Tonemapper::None;
    };

    // **뷰에 낼 효과 하나**(D-342). `blockOffset` 은 `PostProcessStack::blocks` 안에서 그 종류의 블록이 시작하는 자리다.
    struct PostProcessEntry
    {
        UInt32 type = PostProcessEffectRegistry::NotFound;
        UInt32 blockOffset = 0;
        Bool enabled = true;
    };

    // **뷰의 풀린 프로필**(D-342, postprocess-plan §2.5). 카메라·볼륨을 섞은 결과를 프레임워크가 이 모양으로 넘긴다(8·10 단계).
    // `BeginView` 가 그 자리에서 렌더러의 프레임 저장소에 복사하므로, 가리키는 곳은 `BeginView` 가 돌아온 뒤 버려도 된다.
    struct PostProcessStack
    {
        JArrayView<PostProcessEntry> entries;
        JArrayView<std::byte> blocks;
    };
}

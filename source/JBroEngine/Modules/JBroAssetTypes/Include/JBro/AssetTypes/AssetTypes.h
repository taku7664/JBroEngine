#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Types/Uuid.h>

// 에셋을 가리키는 값 타입만 둔다. 로드·캐시를 소유하는 `AssetSystem` 은 엔진 계층(JBroAsset)이며
// 스크립트 표면에 나타나지 않는다(D-50). 컴포넌트 공개 필드가 참조하므로 이 헤더는 프렐류드를 탄다.

namespace JBro
{
    // 영속 식별자. 직렬화되는 값이다. **따로 만든 타입이 아니라 `Uuid` 그대로다**(D-111) - 아이디 역할에
    // 이 타입이 더 가질 것이 없고, 별개 타입으로 감싸면 변환과 코덱이 한 벌 더 생긴다. 임포트 때
    // `Uuid::Generate`, 빌트인은 `Uuid::FromName` 이다.
    using AssetId = Uuid;

    // 이번 실행에서의 위치. 저장하지 않는다.
    struct AssetHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;
    };

    // 에셋의 종류다. 파일에는 이름으로 적히므로(`AssetTypeRules`) 순서를 바꿔도 파일이 깨지지 않는다.
    // 이미지 파일 하나는 `Texture` 와 `Sprite` 둘로 등록된다(D-111) - Sprite 가 슬라이싱·피벗·PPU 를 갖고
    // Texture 를 가리키며, 3D 재질은 Texture 를 참조한다.
    enum class AssetType : std::uint8_t
    {
        Unknown,
        Texture,
        Sprite,
        Mesh,
        Material,
        Shader,
        Canvas,
        Prefab,
        Audio,
        Font
    };

    // 스프라이트 시트를 어떻게 자르는가다. 기존 엔진의 모델을 그대로 잇는다(asset-plan §2.7). `Automatic`(알파 기반
    // 자동 분할)은 기존 엔진도 예약만 하고 구현하지 않았으므로 두지 않는다.
    enum class SpriteSliceType : std::uint8_t
    {
        // 이미지 전체가 프레임 하나다.
        None,
        // 셀의 픽셀 크기로 균일하게 자른다.
        CellSize,
        // 행·열 개수로 균일하게 자른다.
        CellCount
    };

    // 스프라이트 PPU 의 기본이자, 0 이하·비유한으로 적힌 값을 대신하는 값이다(D-117·D-119). 한 곳에만 있다.
    inline constexpr float DefaultPixelsPerUnit = 100.0f;

    // `.jmeta` 의 `Sprite.ImportOptions` 블록이다. 리플렉션으로 읽고 쓴다.
    struct SpriteImportOptions
    {
        SpriteSliceType sliceType = SpriteSliceType::None;
        // CellCount 일 때
        std::uint32_t rowCount = 1;
        std::uint32_t columnCount = 1;
        // CellSize 일 때(픽셀)
        std::uint32_t cellWidth = 32;
        std::uint32_t cellHeight = 32;
        // 공용 - 바깥 여백과 셀 사이 간격(픽셀)
        std::uint32_t marginX = 0;
        std::uint32_t marginY = 0;
        std::uint32_t gapX = 0;
        std::uint32_t gapY = 0;
        // 프레임 안의 피벗(0..1)
        float pivotX = 0.5f;
        float pivotY = 0.5f;
        // 한 유닛에 드는 픽셀 수다(D-117). 프로젝트 기본값은 없다 - 에셋이 전부 말한다. 0 이하나 비유한으로 적힌 값은
        // 로드 때 `DefaultPixelsPerUnit` 으로 바로잡는다.
        float pixelsPerUnit = DefaultPixelsPerUnit;
    };

    // 텍스처를 어떻게 샘플링하는가(D-117). `Nearest` 는 텍셀 그대로(픽셀 아트), `Linear` 는 이웃과 섞는다.
    // `Default` 는 프로젝트의 `TextureFilter` 를 따른다 - 텍스처의 임포트 옵션에서만 뜻이 있다.
    enum class TextureFilter : std::uint8_t
    {
        Default,
        Nearest,
        Linear
    };

    // 텍스처 임포트 옵션이다. `.jmeta` 의 `Texture.ImportOptions` 로 저장된다(D-117).
    struct TextureImportOptions
    {
        TextureFilter filter = TextureFilter::Default;
    };

    // 오디오 에셋의 디코드 방식이다(`.jmeta` 의 `Audio.ImportOptions.mode`, D-197). 파일의 속성이라 에셋 몫이다(D-198).
    enum class AudioImportMode : std::uint8_t
    {
        // 로드 때 전부 PCM 으로 푼다. 짧은 효과음.
        Decompressed,
        // 압축된 바이트를 메모리에 두고 재생하며 푼다. 긴 배경음.
        Streaming,
        // 파일을 메모리에 올리지 않고 디스크에서 흘려 읽는다(D-203). 몇 분짜리 배경음·음성처럼 메모리에 두기 아까운 것.
        // 동시에 흘려 읽는 수가 정해져 있고(기본 8) 웹 빌드에서는 되지 않는다 - 그때는 `Streaming` 으로 둔다.
        StreamFromDisk
    };

    // `.jmeta` 의 `Audio.ImportOptions` 블록이다. **재생 파라미터는 없다** - 볼륨·루프·거리·버스는 컴포넌트가
    // 유일한 원천이다(D-197·D-198). 라우드니스 정규화 게인·루프 지점처럼 파일의 속성이 생기면 여기에 온다.
    struct AudioImportOptions
    {
        AudioImportMode mode = AudioImportMode::Decompressed;
        // 파일의 크기 보정(트림, 0..4, D-205)이다. 녹음마다 다른 크기를 여기서 한 번 맞추면 컴포넌트의 `volume` 은 연출에만 쓴다.
        float gain = 1.0f;
    };

    // `.jmeta` 의 `Font.ImportOptions` 블록이다(D-200, text-plan §4.1). 글자 크기는 컴포넌트의 몫이고(`Text2D::fontSize`,
    // 글자 픽셀), 여기에는 그 픽셀을 유닛으로 옮기는 비율과 아틀라스를 읽는 샘플러만 있다. 텍스처와 같은 규칙이다(D-119):
    // `pixelsPerUnit` 이 0 이하면 기본 100, `filter` 가 `Default` 면 프로젝트의 `TextureFilter`.
    // 렌더 모드(`Bitmap`·`Sdf`)는 SDF 가 서는 4 단계에서 온다 - 쓰이지 않는 옵션을 먼저 두지 않는다.
    struct FontImportOptions
    {
        float         pixelsPerUnit = DefaultPixelsPerUnit;
        TextureFilter filter = TextureFilter::Default;
    };

    // 시트의 한 칸이다. 픽셀 좌표는 왼쪽 위가 원점이다.
    struct SpriteFrame
    {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        float pivotX = 0.5f;
        float pivotY = 0.5f;
    };

    // 레지스트리가 아는 에셋 하나의 읽기 전용 요약이다. 경로는 에셋 폴더 기준 상대경로이고 구분자는 `/` 다.
    struct AssetMetadata
    {
        AssetId id;
        AssetType type = AssetType::Unknown;
        JStringView sourcePath;
    };

    namespace Asset
    {
        struct TextureAsset
        {
            AssetId id;
        };

        struct SpriteAsset
        {
            AssetId id;
        };

        struct MeshAsset
        {
            AssetId id;
        };

        struct MaterialAsset
        {
            AssetId id;
        };

        struct ShaderAsset
        {
            AssetId id;
        };

        struct AudioAsset
        {
            AssetId id;
        };
    }
}

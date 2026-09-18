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
        // 0 이면 프로젝트의 기본 PPU 를 쓴다.
        float pixelsPerUnit = 0.0f;
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
    }
}

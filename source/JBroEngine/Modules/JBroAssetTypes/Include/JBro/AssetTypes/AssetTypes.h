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

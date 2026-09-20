#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/String.h>

#include <string_view>

namespace JBro
{
    // 에셋 타입과 파일의 관계다. 기존 엔진의 규칙을 그대로 잇는다(asset-plan §1.3):
    // **확장자로 타입을 추정하는 것은 `.jmeta` 가 없을 때의 부트스트랩이고, 메타가 있으면 메타가 진실이다.**
    namespace AssetTypeRules
    {
        // 파일에 적히는 이름이다(`Texture`, `Sprite`, ...). `Unknown` 은 `Unknown` 이다.
        const char* GetTypeName(AssetType type) noexcept;
        // 이름에서 타입을 읽는다. 모르는 이름은 `Unknown` 이다.
        AssetType ParseTypeName(std::string_view name) noexcept;

        // 확장자로 타입을 추정한다(대소문자 무관). 이미지(`.png .jpg .jpeg .bmp .tga`)는 `Texture` 다 -
        // 그 파일이 Sprite 도 되는 것은 레지스트리가 안다. 모르는 확장자는 `Unknown` 이다.
        AssetType DetectTypeFromPath(std::string_view path) noexcept;

        // 이미지 파일이면 참이다. 레지스트리가 `Texture` 옆에 `Sprite` 를 함께 세우는 기준이다.
        bool IsImageType(AssetType type) noexcept;

        // 메타 파일의 확장자다(`.jmeta`). 짝 파일 이름 뒤에 그대로 붙는다: `hero.png` -> `hero.png.jmeta`.
        const char* GetMetaExtension() noexcept;
        bool IsMetaPath(std::string_view path) noexcept;
        // 메타를 쓸 때 잠깐 쓰는 임시 파일(`hero.png.jmeta.tmp`)이다. 저장은 여기에 쓰고 바꿔치기한다. 감시가 메타와
        // 같이 무시하고, 스캔은 모르는 확장자라 건너뛴다.
        String MakeMetaScratchPath(std::string_view metaPath);
        bool IsMetaScratchPath(std::string_view path) noexcept;
        String MakeMetaPath(std::string_view path);
    }
}

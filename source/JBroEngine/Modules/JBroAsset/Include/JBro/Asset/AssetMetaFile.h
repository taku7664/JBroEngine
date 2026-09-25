#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/String.h>

namespace JBro
{
    class IPlatform;

    // `.jmeta` 의 내용이다. 짝 파일 옆에 놓이고(`hero.png.jmeta`) 아이디와 타입을 든다.
    //
    // **경로는 적지 않는다.** 경로는 스캔 때 파일이 있는 자리에서 나온다 - 그래야 파일을 옮겨도 아이디가 산다
    // (기존 엔진의 원칙, asset-plan §1.3). 이미지 파일은 `Sprite` 블록에 두 번째 아이디를 갖는다(D-111).
    //
    //   Version: 1
    //   Id: 0123456789abcdeffedcba9876543210
    //   Type: Texture
    //   Sprite:
    //     Id: ...
    //
    // 임포트 옵션은 `Texture.ImportOptions`·`Sprite.ImportOptions` 블록이고 리플렉션 표(`AssetTypesReflection.h`)로
    // 읽고 쓴다(D-120). 블록이 없으면 `has*Options` 가 거짓이고 옵션은 기본값이다. **있으면 전부 읽혀야 한다** - 모르는
    // 키나 틀린 enum 이름은 파일 전체의 실패다(조용히 버리면 그 값이 사라지고 아무도 모른다). 쓰기는 `has*Options` 인
    // 블록만 적으므로, 읽어서 고쳐 다시 쓰면 옵션이 보존된다 - 에디터의 옵션 편집이 이 왕복이다.
    //
    //   Texture:
    //     ImportOptions:
    //       filter: Linear
    //   Sprite:
    //     Id: ...
    //     ImportOptions:
    //       sliceType: CellCount
    struct AssetMetaFile
    {
        std::uint32_t version = 1;
        AssetId id;
        AssetType type = AssetType::Unknown;
        // `Texture` 일 때만 뜻이 있다. 비어 있으면 Sprite 블록이 없었다는 뜻이다.
        AssetId spriteId;
        bool hasTextureOptions = false;
        TextureImportOptions textureOptions;
        bool hasSpriteOptions = false;
        SpriteImportOptions spriteOptions;
        // `Audio` 일 때만 뜻이 있다(`Audio.ImportOptions`, D-197).
        bool hasAudioOptions = false;
        AudioImportOptions audioOptions;
        // `Font` 일 때만 뜻이 있다(`Font.ImportOptions`, D-200).
        bool hasFontOptions = false;
        FontImportOptions fontOptions;
    };

    struct AssetMetaError
    {
        // 0 이면 파일 자체를 열지 못한 것이다.
        std::size_t line = 0;
        String message;
    };

    // 읽는다. 실패하면 `result` 는 손대지 않고 `error` 를 채운다. `Id` 가 없거나 읽히지 않는 것,
    // `Type` 이 모르는 이름인 것, 이미지 타입인데 `Sprite.Id` 가 없는 것이 실패다.
    bool LoadAssetMetaFile(IPlatform& platform, const char* utf8Path, AssetMetaFile& result, AssetMetaError& error);
    bool ParseAssetMetaFile(const char* text, std::size_t length, AssetMetaFile& result, AssetMetaError& error);

    // 쓴다. 글자를 만들지 못하거나(옵션 값이 적히지 않음, 이미지인데 스프라이트 아이디가 빔) 파일을 만들지 못하면 false 다.
    // **적히지 않는 값은 파일을 쓰지 않는다** - 빈 블록을 적으면 다음 읽기가 기본값으로 대신해 옵션이 조용히 사라진다.
    bool SaveAssetMetaFile(IPlatform& platform, const char* utf8Path, const AssetMetaFile& meta);
    bool FormatAssetMetaFile(const AssetMetaFile& meta, String& text);
    // 위의 것을 감싼다. 실패하면 빈 글자다 - 테스트와 같이 실패가 곧 검사인 자리용이고, 파일에 쓰는 쪽은 bool 판을 쓴다.
    String FormatAssetMetaFile(const AssetMetaFile& meta);
}

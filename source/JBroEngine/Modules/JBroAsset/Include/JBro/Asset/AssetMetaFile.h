#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

#include <string_view>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

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
    // 모델의 하위 에셋 하나다(D-391). `key` 는 `종류/이름`(`Mesh/Body`, `Material/Skin`)이고 다시 가져와도 같은 키면 같은 아이디다.
    //
    // 내장 그림(`Texture/`)만 둘을 더 든다(model-import-plan 5 단계). `Hash` 는 원본 바이트의 64 비트 FNV-1a 이고(16 자리 16 진수, 모델을 다시 읽을 때 늘 새로 적는다),
    // `ImportOptions` 는 그 그림 하나의 텍스처 옵션이다 - 그림 파일의 `Texture.ImportOptions` 와 같은 표다. 메시·재질 항목에 둘이 있으면 파일 전체의 실패다.
    //
    //   - Key: Texture/Wood
    //     Id: ...
    //     Hash: 0123456789abcdef
    //     ImportOptions:
    //       mipmaps: true
    //
    // 뼈대·메시 항목(`Skeleton/`·`Mesh/`)만 소켓 목록 `Sockets` 를 든다(D-460). 다른 종류에 있으면 파일 전체의 실패다. 빈 목록은 적지 않는다.
    //
    //   - Key: Skeleton/Armature
    //     Id: ...
    //     Sockets:
    //       - name: RightHand
    //         bone: Hand_R
    //         position: [0, 0.1, 0]
    //
    // 뼈대 항목만 뼈 마스크 목록 `Masks` 를 든다(D-464). 다른 종류에 있으면 파일 전체의 실패다. 빈 목록은 적지 않는다.
    //
    //     Masks:
    //       - name: UpperBody
    //         bones:
    //           - bone: Spine
    //             weight: 1
    struct AssetSubEntry
    {
        String key;
        AssetId id;
        // 0 이면 없다(메시·재질, 또는 해시를 적기 전의 메타).
        UInt64 contentHash = 0;
        Bool hasTextureOptions = false;
        TextureImportOptions textureOptions;
        // 뼈대·메시만(D-460). 사용자가 고친 것이라 모델을 다시 읽어도 같은 키면 지킨다.
        Array<ModelSocket> sockets;
        // 뼈대만(D-464). 사용자가 고친 것이라 모델을 다시 읽어도 같은 키면 지킨다.
        Array<ModelBoneMask> masks;
        // 애니메이션만(D-462). 없으면 가져온 그대로(전 구간·반복·이벤트 없음)다. `clip.source` 가 차 있으면 나눈 클립이라 다시 가져와도 남는다.
        Bool hasClip = false;
        ModelClipSettings clip;
    };

    // 하위 키를 종류와 이름으로 나눈다. 모르는 종류이거나 이름이 비면 false 다.
    Bool SplitSubAssetKey(std::string_view key, AssetType& type, std::string_view& name);

    struct AssetMetaFile
    {
        UInt32 version = 1;
        AssetId id;
        AssetType type = AssetType::Unknown;
        // `Texture` 일 때만 뜻이 있다. 비어 있으면 Sprite 블록이 없었다는 뜻이다.
        AssetId spriteId;
        Bool hasTextureOptions = false;
        TextureImportOptions textureOptions;
        Bool hasSpriteOptions = false;
        SpriteImportOptions spriteOptions;
        // `Audio` 일 때만 뜻이 있다(`Audio.ImportOptions`, D-197).
        Bool hasAudioOptions = false;
        AudioImportOptions audioOptions;
        // `Font` 일 때만 뜻이 있다(`Font.ImportOptions`, D-200).
        Bool hasFontOptions = false;
        FontImportOptions fontOptions;
        // `FontFamily` 일 때만 뜻이 있다(`FontFamily.ImportOptions`, D-225).
        Bool hasFontFamilyOptions = false;
        FontFamilyOptions fontFamilyOptions;
        // `StringTable` 일 때만 뜻이 있다(`StringTable.ImportOptions`, D-226).
        Bool hasStringTableOptions = false;
        StringTableOptions stringTableOptions;
        // `Model` 일 때만 뜻이 있다(`Model.ImportOptions`·`Model.SubAssets`, D-391·D-392). 하위 목록이 없으면(`hasSubAssets` 거짓) 에디터의 스캔이
        // 모델을 읽어 채운다. 게임은 이 목록만 보고 하위 레코드를 세운다 - 모델을 다시 읽지 않는다.
        Bool hasModelOptions = false;
        ModelImportOptions modelOptions;
        Bool hasSubAssets = false;
        Array<AssetSubEntry> subAssets;
    };

    struct AssetMetaError
    {
        // 0 이면 파일 자체를 열지 못한 것이다.
        std::size_t line = 0;
        String message;
    };

    // 읽는다. 실패하면 `result` 는 손대지 않고 `error` 를 채운다. `Id` 가 없거나 읽히지 않는 것,
    // `Type` 이 모르는 이름인 것, 이미지 타입인데 `Sprite.Id` 가 없는 것이 실패다.
    Bool LoadAssetMetaFile(IPlatform& platform, const char* utf8Path, AssetMetaFile& result, AssetMetaError& error);
    Bool ParseAssetMetaFile(const char* text, std::size_t length, AssetMetaFile& result, AssetMetaError& error);

    // 쓴다. 글자를 만들지 못하거나(옵션 값이 적히지 않음, 이미지인데 스프라이트 아이디가 빔) 파일을 만들지 못하면 false 다.
    // **적히지 않는 값은 파일을 쓰지 않는다** - 빈 블록을 적으면 다음 읽기가 기본값으로 대신해 옵션이 조용히 사라진다.
    Bool SaveAssetMetaFile(IPlatform& platform, const char* utf8Path, const AssetMetaFile& meta);
    Bool FormatAssetMetaFile(const AssetMetaFile& meta, String& text);
    // 위의 것을 감싼다. 실패하면 빈 글자다 - 테스트와 같이 실패가 곧 검사인 자리용이고, 파일에 쓰는 쪽은 bool 판을 쓴다.
    String FormatAssetMetaFile(const AssetMetaFile& meta);
}

#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/String.h>

namespace JBro
{
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
    // 임포트 옵션(`ImportOptions:` 블록)은 이 구조체가 들지 않는다. 타입별 로더가 같은 파일을 리플렉션으로 읽는다
    // (asset-plan §2.7). 모르는 키는 읽을 때 건너뛰지만, **쓰기는 이 넷만 적으므로** 옵션이 있는 메타를 이것으로
    // 덮어쓰면 옵션이 사라진다 - 쓰는 쪽은 새로 만드는 메타에만 쓴다.
    struct AssetMetaFile
    {
        std::uint32_t version = 1;
        AssetId id;
        AssetType type = AssetType::Unknown;
        // `Texture` 일 때만 뜻이 있다. 비어 있으면 Sprite 블록이 없었다는 뜻이다.
        AssetId spriteId;
    };

    struct AssetMetaError
    {
        // 0 이면 파일 자체를 열지 못한 것이다.
        std::size_t line = 0;
        String message;
    };

    // 읽는다. 실패하면 `result` 는 손대지 않고 `error` 를 채운다. `Id` 가 없거나 읽히지 않는 것,
    // `Type` 이 모르는 이름인 것, 이미지 타입인데 `Sprite.Id` 가 없는 것이 실패다.
    bool LoadAssetMetaFile(const char* path, AssetMetaFile& result, AssetMetaError& error);
    bool ParseAssetMetaFile(const char* text, std::size_t length, AssetMetaFile& result, AssetMetaError& error);

    // 쓴다. 파일을 만들지 못하면 false 다.
    bool SaveAssetMetaFile(const char* path, const AssetMetaFile& meta);
    String FormatAssetMetaFile(const AssetMetaFile& meta);
}

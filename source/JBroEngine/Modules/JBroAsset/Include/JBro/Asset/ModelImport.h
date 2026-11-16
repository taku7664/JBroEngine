#pragma once

#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/String.h>

#include <string_view>

namespace JBro
{
    class IPlatform;

    // **모델의 하위 에셋 목록을 파일에 맞춘다**(D-338, model-import-plan 3 단계). 모델을 읽어(`DecodeModel`) 메시마다 `Mesh/이름`,
    // `importMaterials` 면 재질마다 `Material/이름` 키를 세우고, 메타의 목록과 맞춘다 - **같은 키는 아이디를 지키고**(캔버스의 참조가 끊기지 않는다),
    // 새 키는 새 아이디, 사라진 키는 뺀다. 내장 그림(`Texture/`)은 5 단계다.
    //
    // 메타의 옵션(`modelOptions`, 없으면 기본값)으로 읽는다. 목록이 달라졌으면 `changed` 가 참이고 `meta.hasSubAssets` 는 늘 참이 된다.
    // 파일은 플랫폼이 연다(D-112) - `.gltf` 의 바깥 버퍼와 OBJ 의 `.mtl` 은 모델 폴더 기준으로 읽는다. 에디터의 임포트 경로이고 게임은 부르지 않는다.
    // 모델을 읽지 못하면 false 이고 메타는 그대로다.
    Bool RefreshModelSubAssets(IPlatform& platform, const char* assetRoot, std::string_view relativePath,
        AssetMetaFile& meta, Bool& changed, String& error);
}

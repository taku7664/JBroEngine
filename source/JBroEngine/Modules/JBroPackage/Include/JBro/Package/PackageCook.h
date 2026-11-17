#pragma once

#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Package/PackageWriter.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/String.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro::Package
{
    struct CookReport
    {
        UInt32 assets = 0;
        // 그림 파일과 모델의 내장 그림마다 하나다.
        UInt32 cookedTextures = 0;
        // 모델의 하위 메시마다 하나(model-import-plan 6 단계)다.
        UInt32 cookedMeshes = 0;
        // 미리 뜨기가 켜진 폰트마다 하나(D-232 4 단계)다.
        UInt32 bakedAtlases = 0;
        UInt64 blobBytes = 0;
        // 굽지 못한 에셋마다 한 줄(영어, 로그용)이다. 하나라도 있으면 쿡은 거짓이다.
        Array<String> failures;
    };

    // 느슨한 에셋 폴더의 에셋들을 패키지 블롭으로 굽는다(D-232, package-plan §2.4). 에셋마다 `Meta` 블롭을 두고, Texture 는 디코드한 RGBA8
    // (`CookedTexture`), Sprite 는 레코드만(메타·픽셀은 주인 Texture 의 것), 나머지는 원본(`Source`)이다. 미리 뜨기가 켜진 Font 는
    // 원본과 함께 미리 뜬 아틀라스(`FontAtlas`)를 둔다 - 게임이 폰트를 열 때 래스터화하지 않는다. `ids` 에 없는 주인·소유 레코드는
    // 따라 넣지 않는다 - 무엇을 넣을지는 부르는 쪽(참조 따라가기)이 정한다. 파일은 플랫폼이 연다(D-112).
    // **모델**은 메타만 두고 원본은 넣지 않는다(model-import-plan 6 단계). 모델을 한 번 읽어 하위 Mesh 마다 엔진이 바로 읽는 메시(`CookedMesh`)를,
    // 내장 그림마다 디코드한 RGBA8(`CookedTexture`)을 그 하위 아이디로 둔다 - 게임은 모델 디코더를 돌리지 않고 곁 파일(`.bin`·`.mtl`)도 찾지 않는다.
    // 재질은 아직 레코드만이다(재질 단계).
    Bool CookAssets(IPlatform& platform, const AssetRegistry& registry, const char* assetRoot, ArrayView<const AssetId> ids,
        PackageWriter& writer, CookReport& report);
}

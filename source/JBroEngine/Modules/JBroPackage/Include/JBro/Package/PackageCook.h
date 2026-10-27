#pragma once

#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/MaterialShader.h>
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
        UInt32 cookedTextures = 0;
        // 미리 뜨기가 켜진 폰트마다 하나(D-232 4 단계)다.
        UInt32 bakedAtlases = 0;
        // 구운 머티리얼 셰이더마다 하나(D-347)다.
        UInt32 compiledShaders = 0;
        UInt64 blobBytes = 0;
        // 굽지 못한 에셋마다 한 줄(영어, 로그용)이다. 하나라도 있으면 쿡은 거짓이다.
        Array<String> failures;
    };

    // 느슨한 에셋 폴더의 에셋들을 패키지 블롭으로 굽는다(D-232, package-plan §2.4). 에셋마다 `Meta` 블롭을 두고, Texture 는 디코드한 RGBA8
    // (`CookedTexture`), Sprite 는 레코드만(메타·픽셀은 주인 Texture 의 것), 나머지는 원본(`Source`)이다. 미리 뜨기가 켜진 Font 는
    // 원본과 함께 미리 뜬 아틀라스(`FontAtlas`)를 둔다 - 게임이 폰트를 열 때 래스터화하지 않는다. `ids` 에 없는 주인·소유 레코드는
    // 따라 넣지 않는다 - 무엇을 넣을지는 부르는 쪽(참조 따라가기)이 정한다. 파일은 플랫폼이 연다(D-112).
    //
    // Shader(`.hlsl`, D-347)는 원본을 싣지 않고 `compiler` 로 두 자리(PostProcess·Sprite)를 모두 구워 본 묶음(`CompiledShader`)만 싣는다 - 게임은 굽지
    // 않는다. 한 자리도 굽지 못했거나 컴파일러가 없거나 Vulkan 의 SPIR-V 를 굽지 못하면 그 셰이더는 실패다.
    Bool CookAssets(IPlatform& platform, const AssetRegistry& registry, const char* assetRoot, ArrayView<const AssetId> ids,
        PackageWriter& writer, CookReport& report, IShaderCompiler* compiler = nullptr);
}

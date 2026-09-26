#pragma once

#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/String.h>

namespace JBro::Package
{
    struct CollectReport
    {
        // 레지스트리에 없는 아이디를 가리킨 자리마다 한 줄(영어)이다. 빌드를 멈추지 않는다 - 지운 에셋을 가리키는 옛 캔버스가 흔하다.
        Array<String> warnings;
    };

    // **참조를 따라 게임이 쓰는 에셋을 모은다**(D-227, package-plan §2.4). `seeds` 에서 너비 우선으로 간다: 에셋마다 메타(패밀리의 칸이 여기 있다)와,
    // 캔버스면 원문에서 32 자리 16 진수 아이디를 읽어 레지스트리에 있는 것을 더한다. 이미지의 Texture 와 Sprite 는 한 짝으로 간다.
    // 글자에서 아이디를 읽는 것은 캔버스의 모양(컴포넌트·필드 이름)을 몰라도 되게 하기 위해서다 - 새 컴포넌트가 에셋 필드를 더해도 여기를 고치지 않는다.
    // `out` 은 찾은 차례이고 겹치지 않는다. 씨가 레지스트리에 없으면 경고다. 이름에서 만든 아이디(버전 8, 빌트인)는 싸 가지 않고 경고하지 않는다.
    void CollectAssets(IPlatform& platform, const AssetRegistry& registry, const char* assetRoot, ArrayView<const AssetId> seeds,
        Array<AssetId>& out, CollectReport& report);

    // 글자 안의 32 자리 16 진수 낱말(앞뒤가 16 진수 글자가 아닌 것)을 아이디로 읽어 더한다. 하이픈 표기는 보지 않는다.
    void FindIdsInText(const char* text, std::size_t length, Array<AssetId>& out);
}

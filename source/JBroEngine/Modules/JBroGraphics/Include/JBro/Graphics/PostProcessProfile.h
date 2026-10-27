#pragma once

#include <JBro/Graphics/PostProcess.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/String.h>
#include <JBro/Types/UInt.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    // **후처리 프로필의 본문**(`.jpost`, D-344, postprocess-plan §2.4). 효과 목록이고, 항목마다 종류·켜짐·덮어쓴 필드만 적는다:
    //
    //   Effects:
    //     - Type: Bloom
    //       Enabled: true
    //       Parameters:
    //         intensity: 1.5
    //
    // **적히지 않은 필드는 그 종류의 기본값이다.** 필드를 덮어쓰는 것(인스펙터의 필드 앞 체크)이 곧 그 필드를 파일에 적는 것이다 - 볼륨이 프로필을
    // 섞을 때(10 단계) 덮어쓴 필드만 섞는다. 효과의 뜻은 효과 종류 표가 안다. 그 표에 없는 종류는 걸지 않고 읽은 그대로 되쓴다(스칼라 값만).

    // 프로필의 항목 하나다.
    struct PostProcessProfileEntry
    {
        // 파일의 `Type` 이다.
        String typeName;
        // 효과 종류 표의 번호다. 표에 없는 종류면 `PostProcessEffectRegistry::NotFound` 다.
        UInt32 type = PostProcessEffectRegistry::NotFound;
        Bool enabled = true;
        // 덮어쓴 필드다. 비트 번호가 파라미터 표(`PostProcessEffectType::parameters`)의 필드 차례다. 64 필드까지다.
        UInt64 overrides = 0;
        // 종류의 기본값 위에 덮어쓴 값을 얹은 블록이다(`blockSize` 바이트). 표에 없는 종류면 비어 있다.
        Array<std::byte> block;
        // 표에 없는 종류의 `Parameters` 다. 되쓸 때 그대로 적는다.
        Array<String> unknownKeys;
        Array<String> unknownValues;
    };

    struct PostProcessProfileDocument
    {
        Array<PostProcessProfileEntry> entries;
    };

    // 본문을 읽는다. 빈 글자는 빈 프로필이다(새로 만든 파일). YAML 이 깨졌으면 거짓이고 `error` 에 줄과 까닭을 적는다.
    // 아는 종류의 모르는 필드·읽히지 않는 값은 버리고 `warnings` 에 적는다 - 프로필 하나가 틀렸다고 화면이 통째로 꺼지지 않는다.
    Bool ParsePostProcessProfile(const char* text, std::size_t length, PostProcessProfileDocument& document, String* error = nullptr,
        Array<String>* warnings = nullptr);

    // 본문을 적는다. 덮어쓴 필드만 적고, 차례는 파라미터 표의 차례다.
    String WritePostProcessProfile(const PostProcessProfileDocument& document);

    // 그 종류의 기본값으로 선 항목이다. 덮어쓴 필드는 없다. 표에 없는 번호면 이름도 블록도 빈 항목이다.
    PostProcessProfileEntry MakePostProcessProfileEntry(UInt32 type);

    // 필드 하나의 덮어쓰기를 켜거나 끈다. 끄면 그 필드가 기본값으로 돌아간다. 표에 없는 종류이거나 필드 번호가 표 밖이면 거짓이다.
    Bool SetPostProcessOverride(PostProcessProfileEntry& entry, UInt32 field, Bool overridden);
    // 파라미터 표에서 파일에 적히는 필드(`serialize`)인가. 숨긴 필드(렌더러 핸들 따위)는 덮어쓸 수 없다.
    Bool IsPostProcessFieldEditable(UInt32 type, UInt32 field);
}

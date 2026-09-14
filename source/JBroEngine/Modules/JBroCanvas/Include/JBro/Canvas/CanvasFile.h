#pragma once

#include <JBro/Types/String.h>

#include <cstddef>

namespace JBro
{
    class Canvas;

    // 캔버스를 `.jcanvas` 로 적는다. 기존 엔진의 확장자와 키 이름·들여쓰기 관례를 따르되,
    // **그 쪽 씬 파일을 읽지는 않는다** — 거기 있는 컴포넌트(`Square2D`, `Text2D`, `Light2D`,
    // `AudioPlayer` …)가 이 엔진에 아직 하나도 없어서 읽어 봐야 거의 다 버려진다.
    // 컴포넌트가 채워진 뒤에 마이그레이션을 따로 본다.
    //
    // 기존 엔진과 일부러 다르게 한 것 둘:
    //
    // - **`Type:` 을 컴포넌트의 맨 앞에 적는다.** 기존 엔진은 필드들 뒤에 적어서, 읽는 쪽이
    //   무슨 타입인지 알기 전에 값들을 먼저 지나가야 했다. 그 파일을 읽지 않기로 했으므로
    //   읽기 좋은 순서로 둔다.
    // - **`Transform2D` 를 따로 빼지 않는다.** 기존 엔진은 그것만 `Components` 밖에 두었는데,
    //   여기서는 트랜스폼도 그냥 컴포넌트다. 예외가 하나 줄면 규칙도 하나 준다.
    //
    // 적히는 것은 프로퍼티 표가 내놓는 것뿐이고, `serialize` 가 꺼진 필드는 빠진다.
    struct CanvasFileError
    {
        String message;
        // 실패한 자리를 사람이 찾을 수 있게 남긴다. 없으면 빈 문자열이다.
        String objectName;
        String typeName;
        String fieldName;
    };

    // 캔버스를 텍스트로 적는다. 실패하면 text 는 손대지 않고 error 를 채운다.
    bool WriteCanvasText(Canvas& canvas, String& text, CanvasFileError& error);
    // 위와 같고, 결과를 파일로 쓴다.
    bool SaveCanvasFile(Canvas& canvas, const char* path, CanvasFileError& error);

    // 텍스트를 **빈 캔버스에** 읽어 넣는다. 이미 내용이 있으면 거절한다 —
    // 섞으면 무엇이 파일에서 온 것인지 알 수 없고, 되돌릴 방법도 없다.
    //
    // 이름으로 컴포넌트를 만들려면 그 타입이 `ComponentRegistry` 에 있어야 한다.
    // 없는 타입을 만나면 조용히 빠뜨리지 않고 멈춘다.
    //
    // **파일에 있는데 코드에 없는 필드는 실패다.** 그 반대(코드에 있는데 파일에 없는 필드)는
    // 기본값으로 두고 넘어간다 — 필드를 더한 것은 예전 씬을 못 읽을 이유가 아니지만,
    // 필드를 지운 것은 그 씬이 들고 있던 값을 버린다는 뜻이라 사람이 알아야 한다.
    bool ReadCanvasText(Canvas& canvas, const char* text, std::size_t length, CanvasFileError& error);
    bool LoadCanvasFile(Canvas& canvas, const char* path, CanvasFileError& error);
}

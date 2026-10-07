#pragma once

#include <JBro/Canvas/Layer.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

#include <cstddef>
#include <cstdint>
#include <JBro/Types/Bool.h>

namespace JBro
{
    class Canvas;
    namespace Object
    {
        class GameObject;
    }

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
    // 오브젝트마다 빌트인은 `Components`, 스크립트는 `Scripts` 에 적는다(D-302). 스크립트가 없으면 `Scripts` 는 적지 않는다.
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
    // **파일은 여기서 열지 않는다**(D-112). 부르는 쪽(에디터·호스트)이 `IPlatform` 으로 읽고 쓴다 - 이 모듈은 플랫폼을
    // 보지 않는다.
    //
    // `Editor` 는 에디터가 저장하는 모양이다 - 에디터에서만 뜻이 있는 오브젝트 플래그(`EditorOnlyObjectFlags`)도 적는다.
    // `Package` 는 게임으로 묶을 때 쓴다 - 그 비트를 지우고 적는다(D-163, 사용자 결정 2026-09-22).
    enum class CanvasWriteMode : std::uint8_t
    {
        Editor,
        Package
    };
    Bool WriteCanvasText(Canvas& canvas, String& text, CanvasFileError& error,
        CanvasWriteMode mode = CanvasWriteMode::Editor);

    // 텍스트를 **빈 캔버스에** 읽어 넣는다. 이미 내용이 있으면 거절한다 —
    // 섞으면 무엇이 파일에서 온 것인지 알 수 없고, 되돌릴 방법도 없다.
    //
    // 이름으로 만든다. `Components` 는 `ComponentRegistry` 에서, `Scripts` 는 `ScriptRegistry` 에서 찾는다.
    // **모르는 스크립트는 읽은 그대로 들고 있다가**(`Canvas::AddUnresolvedScript`) 저장할 때 되쓴다(D-264).
    // 처음에는 멈췄다 - 스크립트 DLL 을 아직 빌드하지 않은 프로젝트가 스크립트가 든 캔버스를 통째로 열지 못했다.
    // 게임으로 묶는 쓰기(`Package`)는 그런 스크립트를 만나면 실패한다.
    // **모르는 컴포넌트는 실패다** - 빌트인은 엔진이 모두 안다. 스크립트가 `Components` 에 섞인 파일(D-302 전)도 실패다. 옮겨 읽지 않는다.
    //
    // **파일에 있는데 코드에 없는 필드는 실패다.** 그 반대(코드에 있는데 파일에 없는 필드)는
    // 기본값으로 두고 넘어간다 — 필드를 더한 것은 예전 씬을 못 읽을 이유가 아니지만,
    // 필드를 지운 것은 그 씬이 들고 있던 값을 버린다는 뜻이라 사람이 알아야 한다.
    Bool ReadCanvasText(Canvas& canvas, const char* text, std::size_t length, CanvasFileError& error);

    // **레이어 에셋**(`.jlayer`, D-287, 기존 `LayerSerializer`). 레이어 하나와 그 위의 오브젝트를 캔버스 파일과 같은 모양으로 적는다:
    // `Version`·`Layer`(캔버스 파일의 `Layers` 항목과 같은 함수로 쓴 노드, 캔버스 안 번호와 원본 에셋은 빠진다)·`Objects`(부모는 파일 안 번호).
    // 레이어 밖의 오브젝트를 가리키는 참조 필드는 비어 있는 것으로 적힌다. 파일은 여기서 열지 않는다(D-112).
    Bool WriteLayerText(Canvas& canvas, LayerId layer, String& text, CanvasFileError& error);

    // 레이어 에셋을 캔버스의 **맨 위에 새 레이어로** 읽어 넣는다(기존 엔진과 같다). 오브젝트는 새로 만들어지고 파일 안 참조는 그 오브젝트끼리 이어진다 -
    // 같은 에셋을 두 번 넣으면 서로 다른 두 벌이다(기존 엔진은 오브젝트 GUID 를 되살려 두 번 넣지 못했다). 원본 에셋 표시는 부르는 쪽이 단다.
    // 읽다 실패하면 만든 것을 모두 거두고 거짓이다 - 캔버스는 읽기 전 그대로다.
    Bool ReadLayerText(Canvas& canvas, const char* text, std::size_t length, LayerId& created, CanvasFileError& error);
    // 글자가 레이어 에셋의 모양인가. 첫 키 둘만 본다 - 메뉴가 프레임마다 묻는다.
    Bool IsLayerText(const char* text);

    // **오브젝트 클립보드**(D-313, 기존 `Serialization::SerializeObjects`). 복사는 고른 나무를 캔버스 파일과 같은 모양의 YAML 로 적어
    // 시스템 클립보드에 둔다 - 다른 에디터 창·글 편집기로 옮겨도 읽힌다. 맨 앞 키 `ObjectsVersion` 이 표식이다.
    // 나무 안을 가리키는 참조는 파일 안 번호로, 나무 밖을 가리키는 참조는 이번 실행의 번호(`@123`)로 적는다 - 붙여넣은 사본도 원본과 같은
    // 바깥을 잡는다(D-233). `session` 은 이 프로세스를 가리키는 글자이고, 읽는 쪽이 다르면 `@` 번호를 버린다.
    // `roots` 는 서로의 조상이 아니어야 한다(고른 것 중 맨 위 것들). 뜨지 못하는 컴포넌트가 하나라도 있으면 거짓이고 `text` 는 그대로다.
    Bool WriteObjectsText(Canvas& canvas, const Array<Object::GameObject*>& roots, const char* session, String& text, CanvasFileError& error);
    Bool IsObjectsText(const char* text);

    // 어디에 붙이는가. `parent` 가 있으면 그 아래, 레이어는 `layer` → 부모의 레이어 → 글자에 적힌 레이어(이 캔버스에 있으면) → 기본 레이어 순이다.
    struct ObjectsTextTarget
    {
        Object::GameObject* parent = nullptr;
        LayerId layer = InvalidLayerId;
        const char* session = nullptr;
    };

    // 오브젝트 클립보드 글자를 새 오브젝트로 읽어 넣는다. 만든 뿌리를 글자의 차례로 `roots` 에 둔다.
    // 읽다 실패하면 만든 것을 모두 거두고 거짓이다 - 캔버스는 읽기 전 그대로다.
    Bool ReadObjectsText(Canvas& canvas, const char* text, std::size_t length, const ObjectsTextTarget& target,
        Array<Object::GameObject*>& roots, CanvasFileError& error);
    // ── 스크립트 핫 리로드의 두 걸음(cpp-script-plan §3.5, D-300) ────────────────────────────
    //
    // DLL 을 내리기 전에 `KeepScriptsAsText` 로 이름으로 붙인 스크립트를 모두 글자로 떠서 모르는 스크립트(D-264)로 옮기고,
    // 새 DLL 을 실은 뒤 `ResolveKeptScripts` 로 이름을 아는 것을 되살린다. 그 사이 스크립트의 값은 캔버스의 모르는 스크립트로 산다 -
    // 새 DLL 이 그 타입을 모르거나 싣기가 실패해도 값은 남고, 저장하면 그대로 적힌다.

    // 이름으로 붙인 스크립트를 모두 떠서 모르는 스크립트로 옮기고 뗀다. 자리(스크립트 목록의 차례)와 스크립트 번호가 따라간다.
    // 오브젝트 참조는 이번 실행의 번호(`@123`)로 뜬다. **하나라도 뜨지 못하면 아무것도 바꾸지 않고 거짓이다** - 되살릴 값이 없는 채로 떼지 않는다.
    // 참이면 스크립트 풀까지 비어 있다(`Canvas::ReleaseModuleScripts`) - 그 뒤에 DLL 을 내려도 된다. 뜬 개수를 `kept` 에 둔다.
    Bool KeepScriptsAsText(Canvas& canvas, std::size_t& kept, CanvasFileError& error);

    // 되살리면서 값을 잇지 못한 것이다. 되살리기는 멈추지 않는다 - 스크립트는 붙고 그 필드만 기본값이다.
    struct ScriptResolveNote
    {
        enum class Kind : std::uint8_t
        {
            // 새 코드에 그 이름의 필드가 없다. 값은 버렸다.
            FieldDropped,
            // 필드는 있는데 값을 읽지 못했다(타입이 바뀌었다). 기본값으로 두었다.
            FieldUnreadable,
            // 타입은 아는데 붙이지 못했다. 모르는 스크립트로 남겼다.
            NotAttached,
        };
        Kind kind = Kind::FieldDropped;
        String objectName;
        String typeName;
        String fieldName;
    };

    // 모르는 스크립트 가운데 이제 이름을 아는 것을 되살린다. 같은 자리에, 들고 있던 스크립트 번호로 붙인다.
    // 필드는 이름이 맞고 읽히는 것만 잇는다(사용자 결정 2026-09-29: "맞는 것만 잇고 나머지는 경고"). 파일을 읽는 `ReadCanvasText` 와 달리
    // 코드에 없는 필드로 멈추지 않는다. 되살린 개수다. 모르는 채인 것은 그대로 남는다.
    std::size_t ResolveKeptScripts(Canvas& canvas, Array<ScriptResolveNote>& notes);
}

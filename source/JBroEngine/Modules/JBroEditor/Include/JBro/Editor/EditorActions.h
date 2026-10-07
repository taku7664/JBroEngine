#pragma once

#include <JBro/Canvas/Layer.h>
#include <JBro/Editor/Command/ComponentAddress.h>
#include <JBro/Core/Core.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

#include <cstdint>

namespace JBro
{
    class EditorApplication;
    namespace Object
    {
        class GameObject;
    }

    // 새로 놓는 오브젝트가 **어디에, 어느 레이어에** 가는가(D-168, 기존 `DrawAddObjectMenu` 의
    // `spawnWorldPos`·`layer`). 비워 두면 원점이고, 레이어는 `ResolveTargetLayer` 가 정한다.
    struct ObjectPlacement
    {
        Bool hasPosition = false;
        // 앞에서부터 트랜스폼의 `position` 에 들어간다. 2D 면 앞의 둘만 쓰인다.
        Float position[3] = {};
        LayerId layer = InvalidLayerId;
    };

    // **에디터가 오브젝트에 하는 일을 한 벌로 모은 것**이다(D-132).
    //
    // 기존 엔진의 `EditorGuiActions`(476줄) 자리다. 오브젝트 추가·복사·붙여넣기·삭제·
    // 부모 해제는 계층에서도, 캔버스 뷰에서도, 메뉴에서도, 단축키에서도 한다. 같은 일을
    // 부르는 자리마다 다시 쓰면 그중 하나만 고쳐지고 나머지는 옛 모양으로 남는다 —
    // 실제로 계층 패널이 자기 안에 전부 들고 있었고, 캔버스 뷰를 만들 때 그대로 한 벌을
    // 더 쓸 뻔했다.
    //
    // **여기 있는 것은 전부 커맨드를 거친다**(§11.5). 값을 직접 쓰는 길은 없다.
    namespace EditorActions
    {
        // ── 하는 일 ──────────────────────────────────────────────────────
        //
        // 메뉴에서도 단축키에서도 부른다. 성공하면 참이다.

        // `parent` 가 널이면 뿌리에 만든다. 만든 것을 고른 것으로 삼는다.
        Object::GameObject* CreateObject(EditorApplication& editor, Object::GameObject* parent,
            const ObjectPlacement& placement = {});

        // **새로 놓는 것이 어느 레이어로 가는가의 한 가지 규칙**이다(D-168, 기존
        // `ResolveTargetLayer`). 부모가 있으면 부모의 레이어, 없으면 고른 것의 레이어, 레이어를 골랐으면
        // 그 레이어(D-279), 셋 다 없으면 `InvalidLayerId`(= 캔버스 기본 레이어)다. 메뉴와 단축키가 함께 쓴다 -
        // 규칙이 갈리면 같은 손짓이 들어온 자리마다 다른 칸에 오브젝트를 만든다.
        LayerId ResolveTargetLayer(EditorApplication& editor, Object::GameObject* parent);

        // 컴포넌트나 스크립트 하나를 붙인다(D-180, D-302). 붙일 수 없는 타입이면 아무 일도 하지 않고 거짓이다.
        Bool AddComponent(EditorApplication& editor, Object::GameObject& object, NameId typeName);
        // 부모를 떼어 뿌리 맨 뒤로 올린다. 이미 뿌리면 거짓이다.
        Bool Unparent(EditorApplication& editor, Object::GameObject& object);
        // 고른 것을 지운다. **맨 위 것들만** 지운다 - 부모를 지우면 자식은 따라 사라지므로
        // 둘 다 대상으로 삼으면 이미 없는 것을 한 번 더 지우려 든다.
        Bool DeleteSelection(EditorApplication& editor);
        Bool DeleteObject(EditorApplication& editor, Object::GameObject& object);

        // **붙일 수 있는 컴포넌트 목록**이다(D-180). 인스펙터의 드롭다운과 오브젝트 메뉴의
        // `컴포넌트 추가` 가 같은 목록을 본다 - 한쪽에만 회색 규칙이 있으면, 목록에서 막힌
        // 것이 메뉴에서는 눌리고 그 뒤로 조용히 아무 일도 일어나지 않는다.
        //
        // 차례는 **갈래로 묶고 갈래 안에서는 이름 순**이다. 갈래끼리의 차례는 이름 순이라
        // 실행할 때마다 같다.
        struct AddComponentList
        {
            Array<NameId>      typeNames;
            // 화면에 보이는 타입 이름이다. 이름표가 들고 있는 글자를 가리킨다.
            Array<const char*> names;
            // 항목마다의 갈래 이름이다. 이미 번역되어 있다.
            Array<const char*> groups;
            // 같은 갈래의 번역하지 않은 이름(`Rendering`)이다. 가이드 포커스 표식이 쓴다 - 번역된 이름은 언어마다 바뀐다.
            Array<const char*> categories;
            // 거짓이면 이미 붙어 있어 더 붙일 수 없다.
            Array<Bool>        addable;
        };
        // 빌트인 컴포넌트를 갈래로 묶고, 마지막 `스크립트` 갈래에 스크립트 DLL 이 등록한 것을 이름 순으로 담는다(D-308).
        // 엔진 안에서는 둘이 따로 살지만(D-302) 사용자에게는 한 목록이다.
        void BuildAddComponentList(const Object::GameObject& object, AddComponentList& out);

        // 이미 열려 있는 메뉴 안에 `컴포넌트 추가` 하위 메뉴를 그린다(D-180, 기존
        // `DrawAddComponentMenu`). 붙였으면 참이다.
        Bool DrawAddComponentMenu(EditorApplication& editor, Object::GameObject& object);
        // **컴포넌트 하나의 메뉴 항목 한 벌**이다(D-298). 인스펙터의 컴포넌트 머리 메뉴와 오브젝트 메뉴의 컴포넌트 하위 메뉴가 같은 것을
        // 그린다 - 복사·붙여넣기·값 붙여넣기, 그 타입에 더한 항목(D-220), 제거. `사용` 과 자리 옮기기는 없다(D-307).
        // 스크립트도 같은 한 벌이다(`kind`, D-302) - 자리 옮기기는 제 목록 안이고, 타입에 더한 항목은 컴포넌트만 갖는다.
        // 이미 열린 메뉴 안에서 부른다. **슬롯 배열을 바꿨으면 거짓이다** - 부르는 쪽은 그 프레임에 슬롯을 더 읽지 않는다.
        Bool DrawComponentMenuItems(EditorApplication& editor, Object::GameObject& object, std::size_t slot,
            const ObjectPlacement& placement = {}, AttachedKind kind = AttachedKind::Component);


        // 오브젝트 하나를 두고 여는 메뉴 한 벌이다(D-170). 계층의 줄과 캔버스 뷰에서
        // 오브젝트를 우클릭한 자리가 같은 것을 쓴다 - 기존 엔진도 두 화면의 메뉴가 같다.
        // **`placement` 는 `자식 오브젝트 추가` 가 쓸 자리**다(캔버스 뷰에서 누른 곳).
        //
        // **거짓이면 그 오브젝트가 더 이상 없을 수 있다.** 삭제·붙여넣기·부모 해제가
        // 계층을 그 자리에서 바꾸므로, 부르는 쪽은 그 프레임에 그 오브젝트를 더 그리지 않는다.
        // 메뉴를 열지 못했으면(우클릭이 아니면) 참이다.
        Bool DrawObjectMenu(EditorApplication& editor, Object::GameObject& object,
            const ObjectPlacement& placement = {});

        // 빈자리(계층의 배경, 캔버스 뷰의 빈 곳)에서 여는 메뉴 한 벌이다.
        // `추가`·`붙여넣기` 로, 둘 다 뿌리에 붙는다. **무언가 바뀌었으면 참이다** -
        // 부르는 쪽은 그 프레임에 그 오브젝트를 더 그리지 않는다.
        // `placement` 로 캔버스 뷰가 **오른쪽 단추를 누른 자리**를 넘긴다(D-168).
        Bool DrawBackgroundMenu(EditorApplication& editor,
            const ObjectPlacement& placement = {});

        // ── 그리는 차례 (D-296) ──────────────────────────────────────────
        //
        // 같은 레이어·같은 `renderOrder` 끼리의 차례(`drawSequence`)를 옮긴다. 오브젝트의 묶음은 첫 `SpriteRenderer2D`(없으면 첫 `Text2D`)의
        // `renderOrder` 이고, 그 오브젝트의 그 묶음 컴포넌트가 한 덩어리로 움직인다. 앞으로는 바로 위의 남의 것 하나를 넘고, 맨 앞은 묶음의 맨 위다.
        // 옮긴 뒤 묶음 전체에 차례를 다시 매기고(맨 위 0, 아래로 -1 씩) 그 바뀐 것들을 커맨드 하나로 쓴다.
        enum class DrawOrderMove : std::uint8_t
        {
            Forward,
            ToFront,
            Backward,
            ToBack,
        };
        // 할 수 없으면 까닭(번역된 글자), 할 수 있으면 nullptr 이다.
        const char* WhyNoDrawOrder(EditorApplication& editor, Object::GameObject& object, DrawOrderMove move);
        Bool MoveDrawOrder(EditorApplication& editor, Object::GameObject& object, DrawOrderMove move);
    }
}

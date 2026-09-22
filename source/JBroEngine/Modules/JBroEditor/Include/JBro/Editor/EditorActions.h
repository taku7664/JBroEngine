#pragma once

#include <JBro/Canvas/Layer.h>
#include <JBro/Core/Core.h>

namespace JBro
{
    class EditorApplication;
    class GameObject;

    // 새로 놓는 오브젝트가 **어디에, 어느 레이어에** 가는가(D-168, 기존 `DrawAddObjectMenu` 의
    // `spawnWorldPos`·`layer`). 비워 두면 원점이고, 레이어는 `ResolveTargetLayer` 가 정한다.
    struct ObjectPlacement
    {
        bool hasPosition = false;
        // 앞에서부터 트랜스폼의 `position` 에 들어간다. 2D 면 앞의 둘만 쓰인다.
        float position[3] = {};
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
        GameObject* CreateObject(EditorApplication& editor, GameObject* parent,
            const ObjectPlacement& placement = {});

        // **새로 놓는 것이 어느 레이어로 가는가의 한 가지 규칙**이다(D-168, 기존
        // `ResolveTargetLayer`). 부모가 있으면 부모의 레이어, 없으면 고른 것의 레이어,
        // 둘 다 없으면 `InvalidLayerId`(= 캔버스 기본 레이어)다. 메뉴와 단축키가 함께 쓴다 -
        // 규칙이 갈리면 같은 손짓이 들어온 자리마다 다른 칸에 오브젝트를 만든다.
        LayerId ResolveTargetLayer(EditorApplication& editor, GameObject* parent);
        // 부모를 떼어 뿌리 맨 뒤로 올린다. 이미 뿌리면 거짓이다.
        bool Unparent(EditorApplication& editor, GameObject& object);
        // 고른 것을 지운다. **맨 위 것들만** 지운다 - 부모를 지우면 자식은 따라 사라지므로
        // 둘 다 대상으로 삼으면 이미 없는 것을 한 번 더 지우려 든다.
        bool DeleteSelection(EditorApplication& editor);
        bool DeleteObject(EditorApplication& editor, GameObject& object);

        // ── 메뉴 항목 ────────────────────────────────────────────────────
        //
        // 이미 열려 있는 메뉴 안에서 부른다. 항목을 그리고, 골렸으면 그 일을 한 뒤 참이다.
        // **회색으로 보이는 규칙도 여기 있다** - 할 수 없는 것이 눌리면 고장과 구분되지 않는다.

        bool DrawCreateObjectItem(EditorApplication& editor, GameObject* parent,
            const ObjectPlacement& placement = {});
        bool DrawCreateChildItem(EditorApplication& editor, GameObject& parent);
        bool DrawUnparentItem(EditorApplication& editor, GameObject& object);
        bool DrawCopyItem(EditorApplication& editor);
        bool DrawPasteItem(EditorApplication& editor);
        // 그 오브젝트의 자식으로 붙인다(D-166). 줄에서 연 메뉴가 쓴다.
        bool DrawPasteAsChildItem(EditorApplication& editor, GameObject& object);
        bool DrawDeleteItem(EditorApplication& editor, GameObject& object);

        // 빈자리(계층의 배경, 캔버스 뷰의 빈 곳)에서 여는 메뉴 한 벌이다.
        // `추가`·`붙여넣기` 로, 둘 다 뿌리에 붙는다. **무언가 바뀌었으면 참이다** -
        // 부르는 쪽은 그 프레임에 그 오브젝트를 더 그리지 않는다.
        // `placement` 로 캔버스 뷰가 **오른쪽 단추를 누른 자리**를 넘긴다(D-168).
        bool DrawBackgroundMenu(EditorApplication& editor,
            const ObjectPlacement& placement = {});
    }
}

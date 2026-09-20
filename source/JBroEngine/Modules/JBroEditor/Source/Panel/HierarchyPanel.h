#pragma once

#include <JBro/Editor/EditorPanel.h>

#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/String.h>

// 행 전체의 사각형을 받아 세 구역으로 나눈다. `ImRect` 가 그 타입이다.
#include <imgui_internal.h>

namespace JBro
{
    class GameObject;

    // 캔버스에 있는 오브젝트를 부모-자식 그대로 보여 주고, 고르면 인스펙터가 받는다.
    //
    // 줄은 공용 트리 위젯이 그린다(ProjectRule §11.1). `ImGui::TreeNodeEx` 로는
    // 줄의 남은 자리를 알 수 없어 이름 옆에 무엇도 얹을 수 없다.
    //
    // **끌어 놓기는 기존 엔진 `CLayerTool` 과 같은 세 구역이다**(D-128) - 행 위쪽은
    // 그 줄 앞에, 가운데는 그 줄의 자식으로, 아래쪽은 그 줄 뒤에. 줄 사이에 얇은 틈을
    // 따로 두면 맞추기 어렵고, 자식으로 넣을 자리와 형제로 넣을 자리가 화면에서 구분되지 않는다.
    class HierarchyPanel final : public EditorPanel
    {
    public:
        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        bool OnCreate(EditorApplication& editor) override;
        void OnDraw() override;
        EditorDock GetPreferredDock() const override { return EditorDock::Left; }

    private:
        // 행에서 무엇을 뜻하는 자리에 떨어뜨렸는가.
        enum class DropWhere : std::uint8_t
        {
            Before,
            Into,
            After
        };

        // 찾는 글자에 걸리는가. 자식이 걸리면 부모도 남는다 - 그러지 않으면
        // 걸린 자식이 갈 곳을 잃는다.
        bool Matches(const GameObject& object) const;
        // `parent` 가 널이면 뿌리이고 `indexInParent` 는 뿌리 순서에서의 자리다.
        void DrawObject(GameObject& object, GameObject* parent, std::size_t indexInParent);
        void DrawDragSource(GameObject& object);
        // 행 전체를 받는 자리로 만든다. 끌고 있는 것이 계층의 것일 때만 놓는다 -
        // 늘 놓으면 보이지 않는 단추가 줄의 클릭을 가로챈다.
        void DrawRowDropTarget(
            GameObject& object, GameObject* parent, std::size_t indexInParent,
            const ImRect& rowRect);
        // 옮길 것을 적어 둔다. 실제 이동은 프레임 끝에서 한다.
        void RecordDrop(GameObject& dragged, GameObject* parent, std::size_t insertAt);
        // 이번 프레임에 떨어진 것을 실제로 옮긴다.
        void FlushPendingMove();
        // 이 오브젝트가 **보여 달라고 한 것의 조상**인가. 그러면 이 프레임에 펼친다.
        bool IsOnRevealPath(const GameObject& object) const;
        // 줄의 우클릭 메뉴. **거짓이면 이 오브젝트가 더 이상 없을 수 있다** -
        // 삭제와 붙여넣기가 계층을 그 자리에서 바꾸므로, 부르는 쪽은 그 줄을 더 그리지 않는다.
        bool DrawObjectContextMenu(GameObject& object);

        EditorApplication* m_editor = nullptr;
        String m_filter;
        // 이번 프레임에 계층의 꾸러미를 끌고 있는가. 매 줄에서 다시 묻지 않는다.
        bool m_dragActive = false;
        // 뿌리 목록. 매 프레임 캔버스에서 받는다(D-128).
        Array<GameObject*> m_roots;

        // **프레임이 끝난 뒤에 옮긴다.** 그리는 도중에 부모를 바꾸면 지금 돌고
        // 있는 자식 배열이 그 자리에서 달라진다 - 순회가 죽은 자리를 읽는다.
        SafePtr<GameObject> m_dragged;
        SafePtr<GameObject> m_dropParent;
        // **끌어 온 것이 목록에서 빠지기 전**을 기준으로 센 자리다. 옮기기 직전에
        // 빠지는 몫을 뺀다 - 그러지 않으면 같은 부모 안에서 아래로 옮길 때 한 칸씩 어긋난다.
        std::size_t m_dropInsertAt = 0;
        bool m_dropToRoot = false;
        bool m_hasDrop = false;

        // **옮긴 것은 보여 준다**(기존 엔진의 계층 표시 요청). 접힌 부모 안으로
        // 끌어다 놓으면 그대로는 화면에서 사라져, 옮겨진 것인지 사라진 것인지
        // 알 수 없다. 다음 프레임에 조상들을 펼치고 그 줄로 스크롤한 뒤 비운다.
        SafePtr<GameObject> m_reveal;
    };
}

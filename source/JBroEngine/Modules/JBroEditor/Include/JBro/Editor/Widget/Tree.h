#pragma once

#include <JBro/Editor/Widget/Common.h>

#include <imgui_internal.h>

namespace JBro::Widget
{
    // 트리 한 줄을 그리면서 **행 전체의 사각형과 내용 영역을 따로 돌려준다**.
    //
    // `ImGui::TreeNodeEx` 로는 이것이 안 된다 - 이름표를 그리고 나면 그 줄의 남은
    // 자리를 알 방법이 없어서, 행에 썸네일·배지·버튼을 얹을 수 없다. 기존 엔진의
    // `ImTree` 가 같은 이유로 `TreeNodeBehavior` 를 통째로 다시 썼고, 여기 옮긴
    // 것은 그 코드다(ProjectRule §11.1).
    //
    // 원본과 다른 점은 이름과 네임스페이스뿐이다. 그리는 규칙은 손대지 않았다 -
    // 눈으로 맞춰 깎은 값들이라 조금만 달라져도 화면이 달라 보인다.
    struct TreeDrawContext
    {
        // 줄 전체. 고름·올려놓음 배경이 이 사각형으로 칠해진다.
        ImRect RowRect;
        // 화살표 오른쪽, 이름이 들어갈 자리. 여기에 무엇을 그리든 자유다.
        ImRect ContentRect;
        bool IsOpen = false;
        bool IsSelected = false;
        // 잘려서 안 그려졌으면 거짓이다. 이때 내용도 그리지 않아야 한다.
        bool IsVisible = false;
    };

    // 이름표를 스스로 그리는 보통 트리. `ImGui::TreeNode` 자리를 대신한다.
    bool Tree(const char* label, ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None);

    // 이름표를 **그리지 않고** 자리만 내준다.
    //
    // `minContentHeight` 는 내용 영역의 최소 높이다. 0 이면 글자 한 줄 높이 -
    // 썸네일처럼 글자보다 높은 것을 얹을 때 행을 그만큼 키운다.
    bool TreeBegin(
        const char* id,
        ImGuiTreeNodeFlags flags,
        TreeDrawContext* outContext = nullptr,
        float minContentHeight = 0.0f);
    void TreeEnd();

    // **줄의 오른쪽 끝에 눈 표시를 얹는다**(D-163). 보이면 눈, 감췄으면 가린 눈이고 눌리면 참이다. 레이어 줄과
    // 오브젝트 줄이 같은 자리·같은 크기로 쓴다 - 기존 레이어 창도 둘을 같은 모양으로 두었다. 커서는 되돌려 둔다.
    // 이것의 Id 는 `id` 다. 줄마다 다른 Id 범위 안에서 부른다.
    bool RowEyeToggle(const TreeDrawContext& row, const char* id, bool shown, const char* tooltip);

    namespace Internal
    {
        template <typename TDrawer>
        void InvokeDrawers(TDrawer&& drawer)
        {
            drawer();
        }

        template <typename TDrawer, typename... TDrawers>
        void InvokeDrawers(TDrawer&& drawer, TDrawers&&... drawers)
        {
            drawer();
            ((ImGui::SameLine(), drawers()), ...);
        }
    }

    // 줄 하나에 여럿을 나란히 얹는다. 커서를 내용 영역으로 옮겼다가 되돌리므로
    // 부르는 쪽은 자리를 계산하지 않는다.
    template <typename... TDrawers>
    bool TreeEx(const char* id, ImGuiTreeNodeFlags flags, TDrawers&&... drawers)
    {
        TreeDrawContext context;
        const bool isOpen = TreeBegin(id, flags, &context);
        TreeEnd();

        if (context.IsVisible)
        {
            if constexpr (sizeof...(drawers) > 0)
            {
                const ImVec2 cursor = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(context.ContentRect.Min);
                Internal::InvokeDrawers(drawers...);
                ImGui::SetCursorScreenPos(cursor);
            }
        }
        return isOpen;
    }
}

#include "HierarchyPanel.h"

#include <JBro/Editor/Command/HierarchyCommands.h>
#include <JBro/Editor/EditorActions.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/Tree.h>
#include <JBro/Runtime/GameObject.h>

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace JBro
{
    namespace
    {
        // 끌고 다니는 꾸러미의 이름이다. 계층 안에서만 받는다.
        constexpr const char* DragPayload = "JBRO_HIERARCHY_MOVE";
        // 행에서 "앞에" / "뒤에" 로 치는 위아래 띠의 몫이다. 기존 엔진과 같은 값이다 -
        // 가운데 절반은 "자식으로" 가 된다.
        constexpr float DropEdgeRatio = 0.25f;

        bool ContainsFold(const char* text, const String& needle)
        {
            if (needle.size() == 0)
            {
                return true;
            }
            if (text == nullptr)
            {
                return false;
            }
            // 대소문자를 가리지 않는다. 한글에는 대소문자가 없으므로 영문
            // 이름에만 걸리는 배려지만, 없으면 영문 이름을 찾을 때 답답하다.
            for (const char* at = text; *at != '\0'; ++at)
            {
                std::size_t index = 0;
                while (index < needle.size() && at[index] != '\0')
                {
                    const char left = at[index];
                    const char right = needle.c_str()[index];
                    const char lowerLeft = (left >= 'A' && left <= 'Z')
                        ? static_cast<char>(left - 'A' + 'a') : left;
                    const char lowerRight = (right >= 'A' && right <= 'Z')
                        ? static_cast<char>(right - 'A' + 'a') : right;
                    if (lowerLeft != lowerRight)
                    {
                        break;
                    }
                    ++index;
                }
                if (index == needle.size())
                {
                    return true;
                }
            }
            return false;
        }

        // 끌어 온 것을 이 부모 밑에 넣을 수 있는가. 자기 자신과 자기 자손은 안 된다.
        bool CanReparent(const GameObject* dragged, const GameObject* newParent)
        {
            if (dragged == nullptr)
            {
                return false;
            }
            for (const GameObject* walk = newParent; walk != nullptr; walk = walk->GetParent())
            {
                if (walk == dragged)
                {
                    return false;
                }
            }
            return true;
        }
    }

    const char* HierarchyPanel::GetTitle() const
    {
        return "Hierarchy";
    }

    const char* HierarchyPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::PanelHierarchy, "Hierarchy");
    }

    bool HierarchyPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        return true;
    }

    bool HierarchyPanel::Matches(const GameObject& object) const
    {
        if (m_filter.size() == 0)
        {
            return true;
        }
        if (ContainsFold(object.GetTag(), m_filter))
        {
            return true;
        }
        // **자식이 걸리면 부모도 남는다.** 부모를 지우면 걸린 자식이 나무에서
        // 떨어져 나가 어디에 있던 것인지 알 수 없다.
        const Array<SafePtr<GameObject>>& children = object.GetChildren();
        for (std::size_t index = 0; index < children.Size(); ++index)
        {
            if (const GameObject* child = children[index].TryGet())
            {
                if (Matches(*child))
                {
                    return true;
                }
            }
        }
        return false;
    }

    void HierarchyPanel::OnDraw()
    {
        if (m_editor == nullptr)
        {
            return;
        }
        Canvas* canvas = m_editor->GetCanvas();
        if (canvas == nullptr)
        {
            ImGui::TextDisabled("%s",
                Loc::TextOr(LocKeys::HierarchyNoProject, "no project is open"));
            return;
        }

        // 이번 프레임에 계층의 꾸러미를 끌고 있는가. 다른 위젯의 끌기(목록 재정렬 등)도
        // 꾸러미를 내므로 **타입까지 봐야** 한다.
        const ImGuiPayload* active = ImGui::GetDragDropPayload();
        m_dragActive = active != nullptr && active->IsDataType(DragPayload);

        Widget::SearchBox("##filter", m_filter)
            .Hint(Loc::TextOr(LocKeys::HierarchySearch, "Search"))
            .Draw();
        ImGui::Spacing();

        // 빈 자리에 우클릭하면 뿌리에 만든다. 기존 엔진도 하이어라키의 맥락
        // 메뉴가 이 자리다.
        if (ImGui::BeginPopupContextWindow("##HierarchyMenu",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            // 빈자리의 메뉴는 캔버스 뷰의 것과 **같은 한 벌**이다(D-132).
            const bool changed = EditorActions::DrawBackgroundMenu(*m_editor);
            ImGui::EndPopup();
            if (changed)
            {
                // 계층이 그 자리에서 달라졌다. 이 프레임에 더 그리지 않는다.
                return;
            }
        }

        if (canvas->GetObjectCount() == 0)
        {
            ImGui::TextDisabled("%s",
                Loc::TextOr(LocKeys::HierarchyEmpty, "the canvas is empty"));
            return;
        }

        // **뿌리부터 내려간다.** 평평하게 늘어놓으면 부모-자식이 안 보이고,
        // 그것이 계층 패널의 존재 이유다. 뿌리의 차례는 캔버스가 든다(D-128).
        canvas->GetRootObjects(m_roots);
        for (std::size_t index = 0; index < m_roots.Size(); ++index)
        {
            if (GameObject* root = m_roots[index])
            {
                DrawObject(*root, nullptr, index);
            }
        }

        // **남은 빈자리에 떨어뜨리면 뿌리 맨 뒤로 올린다.** 계층에서 부모를 떼는
        // 손짓이고, 우클릭 메뉴의 `부모 해제` 와 같은 일을 한다.
        //
        // `ImGui::Dummy` 는 넘긴 크기를 **그대로** 쓴다 - `-FLT_MIN` 을 폭으로 넘기면
        // 사각형이 뒤집혀 받는 자리가 아예 생기지 않는다. 실제로 그랬고, 그래서
        // 부모 해제가 되지 않았다. 남은 높이가 0 일 수도 있으므로 한 줄은 보장한다.
        if (m_dragActive)
        {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const ImVec2 size(
                (std::max)(available.x, 1.0f),
                (std::max)(available.y, ImGui::GetTextLineHeightWithSpacing()));
            const ImVec2 start = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##RootDrop", size);
            if (ImGui::BeginDragDropTarget())
            {
                ImGui::GetWindowDrawList()->AddRect(
                    start,
                    ImVec2(start.x + size.x, start.y + size.y),
                    ImGui::GetColorU32(ImGuiCol_DragDropTarget));
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DragPayload))
                {
                    EditorObjectId id = InvalidEditorObjectId;
                    std::memcpy(&id, payload->Data, sizeof(id));
                    if (GameObject* dragged = m_editor->GetObjectIds().Resolve(id))
                    {
                        RecordDrop(*dragged, nullptr, m_roots.Size());
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        FlushPendingMove();
    }

    void HierarchyPanel::DrawDragSource(GameObject& object)
    {
        if (false == ImGui::BeginDragDropSource(
            ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
        {
            return;
        }
        // 꾸러미에는 **주소가 아니라 에디터 번호**를 담는다. 번호는 지웠다
        // 되살려도 같은 것을 가리킨다(D-72).
        const EditorObjectId id = m_editor->GetObjectIds().Track(&object);
        ImGui::SetDragDropPayload(DragPayload, &id, sizeof(id));
        const char* name = object.GetTag();
        ImGui::TextUnformatted(name != nullptr && *name != '\0'
            ? name
            : Loc::TextOr(LocKeys::HierarchyUnnamed, "(unnamed)"));
        ImGui::EndDragDropSource();
    }

    void HierarchyPanel::DrawRowDropTarget(
        GameObject& object, GameObject* parent, std::size_t indexInParent,
        const ImRect& rowRect)
    {
        if (false == m_dragActive
            || rowRect.Max.x <= rowRect.Min.x || rowRect.Max.y <= rowRect.Min.y)
        {
            // 잘려서 그려지지 않은 줄이다. 뒤집힌 사각형으로 단추를 만들면 받는 자리가
            // 생기지 않는다.
            return;
        }
        // **행 전체를 받는 자리로 만든다.** 트리 마디는 화살표와 이름 폭만 차지해서,
        // 그대로 두면 줄의 오른쪽 빈 곳에 떨어뜨릴 수 없다.
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(rowRect.Min);
        ImGui::InvisibleButton("##RowDrop", rowRect.GetSize());
        ImGui::SetCursorScreenPos(cursor);

        if (false == ImGui::BeginDragDropTarget())
        {
            return;
        }

        const float height = (std::max)(1.0f, rowRect.Max.y - rowRect.Min.y);
        const float local = std::clamp(
            (ImGui::GetIO().MousePos.y - rowRect.Min.y) / height, 0.0f, 1.0f);
        DropWhere where = DropWhere::Into;
        if (local < DropEdgeRatio)
        {
            where = DropWhere::Before;
        }
        else if (local > 1.0f - DropEdgeRatio)
        {
            where = DropWhere::After;
        }

        // **무엇이 될지 먼저 보인다.** 선은 형제로 끼우는 자리, 테두리는 자식으로
        // 들어가는 자리다 - 떨어뜨린 뒤에야 알게 되면 되돌리기로 확인하게 된다.
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 color = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
        if (where == DropWhere::Into)
        {
            draw->AddRect(rowRect.Min, rowRect.Max, color);
        }
        else
        {
            const float y = where == DropWhere::Before ? rowRect.Min.y : rowRect.Max.y;
            draw->AddLine(ImVec2(rowRect.Min.x, y), ImVec2(rowRect.Max.x, y), color, 2.0f);
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DragPayload))
        {
            EditorObjectId id = InvalidEditorObjectId;
            std::memcpy(&id, payload->Data, sizeof(id));
            if (GameObject* dragged = m_editor->GetObjectIds().Resolve(id))
            {
                if (where == DropWhere::Into)
                {
                    RecordDrop(*dragged, &object, object.GetChildren().Size());
                }
                else
                {
                    const std::size_t at = where == DropWhere::Before
                        ? indexInParent : indexInParent + 1;
                    RecordDrop(*dragged, parent, at);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    void HierarchyPanel::RecordDrop(
        GameObject& dragged, GameObject* parent, std::size_t insertAt)
    {
        if (&dragged == parent || false == CanReparent(&dragged, parent))
        {
            return;
        }
        m_dragged = dragged.SafeFromThis();
        m_dropParent = parent != nullptr ? parent->SafeFromThis() : SafePtr<GameObject>();
        m_dropToRoot = parent == nullptr;
        m_dropInsertAt = insertAt;
        m_hasDrop = true;
    }

    void HierarchyPanel::FlushPendingMove()
    {
        if (false == m_hasDrop)
        {
            return;
        }
        m_hasDrop = false;

        GameObject* dragged = m_dragged.TryGet();
        m_dragged = {};
        GameObject* parent = m_dropParent.TryGet();
        m_dropParent = {};
        if (dragged == nullptr)
        {
            return;
        }
        if (false == m_dropToRoot && parent == nullptr)
        {
            // 받기로 한 부모가 그 사이에 사라졌다. 뿌리로 올려 버리면 사용자가
            // 뜻하지 않은 곳에 놓이므로 아무것도 하지 않는다.
            return;
        }

        Canvas* canvas = m_editor->GetCanvas();
        if (canvas == nullptr)
        {
            return;
        }

        // **같은 목록 안에서 옮기면 자기 자리가 먼저 빠진다.** 적어 둔 자리는 빠지기
        // 전을 기준으로 센 것이라, 아래로 옮길 때 한 칸을 빼 주지 않으면 늘 하나씩
        // 밀린 자리에 놓인다.
        std::size_t target = m_dropInsertAt;
        GameObject* oldParent = dragged->GetParent();
        if (oldParent == parent)
        {
            std::size_t own = 0;
            const bool found = parent != nullptr
                ? parent->FindChildIndex(dragged, own)
                : canvas->FindRootIndex(dragged, own);
            if (found && own < target)
            {
                --target;
            }
        }

        EditorObjectRegistry& ids = m_editor->GetObjectIds();
        const EditorObjectId objectId = ids.Track(dragged);
        const EditorObjectId parentId = parent != nullptr
            ? ids.Track(parent) : InvalidEditorObjectId;
        if (m_editor->GetCommands().Execute(MakeOwnerPtr<MoveInHierarchyCommand>(
                *canvas, ids, objectId, parentId, target)))
        {
            // 옮긴 것을 고르고 다음 프레임에 보여 준다. 기존 엔진도 끌어 놓은 뒤
            // 그것을 고른다 - 방금 옮긴 것이 인스펙터에 있어야 이어서 손볼 수 있다.
            m_editor->SetSelectedObject(dragged);
            m_reveal = dragged->SafeFromThis();
        }
    }

    bool HierarchyPanel::IsOnRevealPath(const GameObject& object) const
    {
        const GameObject* target = m_reveal.TryGet();
        if (target == nullptr)
        {
            return false;
        }
        // 자기 자신은 펼칠 필요가 없다. 조상만이다.
        for (const GameObject* walk = target->GetParent();
            walk != nullptr; walk = walk->GetParent())
        {
            if (walk == &object)
            {
                return true;
            }
        }
        return false;
    }

    bool HierarchyPanel::DrawObjectContextMenu(GameObject& object)
    {
        if (false == ImGui::BeginPopupContextItem("##ObjectMenu"))
        {
            return true;
        }
        // 우클릭한 것을 고른 것으로 삼는다. 메뉴가 무엇에 대한 것인지
        // 보이는 것과 어긋나면 안 된다.
        //
        // **이미 골라져 있으면 선택을 흩뜨리지 않는다** - 여럿 골라 놓고
        // 그중 하나에 우클릭하는 것은 "이것들에 대해" 라는 뜻이다.
        if (false == m_editor->IsSelected(&object))
        {
            m_editor->SetSelectedObject(&object);
        }
        // 항목은 공용 한 벌이다(D-132). 캔버스 뷰와 메뉴 막대가 같은 것을 쓴다.
        bool alive = true;
        if (EditorActions::DrawCreateChildItem(*m_editor, object))
        {
            alive = false;
        }
        if (alive && EditorActions::DrawUnparentItem(*m_editor, object))
        {
            // 부모가 바뀌면 지금 도는 자식 배열이 그 자리에서 달라진다.
            alive = false;
        }
        if (alive)
        {
            ImGui::Separator();
            EditorActions::DrawCopyItem(*m_editor);
            if (EditorActions::DrawPasteItem(*m_editor))
            {
                alive = false;
            }
        }
        if (alive)
        {
            ImGui::Separator();
            if (EditorActions::DrawDeleteItem(*m_editor, object))
            {
                // **여기서 `object` 는 이미 없을 수 있다.**
                alive = false;
            }
        }
        ImGui::EndPopup();
        return alive;
    }

    void HierarchyPanel::DrawObject(
        GameObject& object, GameObject* parent, std::size_t indexInParent)
    {
        if (false == Matches(object))
        {
            return;
        }

        // 이름은 태그로 산다 - `Canvas::CreateObject(name)` 이 거기에 넣는다.
        const char* name = object.GetTag();
        if (name == nullptr || *name == '\0')
        {
            name = Loc::TextOr(LocKeys::HierarchyUnnamed, "(unnamed)");
        }

        // **개수는 지금 재 둔다.** 아래에서 우클릭 메뉴가 이 오브젝트를 지울 수 있고,
        // 그 뒤에 배열을 다시 읽으면 죽은 자리를 읽는다.
        const bool hasChildren = object.GetChildren().Size() != 0;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
            | ImGuiTreeNodeFlags_SpanAvailWidth
            | ImGuiTreeNodeFlags_DefaultOpen;
        if (false == hasChildren)
        {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        // **고른 것 전부에 표시한다.** 주된 것 하나만 칠하면 여럿 골라 놓고
        // 무엇이 골라졌는지 화면에서 알 수 없다.
        if (m_editor->IsSelected(&object))
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        // **이름이 아니라 주소로 식별한다.** 같은 이름을 가진 오브젝트가 여럿일 수
        // 있고, ImGui 는 라벨로 항목을 구분하므로 그대로 두면 둘이 한 줄을 나눠 쓴다.
        ImGui::PushID(&object);

        // 보여 달라고 한 것의 조상이면 이 프레임에 펼친다. 같은 프레임에 자식이
        // 그려져야 그 줄까지 닿는다.
        if (IsOnRevealPath(object))
        {
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        }

        // 트리 위젯이 줄 자리를 돌려준다. 이름은 우리가 그 자리에 그린다 -
        // 나중에 눈 표시나 배지를 같은 줄에 얹을 자리가 이것이다.
        Widget::TreeDrawContext row;
        const bool opened = Widget::TreeBegin("##node", flags, &row);
        Widget::TreeEnd();
        if (m_reveal.TryGet() == &object)
        {
            // 닿았다. 보이는 자리로 끌어다 놓고 요청을 비운다 - 한 번짜리다.
            ImGui::SetScrollHereY(0.5f);
            m_reveal = {};
        }

        // **줄에 대한 판단은 여기서 다 한다.** 아래에서 이름을 그리면 ImGui 의
        // "마지막 항목" 이 그 글자로 바뀌어, 줄의 빈 곳을 누른 것이 줄을 누른 것으로
        // 세지 않는다.
        const bool rowHovered = ImGui::IsItemHovered();
        const bool rowToggled = ImGui::IsItemToggledOpen();
        DrawDragSource(object);
        if (false == DrawObjectContextMenu(object))
        {
            // 마디를 열어 두었으면 ImGui 의 짝을 맞춰 닫고 나간다 - 짝이 맞지
            // 않으면 다음 줄들이 한 칸씩 들여써진다.
            if (opened && hasChildren)
            {
                ImGui::TreePop();
            }
            ImGui::PopID();
            return;
        }

        // **누를 때가 아니라 뗄 때 고른다.** 누르는 순간 고르면 끌기를 시작하자마자
        // 선택이 바뀌어 인스펙터가 갈리고, 끌던 대상이 사라진다. 기존 엔진이 같은
        // 이유로 릴리스 기준이다.
        if (rowHovered && false == rowToggled
            && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
            && false == Widget::MouseWasDragged(ImGuiMouseButton_Left))
        {
            const ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl || io.KeyShift)
            {
                if (m_editor->IsSelected(&object))
                {
                    m_editor->RemoveFromSelection(&object);
                }
                else
                {
                    m_editor->AddToSelection(&object);
                }
            }
            else
            {
                m_editor->SetSelectedObject(&object);
            }
        }

        DrawRowDropTarget(object, parent, indexInParent, row.RowRect);

        if (row.IsVisible)
        {
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(row.ContentRect.Min);
            // 꺼져 있는 오브젝트는 흐리게. 사용 여부는 화면에서 바로 보여야 한다.
            if (object.IsActiveSelf())
            {
                ImGui::TextUnformatted(name);
            }
            else
            {
                ImGui::TextDisabled("%s", name);
            }
            ImGui::SetCursorScreenPos(cursor);
        }

        if (opened && hasChildren)
        {
            const Array<SafePtr<GameObject>>& children = object.GetChildren();
            for (std::size_t index = 0; index < children.Size(); ++index)
            {
                if (GameObject* child = children[index].TryGet())
                {
                    DrawObject(*child, &object, index);
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

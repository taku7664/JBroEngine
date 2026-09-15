#include "HierarchyPanel.h"

#include <JBro/Editor/Command/HierarchyCommands.h>
#include <JBro/Editor/Command/ObjectCommands.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/Tree.h>
#include <JBro/Runtime/GameObject.h>

#include <imgui.h>

#include <cstring>
#include <utility>

namespace JBro
{
    namespace
    {
        // 끌고 다니는 꾸러미의 이름이다. 계층 안에서만 받는다.
        constexpr const char* DragPayload = "JBRO_HIERARCHY_MOVE";
        // 줄 사이의 받는 자리 높이. 너무 얇으면 못 맞추고, 두꺼우면 줄이 벌어진다.
        constexpr float DropGapHeight = 4.0f;

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

        Widget::SearchBox("##filter", m_filter)
            .Hint(Loc::TextOr(LocKeys::HierarchySearch, "Search"))
            .Draw();
        ImGui::Spacing();

        // 빈 자리에 우클릭하면 뿌리에 만든다. 기존 엔진도 하이어라키의 맥락
        // 메뉴가 이 자리다.
        if (ImGui::BeginPopupContextWindow("##HierarchyMenu",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem(Loc::TextOr(LocKeys::HierarchyCreateObject,
                "Create Object")))
            {
                auto command = MakeOwnerPtr<CreateObjectCommand>(
                    *canvas, m_editor->GetObjectIds(), "GameObject",
                    InvalidEditorObjectId);
                CreateObjectCommand* raw = command.Get();
                if (m_editor->GetCommands().Execute(std::move(command)))
                {
                    m_editor->SetSelectedObject(
                        m_editor->GetObjectIds().Resolve(raw->GetObjectId()));
                }
            }
            // 빈 자리의 붙여넣기는 뿌리에 붙는다.
            const bool hasClipboard = m_editor->HasClipboard();
            if (false == hasClipboard)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::MenuItem(Loc::TextOr(LocKeys::HierarchyPaste, "Paste"), "Ctrl+V"))
            {
                m_editor->ClearSelection();
                m_editor->PasteClipboard();
            }
            if (false == hasClipboard)
            {
                ImGui::EndDisabled();
            }
            ImGui::EndPopup();
        }

        if (canvas->GetObjectCount() == 0)
        {
            ImGui::TextDisabled("%s",
                Loc::TextOr(LocKeys::HierarchyEmpty, "the canvas is empty"));
            return;
        }

        // **뿌리부터 내려간다.** 평평하게 늘어놓으면 부모-자식이 안 보이고,
        // 그것이 계층 패널의 존재 이유다.
        canvas->ForEachObject([this](GameObject& object) {
            if (object.GetParent() == nullptr)
            {
                DrawObject(object);
            }
        });

        // **빈 자리에 떨어뜨리면 뿌리로 올린다.** 계층에서 부모를 떼는 유일한
        // 손짓이라 이 자리가 없으면 한번 자식이 된 것을 다시 꺼낼 수 없다.
        ImGui::Dummy(ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y));
        DrawDropTarget(nullptr, 0);

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

    void HierarchyPanel::DrawDropTarget(GameObject* parent, std::size_t siblingIndex)
    {
        if (false == ImGui::BeginDragDropTarget())
        {
            return;
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DragPayload))
        {
            EditorObjectId id = InvalidEditorObjectId;
            std::memcpy(&id, payload->Data, sizeof(id));
            if (GameObject* dragged = m_editor->GetObjectIds().Resolve(id))
            {
                m_dragged = dragged->SafeFromThis();
                m_dropParent = parent != nullptr
                    ? parent->SafeFromThis() : SafePtr<GameObject>();
                m_dropToRoot = parent == nullptr;
                m_dropIndex = siblingIndex;
                m_hasDrop = true;
            }
        }
        ImGui::EndDragDropTarget();
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

        EditorObjectRegistry& ids = m_editor->GetObjectIds();
        const EditorObjectId objectId = ids.Track(dragged);
        const EditorObjectId parentId = parent != nullptr
            ? ids.Track(parent) : InvalidEditorObjectId;
        m_editor->GetCommands().Execute(MakeOwnerPtr<MoveInHierarchyCommand>(
            *m_editor->GetCanvas(), ids, objectId, parentId, m_dropIndex));
    }

    void HierarchyPanel::DrawObject(GameObject& object)
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

        const Array<SafePtr<GameObject>>& children = object.GetChildren();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
            | ImGuiTreeNodeFlags_SpanAvailWidth
            | ImGuiTreeNodeFlags_DefaultOpen;
        if (children.Size() == 0)
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

        // 트리 위젯이 줄 자리를 돌려준다. 이름은 우리가 그 자리에 그린다 -
        // 나중에 눈 표시나 배지를 같은 줄에 얹을 자리가 이것이다.
        // **줄 앞의 틈**이다. 여기 떨어뜨리면 이 줄 **앞자리**로 간다.
        // 부모 안에서만 뜻이 있다 - 뿌리끼리의 차례는 캔버스가 순서를 들고
        // 있지 않아 아직 바꿀 수 없다(D-84).
        GameObject* parent = object.GetParent();
        if (parent != nullptr)
        {
            std::size_t ownIndex = 0;
            parent->FindChildIndex(&object, ownIndex);
            const ImVec2 gapStart = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##gap", ImVec2(
                ImGui::GetContentRegionAvail().x, DropGapHeight));
            if (ImGui::BeginDragDropTarget())
            {
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(gapStart.x, gapStart.y + DropGapHeight * 0.5f),
                    ImVec2(gapStart.x + ImGui::GetContentRegionAvail().x,
                        gapStart.y + DropGapHeight * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0f);
                ImGui::EndDragDropTarget();
            }
            DrawDropTarget(parent, ownIndex);
        }

        Widget::TreeDrawContext row;
        const bool opened = Widget::TreeBegin("##node", flags, &row);
        Widget::TreeEnd();
        // 줄 자체에 떨어뜨리면 **그 밑의 자식**이 된다. 맨 뒤에 붙는다.
        DrawDragSource(object);
        DrawDropTarget(&object, object.GetChildren().Size());
        if (row.IsVisible)
        {
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(row.ContentRect.Min);
            // 꺼져 있는 오브젝트는 흐리게. 켜짐 여부는 화면에서 바로 보여야 한다.
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

        if (ImGui::IsItemClicked() && false == ImGui::IsItemToggledOpen())
        {
            // 기존 엔진과 같은 손놀림이다: Ctrl·Shift 는 하나씩 붙였다 뗐다,
            // 맨 클릭은 통째로 바꾼다.
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
        if (ImGui::BeginPopupContextItem("##ObjectMenu"))
        {
            // 우클릭한 것을 고른 것으로 삼는다. 메뉴가 무엇에 대한 것인지
            // 보이는 것과 어긋나면 안 된다.
            //
            // **이미 골라져 있으면 선택을 흩뜨리지 않는다** - 여럿 골라 놓고
            // 그중 하나에 우클릭하는 것은 "이것들에 대해" 라는 뜻이다.
            if (false == m_editor->IsSelected(&object))
            {
                m_editor->SetSelectedObject(&object);
            }
            if (ImGui::MenuItem(Loc::TextOr(LocKeys::HierarchyCreateChild,
                "Create Child")))
            {
                auto command = MakeOwnerPtr<CreateObjectCommand>(
                    *m_editor->GetCanvas(), m_editor->GetObjectIds(), "GameObject",
                    m_editor->GetObjectIds().Track(&object));
                CreateObjectCommand* raw = command.Get();
                if (m_editor->GetCommands().Execute(std::move(command)))
                {
                    m_editor->SetSelectedObject(
                        m_editor->GetObjectIds().Resolve(raw->GetObjectId()));
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(Loc::TextOr(LocKeys::HierarchyCopy, "Copy"), "Ctrl+C"))
            {
                m_editor->CopySelection();
            }
            const bool hasClipboard = m_editor->HasClipboard();
            if (false == hasClipboard)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::MenuItem(Loc::TextOr(LocKeys::HierarchyPaste, "Paste"), "Ctrl+V"))
            {
                m_editor->PasteClipboard();
                ImGui::EndPopup();
                ImGui::PopID();
                return;
            }
            if (false == hasClipboard)
            {
                ImGui::EndDisabled();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(Loc::TextOr(LocKeys::HierarchyDelete, "Delete")))
            {
                // 고른 것을 먼저 비운다 - 지운 뒤에 인스펙터가 죽은 것을 읽지
                // 않게. SafePtr 이 알아서 비우지만, 이 프레임 안에서는 아직 살아 있다.
                m_editor->SetSelectedObject(nullptr);
                m_editor->GetCommands().Execute(MakeOwnerPtr<DeleteObjectCommand>(
                    *m_editor->GetCanvas(), m_editor->GetObjectIds(), &object));
                ImGui::EndPopup();
                ImGui::PopID();
                return;
            }
            ImGui::EndPopup();
        }
        if (opened && children.Size() != 0)
        {
            for (std::size_t index = 0; index < children.Size(); ++index)
            {
                if (GameObject* child = children[index].TryGet())
                {
                    DrawObject(*child);
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

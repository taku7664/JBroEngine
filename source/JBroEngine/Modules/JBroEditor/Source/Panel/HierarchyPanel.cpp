#include "HierarchyPanel.h"

#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Runtime/GameObject.h>

#include <imgui.h>

namespace JBro
{
    const char* HierarchyPanel::GetTitle() const
    {
        return "Hierarchy";
    }

    bool HierarchyPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        return true;
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
            ImGui::TextDisabled("no project is open");
            return;
        }
        if (canvas->GetObjectCount() == 0)
        {
            ImGui::TextDisabled("the canvas is empty");
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
    }

    void HierarchyPanel::DrawObject(GameObject& object)
    {
        // 이름은 태그로 산다 - `Canvas::CreateObject(name)` 이 거기에 넣는다.
        const char* name = object.GetTag();
        if (name == nullptr || *name == '\0')
        {
            name = "(unnamed)";
        }

        const Array<SafePtr<GameObject>>& children = object.GetChildren();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
            | ImGuiTreeNodeFlags_SpanAvailWidth
            | ImGuiTreeNodeFlags_DefaultOpen;
        if (children.Size() == 0)
        {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        if (m_editor->GetSelectedObject() == &object)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        // **이름이 아니라 주소로 식별한다.** 같은 이름을 가진 오브젝트가 여럿일 수
        // 있고, ImGui 는 라벨로 항목을 구분하므로 그대로 두면 둘이 한 줄을 나눠 쓴다.
        ImGui::PushID(&object);
        const bool opened = ImGui::TreeNodeEx("##node", flags, "%s", name);
        if (ImGui::IsItemClicked() && false == ImGui::IsItemToggledOpen())
        {
            m_editor->SetSelectedObject(&object);
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

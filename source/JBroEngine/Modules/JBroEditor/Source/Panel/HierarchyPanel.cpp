#include "HierarchyPanel.h"

#include <JBro/Editor/Command/ObjectCommands.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Runtime/GameObject.h>

#include <imgui.h>

#include <utility>

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
        // 빈 자리에 우클릭하면 뿌리에 만든다. 기존 엔진도 하이어라키의 맥락
        // 메뉴가 이 자리다.
        if (ImGui::BeginPopupContextWindow("##HierarchyMenu",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Create Object"))
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
            ImGui::EndPopup();
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
        // **고른 것 전부에 표시한다.** 주된 것 하나만 칠하면 여럿 골라 놓고
        // 무엇이 골라졌는지 화면에서 알 수 없다.
        if (m_editor->IsSelected(&object))
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        // **이름이 아니라 주소로 식별한다.** 같은 이름을 가진 오브젝트가 여럿일 수
        // 있고, ImGui 는 라벨로 항목을 구분하므로 그대로 두면 둘이 한 줄을 나눠 쓴다.
        ImGui::PushID(&object);
        const bool opened = ImGui::TreeNodeEx("##node", flags, "%s", name);
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
            if (ImGui::MenuItem("Create Child"))
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
            if (ImGui::MenuItem("Delete"))
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

#include "NodeGraphInspectorPanel.h"

#include "NodeGraphPanel.h"

#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/NodeGraph/MathDomain.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace JBro
{
    const char* NodeGraphInspectorPanel::GetTitle() const
    {
        return TypeName;
    }

    const char* NodeGraphInspectorPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::NodeGraphInspectorTitle, "Node Inspector");
    }

    EditorDock NodeGraphInspectorPanel::GetPreferredDock() const
    {
        return EditorDock::Right;
    }

    Bool NodeGraphInspectorPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        return true;
    }

    void NodeGraphInspectorPanel::OnDraw()
    {
        NodeGraphPanel* graph = NodeGraphPanel::FindActive(*m_editor);
        if (graph == nullptr || graph->GetDomain() == nullptr)
        {
            Widget::HintText(Loc::TextOr(LocKeys::NodeGraphNoGraph, "No node graph is open"));
            return;
        }
        DrawInputs(*graph);
        DrawPreview(*graph);
    }

    void NodeGraphInspectorPanel::DrawInputs(NodeGraphPanel& graph)
    {
        const ArrayView<const NodeGraph::NodeId> selection = graph.GetCanvasState().interaction.GetSelection();
        if (selection.Size() != 1)
        {
            Widget::HintText(selection.IsEmpty() ? Loc::TextOr(LocKeys::NodeGraphNoSelection, "No node is selected")
                                                 : Loc::TextOr(LocKeys::NodeGraphManySelected, "Several nodes are selected"));
            return;
        }
        const NodeGraph::Node* node = graph.GetGraph().FindNode(selection[0]);
        if (node == nullptr)
        {
            return;
        }
        const NodeGraph::Domain& domain = *graph.GetDomain();
        const NodeGraph::NodeTypeInfo* type = domain.FindNodeType(node->type.View());
        Widget::SectionHeader(node->type.c_str()).Draw();
        if (type == nullptr)
        {
            Widget::SeverityTextF(Widget::Severity::Warning, "%s", Loc::TextOr(LocKeys::NodeGraphUnknownType, "This domain has no node of this type"));
            return;
        }
        if (type->inputs.IsEmpty())
        {
            return;
        }
        // 노드를 지우거나 되돌리면 이 칸이 가리키는 노드가 바뀐다 - 번호를 떠 두고 고칠 때마다 그래프 창이 다시 찾는다.
        const NodeGraph::NodeId id = node->id;
        Widget::FormLayout layout("##nodeInputs");
        for (std::size_t pin = 0; pin < type->inputs.Size() && pin < node->inputs.Size(); ++pin)
        {
            const NodeGraph::PinInfo& info = type->inputs[pin];
            layout.Row(Widget::FieldLabel(info.name.c_str()),
                [&] {
                    if (graph.GetGraph().FindLinkInto(NodeGraph::PinRef{id, pin}) != nullptr)
                    {
                        Widget::HintText(Loc::TextOr(LocKeys::NodeGraphLinked, "Linked"));
                        return;
                    }
                    const NodeGraph::PinTypeInfo* pinType = domain.FindPinType(info.type);
                    const Int32 count = static_cast<JBro::Int32>(pinType == nullptr ? 4u : std::min(pinType->components.Get(), 4u));
                    char field[48];
                    std::snprintf(field, sizeof(field), "##input%zu", pin);
                    const NodeGraph::Node* current = graph.GetGraph().FindNode(id);
                    if (current == nullptr || pin >= current->inputs.Size())
                    {
                        return;
                    }
                    NodeGraph::PinValue value = current->inputs[pin];
                    if (Widget::ScalarRunField(field, value.values, count, 0.01f, false, 0.0f, 0.0f))
                    {
                        graph.SetInput(id, pin, value);
                    }
                });
        }
    }

    void NodeGraphInspectorPanel::DrawPreview(NodeGraphPanel& graph)
    {
        // 샘플 도메인 `Math` 는 결과 노드의 값을 보인다 - 그래프를 결과물로 바꾸는 자리가 실제로 도는지 눈으로 본다.
        const NodeGraph::Domain& domain = *graph.GetDomain();
        if (domain.GetName().View() != NodeGraph::MathDomainName)
        {
            return;
        }
        Widget::SectionHeader(Loc::TextOr(LocKeys::NodeGraphPreview, "Preview")).SpacingBefore().Draw();
        NodeGraph::PinValue result;
        if (!NodeGraph::EvaluateMath(graph.GetGraph(), domain, result))
        {
            Widget::HintText(Loc::TextOr(LocKeys::NodeGraphNoOutput, "Add an Output node to see the result"));
            return;
        }
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float side = ImGui::GetFrameHeight() * 2.0f;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec4 color(std::clamp(result.values[0].Get(), 0.0f, 1.0f), std::clamp(result.values[1].Get(), 0.0f, 1.0f),
            std::clamp(result.values[2].Get(), 0.0f, 1.0f), 1.0f);
        draw->AddRectFilled(at, ImVec2(at.x + side, at.y + side), ImGui::ColorConvertFloat4ToU32(color), 4.0f);
        draw->AddRect(at, ImVec2(at.x + side, at.y + side), ImGui::ColorConvertFloat4ToU32(EditorTheme::Line), 4.0f);
        ImGui::Dummy(ImVec2(side, side));
        ImGui::SameLine();
        Widget::TextF("%.3f  %.3f  %.3f  %.3f", static_cast<double>(result.values[0].Get()), static_cast<double>(result.values[1].Get()),
            static_cast<double>(result.values[2].Get()), static_cast<double>(result.values[3].Get()));
    }
}

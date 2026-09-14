#include "InspectorPanel.h"

#include <JBro/Editor/EditorApplication.h>
#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/NameTable.h>

#include <imgui.h>

#include <cstring>

namespace JBro
{
    namespace
    {
        // 잎사귀 값을 글자로 주고받는 버퍼다. 코덱이 더 큰 것을 요구하면 그 값은
        // 읽기 전용으로 보여 준다 - 반쪽만 보여 주고 고치게 하면 저장할 때 잘린다.
        constexpr std::size_t TextCapacity = 512;

        bool SameName(NameId id, const char* text)
        {
            const char* name = NameTable::Get().Resolve(id);
            return name != nullptr && std::strcmp(name, text) == 0;
        }

        bool ToText(const TypeDescriptor& type, const void* address, String& text)
        {
            if (type.codec == nullptr || type.codec->ToText == nullptr)
            {
                return false;
            }
            char buffer[TextCapacity] = {};
            std::size_t required = 0;
            if (false == type.codec->ToText(address, buffer, sizeof(buffer), required))
            {
                return false;
            }
            text = buffer;
            return true;
        }
    }

    const char* InspectorPanel::GetTitle() const
    {
        return "Inspector";
    }

    bool InspectorPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        return true;
    }

    void InspectorPanel::OnDraw()
    {
        if (m_editor == nullptr)
        {
            return;
        }
        GameObject* object = m_editor->GetSelectedObject();
        if (object == nullptr)
        {
            ImGui::TextDisabled("nothing is selected");
            return;
        }

        const char* name = object->GetTag();
        ImGui::TextUnformatted(name != nullptr && *name != '\0' ? name : "(unnamed)");
        bool active = object->IsActiveSelf();
        if (ImGui::Checkbox("Active", &active))
        {
            object->SetActive(active);
        }
        ImGui::Separator();

        const Array<ComponentSlot>& components = object->GetComponents();
        for (std::size_t index = 0; index < components.Size(); ++index)
        {
            const ComponentSlot& slot = components[index];
            ComponentBase* component = slot.reference.TryGet();
            if (component == nullptr)
            {
                continue;
            }
            const char* typeName = NameTable::Get().Resolve(slot.typeId);

            // 이름이 아니라 슬롯으로 구분한다. 같은 타입을 두 개 붙일 수 있다.
            ImGui::PushID(static_cast<int>(index));
            const bool opened = ImGui::CollapsingHeader(
                typeName != nullptr ? typeName : "(unknown component)",
                ImGuiTreeNodeFlags_DefaultOpen);
            if (opened)
            {
                bool enabled = component->IsEnabled();
                if (ImGui::Checkbox("Enabled", &enabled))
                {
                    component->SetEnabled(enabled);
                }

                const PropertyTable* table = PropertyRegistry::Lookup(slot.typeId);
                if (table == nullptr)
                {
                    // 저장도 안 되는 컴포넌트다. 조용히 빈 칸으로 두면 왜 안 보이는지
                    // 알 수 없으므로 그렇게 말해 준다.
                    ImGui::TextDisabled("this type never registered its properties");
                }
                else
                {
                    Context context;
                    context.component = component;
                    context.typeId = slot.typeId;
                    DrawFields(*table, component, context);
                }
            }
            ImGui::PopID();
        }
    }

    void InspectorPanel::DrawFields(
        const PropertyTable& table, void* owner, Context& context)
    {
        for (std::uint32_t index = 0; index < table.count; ++index)
        {
            const PropertyInfo& property = table.properties[index];
            if (property.type == nullptr || property.Address == nullptr)
            {
                continue;
            }
            void* address = property.Address(owner);
            if (address == nullptr)
            {
                continue;
            }
            const char* label = property.edit != nullptr
                    && property.edit->displayName != nullptr
                ? property.edit->displayName
                : NameTable::Get().Resolve(property.name);

            // 길에 한 칸 더 내려간다. 그려 놓고 되돌려야 형제 필드가 제 길을 갖는다.
            if (context.path.depth >= SetPropertyCommand::MaxDepth)
            {
                // 너무 깊다. 보여는 주되 고치지는 못하게 둔다 - 잘못된 길로 쓰는
                // 것보다 낫다.
                ImGui::TextDisabled("%s (too deeply nested to edit)",
                    label != nullptr ? label : "?");
                continue;
            }
            context.path.indices[context.path.depth] = index;
            ++context.path.depth;

            ImGui::PushID(static_cast<int>(index));
            DrawValue(label != nullptr ? label : "?", *property.type, address,
                property.edit, context);
            ImGui::PopID();

            --context.path.depth;
        }
    }

    void InspectorPanel::CommitEdit(
        const TypeDescriptor& type, void* address, const String& before, Context& context)
    {
        String after;
        if (false == ToText(type, address, after) || after == before)
        {
            return;
        }
        // **바뀐 값을 도로 되돌려 놓는다.** 커맨드의 `Execute` 가 다시 적용하므로
        // 쓰는 길이 하나로 남는다 - 위젯이 한 번, 커맨드가 한 번 쓰면 되돌리기가
        // 무엇을 되돌리는지가 둘로 갈린다.
        if (type.codec != nullptr && type.codec->FromText != nullptr)
        {
            type.codec->FromText(address, before.c_str(), before.size());
        }
        m_editor->GetCommands().Execute(MakeOwnerPtr<SetPropertyCommand>(
            context.component->SafeFromThis(),
            context.typeId,
            context.path,
            before,
            after));
    }

    void InspectorPanel::DrawValue(
        const char* label,
        const TypeDescriptor& type,
        void* address,
        const PropertyEditInfo* edit,
        Context& context)
    {
        const bool editable = edit == nullptr || edit->editable;
        if (false == editable)
        {
            ImGui::BeginDisabled();
        }

        // enum 은 타입이 이름표를 들고 있다. 이름으로 알아볼 필요가 없다.
        if (type.enumNames != nullptr && type.enumNames->ToIndex != nullptr)
        {
            const EnumNames& names = *type.enumNames;
            String before;
            ToText(type, address, before);
            int current = names.ToIndex(address);
            if (ImGui::Combo(label, &current, names.names, static_cast<int>(names.count))
                && names.FromIndex != nullptr)
            {
                names.FromIndex(address, current);
                CommitEdit(type, address, before, context);
            }
        }
        // 컨테이너는 아직 조작 함수가 없다(ArrayOps/TableOps 미구현). 개수만 보여 준다.
        else if (type.arrayOps != nullptr && type.arrayOps->GetSize != nullptr)
        {
            ImGui::LabelText(label, "%zu item(s)", type.arrayOps->GetSize(address));
        }
        else if (type.tableOps != nullptr && type.tableOps->GetSize != nullptr)
        {
            ImGui::LabelText(label, "%zu entry(s)", type.tableOps->GetSize(address));
        }
        // 구조를 가진 타입은 필드로 말한다. 타고 내려가면 잎사귀에서 코덱을 만난다.
        else if (type.fields != nullptr)
        {
            if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen))
            {
                DrawFields(*type.fields, address, context);
                ImGui::TreePop();
            }
        }
        else if (type.codec != nullptr)
        {
            // **흔한 잎사귀만 제 위젯을 갖는다.** 나머지는 코덱의 글자 왕복으로 그린다.
            //
            // 타입 이름으로 가르는 것이 마음에 걸리지만, 여기 하나뿐이고 빠진 타입은
            // 글자 칸으로 **degrade 할 뿐 깨지지 않는다**. 기존 엔진의 18값 enum 과
            // 다른 점이 그것이다 - 거기서는 빠진 값이 곧 그리지 못하는 필드였다.
            String before;
            const bool snapped = ToText(type, address, before);
            const bool hasRange = edit != nullptr && edit->hasRange;
            bool changed = false;

            if (SameName(type.typeName, "float"))
            {
                float* value = static_cast<float*>(address);
                changed = hasRange
                    ? ImGui::SliderFloat(label, value, edit->rangeMin, edit->rangeMax)
                    : ImGui::DragFloat(label, value, 0.01f);
            }
            else if (SameName(type.typeName, "bool"))
            {
                changed = ImGui::Checkbox(label, static_cast<bool*>(address));
            }
            else if (SameName(type.typeName, "int32"))
            {
                changed = ImGui::DragInt(label, static_cast<int*>(address));
            }
            else if (false == snapped)
            {
                ImGui::LabelText(label, "(too long to show)");
            }
            else if (type.codec->FromText == nullptr)
            {
                ImGui::LabelText(label, "%s", before.c_str());
            }
            else
            {
                char text[TextCapacity] = {};
                const std::size_t copied =
                    before.size() < sizeof(text) - 1 ? before.size() : sizeof(text) - 1;
                std::memcpy(text, before.c_str(), copied);
                if (ImGui::InputText(label, text, sizeof(text),
                    ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    // 읽지 못하는 글자는 값을 건드리지 않는다. 코덱이 그렇게 약속한다.
                    changed = type.codec->FromText(address, text, std::strlen(text));
                }
            }

            if (changed && snapped && editable)
            {
                CommitEdit(type, address, before, context);
            }
        }
        else
        {
            // 필드도 코덱도 없는 타입이다. 등록이 덜 된 것이고, 빈 줄로 두면 모른다.
            ImGui::LabelText(label, "(no way to show this type)");
        }

        if (false == editable)
        {
            ImGui::EndDisabled();
        }
        if (edit != nullptr && edit->tooltip != nullptr && ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", edit->tooltip);
        }
    }
}

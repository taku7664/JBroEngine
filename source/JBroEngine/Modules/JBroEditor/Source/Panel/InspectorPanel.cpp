#include "InspectorPanel.h"

#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Editor/Command/ComponentCommands.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/FormLayout.h>
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

        // 화면에 나오는 이름은 타입 이름 그대로가 아니다(ProjectRule §11.3).
        // `Component::Transform2D` 의 접두어는 코드가 쓰는 것이지 사람이 읽는 것이
        // 아니다.
        const char* DisplayTypeName(const char* typeName)
        {
            if (typeName == nullptr)
            {
                return nullptr;
            }
            const char* lastColon = std::strrchr(typeName, ':');
            if (lastColon != nullptr && *(lastColon + 1) != '\0')
            {
                return lastColon + 1;
            }
            return typeName;
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
        // 안정된 이름이다. 번역하지 않는다 - 창의 정체가 여기 달려 있다.
        return "Inspector";
    }

    const char* InspectorPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::PanelInspector, "Inspector");
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
            ImGui::TextDisabled("%s",
                Loc::TextOr(LocKeys::InspectorNothingSelected, "nothing is selected"));
            return;
        }

        // 여럿 골랐으면 그렇다고 말한다. 주된 것만 보여 주면서 아무 말도 하지
        // 않으면, 나머지가 골라져 있다는 것을 화면에서 알 수 없다.
        const std::size_t chosen = m_editor->GetSelectionCount();
        if (chosen > 1)
        {
            ImGui::TextDisabled(
                Loc::TextOr(LocKeys::InspectorMultipleSelected, "%d objects selected"),
                static_cast<int>(chosen));
        }

        const char* name = object->GetTag();
        ImGui::TextUnformatted(name != nullptr && *name != '\0'
            ? name
            : Loc::TextOr(LocKeys::HierarchyUnnamed, "(unnamed)"));

        {
            Widget::FormLayout header("##object");
            header.Row(
                Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorActive, "Active")),
                [&]() {
                    bool active = object->IsActiveSelf();
                    if (ImGui::Checkbox("##active", &active))
                    {
                        object->SetActive(active);
                    }
                });
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
            const char* typeName =
                DisplayTypeName(NameTable::Get().Resolve(slot.typeId));

            // 이름이 아니라 슬롯으로 구분한다. 같은 타입을 두 개 붙일 수 있다.
            ImGui::PushID(static_cast<int>(index));
            const bool opened = ImGui::CollapsingHeader(
                typeName != nullptr
                    ? typeName
                    : Loc::TextOr(LocKeys::InspectorUnknownComponent,
                        "(unknown component)"),
                ImGuiTreeNodeFlags_DefaultOpen);
            // **머리에 우클릭하면 뗄 수 있다.** 기존 엔진도 여기가 그 자리다.
            // 접힌 채로도 눌러야 하므로 머리를 그린 직후에 둔다.
            if (ImGui::BeginPopupContextItem("##ComponentMenu"))
            {
                if (ImGui::MenuItem(Loc::TextOr(LocKeys::InspectorRemoveComponent,
                        "Remove Component")))
                {
                    RemoveComponent(*object, *component);
                    ImGui::EndPopup();
                    ImGui::PopID();
                    // 뗀 뒤에는 이 프레임의 슬롯 배열이 더 이상 맞지 않는다.
                    // 계속 돌면 죽은 슬롯을 읽는다 - 다음 프레임에 다시 그린다.
                    return;
                }
                ImGui::EndPopup();
            }
            if (opened)
            {
                Widget::FormLayout layout("##component");
                layout.Row(
                    Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorEnabled, "Enabled")),
                    [&]() {
                        bool enabled = component->IsEnabled();
                        if (ImGui::Checkbox("##enabled", &enabled))
                        {
                            component->SetEnabled(enabled);
                        }
                    });

                const PropertyTable* table = PropertyRegistry::Lookup(slot.typeId);
                if (table == nullptr)
                {
                    layout.FullRow([&]() {
                        // 저장도 안 되는 컴포넌트다. 조용히 빈 칸으로 두면 왜
                        // 안 보이는지 알 수 없으므로 그렇게 말해 준다.
                        ImGui::TextDisabled("%s",
                            Loc::TextOr(LocKeys::InspectorUnregisteredType,
                                "this type never registered its properties"));
                    });
                }
                else
                {
                    Context context;
                    context.component = component;
                    context.typeId = slot.typeId;
                    DrawFieldsInto(layout, *table, component, context);
                }
            }
            ImGui::PopID();
        }

        ImGui::Spacing();
        DrawAddComponent(*object);
    }

    // 붙일 수 있는 것은 레지스트리에 있는 것이다. 인스펙터는 여기서도 타입을
    // 하나도 모른다 - 표가 늘면 목록이 는다.
    void InspectorPanel::DrawAddComponent(GameObject& object)
    {
        if (ImGui::Button(Loc::TextOr(LocKeys::InspectorAddComponent, "Add Component"),
            ImVec2(-FLT_MIN, 0.0f)))
        {
            ImGui::OpenPopup("##AddComponent");
        }
        if (false == ImGui::BeginPopup("##AddComponent"))
        {
            return;
        }
        const Array<const ComponentTypeInfo*> types =
            ComponentRegistry::Get().CollectTypes();
        if (types.IsEmpty())
        {
            ImGui::TextDisabled("%s",
                Loc::TextOr(LocKeys::InspectorNoComponentTypes,
                    "no component type has registered itself"));
        }
        for (std::size_t index = 0; index < types.Size(); ++index)
        {
            const char* name = NameTable::Get().Resolve(types[index]->name);
            if (name == nullptr)
            {
                continue;
            }
            if (false == ImGui::MenuItem(DisplayTypeName(name)))
            {
                continue;
            }
            const EditorObjectId objectId = m_editor->GetObjectIds().Track(&object);
            m_editor->GetCommands().Execute(MakeOwnerPtr<AddComponentCommand>(
                *m_editor->GetCanvas(), m_editor->GetObjectIds(), objectId,
                types[index]->name));
            break;
        }
        ImGui::EndPopup();
    }

    void InspectorPanel::RemoveComponent(GameObject& object, ComponentBase& component)
    {
        Canvas* canvas = m_editor->GetCanvas();
        if (canvas == nullptr)
        {
            return;
        }
        const EditorObjectId objectId = m_editor->GetObjectIds().Track(&object);
        m_editor->GetCommands().Execute(MakeOwnerPtr<RemoveComponentCommand>(
            *canvas, m_editor->GetObjectIds(), objectId, &component));
    }

    bool InspectorPanel::CollectScalarRun(
        const TypeDescriptor& type, void* address, ScalarRun& run)
    {
        if (type.fields == nullptr)
        {
            return false;
        }
        for (std::uint32_t index = 0; index < type.fields->count; ++index)
        {
            const PropertyInfo& property = type.fields->properties[index];
            if (property.type == nullptr || property.Address == nullptr)
            {
                return false;
            }
            void* field = property.Address(address);
            if (field == nullptr)
            {
                return false;
            }
            if (property.type->fields != nullptr)
            {
                // 한 단계 더 내려간다. `Rect` 는 `Vec2` 두 개이고, 기존 엔진은
                // 그것도 한 줄에 그렸다.
                if (false == CollectScalarRun(*property.type, field, run))
                {
                    return false;
                }
                continue;
            }
            if (false == SameName(property.type->typeName, "float"))
            {
                return false;
            }
            if (run.count >= ScalarRun::MaxCount)
            {
                // 다섯 개부터는 한 줄에 넣어 봐야 읽을 수 없다. 타고 내려간다.
                return false;
            }
            run.values[run.count] = static_cast<float*>(field);
            ++run.count;
        }
        return run.count >= 2;
    }

    bool InspectorPanel::DrawScalarRun(
        const TypeDescriptor& type, const ScalarRun& run, const PropertyEditInfo* edit)
    {
        float scratch[ScalarRun::MaxCount] = {};
        for (std::uint32_t index = 0; index < run.count; ++index)
        {
            scratch[index] = *run.values[index];
        }

        bool changed = false;
        // 색은 숫자 네 개가 아니라 색이다. 견본과 고르개가 붙는다.
        if (run.count == 4 && SameName(type.typeName, "JBro.Color"))
        {
            changed = ImGui::ColorEdit4("##value", scratch,
                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
        }
        else if (edit != nullptr && edit->hasRange)
        {
            changed = ImGui::SliderScalarN("##value", ImGuiDataType_Float, scratch,
                static_cast<int>(run.count), &edit->rangeMin, &edit->rangeMax);
        }
        else
        {
            changed = ImGui::DragScalarN("##value", ImGuiDataType_Float, scratch,
                static_cast<int>(run.count), 0.01f);
        }
        if (false == changed)
        {
            return false;
        }
        // **주소마다 따로 써 넣는다.** 붙어 있으리라 믿지 않는다.
        for (std::uint32_t index = 0; index < run.count; ++index)
        {
            *run.values[index] = scratch[index];
        }
        return true;
    }

    bool InspectorPanel::DrawLeaf(
        const TypeDescriptor& type,
        void* address,
        const PropertyEditInfo* edit,
        const String& before,
        bool snapped)
    {
        const bool hasRange = edit != nullptr && edit->hasRange;
        if (SameName(type.typeName, "float"))
        {
            float* value = static_cast<float*>(address);
            return hasRange
                ? ImGui::SliderFloat("##value", value, edit->rangeMin, edit->rangeMax)
                : ImGui::DragFloat("##value", value, 0.01f);
        }
        if (SameName(type.typeName, "bool"))
        {
            return ImGui::Checkbox("##value", static_cast<bool*>(address));
        }
        if (SameName(type.typeName, "int32"))
        {
            int* value = static_cast<int*>(address);
            return hasRange
                ? ImGui::SliderInt("##value", value,
                    static_cast<int>(edit->rangeMin), static_cast<int>(edit->rangeMax))
                : ImGui::DragInt("##value", value);
        }
        if (false == snapped)
        {
            ImGui::TextDisabled("%s", Loc::TextOr(LocKeys::InspectorTooLong,
                "(too long to show)"));
            return false;
        }
        if (type.codec->FromText == nullptr)
        {
            ImGui::TextUnformatted(before.c_str());
            return false;
        }
        char text[TextCapacity] = {};
        const std::size_t copied =
            before.size() < sizeof(text) - 1 ? before.size() : sizeof(text) - 1;
        std::memcpy(text, before.c_str(), copied);
        if (ImGui::InputText("##value", text, sizeof(text),
            ImGuiInputTextFlags_EnterReturnsTrue))
        {
            // 읽지 못하는 글자는 값을 건드리지 않는다. 코덱이 그렇게 약속한다.
            return type.codec->FromText(address, text, std::strlen(text));
        }
        return false;
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

    void InspectorPanel::DrawFieldsInto(
        Widget::FormLayout& layout,
        const PropertyTable& table,
        void* owner,
        Context& context)
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
                layout.Row(
                    Widget::FieldLabel(label != nullptr ? label : "?")
                        .Disabled()
                        .Tooltip(Loc::TextOr(LocKeys::InspectorTooDeep,
                            "(too deeply nested to edit)")),
                    [&]() {
                        ImGui::TextDisabled("%s",
                            Loc::TextOr(LocKeys::InspectorTooDeep,
                                "(too deeply nested to edit)"));
                    });
                continue;
            }
            context.path.indices[context.path.depth] = index;
            ++context.path.depth;

            Widget::IdScope id(static_cast<int>(index));
            const bool editable = property.edit == nullptr || property.edit->editable;
            const char* tooltip =
                property.edit != nullptr ? property.edit->tooltip : nullptr;

            // **라벨은 왼쪽 칸이 그린다.** 위젯에 넘기면 좁은 패널에서 잘린다.
            layout.Row(
                Widget::FieldLabel(label != nullptr ? label : "?")
                    .Disabled(false == editable)
                    .Tooltip(tooltip),
                [&]() {
                    DrawValue(label != nullptr ? label : "?", *property.type, address,
                        property.edit, context);
                });

            --context.path.depth;
        }
    }

    void InspectorPanel::DrawValue(
        const char* label,
        const TypeDescriptor& type,
        void* address,
        const PropertyEditInfo* edit,
        Context& context)
    {
        const bool editable = edit == nullptr || edit->editable;
        Widget::DisableScope disabled(false == editable);

        // enum 은 타입이 이름표를 들고 있다. 이름으로 알아볼 필요가 없다.
        if (type.enumNames != nullptr && type.enumNames->ToIndex != nullptr)
        {
            const EnumNames& names = *type.enumNames;
            String before;
            ToText(type, address, before);
            int current = names.ToIndex(address);
            if (ImGui::Combo("##value", &current, names.names,
                    static_cast<int>(names.count))
                && names.FromIndex != nullptr)
            {
                names.FromIndex(address, current);
                CommitEdit(type, address, before, context);
            }
            return;
        }
        // 컨테이너는 아직 조작 함수가 없다(ArrayOps/TableOps 미구현). 개수만 보여 준다.
        if (type.arrayOps != nullptr && type.arrayOps->GetSize != nullptr)
        {
            ImGui::Text(Loc::TextOr(LocKeys::ListElementCount, "%d item(s)"),
                static_cast<int>(type.arrayOps->GetSize(address)));
            return;
        }
        if (type.tableOps != nullptr && type.tableOps->GetSize != nullptr)
        {
            ImGui::Text(Loc::TextOr(LocKeys::ListElementCount, "%d item(s)"),
                static_cast<int>(type.tableOps->GetSize(address)));
            return;
        }

        // **한 값은 한 줄이다**(ProjectRule §11.3). 잎사귀가 전부 실수인 작은
        // 구조체는 타고 내려가지 않고 한 줄에 그린다 - 색 하나가 네 줄을 먹으면
        // 사용자는 색을 고르는 대신 숫자를 맞추게 된다.
        ScalarRun run;
        if (CollectScalarRun(type, address, run))
        {
            String before;
            const bool snapped = ToText(type, address, before);
            if (DrawScalarRun(type, run, edit) && editable)
            {
                if (snapped)
                {
                    CommitEdit(type, address, before, context);
                }
            }
            return;
        }

        if (type.fields != nullptr)
        {
            // 한 줄에 담기지 않는 구조다. 접을 수 있게 두고 타고 내려간다.
            if (ImGui::TreeNodeEx(label != nullptr ? label : "?",
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
            {
                // **표를 트리보다 먼저 닫는다.** `TreeNodeEx` 가 밀어 넣은 Id 를
                // `TreePop` 이 빼내는데, 그 뒤에 `EndTable` 이 돌면 표가 자기
                // 것이 아닌 Id 스택 위에서 끝난다 - ImGui 가 단언으로 잡는다.
                {
                    Widget::FormLayout nested("##nested");
                    DrawFieldsInto(nested, *type.fields, address, context);
                }
                ImGui::TreePop();
            }
            return;
        }

        if (type.codec != nullptr)
        {
            // **흔한 잎사귀만 제 위젯을 갖는다.** 나머지는 코덱의 글자 왕복으로 그린다.
            //
            // 타입 이름으로 가르는 것이 마음에 걸리지만, 여기 하나뿐이고 빠진 타입은
            // 글자 칸으로 **degrade 할 뿐 깨지지 않는다**. 기존 엔진의 18값 enum 과
            // 다른 점이 그것이다 - 거기서는 빠진 값이 곧 그리지 못하는 필드였다.
            String before;
            const bool snapped = ToText(type, address, before);
            if (DrawLeaf(type, address, edit, before, snapped) && snapped && editable)
            {
                CommitEdit(type, address, before, context);
            }
            return;
        }

        // 필드도 코덱도 없는 타입이다. 등록이 덜 된 것이고, 빈 줄로 두면 모른다.
        ImGui::TextDisabled("%s",
            Loc::TextOr(LocKeys::InspectorUndrawableType, "(no way to show this type)"));
    }
}

#include "InspectorPanel.h"

#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Editor/Command/ComponentCommands.h>
#include <JBro/Editor/Command/CompoundCommand.h>
#include <JBro/Editor/Command/ListEdit.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/ScalarRun.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/Editor/Widget/List.h>
#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/NameTable.h>

#include <imgui.h>

#include <cstring>
#include <utility>

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
                    context.owner = object;
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

    Array<InspectorPanel::EditTarget> InspectorPanel::CollectEditTargets(
        const Context& context) const
    {
        Array<EditTarget> targets;
        std::uint32_t ordinal = 0;
        if (context.component == nullptr || context.owner == nullptr
            || false == FindComponentOrdinal(*context.owner, *context.component, ordinal))
        {
            // 주인에게서 자리를 셀 수 없으면 가리킬 방법이 없다. 포인터로 쓰는
            // 커맨드를 만들어 두면 지웠다 되살린 뒤에 조용히 헛돈다.
            return targets;
        }

        // **조상이 함께 골라졌으면 뺀다.** 부모를 옮기면 자식은 따라 움직이므로
        // 둘 다 대상으로 삼으면 자식에게 두 번 적용된다(기존 `GetSelectedTopLevel`).
        const Array<GameObject*> chosen = m_editor->GetTopLevelSelectedObjects();
        for (std::size_t index = 0; index < chosen.Size(); ++index)
        {
            if (ComponentBase* found =
                FindComponentAt(*chosen[index], context.typeId, ordinal))
            {
                targets.Add(EditTarget{chosen[index], found});
            }
        }
        if (targets.IsEmpty())
        {
            // 고른 것이 없거나(인스펙터만 열어 둔 경우) 셈이 어긋났다.
            // 눈앞의 것 하나는 반드시 고쳐져야 한다.
            targets.Add(EditTarget{context.owner, context.component});
        }
        return targets;
    }

    void InspectorPanel::CommitEdit(
        const TypeDescriptor& type, void* address, const String& before, Context& context)
    {
        String after;
        if (false == ToText(type, address, after) || after == before)
        {
            return;
        }

        // **숫자는 델타로, 나머지는 그대로 옮긴다.**
        //
        // 위치가 저마다 다른 오브젝트 셋을 골라 놓고 x 를 끌었을 때, 셋이 한
        // 자리로 모이면 그것은 옮긴 것이 아니라 뭉갠 것이다 - 기존 엔진이
        // 트랜스폼 편집을 델타로 다루는 이유다. 반대로 켜짐 여부나 enum 에는
        // 델타라는 것이 없으므로 고른 값을 그대로 준다.
        ScalarRun editedRun;
        const bool numeric = CollectScalarRun(type, address, editedRun)
            || SameName(type.typeName, "float");
        float delta[ScalarRun::MaxCount] = {};
        std::uint32_t deltaCount = 0;
        if (numeric)
        {
            // 지금 주소에는 위젯이 쓴 값이 들어 있고, `before` 가 그 전 값이다.
            ScalarRun afterRun;
            if (false == CollectScalarRun(type, address, afterRun))
            {
                afterRun.values[0] = static_cast<float*>(address);
                afterRun.count = 1;
            }
            for (std::uint32_t at = 0; at < afterRun.count; ++at)
            {
                delta[at] = *afterRun.values[at];
            }
            deltaCount = afterRun.count;
        }

        // **바뀐 값을 도로 되돌려 놓는다.** 커맨드의 `Execute` 가 다시 적용하므로
        // 쓰는 길이 하나로 남는다 - 위젯이 한 번, 커맨드가 한 번 쓰면 되돌리기가
        // 무엇을 되돌리는지가 둘로 갈린다.
        if (type.codec != nullptr && type.codec->FromText != nullptr)
        {
            type.codec->FromText(address, before.c_str(), before.size());
        }
        if (numeric)
        {
            // 되돌린 뒤에 빼야 진짜 델타다.
            ScalarRun beforeRun;
            if (false == CollectScalarRun(type, address, beforeRun))
            {
                beforeRun.values[0] = static_cast<float*>(address);
                beforeRun.count = 1;
            }
            for (std::uint32_t at = 0; at < deltaCount && at < beforeRun.count; ++at)
            {
                delta[at] -= *beforeRun.values[at];
            }
        }

        const Array<EditTarget> targets = CollectEditTargets(context);
        auto compound = MakeOwnerPtr<CompoundCommand>("Set Property");
        for (std::size_t index = 0; index < targets.Size(); ++index)
        {
            ComponentBase* target = targets[index].component;
            ComponentAddress address;
            if (false == MakeComponentAddress(m_editor->GetObjectIds(),
                *targets[index].owner, *target, address))
            {
                continue;
            }
            String targetBefore;
            if (false == SetPropertyCommand::ReadValue(
                *target, context.typeId, context.path, targetBefore))
            {
                continue;
            }

            String targetAfter = after;
            if (numeric && target != context.component)
            {
                // 그 대상의 값에 같은 델타를 얹고, 그 결과를 글자로 뜬다.
                // **뜬 뒤에는 도로 돌려놓는다** - 쓰는 것은 커맨드의 몫이다.
                void* targetAddress = nullptr;
                const TypeDescriptor* targetType = nullptr;
                if (false == SetPropertyCommand::ResolveLeaf(*target, context.typeId,
                    context.path, targetAddress, targetType))
                {
                    continue;
                }
                ScalarRun run;
                if (false == CollectScalarRun(*targetType, targetAddress, run))
                {
                    run.values[0] = static_cast<float*>(targetAddress);
                    run.count = 1;
                }
                for (std::uint32_t at = 0; at < run.count && at < deltaCount; ++at)
                {
                    *run.values[at] += delta[at];
                }
                const bool read = SetPropertyCommand::ReadValue(
                    *target, context.typeId, context.path, targetAfter);
                SetPropertyCommand::ApplyValue(
                    *target, context.typeId, context.path, targetBefore);
                if (false == read)
                {
                    continue;
                }
            }
            if (targetAfter == targetBefore)
            {
                continue;
            }
            compound->Add(MakeOwnerPtr<SetPropertyCommand>(
                m_editor->GetObjectIds(), address, context.path,
                targetBefore, targetAfter));
        }

        m_editor->GetCommands().Execute(std::move(compound));
    }

    void InspectorPanel::DrawArray(
        const TypeDescriptor& type, void* address, bool editable, Context& context)
    {
        const ArrayOps& ops = *type.arrayOps;
        const TypeDescriptor* element = type.element;
        if (element == nullptr)
        {
            ImGui::TextDisabled("%s",
                Loc::TextOr(LocKeys::InspectorUndrawableType, "(no way to show this type)"));
            return;
        }

        // **목록은 배열에 쓰지 않고 편집을 적어 둔다**(D-86).
        //
        // 위젯이 원소에 쓴 값은 그 자리에서 도로 되돌리고 "몇 번째에 얼마를 더했다" 로
        // 적는다. 추가·삭제·옮기기는 아예 쓰지 않고 적기만 한다. 다 그린 뒤 고른 대상마다
        // 그 편집을 다시 적용해 커맨드로 묶는다 - 쓰는 길이 하나로 남는다(D-71).
        // 처음에는 전부 배열에 곧장 써서 되돌릴 수 없었고, 여럿 골라도 주된 것만 바뀌었다.
        Array<ListEdit> edits;
        std::uint32_t flags = Widget::ListFlagsShowIndex;
        if (false == editable)
        {
            flags |= Widget::ListFlagsReadOnly;
        }

        Widget::ListVirtual(
            "##array",
            static_cast<int>(ops.GetSize(address)),
            [&](int index) -> bool {
                void* item = ops.GetElement(address, static_cast<std::size_t>(index));
                if (item == nullptr)
                {
                    return false;
                }
                ListEdit edit;
                edit.kind = ListEdit::Kind::SetElement;
                edit.index = static_cast<std::uint32_t>(index);

                // 원소도 한 값 한 줄이다. 라벨은 목록이 이미 번호로 그렸다.
                ScalarRun run;
                if (CollectScalarRun(*element, item, run))
                {
                    float before[ScalarRun::MaxCount] = {};
                    for (std::uint32_t at = 0; at < run.count; ++at)
                    {
                        before[at] = *run.values[at];
                    }
                    if (false == DrawScalarRun(*element, run, nullptr))
                    {
                        return false;
                    }
                    edit.deltaCount = run.count;
                    for (std::uint32_t at = 0; at < run.count; ++at)
                    {
                        edit.delta[at] = *run.values[at] - before[at];
                        *run.values[at] = before[at];
                    }
                    edits.Add(std::move(edit));
                    return true;
                }
                if (element->codec == nullptr)
                {
                    ImGui::TextDisabled("%s",
                        Loc::TextOr(LocKeys::InspectorUndrawableType,
                            "(no way to show this type)"));
                    return false;
                }

                String before;
                const bool snapped = ToText(*element, item, before);
                if (false == DrawLeaf(*element, item, nullptr, before, snapped) || false == snapped)
                {
                    return false;
                }
                // 실수 하나는 델타로, 나머지(bool·int·enum·글자)는 고른 값을 그대로 옮긴다.
                // 델타는 **되돌린 뒤에** 잰다 - 되돌리기 전에 재면 위젯이 쓴 값이 델타가 된다.
                ScalarRun single;
                const bool numeric = CollectNumbers(*element, item, single);
                const float after = numeric ? *single.values[0] : 0.0f;
                if (false == numeric && false == ToText(*element, item, edit.text))
                {
                    element->codec->FromText(item, before.c_str(), before.size());
                    return false;
                }
                element->codec->FromText(item, before.c_str(), before.size());
                if (numeric)
                {
                    edit.deltaCount = 1;
                    edit.delta[0] = after - *single.values[0];
                }
                edits.Add(std::move(edit));
                return true;
            },
            [&]() {
                ListEdit edit;
                edit.kind = ListEdit::Kind::Add;
                edits.Add(std::move(edit));
            },
            [&](int index) {
                ListEdit edit;
                edit.kind = ListEdit::Kind::Remove;
                edit.index = static_cast<std::uint32_t>(index);
                edits.Add(std::move(edit));
            },
            [&](int fromIndex, int toIndex) {
                ListEdit edit;
                edit.kind = ListEdit::Kind::Move;
                edit.index = static_cast<std::uint32_t>(fromIndex);
                edit.to = static_cast<std::uint32_t>(toIndex);
                edits.Add(std::move(edit));
            },
            flags);

        if (edits.IsEmpty())
        {
            return;
        }
        const Array<EditTarget> targets = CollectEditTargets(context);
        Array<ComponentAddress> addresses;
        for (std::size_t index = 0; index < targets.Size(); ++index)
        {
            ComponentAddress target;
            if (MakeComponentAddress(m_editor->GetObjectIds(), *targets[index].owner,
                *targets[index].component, target))
            {
                addresses.Add(target);
            }
        }
        m_editor->GetCommands().Execute(MakeListEditCommand(
            m_editor->GetObjectIds(), addresses, context.path, edits));
    }

    // 타고 내려가야 하는 타입인가. 한 줄에 담기는 것과 컨테이너와 enum 은 아니다.
    bool InspectorPanel::NeedsDescent(const TypeDescriptor& type, void* address)
    {
        if (type.fields == nullptr)
        {
            return false;
        }
        if (type.enumNames != nullptr || type.arrayOps != nullptr
            || type.tableOps != nullptr)
        {
            return false;
        }
        ScalarRun run;
        return false == CollectScalarRun(type, address, run);
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

            // **한 줄에 담기지 않는 구조는 같은 표 안에서 이어 그린다.**
            //
            // 값 칸에 표를 하나 더 열면 안쪽 칸 폭이 바깥과 따로 놀아 줄이
            // 어긋나고, 이름이 왼쪽 칸과 트리에 두 번 나온다. 트리 마디를
            // 줄 전체에 걸치게 두고 자식을 같은 표의 다음 줄로 내면 칸이 맞는다.
            if (NeedsDescent(*property.type, address))
            {
                // 줄 전체를 쓴다. 칸을 나누고 값 칸을 비우면 ImGui 가
                // "항목 없이 커서만 옮겼다" 고 단언한다 - 그리고 실제로
                // 트리 마디는 두 칸에 걸쳐 있으므로 나눌 이유도 없다.
                bool opened = false;
                {
                    // **잠긴 값은 타고 내려가도 잠겨 있어야 한다.** 잠금은
                    // `DrawValue` 안에 있었는데, 중첩 구조는 그 길로 가지
                    // 않으므로 여기서 다시 두른다 - 안 그러면 파생값의
                    // 속살만 고칠 수 있게 된다.
                    Widget::DisableScope locked(false == editable);
                    layout.FullRow([&]() {
                        opened = ImGui::TreeNodeEx(label != nullptr ? label : "?",
                            ImGuiTreeNodeFlags_DefaultOpen
                                | ImGuiTreeNodeFlags_SpanAllColumns);
                    });
                    if (opened)
                    {
                        DrawFieldsInto(layout, *property.type->fields, address, context);
                        ImGui::TreePop();
                    }
                }
                --context.path.depth;
                continue;
            }

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
        // 배열은 목록 위젯이 그린다. 원소 접근이 전부 조작 함수를 거치므로
        // 인스펙터는 여기서도 무엇이 든 배열인지 모른다(ProjectRule §11.1).
        if (type.arrayOps != nullptr && type.arrayOps->GetSize != nullptr)
        {
            DrawArray(type, address, editable, context);
            return;
        }
        if (type.tableOps != nullptr && type.tableOps->GetSize != nullptr)
        {
            // 표는 키를 받아야 원소를 만들 수 있고, 그 키 칸을 어떻게 그릴지가
            // 아직 정해지지 않았다. 지금은 개수만 보여 준다 - 목록 위젯의
            // `drawAddRow` 자리가 그것을 위해 열려 있다.
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

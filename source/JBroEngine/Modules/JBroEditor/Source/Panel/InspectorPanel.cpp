#include "InspectorPanel.h"

#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Editor/Command/ComponentCommands.h>
#include <JBro/Editor/Command/ObjectCommands.h>
#include <JBro/Editor/Command/CompoundCommand.h>
#include <JBro/Editor/Command/ListEdit.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorUI.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/ScalarRun.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/Editor/Widget/List.h>
#include <JBro/Editor/Widget/TextField.h>
#include <JBro/Editor/Widget/Scalar.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/EnumCombo.h>
#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Editor/Widget/AssetField.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Editor/Command/SetAssetMetaCommand.h>
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
        // 인스펙터의 미리보기가 차지하는 최대 변(픽셀)이다. 칸이 더 넓어도 이보다 크게
        // 그리지 않는다 - 그림이 창을 다 먹으면 정작 고칠 값들이 스크롤 밖으로 나간다.
        constexpr float PreviewMaxSide = 160.0f;
    }

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
        // 에셋 참조 필드는 `Id` 로 끝나는 `JBro.Uuid` 다 - 해석 패스와 같은 규칙이다(D-115).
        bool IsAssetIdName(const char* name)
        {
            if (name == nullptr)
            {
                return false;
            }
            const std::size_t length = std::strlen(name);
            return length > 2 && name[length - 2] == 'I' && name[length - 1] == 'd';
        }

        // `spriteId` → `Sprite`. 앞부분이 에셋 타입 이름이면 그 타입만 보이고, 아니면 전부다.
        AssetType AssetTypeOfIdName(const char* name)
        {
            char buffer[32] = {};
            const std::size_t length = std::strlen(name) - 2;
            if (length == 0 || length >= sizeof(buffer))
            {
                return AssetType::Unknown;
            }
            std::memcpy(buffer, name, length);
            if (buffer[0] >= 'a' && buffer[0] <= 'z')
            {
                buffer[0] = static_cast<char>(buffer[0] - 'a' + 'A');
            }
            return AssetTypeRules::ParseTypeName(std::string_view(buffer, length));
        }

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
            // 오브젝트 대신 에셋이 골라져 있으면 그 임포트 옵션이다(D-120).
            if (const AssetMetaFile* meta = m_editor->GetSelectedAssetMeta())
            {
                DrawAsset(*meta);
                return;
            }
            Widget::HintTextF("%s",
                Loc::TextOr(LocKeys::InspectorNothingSelected, "nothing is selected"));
            return;
        }

        // 여럿 골랐으면 그렇다고 말한다. 주된 것만 보여 주면서 아무 말도 하지
        // 않으면, 나머지가 골라져 있다는 것을 화면에서 알 수 없다.
        const std::size_t chosen = m_editor->GetSelectionCount();
        if (chosen > 1)
        {
            Widget::HintTextF(
                Loc::TextOr(LocKeys::InspectorMultipleSelected, "%d objects selected"),
                static_cast<int>(chosen));
        }

        {
            Widget::FormLayout header("##object");
            header.Row(
                Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorActive, "Active")),
                [&]() {
                    bool active = object->IsActiveSelf();
                    if (Widget::Checkbox("##active", active))
                    {
                        // **고른 것이 다 따라간다**(D-142). 여럿을 골라 놓고 하나만 꺼지면
                        // 나머지는 화면에서 그대로라 무엇이 바뀌었는지 알 수 없다.
                        Array<EditorObjectId> ids;
                        const Array<GameObject*> chosenObjects = m_editor->GetSelectedObjects();
                        for (std::size_t index = 0; index < chosenObjects.Size(); ++index)
                        {
                            if (chosenObjects[index] != nullptr)
                            {
                                ids.Add(m_editor->GetObjectIds().Track(chosenObjects[index]));
                            }
                        }
                        if (ids.IsEmpty())
                        {
                            ids.Add(m_editor->GetObjectIds().Track(object));
                        }
                        m_editor->GetCommands().Execute(
                            MakeOwnerPtr<SetObjectActiveCommand>(
                                m_editor->GetObjectIds(), ids, active));
                    }
                });
            // **이름을 고칠 수 있다**(D-142). 예전에는 글자로 보여 주기만 해서, 만든
            // 오브젝트의 이름이 `GameObject` 인 채로 굳었다. 이름은 태그다(D-51).
            header.Row(
                Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorName, "Name")),
                [&]() {
                    const char* tag = object->GetTag();
                    // **치는 중이 아니면 늘 오브젝트의 이름을 든다.** 고른 것이 바뀔 때만
                    // 다시 읽으면, 이름 바꾸기를 되돌린 뒤에도 칸에는 옛 글자가 남는다.
                    if (m_namedObject != object || false == m_nameEditing)
                    {
                        m_namedObject = object;
                        m_name = tag != nullptr ? tag : "";
                    }
                    // **편집이 끝날 때 한 번 커맨드를 만든다.** 글자마다 만들면 되돌리기가
                    // 글자 수만큼 필요해진다 - 커맨드 병합은 마우스 드래그에만 걸린다.
                    const bool finished =
                        Widget::TextField("##name", m_name).CommitOnFinish().Draw();
                    m_nameEditing = ImGui::IsItemActive();
                    if (finished)
                    {
                        m_editor->GetCommands().Execute(
                            MakeOwnerPtr<RenameObjectCommand>(m_editor->GetObjectIds(),
                                m_editor->GetObjectIds().Track(object), m_name.c_str()));
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
            const bool opened = Widget::CollapsingSection(
                typeName != nullptr
                    ? typeName
                    : Loc::TextOr(LocKeys::InspectorUnknownComponent,
                        "(unknown component)"));
            // **머리에 우클릭하면 뗄 수 있다.** 기존 엔진도 여기가 그 자리다.
            // 접힌 채로도 눌러야 하므로 머리를 그린 직후에 둔다.
            if (Widget::BeginContextMenu("##ComponentMenu"))
            {
                // **자리 옮기기.** 슬롯 순서가 스크립트 실행 순서다(D-45). 양 끝에서는 그쪽
                // 항목을 잠근다.
                std::size_t moveTo = index;
                if (index == 0)
                {
                    ImGui::BeginDisabled();
                }
                if (Widget::MenuItem(Loc::TextOr(LocKeys::InspectorMoveComponentUp, "Move Up")))
                {
                    moveTo = index - 1;
                }
                if (index == 0)
                {
                    ImGui::EndDisabled();
                }
                const bool last = index + 1 >= components.Size();
                if (last)
                {
                    ImGui::BeginDisabled();
                }
                if (Widget::MenuItem(Loc::TextOr(LocKeys::InspectorMoveComponentDown, "Move Down")))
                {
                    moveTo = index + 1;
                }
                if (last)
                {
                    ImGui::EndDisabled();
                }
                if (moveTo != index)
                {
                    MoveComponent(*object, index, moveTo);
                    Widget::EndContextMenu();
                    ImGui::PopID();
                    // 옮긴 뒤에는 이 프레임의 슬롯 배열이 더 이상 맞지 않는다. 다음 프레임에 다시 그린다.
                    return;
                }
                ImGui::Separator();
                if (Widget::MenuItem(Loc::TextOr(LocKeys::InspectorRemoveComponent,
                        "Remove Component")))
                {
                    RemoveComponent(*object, *component);
                    Widget::EndContextMenu();
                    ImGui::PopID();
                    // 뗀 뒤에는 이 프레임의 슬롯 배열이 더 이상 맞지 않는다.
                    // 계속 돌면 죽은 슬롯을 읽는다 - 다음 프레임에 다시 그린다.
                    return;
                }
                Widget::EndContextMenu();
            }
            if (opened)
            {
                Widget::FormLayout layout("##component");
                layout.Row(
                    Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorEnabled, "Enabled")),
                    [&]() {
                        bool enabled = component->IsEnabled();
                        if (Widget::Checkbox("##enabled", enabled))
                        {
                            // 이것도 커맨드다(D-142). 끈 것을 되돌릴 수 없으면 편집이 아니다.
                            ComponentAddress address;
                            if (MakeComponentAddress(
                                    m_editor->GetObjectIds(), *object, *component, address))
                            {
                                m_editor->GetCommands().Execute(
                                    MakeOwnerPtr<SetComponentEnabledCommand>(
                                        m_editor->GetObjectIds(), address, enabled));
                            }
                        }
                    });

                const PropertyTable* table = PropertyRegistry::Lookup(slot.typeId);
                if (table == nullptr)
                {
                    layout.FullRow([&]() {
                        // 저장도 안 되는 컴포넌트다. 조용히 빈 칸으로 두면 왜
                        // 안 보이는지 알 수 없으므로 그렇게 말해 준다.
                        Widget::HintTextF("%s",
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
        // 검색 드롭다운 하나다(D-116). 현재 번호를 늘 -1 로 주므로 트리거에는 "컴포넌트
        // 추가" 가 보이고, 고르면 그 자리에서 커맨드 하나가 나간다.
        const Array<const ComponentTypeInfo*> types =
            ComponentRegistry::Get().CollectTypes();
        Array<const char*> names;
        names.Reserve(types.Size());
        for (std::size_t index = 0; index < types.Size(); ++index)
        {
            const char* name = NameTable::Get().Resolve(types[index]->name);
            names.Add(name != nullptr ? DisplayTypeName(name) : nullptr);
        }
        int chosen = -1;
        const bool picked = Widget::FilterCombo("##AddComponent",
            ArrayView<const char* const>(names.Data(), names.Size()), chosen)
            .EmptyText(Loc::TextOr(LocKeys::InspectorAddComponent, "Add Component"))
            .NoItemsText(Loc::TextOr(LocKeys::InspectorNoComponentTypes,
                "no component type has registered itself"))
            .Width(-FLT_MIN)
            .Draw();
        if (false == picked || chosen < 0 || static_cast<std::size_t>(chosen) >= types.Size())
        {
            return;
        }
        const EditorObjectId objectId = m_editor->GetObjectIds().Track(&object);
        m_editor->GetCommands().Execute(MakeOwnerPtr<AddComponentCommand>(
            *m_editor->GetCanvas(), m_editor->GetObjectIds(), objectId,
            types[static_cast<std::size_t>(chosen)]->name));
    }

    void InspectorPanel::MoveComponent(GameObject& object, std::size_t from, std::size_t to)
    {
        const EditorObjectId objectId = m_editor->GetObjectIds().Track(&object);
        m_editor->GetCommands().Execute(MakeOwnerPtr<MoveComponentCommand>(
            m_editor->GetObjectIds(), objectId, from, to));
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
            changed = Widget::ColorField("##value", scratch);
        }
        else
        {
            const bool hasRange = edit != nullptr && edit->hasRange;
            changed = Widget::ScalarRunField("##value", scratch, static_cast<int>(run.count),
                0.01f, hasRange, hasRange ? edit->rangeMin : 0.0f, hasRange ? edit->rangeMax : 0.0f);
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

    const InspectorPanel::AssetChoices& InspectorPanel::ChoicesFor(AssetType type)
    {
        const AssetRegistry& registry = m_editor->GetAssetRegistry();
        AssetChoices* choices = nullptr;
        for (std::size_t index = 0; index < m_assetChoices.Size(); ++index)
        {
            if (m_assetChoices[index].type == type)
            {
                choices = &m_assetChoices[index];
            }
        }
        if (choices == nullptr)
        {
            AssetChoices fresh;
            fresh.type = type;
            m_assetChoices.Add(fresh);
            choices = &m_assetChoices[m_assetChoices.Size() - 1];
        }
        if (choices->built && choices->revision == registry.GetRevision())
        {
            return *choices;
        }
        // 레지스트리가 바뀌었을 때만 다시 모은다. 이름은 여기 복사해 두므로 레코드가 옮겨져도 포인터가 살아 있다.
        choices->built = true;
        choices->revision = registry.GetRevision();
        choices->names.Clear();
        choices->ids.Clear();
        for (std::size_t index = 0; index < registry.GetCount(); ++index)
        {
            const AssetRecord& record = registry.GetRecord(index);
            if (type != AssetType::Unknown && record.type != type)
            {
                continue;
            }
            choices->names.Add(record.relativePath);
            choices->ids.Add(record.id);
        }
        choices->namePointers.Clear();
        choices->namePointers.Reserve(choices->names.Size());
        for (std::size_t index = 0; index < choices->names.Size(); ++index)
        {
            choices->namePointers.Add(choices->names[index].c_str());
        }
        return *choices;
    }

    void InspectorPanel::DrawAssetField(
        const char* fieldName, const TypeDescriptor& type, void* address, Context& context)
    {
        const AssetChoices& choices = ChoicesFor(AssetTypeOfIdName(fieldName));
        String before;
        const bool snapped = ToText(type, address, before);
        const bool changed = Widget::AssetField("##value",
            ArrayView<const char* const>(choices.namePointers.Data(), choices.namePointers.Size()),
            ArrayView<const AssetId>(choices.ids.Data(), choices.ids.Size()),
            *static_cast<AssetId*>(address))
            .Draw();
        if (changed && snapped)
        {
            CommitEdit(type, address, before, context);
        }
    }

    bool InspectorPanel::DrawLeaf(
        const TypeDescriptor& type,
        void* address,
        const PropertyEditInfo* edit,
        const String& before,
        bool snapped)
    {
        // **잎사귀는 공용 위젯으로 그린다**(§11.1). 처음에는 여기가 ImGui 원시 호출 뭉치였다.
        // 끌기는 단추 없이 칸 하나라 `##value` 가 곧 그 칸의 Id 다.
        const bool hasRange = edit != nullptr && edit->hasRange;
        if (SameName(type.typeName, "float"))
        {
            float& value = *static_cast<float*>(address);
            return hasRange
                ? Widget::SliderFloat("##value", value, edit->rangeMin, edit->rangeMax)
                : Widget::DragFloat("##value").Speed(0.01f).StepButtons(false)(value);
        }
        if (SameName(type.typeName, "bool"))
        {
            return Widget::Checkbox("##value", *static_cast<bool*>(address));
        }
        if (SameName(type.typeName, "int32"))
        {
            int& value = *static_cast<int*>(address);
            return hasRange
                ? Widget::SliderInt("##value", value,
                    static_cast<int>(edit->rangeMin), static_cast<int>(edit->rangeMax))
                : Widget::DragInt("##value").StepButtons(false)(value);
        }
        if (false == snapped)
        {
            Widget::HintText(Loc::TextOr(LocKeys::InspectorTooLong,
                "(too long to show)"));
            return false;
        }
        if (type.codec->FromText == nullptr)
        {
            Widget::Text(before.c_str());
            return false;
        }
        String text = before;
        if (Widget::TextField("##value", text).CommitOnEnter().MaxLength(TextCapacity - 1)())
        {
            // 읽지 못하는 글자는 값을 건드리지 않는다. 코덱이 그렇게 약속한다.
            return type.codec->FromText(address, text.c_str(), text.size());
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

    void InspectorPanel::DrawAsset(const AssetMetaFile& meta)
    {
        const AssetRecord* record = m_editor->GetAssetRegistry().Find(meta.id);
        Widget::Text(record != nullptr ? record->relativePath.c_str() : "?");

        // **그림을 보여 준다**(D-147, 기존 `AssetInspectorPreview` 자리). 임포트 옵션을
        // 고치는 자리에 그림이 없으면 무엇을 고치고 있는지 이름으로만 알아야 한다.
        const TextureHandle preview = m_editor->GetAssetThumbnail(meta.id);
        if (preview.IsValid())
        {
            // 칸 너비에 맞추되 원본 비율을 지킨다. 늘여 붙이면 픽셀 아트가 기울어 보인다.
            const float width = ImGui::GetContentRegionAvail().x;
            const float side = width < PreviewMaxSide ? width : PreviewMaxSide;
            Widget::Image(preview, ImVec2(side, side));
            ImGui::Spacing();
        }

        // 편집본은 프레임마다 원본에서 새로 뜬다. 위젯이 고친 값은 커맨드가 파일에 쓰고, 다음 프레임의 원본이 그것을
        // 다시 읽어 온다 - 쓰는 길이 하나다(D-89 와 같은 이유).
        AssetMetaFile scratch = meta;
        AssetEditScope scope;
        scope.scratch = &scratch;
        Context context;
        context.asset = &scope;

        const bool image = AssetTypeRules::IsImageType(meta.type);
        int slot = 0;
        const auto drawBlock = [&](const char* title, const TypeDescriptor& type, void* options, bool spriteBlock) {
            // 컴포넌트와 같은 모양이다: 슬롯 번호 → 접는 머리 → 줄 배치 `##import`.
            ImGui::PushID(slot++);
            scope.spriteBlock = spriteBlock;
            if (Widget::CollapsingSection(title) && type.fields != nullptr)
            {
                Widget::FormLayout layout("##import");
                DrawFieldsInto(layout, *type.fields, options, context);
            }
            ImGui::PopID();
        };
        if (meta.type == AssetType::Texture)
        {
            drawBlock(Loc::TextOr(LocKeys::InspectorTextureImportOptions, "Texture Import Options"),
                TypeDescriptorOf<TextureImportOptions>::Get(), &scratch.textureOptions, false);
        }
        if (image)
        {
            drawBlock(Loc::TextOr(LocKeys::InspectorSpriteImportOptions, "Sprite Import Options"),
                TypeDescriptorOf<SpriteImportOptions>::Get(), &scratch.spriteOptions, true);
        }
    }

    void InspectorPanel::CommitAssetEdit(Context& context)
    {
        const AssetMetaFile* original = m_editor->GetSelectedAssetMeta();
        AssetMetaTarget target;
        if (original == nullptr || context.asset == nullptr || context.asset->scratch == nullptr
            || false == m_editor->DescribeSelectedAssetMeta(target))
        {
            return;
        }
        // 고친 블록은 이제 파일에 있어야 한다.
        AssetMetaFile& scratch = *context.asset->scratch;
        if (context.asset->spriteBlock)
        {
            scratch.hasSpriteOptions = true;
        }
        else
        {
            scratch.hasTextureOptions = true;
        }
        const String before = FormatAssetMetaFile(*original);
        const String after = FormatAssetMetaFile(scratch);
        if (before == after)
        {
            return;
        }
        m_editor->GetCommands().Execute(MakeOwnerPtr<SetAssetMetaCommand>(target, before, after));
    }

    void InspectorPanel::CommitEdit(
        const TypeDescriptor& type, void* address, const String& before, Context& context)
    {
        if (context.asset != nullptr)
        {
            (void)type;
            (void)address;
            (void)before;
            CommitAssetEdit(context);
            return;
        }
        if (context.element != nullptr)
        {
            // 목록 원소 안의 잎사귀다. 커맨드는 목록 위젯이 다 그린 뒤에 만든다(D-89).
            RecordElementEdit(type, address, before, context);
            return;
        }
        // **글자는 커맨드가 쓰는 길로 뜬다.** 처음에는 코덱으로 떠서, 코덱이 없는 숫자 묶음
        // (`Vec2`·`Color`)은 여기서 돌아갔다 - 위젯이 쓴 값이 커맨드 없이 남았다(D-89).
        String after;
        if (false == SetPropertyCommand::ReadValue(
                *context.component, context.typeId, context.path, after)
            || after == before)
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
        SetPropertyCommand::ApplyValue(*context.component, context.typeId, context.path, before);
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
            Widget::HintTextF("%s",
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
        // 필드를 가진 원소는 접기 마디로 그려지고(D-89), 마디의 펼침은 행 번호에 붙어 있다.
        // 원소를 옮기거나 지우면 펼침도 따라가야 한다 - 마디 이름이 그 상태의 열쇠다.
        const char* nodeName = NeedsDescent(*element)
            ? DisplayTypeName(NameTable::Get().Resolve(element->typeName))
            : nullptr;
        const int count = static_cast<int>(ops.GetSize(address));
        std::uint32_t flags = Widget::ListFlagsShowIndex;
        if (false == editable)
        {
            flags |= Widget::ListFlagsReadOnly;
        }

        Widget::ListVirtual(
            "##array",
            count,
            [&](int index) -> bool {
                void* item = ops.GetElement(address, static_cast<std::size_t>(index));
                if (item == nullptr)
                {
                    return false;
                }
                // 원소 안의 편집은 컴포넌트 길 대신 이 원소를 들고 적힌다(D-89).
                ElementScope scope;
                scope.edits = &edits;
                scope.index = static_cast<std::uint32_t>(index);
                Context elementContext = context;
                elementContext.element = &scope;
                const std::size_t recorded = edits.Size();
                DrawElement(*element, item, elementContext);
                return edits.Size() != recorded;
            },
            [&]() {
                ListEdit edit;
                edit.kind = ListEdit::Kind::Add;
                edits.Add(std::move(edit));
            },
            [&](int index) {
                if (nodeName != nullptr)
                {
                    Widget::DropRowInt(nodeName, index, count);
                }
                ListEdit edit;
                edit.kind = ListEdit::Kind::Remove;
                edit.index = static_cast<std::uint32_t>(index);
                edits.Add(std::move(edit));
            },
            [&](int fromIndex, int toIndex) {
                if (nodeName != nullptr)
                {
                    Widget::CarryRowInt(nodeName, fromIndex, toIndex);
                }
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

    void InspectorPanel::DrawElement(const TypeDescriptor& type, void* address, Context& context)
    {
        if (false == NeedsDescent(type))
        {
            // **원소도 필드와 같은 잎사귀 규칙이다.** 한 줄 숫자 묶음, enum 콤보, 범위, 글자 칸을
            // 여기서 따로 그리지 않는다. 라벨은 목록이 이미 번호로 그렸다.
            DrawValue("##element", type, address, nullptr, context);
            return;
        }

        // **필드를 가진 구조체는 접기 마디이고, 기본은 접힘이다**(D-89). 원소가 많아도 목록이
        // 짧게 남는다. 펼침 상태는 행 번호에 붙으므로, 원소를 옮기면 펼침은 자리에 남는다.
        const char* name = DisplayTypeName(NameTable::Get().Resolve(type.typeName));
        // 행의 내용 폭이다. 표는 이만큼만 쓴다 - 남은 폭을 다 쓰면 행 끝의 삭제 표시가 밀려난다.
        const float width = ImGui::CalcItemWidth();
        // 계층 패널의 트리 위젯(`Widget::Tree`)은 쓰지 않는다. 그 위젯은 고름·올려놓음 배경을
        // 줄 왼쪽 끝부터 칠해, 목록 행에서는 손잡이와 번호를 덮었다. 중첩 구조체 필드와 같은 마디다.
        if (false == Widget::FoldNode(name != nullptr ? name : "?", ImGuiTreeNodeFlags_None))
        {
            return;
        }
        {
            // **마디가 연 들여쓰기를 필드 표에서는 돌려받는다.** 행 안은 손잡이·번호·삭제 표시를
            // 빼고 남은 자리라, 들여쓰기만큼 값 칸이 더 줄면 좁은 패널에서 값을 읽을 수 없다.
            // Id 는 마디 아래에 그대로 둔다 - 원소마다 필드 표가 따로 서야 한다.
            ImGui::Unindent();
            {
                Widget::FormLayout layout(
                    "##element", 4.0f, ImVec2(2.0f, 1.0f), 0.0f, width);
                DrawFieldsInto(layout, *type.fields, address, context);
            }
            ImGui::Indent();
        }
        Widget::TreePop();
    }

    ListEdit InspectorPanel::MakeElementEdit(const ElementScope& scope)
    {
        ListEdit edit;
        edit.kind = ListEdit::Kind::SetElement;
        edit.index = scope.index;
        for (std::uint32_t step = 0; step < scope.fieldDepth; ++step)
        {
            edit.fieldPath[step] = scope.fieldPath[step];
        }
        edit.fieldDepth = scope.fieldDepth;
        return edit;
    }

    void InspectorPanel::RecordElementRun(
        const ScalarRun& run, const float before[ScalarRun::MaxCount], Context& context)
    {
        // 위젯이 쓴 값은 그 자리에서 도로 되돌리고 "이 원소의 이 필드에 얼마를 더했다" 로 적는다.
        ListEdit edit = MakeElementEdit(*context.element);
        edit.deltaCount = run.count;
        for (std::uint32_t at = 0; at < run.count; ++at)
        {
            edit.delta[at] = *run.values[at] - before[at];
            *run.values[at] = before[at];
        }
        context.element->edits->Add(std::move(edit));
    }

    void InspectorPanel::RecordElementEdit(
        const TypeDescriptor& type, void* address, const String& before, Context& context)
    {
        // 실수 하나는 델타로, 나머지(bool·int·enum·글자)는 고른 값을 그대로 옮긴다(D-83).
        // 델타는 **되돌린 뒤에** 잰다 - 되돌리기 전에 재면 위젯이 쓴 값이 델타가 된다.
        ListEdit edit = MakeElementEdit(*context.element);
        ScalarRun single;
        const bool numeric = CollectNumbers(type, address, single);
        const float after = numeric ? *single.values[0] : 0.0f;
        const bool captured = numeric || ToText(type, address, edit.text);
        type.codec->FromText(address, before.c_str(), before.size());
        if (false == captured)
        {
            return;
        }
        if (numeric)
        {
            edit.deltaCount = 1;
            edit.delta[0] = after - *single.values[0];
        }
        context.element->edits->Add(std::move(edit));
    }

    // 타고 내려가야 하는 타입인가. 한 줄에 담기는 것과 컨테이너와 enum 은 아니다.
    bool InspectorPanel::NeedsDescent(const TypeDescriptor& type)
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
        return false == IsScalarRunType(type);
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
            // 목록 원소 안이면 컴포넌트 길이 아니라 원소 안의 필드 길이다(D-89).
            const bool inElement = context.element != nullptr;
            const bool tooDeep = inElement
                ? context.element->fieldDepth >= ListEdit::MaxFieldDepth
                : context.path.depth >= SetPropertyCommand::MaxDepth;
            if (tooDeep)
            {
                // 너무 깊다. 보여는 주되 고치지는 못하게 둔다 - 잘못된 길로 쓰는
                // 것보다 낫다.
                layout.Row(
                    Widget::FieldLabel(label != nullptr ? label : "?")
                        .Disabled()
                        .Tooltip(Loc::TextOr(LocKeys::InspectorTooDeep,
                            "(too deeply nested to edit)")),
                    [&]() {
                        Widget::HintTextF("%s",
                            Loc::TextOr(LocKeys::InspectorTooDeep,
                                "(too deeply nested to edit)"));
                    });
                continue;
            }
            if (inElement)
            {
                context.element->fieldPath[context.element->fieldDepth] = index;
                ++context.element->fieldDepth;
            }
            else
            {
                context.path.indices[context.path.depth] = index;
                ++context.path.depth;
            }
            const auto leave = [&]() {
                if (inElement)
                {
                    --context.element->fieldDepth;
                }
                else
                {
                    --context.path.depth;
                }
            };

            // **원소 안의 저장하지 않는 필드는 잠근다**(D-89). 목록은 전체의 글자로 되돌리는데 그
            // 글자에 이 필드가 없다 - 고쳐도 편집이 빠지고, 목록을 되돌릴 때마다 기본값이 된다.
            const bool editable = (property.edit == nullptr || property.edit->editable)
                && (false == inElement || property.serialize);
            const char* tooltip =
                property.edit != nullptr ? property.edit->tooltip : nullptr;

            // **필드를 가진 구조체 원소의 목록은 표를 끊고 줄 전체를 쓴다**(D-89). 값 칸 안에 두면
            // 펼친 원소의 필드 표가 또 라벨 칸을 가져, 좁은 패널에서 값이 몇 픽셀만 남았다
            // (`100` 이 `1` 로 보였다). 라벨은 목록 한 줄 위에 선다.
            //
            // 끊기는 이 필드의 Id 를 쌓기 전에 한다 - 쌓은 채로 표를 닫으면 표가 제 Id 대신 그것을
            // 뺀다. 트리 마디가 열린 자리도 같은 이유로 끊지 못해 값 칸에 둔다.
            if (false == inElement && context.openTrees == 0
                && property.type->arrayOps != nullptr && property.type->element != nullptr
                && NeedsDescent(*property.type->element))
            {
                layout.Break([&]() {
                    Widget::IdScope id(static_cast<int>(index));
                    Widget::FieldLabel(label != nullptr ? label : "?")
                        .Disabled(false == editable)
                        .Tooltip(tooltip)
                        .Draw();
                    Widget::DisableScope locked(false == editable);
                    DrawValue(label != nullptr ? label : "?", *property.type, address,
                        property.edit, context);
                });
                leave();
                continue;
            }

            Widget::IdScope id(static_cast<int>(index));

            // **한 줄에 담기지 않는 구조는 같은 표 안에서 이어 그린다.**
            //
            // 값 칸에 표를 하나 더 열면 안쪽 칸 폭이 바깥과 따로 놀아 줄이
            // 어긋나고, 이름이 왼쪽 칸과 트리에 두 번 나온다. 트리 마디를
            // 줄 전체에 걸치게 두고 자식을 같은 표의 다음 줄로 내면 칸이 맞는다.
            if (NeedsDescent(*property.type))
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
                        opened = Widget::FoldNode(label != nullptr ? label : "?",
                            ImGuiTreeNodeFlags_DefaultOpen
                                | ImGuiTreeNodeFlags_SpanAllColumns);
                    });
                    if (opened)
                    {
                        ++context.openTrees;
                        DrawFieldsInto(layout, *property.type->fields, address, context);
                        --context.openTrees;
                        Widget::TreePop();
                    }
                }
                leave();
                continue;
            }

            // **라벨은 왼쪽 칸이 그린다.** 위젯에 넘기면 좁은 패널에서 잘린다.
            layout.Row(
                Widget::FieldLabel(label != nullptr ? label : "?")
                    .Disabled(false == editable)
                    .Tooltip(tooltip),
                [&]() {
                    // 값의 잠금은 `DrawValue` 가 편집 정보로 두르지만, 원소 안의 저장하지 않는
                    // 필드는 편집 정보에 없는 잠금이라 여기서 두른다.
                    Widget::DisableScope locked(false == editable);
                    DrawValue(label != nullptr ? label : "?", *property.type, address,
                        property.edit, context);
                });

            leave();
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
            if (Widget::EnumCombo("##value", names, address))
            {
                CommitEdit(type, address, before, context);
            }
            return;
        }
        // **원소 안의 배열·표는 개수만 보여 준다**(D-89). 목록 안의 목록을 고치려면 목록 편집이
        // 재귀해야 하고, 그것은 따로 설계할 일이다.
        if (context.element != nullptr && (type.arrayOps != nullptr || type.tableOps != nullptr))
        {
            const std::size_t count = type.arrayOps != nullptr
                ? (type.arrayOps->GetSize != nullptr ? type.arrayOps->GetSize(address) : 0)
                : (type.tableOps->GetSize != nullptr ? type.tableOps->GetSize(address) : 0);
            Widget::HintTextF(Loc::TextOr(LocKeys::ListElementCount, "%d item(s)"),
                static_cast<int>(count));
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
            Widget::TextF(Loc::TextOr(LocKeys::ListElementCount, "%d item(s)"),
                static_cast<int>(type.tableOps->GetSize(address)));
            return;
        }

        // **한 값은 한 줄이다**(ProjectRule §11.3). 잎사귀가 전부 실수인 작은
        // 구조체는 타고 내려가지 않고 한 줄에 그린다 - 색 하나가 네 줄을 먹으면
        // 사용자는 색을 고르는 대신 숫자를 맞추게 된다.
        ScalarRun run;
        if (CollectScalarRun(type, address, run))
        {
            if (context.element != nullptr)
            {
                // 원소 안에서는 글자를 뜨지 않는다. 칸마다 전 값을 들고 델타로 적는다.
                float before[ScalarRun::MaxCount] = {};
                for (std::uint32_t at = 0; at < run.count; ++at)
                {
                    before[at] = *run.values[at];
                }
                if (DrawScalarRun(type, run, edit) && editable)
                {
                    RecordElementRun(run, before, context);
                }
                return;
            }
            // 숫자 묶음에는 코덱이 없다. 커맨드가 쓰는 글자(전체의 YAML)로 뜬다(D-89). 에셋 옵션에는 컴포넌트가
            // 없다 - 그 편집은 메타 전체를 뜨므로 여기 글자는 쓰이지 않는다.
            String before;
            const bool snapped = context.component != nullptr
                ? SetPropertyCommand::ReadValue(*context.component, context.typeId, context.path, before)
                : context.asset != nullptr;
            if (DrawScalarRun(type, run, edit) && editable)
            {
                if (snapped)
                {
                    CommitEdit(type, address, before, context);
                }
            }
            return;
        }

        // **`AssetId` 는 드롭다운이다**(D-116). 원소 안의 아이디는 아직 글자 칸이다 - 목록 원소
        // 편집은 값을 글자로 모아 커맨드를 만드는 길이라(D-89) 이 칸이 그 길을 타려면 따로 봐야 한다.
        if (context.element == nullptr && SameName(type.typeName, "JBro.Uuid")
            && IsAssetIdName(label))
        {
            DrawAssetField(label, type, address, context);
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
        Widget::HintTextF("%s",
            Loc::TextOr(LocKeys::InspectorUndrawableType, "(no way to show this type)"));
    }
}

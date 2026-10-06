#include "InspectorPanel.h"
#include <JBro/Framework2D/Component/Transform2D.h>
#include "InspectorFieldExtras.h"

#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Button.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Editor/Command/CanvasCommands.h>
#include <JBro/Editor/Command/LayerCommands.h>
#include <JBro/Editor/Command/ComponentCommands.h>
#include <JBro/Editor/Command/ObjectCommands.h>
#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/Command/CompoundCommand.h>
#include <JBro/Editor/Command/ListEdit.h>
#include <JBro/Editor/ComponentMenuTable.h>
#include <JBro/Editor/EditorActions.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorNames.h>
#include <JBro/Editor/EditorUI.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/ScalarRun.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/Editor/Widget/GuideFocus.h>
#include <JBro/Editor/Widget/List.h>
#include <JBro/Editor/Widget/TextField.h>
#include <JBro/Editor/Widget/Scalar.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/EnumCombo.h>
#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Editor/Widget/AssetField.h>
#include <JBro/Editor/Widget/Waveform.h>
#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AudioDecoder.h>
#include <JBro/Audio/AudioSystem.h>
#include <JBro/AudioTypes/AudioBusName.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Editor/EditorPaths.h>
#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Editor/Command/SetAssetMetaCommand.h>
#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/TextStore.h>
#include <JBro/Types/NameTable.h>

#include <imgui.h>

#include <cstring>
#include <utility>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        const ComponentTypeId Transform2DTypeId = MakeStableTypeId(Component::Transform2D::StaticTypeName());
        const NameId AnchorFieldName = MakeNameId("anchor");
    }

    namespace
    {
        // 인스펙터의 미리보기가 차지하는 최대 변(픽셀)이다. 칸이 더 넓어도 이보다 크게
        // 그리지 않는다 - 그림이 창을 다 먹으면 정작 고칠 값들이 스크롤 밖으로 나간다.
        constexpr Float PreviewMaxSide = 160.0f;
    }

    namespace
    {
        // 잎사귀 값을 글자로 주고받는 버퍼다. 코덱이 더 큰 것을 요구하면 그 값은
        // 읽기 전용으로 보여 준다 - 반쪽만 보여 주고 고치게 하면 저장할 때 잘린다.
        constexpr std::size_t TextCapacity = 512;

        Bool SameName(NameId id, const char* text)
        {
            const char* name = NameTable::Get().Resolve(id);
            return name != nullptr && std::strcmp(name, text) == 0;
        }

        // 화면에 나오는 이름은 타입 이름 그대로가 아니다(ProjectRule §11.3).
        // `Component::Transform2D` 의 접두어는 코드가 쓰는 것이지 사람이 읽는 것이
        // 아니다.
        // 에셋 참조 필드는 `Id` 로 끝나는 `JBro.Uuid` 다 - 해석 패스와 같은 규칙이다(D-115).
        Bool IsAssetIdName(const char* name)
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
            return AssetTypeRules::TypeOfIdFieldName(name != nullptr ? std::string_view(name) : std::string_view());
        }

        using EditorNames::DisplayTypeName;

        Bool ToText(const TypeDescriptor& type, const void* address, String& text)
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
        return TypeName;
    }

    const char* InspectorPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::PanelInspector, "Inspector");
    }

    Bool InspectorPanel::OnCreate(EditorApplication& editor)
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
        // 지난 프레임에 미리 듣기 칸을 그리지 않았으면(다른 것을 골랐다) 미리 듣기를 멈춘다.
        if (false == m_audioDrawn)
        {
            if (System::AudioSystem* audio = m_editor->GetAudio(); audio != nullptr && audio->IsPreviewPlaying())
            {
                audio->StopPreview();
            }
            m_audioAsset = {};
        }
        m_audioDrawn = false;
        GameObject* object = m_editor->GetSelectedObject();
        if (object == nullptr)
        {
            // 오브젝트 대신 에셋이 골라져 있으면 그 임포트 옵션이다(D-120).
            if (const AssetMetaFile* meta = m_editor->GetSelectedAssetMeta())
            {
                DrawAsset(*meta);
                return;
            }
            // 레이어를 골랐으면 그 레이어의 값이다(D-279).
            const LayerId layer = m_editor->GetSelectedLayer();
            if (layer != InvalidLayerId)
            {
                DrawLayer(layer);
                return;
            }
            // 캔버스를 골랐으면 캔버스의 값이다(D-186).
            if (m_editor->IsCanvasSelected())
            {
                DrawCanvas();
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
                static_cast<JBro::Int32>(chosen));
        }

        {
            Widget::FormLayout header("##object");
            header.Row(
                Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorActive, "Active")),
                [&]() {
                    Bool active = object->IsActiveSelf();
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
                    const Bool finished =
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
            ImGui::PushID(static_cast<JBro::Int32>(index));
            // 가이드의 표식은 그 타입의 첫째에만 단다(D-273, `Context::guideTarget`). 가이드가 견주는 값도 첫째다.
            Bool firstOfType = true;
            for (std::size_t earlier = 0; earlier < index && firstOfType; ++earlier)
            {
                firstOfType = components[earlier].typeId != slot.typeId;
            }
            if (firstOfType)
            {
                Widget::SetNextItemTarget(GuideFocusTargets::InspectorComponent(slot.typeId));
            }
            const Bool opened = Widget::CollapsingSection(
                typeName != nullptr
                    ? typeName
                    : Loc::TextOr(LocKeys::InspectorUnknownComponent,
                        "(unknown component)"),
                true, true);
            // 머리 오른쪽 끝의 메뉴 단추가 설 자리다. 단추는 메뉴를 다 그린 뒤에 얹는다 - 우클릭 메뉴는 **바로 앞 항목**
            // (머리)에 붙으므로, 단추를 먼저 그리면 머리의 우클릭이 단추로 옮겨 간다.
            const ImVec2 headerMin = ImGui::GetItemRectMin();
            const ImVec2 headerMax = ImGui::GetItemRectMax();
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
                if (Widget::MenuItem(Loc::TextOr(LocKeys::InspectorMoveComponentUp, "Move Up"), nullptr, true, nullptr, Icons::ArrowUp))
                {
                    moveTo = index - 1;
                }
                if (index == 0)
                {
                    ImGui::EndDisabled();
                }
                const Bool last = index + 1 >= components.Size();
                if (last)
                {
                    ImGui::BeginDisabled();
                }
                if (Widget::MenuItem(Loc::TextOr(LocKeys::InspectorMoveComponentDown, "Move Down"), nullptr, true, nullptr, Icons::ArrowDown))
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
                // **복사·붙여넣기**(D-167). 기존 엔진도 이 메뉴에 둘을 나란히 두었다.
                // 복사는 값만 뜨므로 화면이 그대로고, 붙여넣기는 같은 타입을 하나 더 붙인다.
                if (Widget::MenuItem(Loc::TextOr(LocKeys::InspectorCopyComponent,
                        "Copy Component"), nullptr, true, nullptr, Icons::Copy))
                {
                    m_editor->CopyComponent(*component);
                }
                Bool pasted = false;
                {
                    // 하나만 붙는 타입이 이미 있으면 회색이다(D-180). 눌러도 아무 일이
                    // 일어나지 않는 항목을 켜 두면 고장과 구분되지 않는다.
                    const Bool canPaste = m_editor->CanPasteComponent(*object);
                    // 떠 둔 것이 없는 것과, 떠 두었지만 이미 붙어 있는 것은 다른 이야기다.
                    const char* why = m_editor->HasComponentClipboard()
                        ? Loc::TextOr(LocKeys::CommonAlreadyAdded, "Already added")
                        : Loc::TextOr(LocKeys::BlockedClipboardEmpty, "nothing has been copied");
                    if (Widget::MenuItem(Loc::TextOr(LocKeys::InspectorPasteComponent,
                            "Paste Component"), nullptr, canPaste, why))
                    {
                        m_editor->PasteComponent(*object);
                        pasted = true;
                    }
                }
                if (pasted)
                {
                    Widget::EndContextMenu();
                    ImGui::PopID();
                    // 슬롯이 하나 늘었다. 이 프레임의 배열은 더 이상 맞지 않는다.
                    return;
                }
                {
                    // **값만 덮어쓰기.** 떠 둔 것이 같은 타입일 때만 켜진다. `Transform2D` 처럼
                    // 하나만 있어야 뜻이 서는 타입에서는 이쪽이 쓰는 손짓이다(D-167) -
                    // 기존 엔진에는 새로 하나 더 붙이는 쪽만 있었다.
                    const Bool canPasteValues = m_editor->CanPasteComponentValues(*component);
                    if (false == canPasteValues)
                    {
                        ImGui::BeginDisabled();
                    }
                    if (Widget::MenuItem(Loc::TextOr(LocKeys::InspectorPasteComponentValues,
                            "Paste Component Values")))
                    {
                        m_editor->PasteComponentValues(*object, *component);
                    }
                    if (false == canPasteValues)
                    {
                        ImGui::EndDisabled();
                    }
                }
                // **컴포넌트마다 더한 항목**(D-220). 오브젝트 메뉴와 같은 표다. 이 메뉴는 이미 이 인스턴스의
                // 것이므로 하위 메뉴 없이 늘어놓는다. 떼기는 무거운 손짓이라 그 아래 맨 끝에 남긴다.
                // `Has` 는 주소를 만들기 전에 거르는 것일 뿐이다 - 항목이 없으면 `DrawItems` 는 구분선도 긋지 않는다.
                ComponentMenuTable& menus = m_editor->GetComponentMenus();
                ComponentMenuContext hookContext;
                if (menus.Has(slot.typeId)
                    && MakeComponentAddress(m_editor->GetObjectIds(), *object, *component, hookContext.address))
                {
                    hookContext.editor = m_editor;
                    hookContext.component = component;
                    if (false == menus.DrawItems(hookContext, true))
                    {
                        Widget::EndContextMenu();
                        ImGui::PopID();
                        // 훅이 슬롯 배열을 바꿨을 수 있다. 다음 프레임에 다시 그린다.
                        return;
                    }
                }
                ImGui::Separator();
                if (Widget::MenuItem(Loc::TextOr(LocKeys::InspectorRemoveComponent,
                        "Remove Component"), nullptr, true, nullptr, Icons::Delete))
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
            // **메뉴는 단추로도 연다**(D-278). 우클릭만 되면 메뉴가 있다는 것을 알 수 없다. 같은 메뉴를 다음 프레임에 연다.
            {
                const ImVec2 cursor = ImGui::GetCursorScreenPos();
                const Float side = headerMax.y - headerMin.y;
                ImGui::SetCursorScreenPos(ImVec2(headerMax.x - side, headerMin.y));
                if (Widget::IconButton("##component_menu", Icons::Menu)
                        .Size(ImVec2(side, side))
                        .Tooltip(Loc::TextOr(LocKeys::InspectorComponentMenu, "Component menu"))
                        .Draw())
                {
                    Widget::OpenContextMenu("##ComponentMenu");
                }
                ImGui::SetCursorScreenPos(cursor);
            }
            if (opened)
            {
                Widget::FormLayout layout("##component");
                layout.Row(
                    Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorEnabled, "Enabled")),
                    [&]() {
                        Bool enabled = component->IsEnabled();
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
                    context.guideTarget = firstOfType;
                    DrawFieldsInto(layout, *table, component, context);
                }
            }
            ImGui::PopID();
        }

        ImGui::Spacing();
        DrawAddComponent(*object);
    }

    // **캔버스 자신의 값**이다(D-186, 기존 `DrawCanvasInspector`). 지금은 배경색 하나다 -
    // 기존 엔진에는 뷰포트 목록도 있었지만 우리에게는 뷰포트라는 것이 없다.
    void InspectorPanel::DrawCanvas()
    {
        Canvas* canvas = m_editor->GetCanvas();
        if (canvas == nullptr)
        {
            Widget::HintTextF("%s",
                Loc::TextOr(LocKeys::HierarchyNoProject, "no project is open"));
            return;
        }
        Widget::SectionHeader(
            Loc::TextOr(LocKeys::InspectorCanvasProperties, "Canvas")).Draw();
        Widget::FormLayout layout("##canvas");
        layout.Row(
            Widget::FieldLabel(
                Loc::TextOr(LocKeys::InspectorCanvasBackground, "Background colour")),
            [&]() {
                Color color = canvas->GetBackgroundColor();
                if (Widget::ColorField("##background", color.Data()))
                {
                    // **커맨드로만 고친다**(§11.5). 고르개를 끄는 동안 프레임마다 값이
                    // 바뀌는데, 커맨드끼리 합쳐지므로 되돌리기는 한 번이다.
                    m_editor->GetCommands().Execute(
                        MakeOwnerPtr<SetCanvasBackgroundCommand>(*canvas, color));
                }
            });
    }

    // **레이어의 값**이다(D-279, 기존 `DrawSelectedLayerInspector`). 이름·표시·혼합 모드·불투명도·공간을 한 자리에서 고친다 -
    // 계층의 줄과 메뉴에도 이름·표시·공간이 있지만, 혼합 모드와 불투명도는 여기가 유일한 자리다. 모두 커맨드로 간다(§11.5).
    void InspectorPanel::DrawLayer(LayerId layerId)
    {
        Canvas* canvas = m_editor->GetCanvas();
        Layer* layer = canvas != nullptr ? canvas->FindLayer(layerId) : nullptr;
        if (layer == nullptr)
        {
            return;
        }
        Widget::SectionHeader(Loc::TextOr(LocKeys::InspectorLayerProperties, "Layer")).Draw();
        Widget::FormLayout layout("##layer");
        // **원본 에셋은 읽기 전용이다**(D-287, 기존과 같다). 여기서 갈아 끼우는 것은 레이어와 오브젝트를 통째로 바꾸는 일이라 칸 하나로 할 일이 아니다 -
        // 다른 레이어 에셋은 계층에 끌어 넣는다. 단추는 에셋 브라우저에서 그 파일을 보인다.
        if (false == layer->GetSourceAsset().IsNull())
        {
            layout.Row(
                Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorLayerSourceAsset, "Layer Asset")),
                [&]() {
                    const AssetRecord* record = m_editor->GetAssetRegistry().Find(layer->GetSourceAsset());
                    if (record == nullptr)
                    {
                        Widget::HintText(Loc::TextOr(LocKeys::InspectorLayerSourceMissing, "the asset is gone"));
                        return;
                    }
                    if (Widget::IconButton("##layerSourceReveal", Icons::Search)
                            .Tooltip(Loc::TextOr(LocKeys::InspectorLayerSourceReveal, "show in the asset browser"))
                            .Draw())
                    {
                        m_editor->RevealAssetInBrowser(record->id);
                    }
                    ImGui::SameLine();
                    Widget::Text(record->relativePath.c_str());
                });
        }
        layout.Row(
            Widget::FieldLabel(Loc::TextOr(LocKeys::HierarchyLayerName, "Name")),
            [&]() {
                // 오브젝트 이름 칸과 같다: 치는 중이 아니면 늘 레이어의 이름을 들고, 편집이 끝날 때 커맨드 하나다(D-183).
                if (m_namedLayer != layerId || false == m_layerNameEditing)
                {
                    m_namedLayer = layerId;
                    m_layerName = layer->GetName();
                }
                const Bool finished = Widget::TextField("##layerName", m_layerName).CommitOnFinish().Draw();
                m_layerNameEditing = ImGui::IsItemActive();
                if (finished && m_layerName != layer->GetName())
                {
                    m_editor->GetCommands().Execute(MakeOwnerPtr<RenameLayerCommand>(*canvas, layerId, m_layerName.c_str()));
                }
            });
        layout.Row(
            Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorLayerVisible, "Visible")),
            [&]() {
                Bool visible = layer->IsVisible();
                if (Widget::Checkbox("##layerVisible", visible))
                {
                    m_editor->GetCommands().Execute(MakeOwnerPtr<SetLayerVisibleCommand>(*canvas, layerId, visible));
                }
            });
        layout.Row(
            Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorLayerBlend, "Blend Mode"))
                .Tooltip(Loc::TextOr(LocKeys::InspectorLayerBlendTooltip,
                    "the layer is drawn as one image and laid onto what is below it this way")),
            [&]() {
                // 항목 차례는 `LayerBlend` 의 값 차례다.
                const char* const blends[] = {
                    Loc::TextOr(LocKeys::InspectorLayerBlendNormal, "Normal"),
                    Loc::TextOr(LocKeys::InspectorLayerBlendAdditive, "Additive"),
                    Loc::TextOr(LocKeys::InspectorLayerBlendMultiply, "Multiply"),
                    Loc::TextOr(LocKeys::InspectorLayerBlendScreen, "Screen"),
                    Loc::TextOr(LocKeys::InspectorLayerBlendSubtract, "Subtract"),
                    Loc::TextOr(LocKeys::InspectorLayerBlendLighten, "Lighten"),
                    Loc::TextOr(LocKeys::InspectorLayerBlendDarken, "Darken"),
                    Loc::TextOr(LocKeys::InspectorLayerBlendOverlay, "Overlay"),
                    Loc::TextOr(LocKeys::InspectorLayerBlendSoftLight, "Soft Light"),
                    Loc::TextOr(LocKeys::InspectorLayerBlendHardLight, "Hard Light"),
                    Loc::TextOr(LocKeys::InspectorLayerBlendColorDodge, "Color Dodge"),
                    Loc::TextOr(LocKeys::InspectorLayerBlendColorBurn, "Color Burn"),
                    Loc::TextOr(LocKeys::InspectorLayerBlendDifference, "Difference")};
                static_assert(sizeof(blends) / sizeof(blends[0]) == LayerBlendCount, "one item per blend");
                Int32 current = static_cast<JBro::Int32>(layer->GetBlend());
                constexpr Int32 count = static_cast<JBro::Int32>(LayerBlendCount);
                if (Widget::FilterCombo("##layerBlend", ArrayView<const char* const>(blends, LayerBlendCount), current).Draw()
                    && current >= 0 && current < count)
                {
                    m_editor->GetCommands().Execute(MakeOwnerPtr<SetLayerCompositeCommand>(
                        *canvas, layerId, static_cast<LayerBlend>(current.Get()), layer->GetOpacity()));
                }
            });
        layout.Row(
            Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorLayerOpacity, "Opacity")),
            [&]() {
                // 끄는 동안 프레임마다 커맨드가 생기고 매니저가 합친다 - 끌기 하나가 되돌리기 하나다.
                Float opacity = layer->GetOpacity();
                if (Widget::SliderField("##layerOpacity", opacity, 0.0f, 1.0f)())
                {
                    m_editor->GetCommands().Execute(
                        MakeOwnerPtr<SetLayerCompositeCommand>(*canvas, layerId, layer->GetBlend(), opacity));
                }
            });
        // 패럴랙스는 월드 레이어에만 뜻이 있다(D-286). 끄는 동안 커맨드가 합쳐진다.
        if (layer->GetSpace() == LayerSpace::World)
        {
            layout.Row(
                Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorLayerParallax, "Parallax Factor"))
                    .Tooltip(Loc::TextOr(LocKeys::InspectorLayerParallaxTooltip,
                        "how far this layer moves when the camera moves")),
                [&]() {
                    Float parallax = layer->GetParallax();
                    if (Widget::DragField("##layerParallax", parallax).Range(0.0f, 10.0f).Speed(0.01f).Step(0.05f)())
                    {
                        m_editor->GetCommands().Execute(MakeOwnerPtr<SetLayerParallaxCommand>(*canvas, layerId, parallax));
                    }
                });
            // 빛을 받는지도 월드 레이어에만 뜻이 있다(D-291). 화면 레이어는 늘 빛을 받지 않는다.
            layout.Row(
                Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorLayerLit, "Lit"))
                    .Tooltip(Loc::TextOr(LocKeys::InspectorLayerLitTooltip,
                        "the objects on this layer are lit by the canvas lights; off draws them in their own colours")),
                [&]() {
                    Bool lit = layer->IsLit();
                    if (Widget::Checkbox("##layerLit", lit))
                    {
                        m_editor->GetCommands().Execute(MakeOwnerPtr<SetLayerLitCommand>(*canvas, layerId, lit));
                    }
                });
        }
        layout.Row(
            Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorLayerSpace, "Space"))
                .Tooltip(Loc::TextOr(LocKeys::HierarchyLayerScreenTooltip,
                    "the objects on this layer stay fixed on the screen whatever the camera does, in reference-resolution pixels")),
            [&]() {
                const char* const spaces[] = {
                    Loc::TextOr(LocKeys::InspectorLayerSpaceWorld, "World"),
                    Loc::TextOr(LocKeys::InspectorLayerSpaceScreen, "Screen")};
                Int32 current = static_cast<JBro::Int32>(layer->GetSpace());
                if (Widget::FilterCombo("##layerSpace", ArrayView<const char* const>(spaces, 2), current).ShowFilter(false).Draw()
                    && current >= 0 && current < 2 && static_cast<LayerSpace>(current.Get()) != layer->GetSpace())
                {
                    // 루트의 자리까지 한 커맨드다(D-237) - 계층 메뉴의 "화면 레이어로 바꾸기" 와 같은 길이다.
                    if (OwnerPtr<EditorCommand> command = m_editor->MakeLayerSpaceCommand(
                            layerId, static_cast<LayerSpace>(current.Get()), layer->GetScaleMode()))
                    {
                        m_editor->GetCommands().Execute(std::move(command));
                    }
                }
            });
        if (layer->GetSpace() == LayerSpace::Screen)
        {
            layout.Row(
                Widget::FieldLabel(Loc::TextOr(LocKeys::HierarchyLayerScaleMode, "Scale Mode")),
                [&]() {
                    // 값은 타입의 이름 그대로다 - 계층 메뉴와 같다.
                    const char* const modes[] = {"FixedHeight", "FixedWidth", "Contain", "ConstantPixel"};
                    Int32 current = static_cast<JBro::Int32>(layer->GetScaleMode());
                    if (Widget::FilterCombo("##layerScaleMode", ArrayView<const char* const>(modes, 4), current).ShowFilter(false).Draw()
                        && current >= 0 && current < 4)
                    {
                        if (OwnerPtr<EditorCommand> command = m_editor->MakeLayerSpaceCommand(
                                layerId, LayerSpace::Screen, static_cast<ScreenScaleMode>(current.Get())))
                        {
                            m_editor->GetCommands().Execute(std::move(command));
                        }
                    }
                });
        }
    }

    // 붙일 수 있는 것은 레지스트리에 있는 것이다. 인스펙터는 여기서도 타입을
    // 하나도 모른다 - 표가 늘면 목록이 는다.
    void InspectorPanel::DrawAddComponent(GameObject& object)
    {
        // 검색 드롭다운 하나다(D-116). 현재 번호를 늘 -1 로 주므로 트리거에는 "컴포넌트
        // 추가" 가 보이고, 고르면 그 자리에서 커맨드 하나가 나간다.
        //
        // 목록은 오브젝트 메뉴와 **같은 것**을 쓴다(D-180). 갈래로 묶이고, 이미 붙어 있어
        // 더 붙일 수 없는 것은 회색으로 남는다 - 목록에서 빼 버리면 찾던 이름이 사라진다.
        EditorActions::AddComponentList list;
        EditorActions::BuildAddComponentList(object, list);
        Int32 chosen = -1;
        // 고르기 칸 앞의 + 가 무엇을 하는 칸인지 말한다(D-278).
        Widget::InlineIcon(Icons::Plus);
        Widget::SetNextItemTarget(GuideFocusTargets::InspectorAddComponent());
        const Bool picked = Widget::FilterCombo("##AddComponent",
            ArrayView<const char* const>(list.names.Data(), list.names.Size()), chosen)
            .EmptyText(Loc::TextOr(LocKeys::InspectorAddComponent, "Add Component"))
            .NoItemsText(Loc::TextOr(LocKeys::InspectorNoComponentTypes,
                "no component type has registered itself"))
            .ItemGroups(ArrayView<const char* const>(list.groups.Data(), list.groups.Size()))
            .ItemEnabled(ArrayView<const Bool>(list.addable.Data(), list.addable.Size()))
            .DisabledTooltip(Loc::TextOr(LocKeys::CommonAlreadyAdded, "Already added"))
            .ItemTargets(GuideFocusTargets::ComponentListItem(0).name,
                ArrayView<const UInt64>(list.typeNames.Data(), list.typeNames.Size()))
            .Width(-FLT_MIN)
            .Draw();
        if (false == picked || chosen < 0
            || static_cast<std::size_t>(chosen) >= list.typeNames.Size())
        {
            return;
        }
        EditorActions::AddComponent(
            *m_editor, object, list.typeNames[static_cast<std::size_t>(chosen)]);
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

    Bool InspectorPanel::DrawScalarRun(
        const TypeDescriptor& type, const ScalarRun& run, const PropertyEditInfo* edit)
    {
        Float scratch[ScalarRun::MaxCount] = {};
        for (UInt32 index = 0; index < run.count; ++index)
        {
            scratch[index] = *run.values[index];
        }

        Bool changed = false;
        // 색은 숫자 네 개가 아니라 색이다. 견본과 고르개가 붙는다.
        if (run.count == 4 && SameName(type.typeName, "JBro.Color"))
        {
            changed = Widget::ColorField("##value", scratch);
        }
        else
        {
            const Bool hasRange = edit != nullptr && edit->hasRange;
            changed = Widget::ScalarRunField("##value", scratch, static_cast<JBro::Int32>(run.count),
                0.01f, hasRange, hasRange ? edit->rangeMin : Float(0.0f), hasRange ? edit->rangeMax : Float(0.0f));
        }
        if (false == changed)
        {
            return false;
        }
        // **주소마다 따로 써 넣는다.** 붙어 있으리라 믿지 않는다.
        for (UInt32 index = 0; index < run.count; ++index)
        {
            *run.values[index] = scratch[index];
        }
        return true;
    }

    const InspectorPanel::AssetChoices& InspectorPanel::ChoicesFor(AssetType type, AssetType also)
    {
        const AssetRegistry& registry = m_editor->GetAssetRegistry();
        AssetChoices* choices = nullptr;
        for (std::size_t index = 0; index < m_assetChoices.Size(); ++index)
        {
            if (m_assetChoices[index].type == type && m_assetChoices[index].also == also)
            {
                choices = &m_assetChoices[index];
            }
        }
        if (choices == nullptr)
        {
            AssetChoices fresh;
            fresh.type = type;
            fresh.also = also;
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
            if (type != AssetType::Unknown && record.type != type && (also == AssetType::Unknown || record.type != also))
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

    void InspectorPanel::DrawAudioBusField(const TypeDescriptor& type, void* address, Context& context)
    {
        AudioBusName& bus = *static_cast<AudioBusName*>(address);
        const ProjectFile& project = m_editor->GetProjectFile();
        m_busNames.Clear();
        m_busEnabled.Clear();
        m_busNames.Add(String(AudioMasterBusName));
        m_busEnabled.Add(true);
        Int32 current = bus.IsMaster() ? 0 : -1;
        for (std::size_t index = 0; index < project.audioBuses.Size(); ++index)
        {
            const String& name = project.audioBuses[index].name;
            if (name.empty() || name == AudioMasterBusName)
            {
                continue;
            }
            if (current < 0 && MakeNameId(name.c_str()) == bus.id)
            {
                current = static_cast<JBro::Int32>(m_busNames.Size());
            }
            m_busNames.Add(name);
            m_busEnabled.Add(true);
        }
        // 목록에 없는 이름은 지우지 않고 보여 준다 - 그 자리에서 왜 Master 로 울리는지 알 수 있게. 고를 수는 없다.
        const Bool missing = current < 0;
        if (missing)
        {
            const char* text = NameTable::Get().Resolve(bus.id);
            current = static_cast<JBro::Int32>(m_busNames.Size());
            m_busNames.Add(String(text[0] != '\0' ? text : "?"));
            m_busEnabled.Add(false);
        }
        m_busNamePointers.Clear();
        for (std::size_t index = 0; index < m_busNames.Size(); ++index)
        {
            m_busNamePointers.Add(m_busNames[index].c_str());
        }
        String before;
        const Bool snapped = ToText(type, address, before);
        Int32 chosen = current;
        const Bool changed = Widget::FilterCombo("##value",
            ArrayView<const char* const>(m_busNamePointers.Data(), m_busNamePointers.Size()), chosen)
            .ItemEnabled(ArrayView<const Bool>(m_busEnabled.Data(), m_busEnabled.Size()))
            .ShowFilter(true)
            .Draw();
        if (missing)
        {
            Widget::HoveredTooltip(Loc::TextOr(LocKeys::InspectorAudioBusMissing,
                "Not one of the project's audio buses - it plays on Master"));
        }
        if (changed && snapped && chosen >= 0 && static_cast<std::size_t>(chosen) < m_busNames.Size())
        {
            bus = chosen == 0 ? AudioBusName{} : AudioBusName::FromText(m_busNames[static_cast<std::size_t>(chosen)].c_str());
            CommitEdit(type, address, before, context);
        }
    }

    void InspectorPanel::DrawLayerMaskField(const TypeDescriptor& type, void* address, Context& context)
    {
        const ProjectFile& project = m_editor->GetProjectFile();
        const char* names[32] = {};
        for (std::size_t index = 0; index < project.physicsLayers.Size() && index < 32; ++index)
        {
            names[index] = project.physicsLayers[index].c_str();
        }
        String before;
        const Bool snapped = ToText(type, address, before);
        UInt32& mask = *static_cast<UInt32*>(address);
        if (Widget::LayerMaskField("##value", ArrayView<const char* const>(names, 32), mask) && snapped)
        {
            CommitEdit(type, address, before, context);
        }
    }

    void InspectorPanel::DrawObjectField(const TypeDescriptor& type, void* address, Context& context)
    {
        GameObjectHandle& handle = *static_cast<GameObjectHandle*>(address);
        m_objectNames.Clear();
        m_objectIds.Clear();
        m_objectNames.Add(String(Loc::TextOr(LocKeys::InspectorObjectNone, "None")));
        m_objectIds.Add(InvalidInstanceId);
        Int32 current = handle.GetInstanceId() == InvalidInstanceId ? 0 : -1;
        if (Canvas* canvas = m_editor->GetCanvas())
        {
            canvas->ForEachObject([&](GameObject& object) {
                if (object.GetInstanceId() == handle.GetInstanceId())
                {
                    current = static_cast<JBro::Int32>(m_objectIds.Size());
                }
                const char* name = object.GetTag();
                m_objectNames.Add(String(name != nullptr ? name : ""));
                m_objectIds.Add(object.GetInstanceId());
            });
        }
        m_objectNamePointers.Clear();
        for (const String& name : m_objectNames)
        {
            m_objectNamePointers.Add(name.c_str());
        }
        String before;
        const Bool snapped = ToText(type, address, before);
        Int32 chosen = current;
        UInt64 dropped = 0;
        // 하이어라키의 끌기 페이로드는 에디터 오브젝트 번호다(주소를 담지 않는다).
        const Bool changed = Widget::ObjectField("##value",
            ArrayView<const char* const>(m_objectNamePointers.Data(), m_objectNamePointers.Size()), chosen,
            Widget::DragKind::HierarchyObject, dropped);
        if (false == changed || false == snapped)
        {
            return;
        }
        if (dropped != 0)
        {
            GameObject* object = m_editor->GetObjectIds().Resolve(static_cast<EditorObjectId>(dropped));
            if (object == nullptr)
            {
                return;
            }
            handle = object->GetScriptHandle();
        }
        else if (chosen >= 0 && static_cast<std::size_t>(chosen) < m_objectIds.Size())
        {
            handle = chosen == 0 ? GameObjectHandle{} : Internal::GameObjectHandleAccess::FromId(m_objectIds[static_cast<std::size_t>(chosen)]);
        }
        else
        {
            return;
        }
        CommitEdit(type, address, before, context);
    }

    void InspectorPanel::DrawAudioPreview(const AssetMetaFile& meta)
    {
        m_audioDrawn = true;
        AssetSystem* assets = m_editor->GetAssetSystem();
        System::AudioSystem* audio = m_editor->GetAudio();
        if (assets == nullptr)
        {
            return;
        }
        // 에셋이나 자료의 판이 바뀌었으면 다시 잰다. 싣기는 이때 한 번이고(동기), 파형도 이때 푼다.
        const AssetHandle loaded = assets->Find(meta.id);
        const AudioData* current = assets->GetAudio(loaded);
        const UInt32 generation = current != nullptr ? current->dataGeneration : UInt32(0);
        if (false == (m_audioAsset == meta.id) || (current != nullptr && generation != m_audioGeneration))
        {
            if (audio != nullptr && false == (m_audioAsset == meta.id))
            {
                audio->StopPreview();
            }
            m_audioAsset = meta.id;
            m_audioPeaks.Clear();
            m_audioReadable = false;
            const AssetHandle held = assets->Load(meta.id);
            if (const AudioData* data = assets->GetAudio(held))
            {
                m_audioReadable = true;
                m_audioGeneration = data->dataGeneration;
                m_audioSampleRate = data->sampleRate;
                m_audioChannels = data->channels;
                m_audioSeconds = data->sampleRate > 0 ? static_cast<double>(data->frameCount) / data->sampleRate : 0.0;
                constexpr UInt32 Buckets = 512;
                assets->ComputeAudioPeaks(held, Buckets, m_audioPeaks);
            }
            // 붙잡지 않는다. 참조 수 0 이어도 `CollectUnused` 까지 살고, 내려가면 해제 알림이 미리 듣기를 멈춘다.
            assets->Release(held);
        }
        if (false == m_audioReadable)
        {
            Widget::ValidationMessage(Widget::Severity::Warning,
                Loc::TextOr(LocKeys::InspectorAudioUnreadable, "This audio file could not be read")).Draw();
            return;
        }

        const AssetHandle handle = assets->Find(meta.id);
        const Bool playingThis = audio != nullptr && audio->IsPreviewPlaying()
            && audio->GetPreviewClip().index == handle.index && audio->GetPreviewClip().generation == handle.generation;
        const auto play = [&](double from) {
            if (audio == nullptr)
            {
                return;
            }
            const AssetHandle held = assets->Load(meta.id);
            if (audio->PlayPreview(held, m_audioLoop) && from > 0.0)
            {
                audio->SeekPreview(from);
            }
            assets->Release(held);
        };
        {
            Widget::FormLayout layout("##audioInfo");
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorAudioFormat, "Format")),
                [&]() { Widget::TextF("%u Hz, %u ch", m_audioSampleRate, m_audioChannels); });
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorAudioLength, "Length")),
                [&]() {
                    const Int32 minutes = static_cast<JBro::Int32>(m_audioSeconds / 60.0);
                    Widget::TextF("%d:%05.2f", minutes, m_audioSeconds - minutes * 60.0);
                });
        }
        const double position = playingThis ? audio->GetPreviewTime() : 0.0;
        const Float progress = playingThis && m_audioSeconds > 0.0 ? static_cast<JBro::Float>(position / m_audioSeconds) : Float(-1.0f);
        Float seek = 0.0f;
        if (Widget::Waveform("##waveform", ArrayView<const Float>(m_audioPeaks.Data(), m_audioPeaks.Size()), progress,
                56.0f, seek))
        {
            // 누른 자리부터 듣는다. 이미 울리고 있으면 그 자리로 옮기기만 한다.
            if (playingThis)
            {
                audio->SeekPreview(seek * m_audioSeconds);
            }
            else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                play(seek * m_audioSeconds);
            }
        }
        Widget::HoveredTooltip(Loc::TextOr(LocKeys::InspectorAudioSeekHint, "Click to play from there"));
        if (audio == nullptr)
        {
            Widget::HintText(Loc::TextOr(LocKeys::InspectorAudioOff, "Audio is off in this editor"));
            return;
        }
        // 라벨은 왼쪽 칸이 그린다(§11.2). 위젯에는 `##이름` 만 넘긴다.
        Widget::FormLayout layout("##audioPreview");
        layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorAudioPreview, "Preview")),
            [&]() {
                if (playingThis)
                {
                    if (Widget::IconButton("##audio_stop", Icons::Stop)
                            .Caption(Loc::TextOr(LocKeys::InspectorAudioStop, "Stop"))
                            .Draw())
                    {
                        audio->StopPreview();
                    }
                    ImGui::SameLine();
                    const Int32 minutes = static_cast<JBro::Int32>(position / 60.0);
                    Widget::TextF("%d:%05.2f", minutes, position - minutes * 60.0);
                }
                else if (Widget::IconButton("##audio_play", Icons::Play)
                             .Caption(Loc::TextOr(LocKeys::InspectorAudioPlay, "Play"))
                             .Draw())
                {
                    play(0.0);
                }
            });
        layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorAudioLoop, "Loop")),
            [&]() { Widget::Checkbox("##loop", m_audioLoop); });
    }

    void InspectorPanel::DrawAssetField(
        const char* fieldName, const TypeDescriptor& type, void* address, Context& context)
    {
        // 텍스트의 `fontId` 는 폰트와 폰트 패밀리를 다 받는다(D-225). 패밀리 안의 칸(`regularFontId` 따위)은 폰트만이다.
        const AssetChoices& choices = std::strcmp(fieldName, "fontId") == 0
            ? ChoicesFor(AssetType::Font, AssetType::FontFamily)
            : ChoicesFor(AssetTypeOfIdName(fieldName));
        String before;
        const Bool snapped = ToText(type, address, before);
        const Bool changed = Widget::AssetField("##value",
            ArrayView<const char* const>(choices.namePointers.Data(), choices.namePointers.Size()),
            ArrayView<const AssetId>(choices.ids.Data(), choices.ids.Size()),
            *static_cast<AssetId*>(address))
            .Draw();
        // **이 에셋이 어디 있는지 물어볼 수 있다**(D-193, 기존은 더블클릭이었다).
        // 우리 칸은 콤보라 두 번 누르면 두 번째가 팝업 위에 떨어진다 - 같은 일을
        // 우클릭으로 한다.
        {
            const AssetId chosen = *static_cast<const AssetId*>(address);
            if (Widget::BeginContextMenu("##assetFieldMenu"))
            {
                if (Widget::MenuItem(
                        Loc::TextOr(LocKeys::AssetsFindInBrowser, "Find in Asset Browser"),
                        nullptr, false == chosen.IsNull(),
                        Loc::TextOr(LocKeys::BlockedNoAssetHere, "this field is empty")))
                {
                    m_editor->RevealAssetInBrowser(chosen);
                }
                Widget::EndContextMenu();
            }
        }
        if (changed && snapped)
        {
            CommitEdit(type, address, before, context);
        }
    }

    void InspectorPanel::DrawTextBody(const TypeDescriptor& type, void* address, Bool editable, Bool multiline, Context& context)
    {
        TextId& id = *static_cast<TextId*>(address);
        // **매 프레임 저장소의 글자를 넘긴다.** 치는 동안에는 ImGui 가 제 버퍼를 들고 넘긴 글자를 보지 않고, 편집이 끝나는
        // 프레임에 친 글자를 돌려준다. 그래서 되돌리기·스크립트가 바꾼 글자는 치지 않을 때 바로 보인다. (처음 판은 이름 칸처럼
        // 치는 칸의 글자를 따로 들었는데, 그 상태를 지우는 뮤테이션이 모든 검사를 지나 - 같은 동작이라 - 뺐다.)
        const ArrayView<const char> stored = TextStore::Get().GetText(id);
        String draft(stored.Data(), stored.Size());
        // **문자열 표의 키는 표에서 고른다**(D-226). 표에 키가 하나라도 있으면 검색 드롭다운이고, 없으면(표를 아직 채우지 않았다) 한 줄 칸이다.
        // 표에 없는 키가 적혀 있으면 트리거에 그 키가 그대로 보인다 - 빠진 번역도 화면에서 보인다.
        if (false == multiline)
        {
            const Array<String>& keys = m_editor->GetStringKeys();
            if (false == keys.IsEmpty())
            {
                m_keyNames.Clear();
                Int32 current = -1;
                for (std::size_t index = 0; index < keys.Size(); ++index)
                {
                    m_keyNames.Add(keys[index].c_str());
                    if (keys[index] == draft)
                    {
                        current = static_cast<JBro::Int32>(index);
                    }
                }
                const Bool picked = Widget::FilterCombo("##value", ArrayView<const char* const>(m_keyNames.Data(), m_keyNames.Size()),
                    current)
                                        .EmptyText(draft.empty() ? Loc::TextOr(LocKeys::InspectorNoStringKey, "(none)") : draft.c_str())
                                        .Draw();
                if (picked && editable && current >= 0)
                {
                    String before;
                    if (SetPropertyCommand::ReadValue(*context.component, context.typeId, context.path, before))
                    {
                        const String& chosen = keys[static_cast<std::size_t>(current)];
                        TextStore::Get().Assign(id, chosen.c_str(), chosen.size());
                        CommitEdit(type, address, before, context);
                    }
                }
                return;
            }
        }
        const Bool finished = multiline ? Widget::TextField("##value", draft).Multiline().CommitOnFinish().Draw()
                                        : Widget::TextField("##value", draft).CommitOnFinish().Draw();
        if (finished && editable)
        {
            // 편집 전 값은 코덱 글자로 뜬다(길이 제한 없는 길). 새 글자를 저장소에 쓰고 나면 `CommitEdit` 가
            // 옛 글자로 되돌려 놓고 고른 것 모두에 커맨드 하나를 만든다 - 다른 필드와 같은 길이다.
            String before;
            if (SetPropertyCommand::ReadValue(*context.component, context.typeId, context.path, before))
            {
                TextStore::Get().Assign(id, draft.c_str(), draft.size());
                CommitEdit(type, address, before, context);
            }
        }
    }

    Bool InspectorPanel::DrawLeaf(
        const TypeDescriptor& type,
        void* address,
        const PropertyEditInfo* edit,
        const String& before,
        Bool snapped)
    {
        // **잎사귀는 공용 위젯으로 그린다**(§11.1). 처음에는 여기가 ImGui 원시 호출 뭉치였다.
        // 끌기는 단추 없이 칸 하나라 `##value` 가 곧 그 칸의 Id 다.
        const Bool hasRange = edit != nullptr && edit->hasRange;
        if (SameName(type.typeName, "float"))
        {
            Float& value = *static_cast<Float*>(address);
            return hasRange
                ? Widget::SliderField("##value", value, edit->rangeMin, edit->rangeMax)()
                : Widget::DragField("##value", value).Speed(0.01f).StepButtons(false)();
        }
        if (SameName(type.typeName, "JBro.Radian"))
        {
            // **저장되는 값은 라디안이고 보이는 값은 도다**(D-247). 사람이 인스펙터에서
            // 1.5707 을 읽고 직각인 줄 아는 일은 없다. 되돌려 넣을 때 다시 라디안이 되므로
            // 파일에 적히는 숫자와 델타 계산은 전과 같다.
            Radian& value = *static_cast<Radian*>(address);
            Float degrees = value.ToDegree().Get();
            const Bool changed = hasRange
                ? Widget::SliderField("##value", degrees, edit->rangeMin, edit->rangeMax)()
                : Widget::DragField("##value", degrees).Speed(0.5f).Format("%.2f\u00b0").StepButtons(false)();
            if (changed)
            {
                value = Degree(degrees);
            }
            return changed;
        }
        if (SameName(type.typeName, "JBro.Degree"))
        {
            Degree& value = *static_cast<Degree*>(address);
            Float degrees = value.Get();
            const Bool changed = hasRange
                ? Widget::SliderField("##value", degrees, edit->rangeMin, edit->rangeMax)()
                : Widget::DragField("##value", degrees).Speed(0.5f).Format("%.2f\u00b0").StepButtons(false)();
            if (changed)
            {
                value = Degree(degrees);
            }
            return changed;
        }
        if (SameName(type.typeName, "bool"))
        {
            return Widget::Checkbox("##value", *static_cast<Bool*>(address));
        }
        if (SameName(type.typeName, "int32"))
        {
            Int32& value = *static_cast<Int32*>(address);
            return hasRange
                ? Widget::SliderField("##value", value,
                    static_cast<JBro::Int32>(edit->rangeMin), static_cast<JBro::Int32>(edit->rangeMax))()
                : Widget::DragField("##value", value).StepButtons(false)();
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
        UInt32 ordinal = 0;
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
        // **무엇을 보고 있는지 머리에 적는다**(D-173, 기존 에셋 인스펙터의 첫 네 줄).
        // 경로 한 줄만 있으면 그 파일이 어떤 종류로 등록되었는지, 번호가 무엇인지 알 길이
        // 에디터 안에 없었다 - `.jmeta` 를 직접 열어 봐야 했다.
        {
            Widget::FormLayout header("##assetHeader");
            const char* path = record != nullptr ? record->relativePath.c_str() : "?";
            const char* leaf = EditorPaths::LeafOfPath(path);
            header.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorAssetName, "Asset")),
                [&]() { Widget::WrappedText(leaf != nullptr ? leaf : path); });
            header.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorAssetType, "Type")),
                [&]() {
                    const AssetType type = record != nullptr ? record->type : AssetType::Unknown;
                    const char* name = AssetTypeRules::GetTypeName(type);
                    Widget::Text(name != nullptr ? name : "?");
                });
            header.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorAssetPath, "Path")),
                [&]() { Widget::WrappedText(path); });
            header.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorAssetId, "Id")),
                [&]() {
                    char text[Uuid::TextCapacity] = {};
                    if (meta.id.ToText(text, sizeof(text)))
                    {
                        // 32 자리라 좁은 칸에서는 한 줄에 들어가지 않는다. 잘라 보이면
                        // 그 번호로 파일을 찾을 수 없다.
                        Widget::WrappedText(text);
                    }
                });
        }
        ImGui::Separator();

        // **그림을 보여 준다**(D-147, 기존 `AssetInspectorPreview` 자리). 임포트 옵션을
        // 고치는 자리에 그림이 없으면 무엇을 고치고 있는지 이름으로만 알아야 한다.
        const TextureHandle preview = m_editor->GetAssetThumbnail(meta.id);
        if (preview.IsValid())
        {
            // 칸 너비에 맞추되 원본 비율을 지킨다. 늘여 붙이면 픽셀 아트가 기울어 보인다.
            const Float width = ImGui::GetContentRegionAvail().x;
            const Float side = width < PreviewMaxSide ? width : PreviewMaxSide;
            UInt32 sourceWidth = 0;
            UInt32 sourceHeight = 0;
            m_editor->GetAssetSourceSize(meta.id, sourceWidth, sourceHeight);
            Widget::Image(preview, Widget::FitInside(sourceWidth, sourceHeight, ImVec2(side, side)));
            ImGui::Spacing();
        }
        // **그림은 뷰어에서 크게 본다**(D-173, 기존 `뷰어에서 열기`). 에셋 브라우저에서 두 번
        // 누르는 길만 있어서, 인스펙터에서 옵션을 고치다 칸을 확인하려면 브라우저로 건너가야 했다.
        if (meta.type == AssetType::Audio)
        {
            DrawAudioPreview(meta);
            ImGui::Spacing();
        }
        if (AssetTypeRules::IsImageType(meta.type))
        {
            if (Widget::IconButton("##open_in_viewer", Icons::OpenExternal)
                    .Caption(Loc::TextOr(LocKeys::InspectorOpenInViewer, "Open in Viewer"))
                    .Draw())
            {
                m_editor->OpenSpriteViewer(meta.id);
            }
            ImGui::Spacing();
        }
        DrawAssetOptions(meta);
    }

    void InspectorPanel::DrawAssetOptions(const AssetMetaFile& meta)
    {
        // 편집본은 프레임마다 원본에서 새로 뜬다. 위젯이 고친 값은 커맨드가 파일에 쓰고, 다음 프레임의 원본이 그것을
        // 다시 읽어 온다 - 쓰는 길이 하나다(D-89 와 같은 이유).
        AssetMetaFile scratch = meta;
        AssetEditScope scope;
        scope.scratch = &scratch;
        Context context;
        context.asset = &scope;

        const Bool image = AssetTypeRules::IsImageType(meta.type);
        Int32 slot = 0;
        const auto drawBlock = [&](const char* title, const TypeDescriptor& type, void* options, Bool spriteBlock,
                                     Bool audioBlock = false, Bool fontBlock = false, Bool fontFamilyBlock = false,
                                     Bool stringTableBlock = false) {
            // 컴포넌트와 같은 모양이다: 슬롯 번호 → 접는 머리 → 줄 배치 `##import`.
            ImGui::PushID(slot++);
            scope.spriteBlock = spriteBlock;
            scope.audioBlock = audioBlock;
            scope.fontBlock = fontBlock;
            scope.fontFamilyBlock = fontFamilyBlock;
            scope.stringTableBlock = stringTableBlock;
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
        if (meta.type == AssetType::Audio)
        {
            drawBlock(Loc::TextOr(LocKeys::InspectorAudioImportOptions, "Audio Import Options"),
                TypeDescriptorOf<AudioImportOptions>::Get(), &scratch.audioOptions, false, true);
        }
        // 폰트의 PPU·필터(D-200). 고치면 제자리 재로드로 글자가 새 크기로 다시 뜬다.
        if (meta.type == AssetType::Font)
        {
            drawBlock(Loc::TextOr(LocKeys::InspectorFontImportOptions, "Font Import Options"),
                TypeDescriptorOf<FontImportOptions>::Get(), &scratch.fontOptions, false, false, true);
        }
        // 폰트 패밀리의 네 칸(D-225). 고치면 제자리 재로드로 칸의 폰트가 바뀌고, 그 패밀리를 쓰는 텍스트가 다시 레이아웃된다.
        if (meta.type == AssetType::FontFamily)
        {
            drawBlock(Loc::TextOr(LocKeys::InspectorFontFamilyFaces, "Font Family"),
                TypeDescriptorOf<FontFamilyOptions>::Get(), &scratch.fontFamilyOptions, false, false, false, true);
        }
        // 문자열 표의 로케일(D-226). 고치면 제자리 재로드로 표가 다른 언어의 것이 되고, 키가 있는 텍스트가 다시 레이아웃된다.
        if (meta.type == AssetType::StringTable)
        {
            drawBlock(Loc::TextOr(LocKeys::InspectorStringTable, "String Table"),
                TypeDescriptorOf<StringTableOptions>::Get(), &scratch.stringTableOptions, false, false, false, false, true);
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
        else if (context.asset->audioBlock)
        {
            scratch.hasAudioOptions = true;
        }
        else if (context.asset->fontBlock)
        {
            scratch.hasFontOptions = true;
        }
        else if (context.asset->fontFamilyBlock)
        {
            scratch.hasFontFamilyOptions = true;
        }
        else if (context.asset->stringTableBlock)
        {
            scratch.hasStringTableOptions = true;
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
        // (`Vector2`·`Color`)은 여기서 돌아갔다 - 위젯이 쓴 값이 커맨드 없이 남았다(D-89).
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
        const Bool numeric = CollectScalarRun(type, address, editedRun)
            || SameName(type.typeName, "float")
            || SameName(type.typeName, "JBro.Radian")
            || SameName(type.typeName, "JBro.Degree");
        Float delta[ScalarRun::MaxCount] = {};
        UInt32 deltaCount = 0;
        if (numeric)
        {
            // 지금 주소에는 위젯이 쓴 값이 들어 있고, `before` 가 그 전 값이다.
            ScalarRun afterRun;
            if (false == CollectScalarRun(type, address, afterRun))
            {
                afterRun.values[0] = static_cast<Float*>(address);
                afterRun.count = 1;
            }
            for (UInt32 at = 0; at < afterRun.count; ++at)
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
                beforeRun.values[0] = static_cast<Float*>(address);
                beforeRun.count = 1;
            }
            for (UInt32 at = 0; at < deltaCount && at < beforeRun.count; ++at)
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
                    run.values[0] = static_cast<Float*>(targetAddress);
                    run.count = 1;
                }
                for (UInt32 at = 0; at < run.count && at < deltaCount; ++at)
                {
                    *run.values[at] += delta[at];
                }
                const Bool read = SetPropertyCommand::ReadValue(
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
        const TypeDescriptor& type, void* address, Bool editable, Context& context)
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
        const Int32 count = static_cast<JBro::Int32>(ops.GetSize(address));
        UInt32 flags = Widget::ListFlagsShowIndex;
        if (false == editable)
        {
            flags |= Widget::ListFlagsReadOnly;
        }

        Widget::ListVirtual(
            "##array",
            count,
            [&](Int32 index) -> Bool {
                void* item = ops.GetElement(address, static_cast<std::size_t>(index));
                if (item == nullptr)
                {
                    return false;
                }
                // 원소 안의 편집은 컴포넌트 길 대신 이 원소를 들고 적힌다(D-89).
                ElementScope scope;
                scope.edits = &edits;
                scope.index = static_cast<JBro::UInt32>(index);
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
            [&](Int32 index) {
                if (nodeName != nullptr)
                {
                    Widget::DropRowInt(nodeName, index, count);
                }
                ListEdit edit;
                edit.kind = ListEdit::Kind::Remove;
                edit.index = static_cast<JBro::UInt32>(index);
                edits.Add(std::move(edit));
            },
            [&](Int32 fromIndex, Int32 toIndex) {
                if (nodeName != nullptr)
                {
                    Widget::CarryRowInt(nodeName, fromIndex, toIndex);
                }
                ListEdit edit;
                edit.kind = ListEdit::Kind::Move;
                edit.index = static_cast<JBro::UInt32>(fromIndex);
                edit.to = static_cast<JBro::UInt32>(toIndex);
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
        const Float width = ImGui::CalcItemWidth();
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
        for (UInt32 step = 0; step < scope.fieldDepth; ++step)
        {
            edit.fieldPath[step] = scope.fieldPath[step];
        }
        edit.fieldDepth = scope.fieldDepth;
        return edit;
    }

    void InspectorPanel::RecordElementRun(
        const ScalarRun& run, const Float before[ScalarRun::MaxCount], Context& context)
    {
        // 위젯이 쓴 값은 그 자리에서 도로 되돌리고 "이 원소의 이 필드에 얼마를 더했다" 로 적는다.
        ListEdit edit = MakeElementEdit(*context.element);
        edit.deltaCount = run.count;
        for (UInt32 at = 0; at < run.count; ++at)
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
        const Bool numeric = CollectNumbers(type, address, single);
        const Float after = numeric ? *single.values[0] : Float(0.0f);
        const Bool captured = numeric || ToText(type, address, edit.text);
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
    Bool InspectorPanel::NeedsDescent(const TypeDescriptor& type)
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
        for (UInt32 index = 0; index < table.count; ++index)
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
            // **앵커는 화면 레이어의 것이다**(D-237). 월드 레이어의 `Transform2D` 에는 뜻이 없으니 줄을 두지 않는다.
            if (context.element == nullptr && context.owner != nullptr && context.typeId == Transform2DTypeId && property.name == AnchorFieldName)
            {
                const Layer* layer = context.owner->GetLayer();
                if (layer == nullptr || layer->GetSpace() != LayerSpace::Screen)
                {
                    continue;
                }
            }

            // 길에 한 칸 더 내려간다. 그려 놓고 되돌려야 형제 필드가 제 길을 갖는다.
            // 목록 원소 안이면 컴포넌트 길이 아니라 원소 안의 필드 길이다(D-89).
            const Bool inElement = context.element != nullptr;
            const Bool tooDeep = inElement
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
            const Bool editable = (property.edit == nullptr || property.edit->editable)
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
                    Widget::IdScope id(static_cast<JBro::Int32>(index));
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

            Widget::IdScope id(static_cast<JBro::Int32>(index));

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
                Bool opened = false;
                {
                    // **잠긴 값은 타고 내려가도 잠겨 있어야 한다.** 잠금은
                    // `DrawValue` 안에 있었는데, 중첩 구조는 그 길로 가지
                    // 않으므로 여기서 다시 두른다 - 안 그러면 파생값의
                    // 속살만 고칠 수 있게 된다.
                    Widget::DisableScope locked(false == editable);
                    layout.FullRow([&]() {
                        if (false == inElement && context.path.depth == 1 && context.component != nullptr && context.guideTarget)
                        {
                            Widget::SetNextItemTarget(GuideFocusTargets::InspectorField(context.typeId, property.name));
                        }
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
            // 컴포넌트의 맨 위 필드 줄은 가이드 포커스가 가리킬 수 있다(D-251). 한 줄 전체가 대상이다.
            if (false == inElement && context.path.depth == 1 && context.component != nullptr && context.guideTarget)
            {
                Widget::ReportGuideTarget(GuideFocusTargets::InspectorField(context.typeId, property.name),
                    layout.GetLastRowMin(), layout.GetLastRowMax(), false, ImGui::IsItemDeactivatedAfterEdit());
            }

            // **이 필드에 붙는 줄이 있으면 바로 밑에 그린다**(D-165). 무엇을 붙일지는 인스펙터가 모른다 - 표가 안다.
            if (false == inElement && context.path.depth == 1 && context.asset == nullptr
                && context.component != nullptr && context.owner != nullptr)
            {
                if (const FieldExtraDraw extra = FindFieldExtra(context.typeId, property.name))
                {
                    FieldExtraContext extraContext;
                    extraContext.editor = m_editor;
                    extraContext.owner = context.owner;
                    extraContext.component = context.component;
                    extraContext.typeId = context.typeId;
                    extra(layout, extraContext);
                }
            }

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
        const Bool editable = edit == nullptr || edit->editable;
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
                static_cast<JBro::Int32>(count));
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
                static_cast<JBro::Int32>(type.tableOps->GetSize(address)));
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
                Float before[ScalarRun::MaxCount] = {};
                for (UInt32 at = 0; at < run.count; ++at)
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
            const Bool snapped = context.component != nullptr
                ? SetPropertyCommand::ReadValue(*context.component, context.typeId, context.path, before)
                : Bool(context.asset != nullptr);
            if (DrawScalarRun(type, run, edit) && editable)
            {
                if (snapped)
                {
                    CommitEdit(type, address, before, context);
                }
            }
            return;
        }

        // 오디오 버스는 프로젝트 목록의 드롭다운이다(D-197).
        if (context.element == nullptr && SameName(type.typeName, "JBro.AudioBusName"))
        {
            DrawAudioBusField(type, address, context);
            return;
        }
        // 물리 레이어는 프로젝트의 레이어 이름으로 고른다(D-233).
        if (context.element == nullptr && SameName(type.typeName, "JBro.PhysicsLayerMask"))
        {
            DrawLayerMaskField(type, address, context);
            return;
        }
        // 오브젝트 참조는 캔버스의 오브젝트 목록이다(D-233).
        if (context.element == nullptr && context.component != nullptr && SameName(type.typeName, "JBro.GameObjectHandle"))
        {
            DrawObjectField(type, address, context);
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

        // 텍스트의 글자는 줄바꿈을 그대로 치는 여러 줄 칸이다. 한 줄 칸은 줄바꿈을 이스케이프 글자로 보여 주고
        // 512 바이트에서 끊겼다.
        if (context.element == nullptr && context.component != nullptr && SameName(type.typeName, "JBro.TextId"))
        {
            // 문자열 표의 키(`textKey`, D-226)는 한 줄이다. 줄바꿈이 든 키는 표에 적을 수 없다.
            DrawTextBody(type, address, editable, false == (label != nullptr && std::strcmp(label, "textKey") == 0), context);
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
            const Bool snapped = ToText(type, address, before);
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

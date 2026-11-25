#include "InspectorPanel.h"
#include <JBro/Framework2D/Component/Transform2D.h>
#include "InspectorFieldExtras.h"

#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Button.h>
#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Editor/Command/CanvasCommands.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2DSystem/System/Camera2DSystem.h>
#include <JBro/Framework3D/Component/Camera3D.h>
#include <JBro/Framework3DSystem/System/Camera3DSystem.h>
#include <JBro/Framework3DSystem/System/Animation3DSystem.h>
#include <JBro/Framework3D/Component/Animator3D.h>
#include <JBro/Framework3D/Component/BoneAttachment3D.h>
#include <JBro/Editor/Command/LayerCommands.h>
#include <JBro/Editor/Command/ComponentCommands.h>
#include <JBro/Editor/Command/ObjectCommands.h>
#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/Command/CompoundCommand.h>
#include <JBro/Editor/Command/ListEdit.h>
#include <JBro/Editor/ComponentMenuTable.h>
#include <JBro/Editor/EditorActions.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorTheme.h>
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
#include <JBro/Editor/Widget/DragDrop.h>
#include <JBro/Editor/Widget/ComponentDrag.h>
#include <JBro/Editor/Command/SetAssetSourceCommand.h>
#include <JBro/Graphics/PostProcessProfile.h>
#include <JBro/Asset/RenderTextureFile.h>
#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AudioDecoder.h>
#include <JBro/Audio/AudioMixer.h>
#include <JBro/Audio/AudioSystem.h>
#include <JBro/AudioTypes/AudioBusName.h>
#include <JBro/Editor/Widget/Meter.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/Core/Log.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Editor/EditorPaths.h>
#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Editor/Command/SetAssetFileCommand.h>
#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/TextStore.h>
#include <JBro/Types/NameTable.h>

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <utility>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>
#include <JBro/Framework3DSystem/Rendering/MeshLibrary.h>
#include <JBro/Asset/MaterialFile.h>
#include <JBro/Asset/ModelImport.h>

namespace JBro
{
    // 모델 메타 하위 항목의 소켓 목록만 보이는 표다(D-460). 인스펙터의 필드 그리기가 목록 위젯·원소 편집을 그대로 쓰게 항목 자체를 가리킨다.
    struct AssetSubEntrySockets;

    template <>
    struct TypeDescriptorOf<AssetSubEntrySockets>
    {
        static const TypeDescriptor& Get()
        {
            static const FieldEntry entries[] =
            {
                MakeFieldEntry<&AssetSubEntry::sockets>(),
            };
            static const StaticPropertyTable<1> fields { entries };
            static const TypeDescriptor descriptor = MakeStructTypeDescriptor<AssetSubEntry>("JBro.AssetSubEntrySockets", fields.Get());
            return descriptor;
        }
    };

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

        // `VisibleWhen` 이 가리키는 스위치가 켜져 있는가(D-309). 같은 표의 `Bool` 필드만 본다 - 없거나 `Bool` 이 아니면 켜진 것으로 보아
        // 줄을 남긴다(오타가 필드를 숨기지 않게).
        Bool IsSwitchOn(const PropertyTable& table, void* owner, const char* fieldName)
        {
            for (UInt32 index = 0; index < table.count; ++index)
            {
                const PropertyInfo& property = table.properties[index];
                if (false == SameName(property.name, fieldName))
                {
                    continue;
                }
                // 주소가 아니라 타입 이름으로 견준다 - 스크립트 DLL 이 등록한 표의 서술자는 제 모듈의 것이다.
                if (property.type == nullptr || property.type->typeName != TypeDescriptorOf<Bool>::Get().typeName
                    || property.Address == nullptr)
                {
                    return true;
                }
                const void* address = property.Address(owner);
                return address == nullptr || *static_cast<const Bool*>(address);
            }
            return true;
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
        // 지난 프레임에 미리 듣기 칸을 그리지 않았으면(다른 것을 골랐다) 미리 듣기를 멈춘다. **인스펙터가 소리를 보이고 있던 때만이다**
        // (D-306) - 사운드 이펙트 창의 미리 듣기는 인스펙터와 따로 울린다.
        if (false == m_audioDrawn && false == m_audioAsset.IsNull())
        {
            if (System::AudioSystem* audio = m_editor->GetAudio(); audio != nullptr && audio->IsPreviewPlaying())
            {
                audio->StopPreview();
            }
            m_audioAsset = {};
        }
        m_audioDrawn = false;
        if (false == m_socketDrawn && m_socketAsset.generation != 0)
        {
            ReleaseSocketAsset();
        }
        m_socketDrawn = false;
        Object::GameObject* object = m_editor->GetSelectedObject();
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

        // **스크립트가 중단점에 선 동안은 보기만 한다**(D-330 2 단계). 편집 커맨드가 잠겨 있으니 칸도 잠근다 - 고칠 수 있게 보이는데 되돌아가면 고장으로 보인다.
        // 접기·펼치기는 그대로 둔다. 둘러보는 것이 선 까닭이다.
        const Bool atBreak = m_editor->IsAtScriptBreak();
        {
            Widget::DisableScope breakLock(atBreak);
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
                        const Array<Object::GameObject*> chosenObjects = m_editor->GetSelectedObjects();
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
            // 오브젝트의 이름이 `GameObject` 인 채로 굳었다. 이름은 인턴된 정수다(D-51).
            header.Row(
                Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorName, "Name")),
                [&]() {
                    const char* tag = object->GetName();
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
            // **태그는 프로젝트 목록에서 고른다**(D-297). 글자 칸이면 오타가 그대로 다른 태그가 된다. 목록 끝의 `태그 추가...` 가
            // 그 자리에서 새 태그를 목록에 더하고 바로 단다 - 프로젝트 설정까지 가지 않는다.
            header.Row(
                Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorTag, "Tag")),
                [&]() { DrawTagField(*object); });
        }
        ImGui::Separator();

        // **고른 컴포넌트**(D-304). 그 칸이 이 오브젝트에 없으면 첫 컴포넌트로 간다(기존 엔진도 범위를 넘으면 0 이었다).
        {
            const std::size_t count = m_editor->GetInspectedKind() == AttachedKind::Script
                ? object->GetScripts().Size() : object->GetComponents().Size();
            if (m_editor->GetInspectedSlot() >= count)
            {
                m_editor->SetInspectedSlot(AttachedKind::Component, 0);
            }
        }

        // **컴포넌트, 그다음 스크립트다**(D-302). 두 목록은 따로 살고, 자리 옮기기도 제 목록 안에서만 한다.
        const Array<ComponentSlot>& components = object->GetComponents();
        for (std::size_t index = 0; index < components.Size(); ++index)
        {
            if (false == DrawAttached(*object, components[index].reference.TryGet(), components[index].typeId,
                    index, components.Size()))
            {
                return;
            }
        }
        // 스크립트는 컴포넌트 바로 뒤에 같은 모양으로 선다(D-308) - 사용자에게는 둘이 같은 컴포넌트다.
        const Array<ScriptSlot>& scripts = object->GetScripts();
        // 슬롯 번호가 컴포넌트와 겹치므로 Id 를 한 겹 더 두른다.
        ImGui::PushID("##scripts");
        for (std::size_t index = 0; index < scripts.Size(); ++index)
        {
            if (false == DrawAttached(*object, scripts[index].reference.TryGet(), scripts[index].typeId,
                    index, scripts.Size()))
            {
                ImGui::PopID();
                return;
            }
        }

        // **이 엔진이 모르는 스크립트도 보인다**(D-264). 캔버스 파일이 읽은 그대로 들고 있는 것이라 값은 보이지 않는다 -
        // 아무것도 안 보이면 스크립트가 사라진 줄 알고, 저장하면 사라지는 줄 안다.
        Canvas* canvas = m_editor->GetCanvas();
        if (const Array<UnresolvedScript>* kept = canvas != nullptr ? canvas->FindUnresolvedScripts(object) : nullptr)
        {
            for (std::size_t index = 0; index < kept->Size(); ++index)
            {
                ImGui::PushID(static_cast<int>(scripts.Size() + index));
                const char* typeName = DisplayTypeName((*kept)[index].typeName.c_str());
                if (Widget::CollapsingSection(typeName != nullptr ? typeName : (*kept)[index].typeName.c_str()))
                {
                    Widget::FormLayout layout("##Unresolved");
                    layout.FullRow([&]() {
                        Widget::HintTextF("%s",
                            Loc::TextOr(LocKeys::InspectorUnresolvedScript,
                                "This script could not be found. Its saved values are kept."));
                    });
                }
                ImGui::PopID();
            }
        }
        ImGui::PopID();

        ImGui::Spacing();
        {
            Widget::DisableScope breakLock(atBreak);
            DrawAddComponent(*object);
        }
    }

    Bool InspectorPanel::DrawAttached(Object::GameObject& object, AttachedRef attached, ComponentTypeId typeId,
        std::size_t index, std::size_t count)
    {
        (void)count;
        if (false == attached.IsValid())
        {
            return true;
        }
        const AttachedKind kind = attached.GetKind();
        // 이름이 아니라 슬롯으로 구분한다. 같은 타입을 두 개 붙일 수 있다.
        ImGui::PushID(static_cast<JBro::Int32>(index));
        // 이 컴포넌트가 차지하는 영역의 위 끝이다. 머리부터 마지막 필드까지가 고르는 자리다(D-304).
        const ImVec2 areaMin = ImGui::GetCursorScreenPos();
        const char* typeName =
            DisplayTypeName(NameTable::Get().Resolve(typeId));

        // 가이드의 표식은 그 목록에서 그 타입의 첫째에만 단다(D-273, `Context::guideTarget`). 가이드가 견주는 값도 첫째다.
        Bool firstOfType = true;
        if (kind == AttachedKind::Script)
        {
            const Array<ScriptSlot>& scripts = object.GetScripts();
            for (std::size_t earlier = 0; earlier < index && earlier < scripts.Size() && firstOfType; ++earlier)
            {
                firstOfType = scripts[earlier].typeId != typeId;
            }
        }
        else
        {
            const Array<ComponentSlot>& components = object.GetComponents();
            for (std::size_t earlier = 0; earlier < index && earlier < components.Size() && firstOfType; ++earlier)
            {
                firstOfType = components[earlier].typeId != typeId;
            }
        }
        if (firstOfType)
        {
            Widget::SetNextItemTarget(GuideFocusTargets::InspectorComponent(typeId));
        }
        const Bool chosen = m_editor->GetInspectedKind() == kind && m_editor->GetInspectedSlot() == index;
        const Bool opened = Widget::CollapsingSection(
            typeName != nullptr
                ? typeName
                : Loc::TextOr(LocKeys::InspectorUnknownComponent,
                    "(unknown component)"),
            true, true, chosen);
        // 머리 오른쪽 끝의 메뉴 단추가 설 자리다. 단추는 메뉴를 다 그린 뒤에 얹는다 - 우클릭 메뉴는 **바로 앞 항목**
        // (머리)에 붙으므로, 단추를 먼저 그리면 머리의 우클릭이 단추로 옮겨 간다.
        const ImVec2 headerMin = ImGui::GetItemRectMin();
        const ImVec2 headerMax = ImGui::GetItemRectMax();
        if (firstOfType)
        {
            Widget::ReportGuideTarget(GuideFocusTargets::InspectorComponentMenu(typeId), headerMin, headerMax,
                ImGui::IsPopupOpen("##ComponentMenu"), false);
        }
        // **머리를 끌어 순서를 바꾼다**(D-294). 메뉴의 위로·아래로와 같은 커맨드다. 끌기·받기는 직전 항목(머리)에 붙고,
        // 툴팁 창을 닫으면 직전 항목이 머리로 돌아오므로 아래 우클릭 메뉴도 그대로 머리에 붙는다.
        if (DrawComponentDrag(object, kind, index, typeName, headerMin, headerMax))
        {
            ImGui::PopID();
            // 옮긴 뒤에는 이 프레임의 슬롯 배열이 더 이상 맞지 않는다. 다음 프레임에 다시 그린다.
            return false;
        }
        // **머리에 우클릭하면 뗄 수 있다.** 기존 엔진도 여기가 그 자리다.
        // 접힌 채로도 눌러야 하므로 머리를 그린 직후에 둔다.
        if (Widget::BeginContextMenu("##ComponentMenu"))
        {
            // **오브젝트 메뉴의 컴포넌트 하위 메뉴와 같은 한 벌이다**(D-298). 사용·자리 옮기기·복사·붙여넣기·더한 항목·제거.
            // 스크립트도 같은 한 벌이고 제 목록 안에서만 옮긴다(D-302).
            const Bool unchanged = EditorActions::DrawComponentMenuItems(*m_editor, object, index, {}, kind);
            Widget::EndContextMenu();
            if (false == unchanged)
            {
                ImGui::PopID();
                // 슬롯 배열이 바뀌었다. 이 프레임의 배열을 더 읽으면 죽은 슬롯을 본다 - 다음 프레임에 다시 그린다.
                return false;
            }
        }
        // **메뉴는 단추로도 연다**(D-278). 우클릭만 되면 메뉴가 있다는 것을 알 수 없다. 같은 메뉴를 다음 프레임에 연다.
        {
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const Float side = headerMax.y - headerMin.y;
            ImGui::SetCursorScreenPos(ImVec2(headerMax.x - side, headerMin.y));
            if (Widget::IconButton("##component_menu", Icons::Menu)
                    .Size(ImVec2(side, side))
                    .Flat()
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
                    Bool enabled = attached.IsEnabled();
                    if (Widget::Checkbox("##enabled", enabled))
                    {
                        // 이것도 커맨드다(D-142). 끈 것을 되돌릴 수 없으면 편집이 아니다.
                        ComponentAddress address;
                        if (MakeAttachedAddress(
                                m_editor->GetObjectIds(), object, attached, address))
                        {
                            m_editor->GetCommands().Execute(
                                MakeOwnerPtr<SetComponentEnabledCommand>(
                                    m_editor->GetObjectIds(), address, enabled));
                        }
                    }
                });

            const PropertyTable* table = PropertyRegistry::Lookup(typeId);
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
                context.owner = &object;
                context.attached = attached;
                context.typeId = typeId;
                context.guideTarget = firstOfType;
                DrawFieldsInto(layout, *table, attached.GetInstance(), context);
            }
        }
        // **영역을 누르면 그 컴포넌트를 고른다**(D-304, 기존 엔진은 탭을 골랐다). 머리든 필드든 이 컴포넌트가 차지한 곳을 누르면 된다 - 값을 고치려고
        // 눌러도 같이 골라진다. 캔버스 뷰는 고른 것만 따른다(라이트 점·폴리곤 포인트·카메라 뷰포트 인디케이터). 같은 타입이 여럿이면 누른 그 하나다.
        {
            const ImVec2 areaMax(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x, ImGui::GetCursorScreenPos().y);
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const Bool inside = mouse.x >= areaMin.x && mouse.x < areaMax.x && mouse.y >= areaMin.y && mouse.y < areaMax.y;
            const Bool pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
            if (inside && pressed
                && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            {
                m_editor->SetInspectedSlot(kind, index);
            }
            // 고른 것은 머리를 도킹의 고른 탭처럼 칠한다(`CollapsingSection` 의 `focused`). 영역 테두리는 쓰지 않는다 - 필드 줄마다 겹쳐 보인다.
        }
        ImGui::PopID();
        return true;
    }

    // **캔버스 자신의 값**이다(D-186, 기존 `DrawCanvasInspector`). 배경색과 뷰포트 목록(D-438)이다.
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
        DrawCanvasViewports(*canvas);
        // 환경은 3D 의 것이다(D-447) - 2D 캔버스에는 보이지 않는다.
        if (m_editor->GetFrameworkKind() == FrameworkKind::Framework3D)
        {
            DrawCanvasEnvironment(*canvas);
        }
    }

    // **캔버스 환경**(D-447, Unity 의 Lighting 창 Environment 자리). 출처에 따라 그 출처의 칸만 보인다. 편집은 환경 전체를 뜬 커맨드 하나다.
    void InspectorPanel::DrawCanvasEnvironment(Canvas& canvas)
    {
        Widget::SectionHeader(Loc::TextOr(LocKeys::InspectorCanvasEnvironment, "Environment")).SpacingBefore().Draw();
        const CanvasEnvironment& environment = canvas.GetEnvironment();
        CanvasEnvironment next = environment;
        UInt32 key = 0;
        Bool changed = false;
        const AssetChoices& skies = ChoicesFor(AssetType::EnvironmentMap);
        {
            Widget::FormLayout layout("##environment");
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorEnvironmentAmbientSource, "Ambient Source"))
                    .Tooltip(Loc::TextOr(LocKeys::InspectorEnvironmentAmbientSourceTooltip, "where the light a standard material receives from every side comes from")),
                [&]() {
                    const char* const names[EnvironmentAmbientSourceCount] = {Loc::TextOr(LocKeys::InspectorEnvironmentSourceColor, "Color"),
                        Loc::TextOr(LocKeys::InspectorEnvironmentSourceGradient, "Gradient"), Loc::TextOr(LocKeys::InspectorEnvironmentSourceSky, "Sky")};
                    Int32 chosen = static_cast<JBro::Int32>(static_cast<std::uint8_t>(environment.ambientSource));
                    if (Widget::FilterCombo("##environmentSource", ArrayView<const char* const>(names, EnvironmentAmbientSourceCount), chosen).ShowFilter(false).Draw()
                        && chosen >= 0 && chosen < static_cast<JBro::Int32>(EnvironmentAmbientSourceCount))
                    {
                        next.ambientSource = static_cast<EnvironmentAmbientSource>(static_cast<std::uint8_t>(chosen.Get()));
                        changed = true;
                    }
                });
            const auto colorRow = [&](const char* label, const char* fallback, const char* id, Color CanvasEnvironment::*field, UInt32 mergeKey) {
                layout.Row(Widget::FieldLabel(Loc::TextOr(label, fallback)), [&]() {
                    Color color = environment.*field;
                    if (Widget::ColorField(id, color.Data()))
                    {
                        next.*field = color;
                        key = mergeKey;
                        changed = true;
                    }
                });
            };
            if (environment.ambientSource == EnvironmentAmbientSource::Color)
            {
                colorRow(LocKeys::InspectorEnvironmentAmbientColor, "Ambient Color", "##environmentColor", &CanvasEnvironment::ambientColor, 1);
            }
            else if (environment.ambientSource == EnvironmentAmbientSource::Gradient)
            {
                colorRow(LocKeys::InspectorEnvironmentSkyColor, "Sky Color", "##environmentSkyColor", &CanvasEnvironment::skyColor, 2);
                colorRow(LocKeys::InspectorEnvironmentEquatorColor, "Equator Color", "##environmentEquatorColor", &CanvasEnvironment::equatorColor, 3);
                colorRow(LocKeys::InspectorEnvironmentGroundColor, "Ground Color", "##environmentGroundColor", &CanvasEnvironment::groundColor, 4);
            }
            const auto scalarRow = [&](const char* label, const char* fallback, const char* id, Float CanvasEnvironment::*field, Float speed, Bool clamp,
                                       Float low, Float high, UInt32 mergeKey) {
                layout.Row(Widget::FieldLabel(Loc::TextOr(label, fallback)), [&]() {
                    Float value = environment.*field;
                    if (Widget::ScalarRunField(id, &value, 1, speed, clamp, low, high))
                    {
                        next.*field = value;
                        key = mergeKey;
                        changed = true;
                    }
                });
            };
            scalarRow(LocKeys::InspectorEnvironmentAmbientIntensity, "Ambient Intensity", "##environmentAmbientIntensity", &CanvasEnvironment::ambientIntensity,
                0.01f, true, 0.0f, 8.0f, 5);
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorEnvironmentSky, "Sky Map"))
                    .Tooltip(Loc::TextOr(LocKeys::InspectorEnvironmentSkyTooltip, "a .hdr panorama")),
                [&]() {
                    AssetId sky = environment.skyId;
                    if (Widget::AssetField("##environmentSky", ArrayView<const char* const>(skies.namePointers.Data(), skies.namePointers.Size()),
                            ArrayView<const AssetId>(skies.ids.Data(), skies.ids.Size()), sky)
                            .Draw())
                    {
                        next.skyId = sky;
                        changed = true;
                    }
                });
            scalarRow(LocKeys::InspectorEnvironmentSkyIntensity, "Sky Intensity", "##environmentSkyIntensity", &CanvasEnvironment::skyIntensity,
                0.01f, true, 0.0f, 8.0f, 6);
            scalarRow(LocKeys::InspectorEnvironmentSkyRotation, "Sky Rotation", "##environmentSkyRotation", &CanvasEnvironment::skyRotation,
                1.0f, true, -360.0f, 360.0f, 7);
            scalarRow(LocKeys::InspectorEnvironmentReflectionIntensity, "Reflection Intensity", "##environmentReflection", &CanvasEnvironment::reflectionIntensity,
                0.01f, true, 0.0f, 1.0f, 8);
        }
        if (changed && false == next.SameSettings(environment))
        {
            // **커맨드로만 고친다**(§11.5). 하늘을 바꾸면 에디터가 커맨드 뒤에 캔버스 에셋을 다시 묶어(D-115) 핸들이 풀린다.
            m_editor->GetCommands().Execute(MakeOwnerPtr<SetCanvasEnvironmentCommand>(canvas, next, key));
        }
    }

    void InspectorPanel::DrawCanvasViewports(Canvas& canvas)
    {
        Widget::SectionHeader(Loc::TextOr(LocKeys::InspectorCanvasViewports, "Viewports")).SpacingBefore().Draw();
        const Bool is3D = m_editor->GetFrameworkKind() == FrameworkKind::Framework3D;

        // 카메라 고르기의 목록: 맨 앞이 자동이고, 그 차원의 카메라를 단 오브젝트가 캔버스 차례로 온다.
        m_objectNames.Clear();
        m_objectIds.Clear();
        m_objectNames.Add(String(Loc::TextOr(LocKeys::InspectorViewportCameraAuto, "Auto")));
        m_objectIds.Add(InvalidInstanceId);
        const auto addCamera = [&](Object::GameObject* owner) {
            if (owner == nullptr)
            {
                return;
            }
            for (std::size_t at = 0; at < m_objectIds.Size(); ++at)
            {
                if (m_objectIds[at] == owner->GetInstanceId())
                {
                    return;
                }
            }
            const char* name = owner->GetName();
            m_objectNames.Add(String(name != nullptr ? name : ""));
            m_objectIds.Add(owner->GetInstanceId());
        };
        if (is3D)
        {
            canvas.ForEach<Component::Camera3D>([&](Component::Camera3D& camera) { addCamera(Internal::CanvasAccess::GetOwner(camera)); });
        }
        else
        {
            canvas.ForEach<Component::Camera2D>([&](Component::Camera2D& camera) { addCamera(Internal::CanvasAccess::GetOwner(camera)); });
        }
        m_objectNamePointers.Clear();
        for (const String& name : m_objectNames)
        {
            m_objectNamePointers.Add(name.c_str());
        }
        const UInt32 activeCameras = is3D ? System::Camera3DSystem::CountActiveCameras(canvas) : System::Camera2DSystem::CountActiveCameras(canvas);

        // 레이어 고르기는 월드 레이어와 화면 레이어를 따로 든다(D-439). 개수 제한이 없다(`LayerListField`).
        m_viewportWorldLayerIds.Clear();
        m_viewportWorldLayerNames.Clear();
        m_viewportScreenLayerIds.Clear();
        m_viewportScreenLayerNames.Clear();
        for (std::size_t at = 0; at < canvas.GetLayerCount(); ++at)
        {
            const Layer* layer = canvas.GetLayerAt(at);
            if (layer == nullptr)
            {
                continue;
            }
            if (layer->GetSpace() == LayerSpace::Screen)
            {
                m_viewportScreenLayerIds.Add(layer->GetId());
                m_viewportScreenLayerNames.Add(layer->GetName());
            }
            else
            {
                m_viewportWorldLayerIds.Add(layer->GetId());
                m_viewportWorldLayerNames.Add(layer->GetName());
            }
        }
        const AssetChoices& textures = ChoicesFor(AssetType::RenderTexture);

        if (canvas.GetViewportCount() == 0)
        {
            Widget::ValidationMessage(Widget::Severity::Warning, Loc::TextOr(LocKeys::InspectorViewportNone, "there is no viewport, so the world is not drawn")).Draw();
        }

        // **편집은 목록 전체를 앞뒤로 뜬 커맨드 하나다**(§11.5). 한 프레임에 하나만 낸다 - 낸 뒤에는 목록이 바뀌었으므로 그리기를 멈춘다.
        Array<CanvasViewport> edited;
        UInt32 mergeKey = 0;
        Bool changed = false;
        for (std::size_t index = 0; index < canvas.GetViewportCount() && false == changed; ++index)
        {
            const CanvasViewport& viewport = *canvas.GetViewportAt(index);
            Widget::IdScope scope(static_cast<JBro::Int32>(index));
            char title[160] = {};
            std::snprintf(title, sizeof(title), "%s###viewport", viewport.name.empty() ? "Viewport" : viewport.name.c_str());
            if (false == Widget::CollapsingSection(title, true))
            {
                continue;
            }
            CanvasViewport next = viewport;
            UInt32 key = 0;
            // 표는 소멸자에서 닫힌다 - 경고와 단추는 표가 닫힌 뒤 패널 폭 전체에 그린다(안에 두면 값 칸에서 시작해 잘린다).
            {
                Widget::FormLayout layout("##viewport");
                layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorViewportName, "Name")), [&]() {
                    if (m_namedViewport != static_cast<JBro::Int32>(index) || false == m_viewportNameEditing)
                    {
                        m_namedViewport = static_cast<JBro::Int32>(index);
                        m_viewportName = viewport.name;
                    }
                    const Bool finished = Widget::TextField("##viewportName", m_viewportName).CommitOnFinish().Draw();
                    m_viewportNameEditing = ImGui::IsItemActive();
                    if (finished && m_viewportName != viewport.name)
                    {
                        next.name = m_viewportName;
                        changed = true;
                    }
                });
                layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorViewportActive, "Active")), [&]() {
                    Bool active = viewport.active;
                    if (Widget::Checkbox("##viewportActive", active))
                    {
                        next.active = active;
                        changed = true;
                    }
                });
                layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorViewportCamera, "Camera"))
                        .Tooltip(Loc::TextOr(LocKeys::InspectorViewportCameraTooltip, "the camera this viewport draws with - Auto uses the first active camera")),
                    [&]() {
                        Int32 chosen = viewport.HasCamera() ? -1 : 0;
                        for (std::size_t at = 1; at < m_objectIds.Size(); ++at)
                        {
                            if (m_objectIds[at] == viewport.camera)
                            {
                                chosen = static_cast<JBro::Int32>(at);
                            }
                        }
                        UInt64 dropped = 0;
                        if (Widget::ObjectField("##viewportCamera",
                                ArrayView<const char* const>(m_objectNamePointers.Data(), m_objectNamePointers.Size()), chosen,
                                Widget::DragKind::HierarchyObject, dropped))
                        {
                            if (dropped != 0)
                            {
                                Object::GameObject* object = m_editor->GetObjectIds().Resolve(static_cast<EditorObjectId>(dropped));
                                if (object != nullptr)
                                {
                                    next.camera = object->GetInstanceId();
                                    changed = true;
                                }
                            }
                            else if (chosen >= 0 && static_cast<std::size_t>(chosen) < m_objectIds.Size())
                            {
                                next.camera = m_objectIds[static_cast<std::size_t>(chosen)];
                                changed = true;
                            }
                        }
                    });
                layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorViewportOrigin, "Position"))
                        .Tooltip(Loc::TextOr(LocKeys::InspectorViewportOriginTooltip, "the top-left corner on the screen, as a 0 to 1 ratio")),
                    [&]() {
                        Float values[2] = {viewport.origin.x, viewport.origin.y};
                        if (Widget::ScalarRunField("##viewportOrigin", values, 2, 0.005f, true, 0.0f, 1.0f))
                        {
                            next.origin = Vector2{values[0], values[1]};
                            key = static_cast<JBro::UInt32>(index) * 8u + 1u;
                            changed = true;
                        }
                    });
                layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorViewportSize, "Size"))
                        .Tooltip(Loc::TextOr(LocKeys::InspectorViewportSizeTooltip, "the width and height on the screen, as a 0 to 1 ratio")),
                    [&]() {
                        Float values[2] = {viewport.size.x, viewport.size.y};
                        if (Widget::ScalarRunField("##viewportSize", values, 2, 0.005f, true, 0.0f, 1.0f))
                        {
                            next.size = Vector2{values[0], values[1]};
                            key = static_cast<JBro::UInt32>(index) * 8u + 2u;
                            changed = true;
                        }
                    });
                layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorViewportOriginPixels, "Position Pixels"))
                        .Tooltip(Loc::TextOr(LocKeys::InspectorViewportOriginPixelsTooltip, "pixels added to the ratio position")),
                    [&]() {
                        Float values[2] = {viewport.originPixels.x, viewport.originPixels.y};
                        if (Widget::ScalarRunField("##viewportOriginPixels", values, 2, 1.0f, false, 0.0f, 0.0f))
                        {
                            next.originPixels = Vector2{values[0], values[1]};
                            key = static_cast<JBro::UInt32>(index) * 8u + 3u;
                            changed = true;
                        }
                    });
                layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorViewportSizePixels, "Size Pixels"))
                        .Tooltip(Loc::TextOr(LocKeys::InspectorViewportSizePixelsTooltip, "pixels added to the ratio size")),
                    [&]() {
                        Float values[2] = {viewport.sizePixels.x, viewport.sizePixels.y};
                        if (Widget::ScalarRunField("##viewportSizePixels", values, 2, 1.0f, false, 0.0f, 0.0f))
                        {
                            next.sizePixels = Vector2{values[0], values[1]};
                            key = static_cast<JBro::UInt32>(index) * 8u + 4u;
                            changed = true;
                        }
                    });
                layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorViewportClear, "Clear Background"))
                        .Tooltip(Loc::TextOr(LocKeys::InspectorViewportClearTooltip,
                            "fill this rectangle with the camera's clear colour before drawing over earlier viewports")),
                    [&]() {
                        Bool clear = viewport.clearViewport;
                        if (Widget::Checkbox("##viewportClear", clear))
                        {
                            next.clearViewport = clear;
                            changed = true;
                        }
                    });
                layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorViewportRenderTexture, "Render Texture"))
                        .Tooltip(Loc::TextOr(LocKeys::InspectorViewportRenderTextureTooltip, "draw into this render texture instead of the screen")),
                    [&]() {
                        AssetId texture = viewport.renderTextureId;
                        if (Widget::AssetField("##viewportRenderTexture",
                                ArrayView<const char* const>(textures.namePointers.Data(), textures.namePointers.Size()),
                                ArrayView<const AssetId>(textures.ids.Data(), textures.ids.Size()), texture)
                                .Draw())
                        {
                            next.renderTextureId = texture;
                            changed = true;
                        }
                    });
                // 칸 하나가 고친 쪽만 다시 적고 다른 쪽은 그대로 둔다. 월드는 모두 켜면 비우고("모두" - 나중에 만든 레이어도 그린다),
                // 하나도 없으면 "월드 레이어 없음" 표시를 남긴다. 화면은 켠 것만 적는다.
                const auto keepScreen = [&](Array<LayerId>& out) {
                    for (std::size_t at = 0; at < viewport.layers.Size(); ++at)
                    {
                        const Layer* layer = canvas.FindLayer(viewport.layers[at]);
                        if (layer != nullptr && layer->GetSpace() == LayerSpace::Screen)
                        {
                            out.Add(viewport.layers[at]);
                        }
                    }
                };
                const auto keepWorld = [&](Array<LayerId>& out) {
                    for (std::size_t at = 0; at < viewport.layers.Size(); ++at)
                    {
                        const Layer* layer = canvas.FindLayer(viewport.layers[at]);
                        if (layer == nullptr || layer->GetSpace() != LayerSpace::Screen)
                        {
                            out.Add(viewport.layers[at]);
                        }
                    }
                };
                layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorViewportLayers, "World Layers"))
                        .Tooltip(Loc::TextOr(LocKeys::InspectorViewportLayersTooltip, "the world layers this viewport draws")),
                    [&]() {
                        m_viewportLayerOn.Clear();
                        for (std::size_t at = 0; at < m_viewportWorldLayerIds.Size(); ++at)
                        {
                            m_viewportLayerOn.Add(ViewportDrawsLayer(canvas, viewport, m_viewportWorldLayerIds[at]));
                        }
                        if (Widget::LayerListField("##viewportLayers",
                                ArrayView<const char* const>(m_viewportWorldLayerNames.Data(), m_viewportWorldLayerNames.Size()), m_viewportLayerOn))
                        {
                            next.layers.Clear();
                            std::size_t onCount = 0;
                            for (std::size_t at = 0; at < m_viewportLayerOn.Size(); ++at)
                            {
                                onCount += m_viewportLayerOn[at] ? 1u : 0u;
                            }
                            if (onCount != m_viewportLayerOn.Size())
                            {
                                for (std::size_t at = 0; at < m_viewportLayerOn.Size(); ++at)
                                {
                                    if (m_viewportLayerOn[at])
                                    {
                                        next.layers.Add(m_viewportWorldLayerIds[at]);
                                    }
                                }
                                if (onCount == 0)
                                {
                                    next.layers.Add(InvalidLayerId);
                                }
                            }
                            keepScreen(next.layers);
                            changed = true;
                        }
                    });
                if (false == m_viewportScreenLayerIds.IsEmpty())
                {
                    layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorViewportScreenLayers, "Screen Layers"))
                            .Tooltip(Loc::TextOr(LocKeys::InspectorViewportScreenLayersTooltip, "the screen layers drawn inside this viewport")),
                        [&]() {
                            m_viewportLayerOn.Clear();
                            for (std::size_t at = 0; at < m_viewportScreenLayerIds.Size(); ++at)
                            {
                                m_viewportLayerOn.Add(ViewportDrawsLayer(canvas, viewport, m_viewportScreenLayerIds[at]));
                            }
                            if (Widget::LayerListField("##viewportScreenLayers",
                                    ArrayView<const char* const>(m_viewportScreenLayerNames.Data(), m_viewportScreenLayerNames.Size()), m_viewportLayerOn))
                            {
                                next.layers.Clear();
                                keepWorld(next.layers);
                                for (std::size_t at = 0; at < m_viewportLayerOn.Size(); ++at)
                                {
                                    if (m_viewportLayerOn[at])
                                    {
                                        next.layers.Add(m_viewportScreenLayerIds[at]);
                                    }
                                }
                                changed = true;
                            }
                        });
                }
            }

            // 경고: 못 찾는 카메라, 자동인데 켜진 카메라가 여럿.
            if (viewport.HasCamera())
            {
                const Bool found = is3D ? System::Camera3DSystem::ResolveViewportCamera(canvas, viewport) != nullptr
                                        : System::Camera2DSystem::ResolveViewportCamera(canvas, viewport) != nullptr;
                if (false == found)
                {
                    Widget::ValidationMessage(Widget::Severity::Warning,
                        Loc::TextOr(LocKeys::InspectorViewportUnresolved, "the camera object is gone or has no active camera, so this viewport draws nothing")).Draw();
                }
            }
            else if (activeCameras > 1)
            {
                char ambiguous[256] = {};
                std::snprintf(ambiguous, sizeof(ambiguous),
                    Loc::TextOr(LocKeys::InspectorViewportAmbiguous, "%u cameras are active - Auto draws with the first one"), static_cast<unsigned>(activeCameras.Get()));
                Widget::ValidationMessage(Widget::Severity::Warning, ambiguous).Draw();
            }

            // 차례 바꾸기와 빼기. 목록 차례가 그리는 차례다.
            const Bool first = index == 0;
            const Bool last = index + 1 == canvas.GetViewportCount();
            // 아이콘 단추 셋이다 - 좁은 인스펙터에서 글자 단추는 한 줄을 넘어 잘린다. 뜻은 툴팁이 말한다.
            Int32 move = 0;
            Bool remove = false;
            if (Widget::IconButton("##viewportUp", Icons::ArrowUp)
                    .Tooltip(Loc::TextOr(LocKeys::InspectorViewportMoveUp, "Move Up")).Disabled(first).Draw())
            {
                move = -1;
            }
            ImGui::SameLine();
            if (Widget::IconButton("##viewportDown", Icons::ArrowDown)
                    .Tooltip(Loc::TextOr(LocKeys::InspectorViewportMoveDown, "Move Down")).Disabled(last).Draw())
            {
                move = 1;
            }
            ImGui::SameLine();
            if (Widget::IconButton("##viewportRemove", Icons::Delete)
                    .Tooltip(Loc::TextOr(LocKeys::InspectorViewportRemove, "Remove")).Draw())
            {
                remove = true;
            }

            if (changed || move != 0 || remove)
            {
                edited = canvas.GetViewports();
                if (remove)
                {
                    edited.RemoveAt(index);
                }
                else if (move != 0)
                {
                    const std::size_t other = move < 0 ? index - 1 : index + 1;
                    const CanvasViewport swapped = edited[other];
                    edited[other] = edited[index];
                    edited[index] = swapped;
                }
                else
                {
                    edited[index] = next;
                }
                mergeKey = key;
                changed = true;
            }
        }

        if (false == changed && Widget::Button(Loc::TextOr(LocKeys::InspectorViewportAdd, "Add Viewport"), Icons::Plus))
        {
            edited = canvas.GetViewports();
            CanvasViewport added = Canvas::MakeDefaultViewport();
            char name[32] = {};
            std::snprintf(name, sizeof(name), "Viewport %u", static_cast<unsigned>(edited.Size() + 1));
            added.name = name;
            edited.Add(added);
            changed = true;
        }
        if (changed)
        {
            m_editor->GetCommands().Execute(MakeOwnerPtr<SetCanvasViewportsCommand>(canvas, edited, mergeKey));
        }
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
        // **레이어의 후처리 프로필**(D-345). 프로필 에셋 중에서 고른다 - 효과는 그 프로필 에셋을 고르면 인스펙터가 고친다.
        layout.Row(
            Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorLayerPostProcess, "Post-Process Profile"))
                .Tooltip(Loc::TextOr(LocKeys::InspectorLayerPostProcessTooltip,
                    "the profile's layer effects are applied before the layer is laid onto what is below")),
            [&]() {
                const AssetChoices& choices = ChoicesFor(AssetType::PostProcessProfile);
                AssetId profile = layer->GetPostProcessProfile();
                if (Widget::AssetField("##layerPostProcess",
                        ArrayView<const char* const>(choices.namePointers.Data(), choices.namePointers.Size()),
                        ArrayView<const AssetId>(choices.ids.Data(), choices.ids.Size()), profile)
                        .Draw())
                {
                    m_editor->GetCommands().Execute(MakeOwnerPtr<SetLayerPostProcessProfileCommand>(*canvas, layerId, profile));
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
    void InspectorPanel::DrawAddComponent(Object::GameObject& object)
    {
        // 검색 드롭다운 하나다(D-116). 현재 번호를 늘 -1 로 주므로 트리거에는 "컴포넌트
        // 추가" 가 보이고, 고르면 그 자리에서 커맨드 하나가 나간다.
        //
        // 목록은 오브젝트 메뉴와 **같은 것**을 쓴다(D-180). 갈래로 묶이고, 이미 붙어 있어
        // 더 붙일 수 없는 것은 회색으로 남는다 - 목록에서 빼 버리면 찾던 이름이 사라진다.
        EditorActions::AddComponentList list;
        EditorActions::BuildAddComponentList(*m_editor, object, list);
        Int32 chosen = -1;
        // + 는 버튼 **안에** 선다(D-308, 기존 `+ 컴포넌트 추가`). 밖에 두면 버튼과 따로 노는 글자처럼 보인다.
        Widget::SetNextItemTarget(GuideFocusTargets::InspectorAddComponent());
        const Bool picked = Widget::FilterCombo("##AddComponent",
            ArrayView<const char* const>(list.names.Data(), list.names.Size()), chosen)
            .EmptyText(Loc::TextOr(LocKeys::InspectorAddComponent, "Add Component"))
            .Icon(Icons::Plus)
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

    Bool InspectorPanel::DrawComponentDrag(Object::GameObject& object, AttachedKind kind, std::size_t index, const char* typeName,
        const ImVec2& headerMin, const ImVec2& headerMax)
    {
        const EditorObjectId objectId = m_editor->GetObjectIds().Track(&object);
        if (Widget::BeginDragSource())
        {
            Widget::ComponentDragPayload payload;
            payload.object = objectId;
            payload.slot = static_cast<JBro::UInt64>(index);
            payload.kind = kind;
            if (kind == AttachedKind::Component && index < object.GetComponents().Size())
            {
                payload.instance = object.GetComponents()[index].reference->GetInstanceId();
            }
            else if (kind == AttachedKind::Script && index < object.GetScripts().Size())
            {
                payload.instance = object.GetScripts()[index].reference->GetInstanceId();
            }
            Widget::SetComponentDragPayload(payload);
            Widget::Text(typeName != nullptr ? typeName
                                             : Loc::TextOr(LocKeys::InspectorUnknownComponent, "(unknown component)"));
            Widget::EndDragSource();
        }
        if (false == Widget::BeginDropTarget())
        {
            return false;
        }
        Bool moved = false;
        const Widget::DropPayload drop =
            Widget::AcceptDrop(Widget::DragKind::InspectorComponent, Widget::DropFeedback::None);
        Widget::ComponentDragPayload dragged;
        // 다른 오브젝트나 다른 목록의 머리에서 온 것은 받지 않는다 - 옮기기는 한 목록 안의 순서다(D-302).
        if (Widget::ReadDropValue(drop, dragged) && dragged.object == objectId && dragged.kind == kind)
        {
            // 위 절반은 이 머리의 앞, 아래 절반은 뒤다.
            const Float height = (std::max)(1.0f, headerMax.y - headerMin.y);
            const Bool above = (ImGui::GetIO().MousePos.y - headerMin.y) / height < 0.5f;
            const std::size_t from = static_cast<std::size_t>(dragged.slot.Get());
            const Bool matches = kind == AttachedKind::Component
                ? from < object.GetComponents().Size() && object.GetComponents()[from].reference->GetInstanceId() == dragged.instance
                : from < object.GetScripts().Size() && object.GetScripts()[from].reference->GetInstanceId() == dragged.instance;
            if (!matches)
            {
                Widget::EndDropTarget();
                return false;
            }
            const std::size_t to = ComponentDropSlot(from, index, above);
            if (to != from)
            {
                Widget::DrawDropLine(headerMin.x, headerMax.x, above ? headerMin.y : headerMax.y);
            }
            if (drop.delivered && to != from)
            {
                MoveAttached(object, from, to, kind);
                moved = true;
            }
        }
        Widget::EndDropTarget();
        return moved;
    }

    void InspectorPanel::DrawTagField(Object::GameObject& object)
    {
        // 고른 것이 모두 바뀐다(활성 칸과 같다, D-142).
        const auto selectedIds = [&]() {
            Array<EditorObjectId> ids;
            const Array<Object::GameObject*> chosen = m_editor->GetSelectedObjects();
            for (std::size_t index = 0; index < chosen.Size(); ++index)
            {
                if (chosen[index] != nullptr)
                {
                    ids.Add(m_editor->GetObjectIds().Track(chosen[index]));
                }
            }
            if (ids.IsEmpty())
            {
                ids.Add(m_editor->GetObjectIds().Track(&object));
            }
            return ids;
        };
        // 항목: `태그 없음`, 프로젝트의 태그들, (목록에 없는 지금 태그), `태그 추가...`.
        const Array<String>& tags = m_editor->GetProjectFile().tags;
        const char* current = object.GetTagId() != InvalidNameId ? object.GetTag() : nullptr;
        Array<const char*> items;
        items.Add(Loc::TextOr(LocKeys::InspectorTagNone, "Untagged"));
        Int32 chosen = 0;
        for (const String& tag : tags)
        {
            if (current != nullptr && tag == current)
            {
                chosen = static_cast<JBro::Int32>(items.Size());
            }
            items.Add(tag.c_str());
        }
        // 목록에서 지운 태그를 단 오브젝트도 제 태그를 보인다. 고르면 그 태그가 그대로 남는다.
        if (current != nullptr && chosen == 0)
        {
            chosen = static_cast<JBro::Int32>(items.Size());
            items.Add(current);
        }
        const Int32 addIndex = static_cast<JBro::Int32>(items.Size());
        items.Add(Loc::TextOr(LocKeys::InspectorTagAdd, "Add Tag..."));
        const Int32 before = chosen;
        if (Widget::FilterCombo("##tag", ArrayView<const char* const>(items.Data(), items.Size()), chosen).Draw() && chosen != before)
        {
            if (chosen == addIndex)
            {
                m_newTag.clear();
                Widget::OpenContextMenu("##addTag");
            }
            else
            {
                const char* tag = chosen == 0 ? "" : items[static_cast<std::size_t>(chosen.Get())];
                m_editor->GetCommands().Execute(MakeOwnerPtr<SetObjectTagCommand>(m_editor->GetObjectIds(), selectedIds(), tag));
            }
        }
        if (Widget::BeginOpenedContextMenu("##addTag"))
        {
            const Bool entered = Widget::TextField("##newTag", m_newTag)
                .Hint(Loc::TextOr(LocKeys::InspectorTagNameHint, "new tag name"))
                .CommitOnEnter()
                .Width(180.0f)
                .Draw();
            const Bool clicked = Widget::Button(Loc::TextOr(LocKeys::InspectorTagAddConfirm, "Add"));
            if ((entered || clicked) && false == m_newTag.empty())
            {
                ProjectFileError error;
                if (m_editor->AddProjectTag(m_newTag.c_str(), error))
                {
                    m_editor->GetCommands().Execute(
                        MakeOwnerPtr<SetObjectTagCommand>(m_editor->GetObjectIds(), selectedIds(), m_newTag.c_str()));
                }
                else
                {
                    Log::Write(LogLevel::Warning, "editor", "the tag could not be added: %s", error.message.c_str());
                }
                m_newTag.clear();
                Widget::CloseContextMenu();
            }
            Widget::EndContextMenu();
        }
    }

    void InspectorPanel::MoveAttached(Object::GameObject& object, std::size_t from, std::size_t to, AttachedKind kind)

    {
        const EditorObjectId objectId = m_editor->GetObjectIds().Track(&object);
        m_editor->GetCommands().Execute(MakeOwnerPtr<MoveComponentCommand>(
            m_editor->GetObjectIds(), objectId, from, to, kind));
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
        // **빌트인 도형이 메시 목록의 앞에 선다**(D-388 우선순위 2). 레지스트리에 없는 아이디라, 넣지 않으면 새 프로젝트의 큐브가
        // `(없는 에셋)` 으로 보였다(시나리오 D). 파일 에셋과 겹치지 않게 이름에 경로가 없다.
        if (type == AssetType::Mesh || also == AssetType::Mesh)
        {
            for (UInt32 index = 0; index < MeshLibrary::GetBuiltinCount(); ++index)
            {
                const MeshLibrary::BuiltinMesh builtin = MeshLibrary::GetBuiltin(index);
                choices->names.Add(String(builtin.name));
                choices->ids.Add(builtin.id);
            }
        }
        for (std::size_t index = 0; index < registry.GetCount(); ++index)
        {
            const AssetRecord& record = registry.GetRecord(index);
            if (type != AssetType::Unknown && record.type != type && (also == AssetType::Unknown || record.type != also))
            {
                continue;
            }
            // 모델의 하위 에셋(D-391)은 경로가 모델과 같다. 이름을 붙여야 칸에서 갈린다.
            if (record.name.empty())
            {
                choices->names.Add(record.relativePath);
            }
            else
            {
                String label = record.relativePath;
                label += " / ";
                label += record.name;
                choices->names.Add(std::move(label));
            }
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
        Handle::GameObject& handle = *static_cast<Handle::GameObject*>(address);
        m_objectNames.Clear();
        m_objectIds.Clear();
        m_objectNames.Add(String(Loc::TextOr(LocKeys::InspectorObjectNone, "None")));
        m_objectIds.Add(InvalidInstanceId);
        Int32 current = handle.GetInstanceId() == InvalidInstanceId ? 0 : -1;
        if (Canvas* canvas = m_editor->GetCanvas())
        {
            canvas->ForEachObject([&](Object::GameObject& object) {
                if (object.GetInstanceId() == handle.GetInstanceId())
                {
                    current = static_cast<JBro::Int32>(m_objectIds.Size());
                }
                const char* name = object.GetName();
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
            Object::GameObject* object = m_editor->GetObjectIds().Resolve(static_cast<EditorObjectId>(dropped));
            if (object == nullptr)
            {
                return;
            }
            handle = object->GetScriptHandle();
        }
        else if (chosen >= 0 && static_cast<std::size_t>(chosen) < m_objectIds.Size())
        {
            handle = chosen == 0 ? Handle::GameObject{} : Internal::GameObjectHandleAccess::FromId(m_objectIds[static_cast<std::size_t>(chosen)]);
        }
        else
        {
            return;
        }
        CommitEdit(type, address, before, context);
    }

    void InspectorPanel::DrawComponentHandleField(const TypeDescriptor& type, void* address, Context& context)
    {
        Internal::ComponentHandle& handle = *static_cast<Internal::ComponentHandle*>(address);
        const InstanceRef& reference = Internal::ComponentHandleAccess::GetReference(handle);
        const char* targetName = NameTable::Get().Resolve(type.refTarget->targetType);
        const ComponentTypeId typeId = targetName != nullptr ? MakeStableTypeId(targetName) : InvalidComponentTypeId;
        m_objectNames.Clear();
        m_componentRefs.Clear();
        m_objectNames.Add(String(Loc::TextOr(LocKeys::InspectorObjectNone, "None")));
        m_componentRefs.Add(InstanceRef{});
        Int32 current = reference.ComponentId == InvalidInstanceId ? 0 : -1;
        if (Canvas* canvas = m_editor->GetCanvas())
        {
            canvas->ForEachObject([&](Object::GameObject& object) {
                // 한 오브젝트에 같은 타입이 여럿이면 두 번째부터 이름 뒤에 차례를 붙인다.
                Int32 ordinal = 0;
                for (const ComponentSlot& slot : object.GetComponents())
                {
                    ComponentBase* component = slot.reference.TryGet();
                    if (slot.typeId != typeId || component == nullptr)
                    {
                        continue;
                    }
                    if (component->GetInstanceId() == reference.ComponentId)
                    {
                        current = static_cast<JBro::Int32>(m_componentRefs.Size());
                    }
                    const char* name = object.GetName();
                    String label(name != nullptr ? name : "");
                    if (ordinal > 0)
                    {
                        label += " (";
                        label += std::to_string(ordinal.Get() + 1).c_str();
                        label += ")";
                    }
                    m_objectNames.Add(std::move(label));
                    InstanceRef entry;
                    entry.ObjectId = object.GetInstanceId();
                    entry.ComponentId = component->GetInstanceId();
                    m_componentRefs.Add(entry);
                    ++ordinal;
                }
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
        const Bool changed = Widget::ObjectField("##value",
            ArrayView<const char* const>(m_objectNamePointers.Data(), m_objectNamePointers.Size()), chosen,
            Widget::DragKind::HierarchyObject, dropped);
        if (false == changed || false == snapped)
        {
            return;
        }
        if (dropped != 0)
        {
            // 끌어다 놓은 오브젝트에 그 타입이 붙어 있으면 첫 번째를 고른다. 없으면 그대로 둔다.
            Object::GameObject* object = m_editor->GetObjectIds().Resolve(static_cast<EditorObjectId>(dropped));
            InstanceRef found;
            if (object != nullptr)
            {
                for (const ComponentSlot& slot : object->GetComponents())
                {
                    ComponentBase* component = slot.reference.TryGet();
                    if (slot.typeId == typeId && component != nullptr)
                    {
                        found.ObjectId = object->GetInstanceId();
                        found.ComponentId = component->GetInstanceId();
                        break;
                    }
                }
            }
            if (found.ComponentId == InvalidInstanceId)
            {
                return;
            }
            Internal::ComponentHandleAccess::SetReference(handle, found);
        }
        else if (chosen >= 0 && static_cast<std::size_t>(chosen) < m_componentRefs.Size())
        {
            Internal::ComponentHandleAccess::SetReference(handle, m_componentRefs[static_cast<std::size_t>(chosen)]);
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
                    // 지금 / 길이다(기존 `"%.2f / %.2f s"`, D-305).
                    const Int32 minutes = static_cast<JBro::Int32>(position / 60.0);
                    const Int32 totalMinutes = static_cast<JBro::Int32>(m_audioSeconds / 60.0);
                    Widget::TextF("%d:%05.2f / %d:%05.2f", minutes, position - minutes * 60.0, totalMinutes,
                        m_audioSeconds - totalMinutes * 60.0);
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
        // **울리는 동안 위치 막대와 스펙트럼**(D-305, 기존 탐색 막대와 `ImSpectrumVisualizer`). 막대를 끌면 그 자리로 옮긴다.
        // 스펙트럼은 믹서의 최근 출력이다 - 편집 중에는 미리 듣기만 울린다.
        if (playingThis && m_audioSeconds > 0.0)
        {
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorAudioPosition, "Position")),
                [&]() {
                    Float seconds = static_cast<JBro::Float>(position);
                    if (Widget::SliderField("##position", seconds, 0.0f, static_cast<JBro::Float>(m_audioSeconds))
                            .Format("%.2f s")
                            .Draw())
                    {
                        audio->SeekPreview(static_cast<double>(seconds.Get()));
                    }
                });
            if (AudioMixer* mixer = audio->GetMixer())
            {
                mixer->ComputeSpectrum(m_audioSpectrum, AudioSpectrumBands);
                layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorAudioSpectrum, "Spectrum")),
                    [&]() {
                        Widget::Spectrum("##spectrum", {m_audioSpectrum, AudioSpectrumBands}, ImGui::GetFrameHeight() * 2.5f);
                    });
            }
        }
    }

    void InspectorPanel::DrawAssetField(
        const char* fieldName, const TypeDescriptor& type, void* address, Context& context)
    {
        // 텍스트의 `fontId` 는 폰트와 폰트 패밀리를 다 받는다(D-225). 패밀리 안의 칸(`regularFontId` 따위)은 폰트만이다.
        // 그림 칸(`...TextureId` - 표준 머티리얼의 칸뿐이다)은 렌더 텍스처도 받는다(D-439) - 3D 뷰포트가 그린 그림을 메시에 입힌다.
        const AssetType fieldType = AssetTypeOfIdName(fieldName);
        const AssetChoices& choices = std::strcmp(fieldName, "fontId") == 0
            ? ChoicesFor(AssetType::Font, AssetType::FontFamily)
            : (fieldType == AssetType::Texture ? ChoicesFor(AssetType::Texture, AssetType::RenderTexture) : ChoicesFor(fieldType));
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
                    if (SetPropertyCommand::ReadValue(context.attached.GetInstance(), context.typeId, context.path, before))
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
            if (SetPropertyCommand::ReadValue(context.attached.GetInstance(), context.typeId, context.path, before))
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
        // 범위가 있어도 `Clamp` 면 슬라이더가 아니라 범위로 자르는 끌기 칸이다(D-298).
        const Bool hasRange = edit != nullptr && edit->hasRange && edit->slider;
        const Bool clamped = edit != nullptr && edit->hasRange && false == edit->slider;
        const Float clampMin = clamped ? edit->rangeMin : Float(0.0f);
        const Float clampMax = clamped ? edit->rangeMax : Float(0.0f);
        if (SameName(type.typeName, "float"))
        {
            Float& value = *static_cast<Float*>(address);
            if (clamped)
            {
                return Widget::DragField("##value", value).Speed(0.01f).Range(clampMin, clampMax).StepButtons(false)();
            }
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
            const Bool changed = clamped
                ? Widget::DragField("##value", degrees).Speed(0.5f).Range(clampMin, clampMax).Format("%.2f\u00b0").StepButtons(false)()
                : hasRange
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
        if (false == context.attached.IsValid() || context.owner == nullptr
            || false == FindAttachedOrdinal(*context.owner, context.attached, ordinal))

        {
            // 주인에게서 자리를 셀 수 없으면 가리킬 방법이 없다. 포인터로 쓰는
            // 커맨드를 만들어 두면 지웠다 되살린 뒤에 조용히 헛돈다.
            return targets;
        }

        // **조상이 함께 골라졌으면 뺀다.** 부모를 옮기면 자식은 따라 움직이므로
        // 둘 다 대상으로 삼으면 자식에게 두 번 적용된다(기존 `GetSelectedTopLevel`).
        const Array<Object::GameObject*> chosen = m_editor->GetTopLevelSelectedObjects();
        for (std::size_t index = 0; index < chosen.Size(); ++index)
        {
            const AttachedRef found = FindAttachedAt(*chosen[index], context.attached.GetKind(), context.typeId, ordinal);
            if (found.IsValid())
            {
                targets.Add(EditTarget{chosen[index], found});
            }
        }
        if (targets.IsEmpty())
        {
            // 고른 것이 없거나(인스펙터만 열어 둔 경우) 셈이 어긋났다.
            // 눈앞의 것 하나는 반드시 고쳐져야 한다.
            targets.Add(EditTarget{context.owner, context.attached});
        }
        return targets;
    }

    void InspectorPanel::DrawAsset(const AssetMetaFile& meta)
    {
        // **모델의 하위 에셋을 골랐으면 그것을 보인다**(model-import-plan 5 단계). 메타는 모델의 것이고(하위 에셋은 제 메타가 없다),
        // 머리·그림·옵션은 고른 하위 에셋의 것이다. 그림의 Sprite 는 그림 파일의 인스펙터 그대로다(두 블록이 한 파일의 옵션이다).
        const AssetRegistry& registry = m_editor->GetAssetRegistry();
        const AssetRecord* selected = registry.Find(m_editor->GetSelectedAsset());
        const AssetRecord* owner = meta.type == AssetType::Model && selected != nullptr && selected->owner == meta.id ? registry.Find(meta.id) : nullptr;
        const AssetRecord* record = owner != nullptr ? selected : registry.Find(meta.id);
        const AssetId shownId = record != nullptr ? record->id : meta.id;
        // **무엇을 보고 있는지 머리에 적는다**(D-173, 기존 에셋 인스펙터의 첫 네 줄).
        // 경로 한 줄만 있으면 그 파일이 어떤 종류로 등록되었는지, 번호가 무엇인지 알 길이
        // 에디터 안에 없었다 - `.jmeta` 를 직접 열어 봐야 했다.
        {
            Widget::FormLayout header("##assetHeader");
            const char* path = record != nullptr ? record->relativePath.c_str() : "?";
            const char* leaf = EditorPaths::LeafOfPath(path);
            header.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorAssetName, "Asset")),
                [&]() { Widget::WrappedText(owner != nullptr ? record->name.c_str() : (leaf != nullptr ? leaf : path)); });
            if (owner != nullptr)
            {
                header.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorOwnerModel, "Model")),
                    [&]() { Widget::WrappedText(leaf != nullptr ? leaf : path); });
            }
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
                    if (shownId.ToText(text, sizeof(text)))
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
        const TextureHandle preview = m_editor->GetAssetThumbnail(shownId);
        if (preview.IsValid())
        {
            // 칸 너비에 맞추되 원본 비율을 지킨다. 늘여 붙이면 픽셀 아트가 기울어 보인다.
            const Float width = ImGui::GetContentRegionAvail().x;
            const Float side = width < PreviewMaxSide ? width : PreviewMaxSide;
            UInt32 sourceWidth = 0;
            UInt32 sourceHeight = 0;
            m_editor->GetAssetSourceSize(shownId, sourceWidth, sourceHeight);
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
        // 사운드 이펙트는 편집 창에서 고친다(D-306, 기존 인스펙터의 `Open Effect Editor`).
        if (meta.type == AssetType::AudioEffect)
        {
            Widget::HintText(Loc::TextOr(LocKeys::InspectorSoundEffectHint, "Edit sound effects in their own window"));
            if (Widget::IconButton("##open_effect_editor", Icons::OpenExternal)
                    .Caption(Loc::TextOr(LocKeys::InspectorOpenSoundEffect, "Open Effect Editor"))
                    .Draw())
            {
                m_editor->OpenSoundEffectEditor(meta.id);
            }
            ImGui::Spacing();
        }
        if (meta.type == AssetType::PostProcessProfile)
        {
            DrawPostProcessProfile(meta);
            ImGui::Spacing();
        }
        // `.jmat` 은 두 갈래다(2026-11-19 main 통합). 표준 머티리얼(D-399)은 아래 `DrawAssetOptions` 의 머티리얼 블록이 그리고,
        // 셰이더 머티리얼(D-347)은 여기서 셰이더·파라미터·텍스처를 그린다. 에디터가 표준 머티리얼로 읽지 못한 것이 셰이더 머티리얼이다.
        if (meta.type == AssetType::Material && m_editor->GetSelectedMaterial() == nullptr)
        {
            DrawMaterial(meta);
            ImGui::Spacing();
        }
        if (meta.type == AssetType::Shader)
        {
            DrawShader(meta);
            ImGui::Spacing();
        }
        if (meta.type == AssetType::RenderTexture)
        {
            DrawRenderTexture(meta);
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
        if (owner != nullptr)
        {
            DrawSubAssetOptions(meta, *record);
            return;
        }
        DrawAssetOptions(meta);
    }

    void InspectorPanel::DrawSubAssetOptions(const AssetMetaFile& meta, const AssetRecord& record)
    {
        // 메시·재질은 제 옵션이 없다 - 모델의 옵션이 정한다(모델을 고르면 보인다). 내장 그림은 그림마다 텍스처 옵션이 있다.
        std::size_t subIndex = meta.subAssets.Size();
        for (std::size_t index = 0; index < meta.subAssets.Size(); ++index)
        {
            if (meta.subAssets[index].id == record.id)
            {
                subIndex = index;
            }
        }
        // **모델의 재질은 보이되 고치지 않는다**(D-399 (2)). 파일에서 온 값이라 여기서 고쳐도 다시 가져오면 사라진다 - 고치려면 재질을 꺼내야 한다(뒤).
        if (record.type == AssetType::Material)
        {
            const MaterialDesc* shown = m_editor->GetSelectedMaterial();
            if (shown == nullptr)
            {
                return;
            }
            Widget::WrappedHintText(Loc::TextOr(LocKeys::InspectorModelMaterialReadOnly, "This material comes from the model file and cannot be edited here"));
            MaterialDesc copy = *shown;
            AssetEditScope readOnly;
            readOnly.block = AssetEditScope::Block::Material;
            Context context;
            context.asset = &readOnly;
            ImGui::PushID("##modelMaterial");
            if (Widget::CollapsingSection(Loc::TextOr(LocKeys::InspectorMaterial, "Material")))
            {
                Widget::DisableScope disabled(true);
                Widget::FormLayout layout("##import");
                const TypeDescriptor& type = TypeDescriptorOf<MaterialDesc>::Get();
                if (type.fields != nullptr)
                {
                    DrawFieldsInto(layout, *type.fields, &copy, context);
                }
            }
            ImGui::PopID();
            return;
        }
        if ((record.type == AssetType::Skeleton || record.type == AssetType::Mesh) && subIndex < meta.subAssets.Size())
        {
            DrawSubAssetSockets(meta, record, subIndex);
            return;
        }
        // 클립의 구간·반복·이벤트·나누기는 모델 에디터에서 고친다(D-462) - 타임라인을 보며 고쳐야 한다.
        if (record.type == AssetType::AnimationClip)
        {
            Widget::WrappedHintText(Loc::TextOr(LocKeys::InspectorClipHint, "Edit this clip's range, loop and events in the model editor"));
            if (Widget::IconButton("##open_model_editor", Icons::OpenExternal)
                    .Caption(Loc::TextOr(LocKeys::InspectorOpenModelEditor, "Open Model Editor"))
                    .Draw())
            {
                m_editor->OpenModelEditor(record.id);
            }
            return;
        }
        if (record.type != AssetType::Texture || subIndex == meta.subAssets.Size())
        {
            return;
        }
        AssetMetaFile scratch = meta;
        AssetSubEntry& entry = scratch.subAssets[subIndex];
        if (false == entry.hasTextureOptions)
        {
            entry.textureOptions = ModelTextureDefaults();
        }
        AssetEditScope scope;
        scope.scratch = &scratch;
        scope.block = AssetEditScope::Block::SubTexture;
        scope.subIndex = subIndex;
        Context context;
        context.asset = &scope;

        // **같은 그림**(model-import-plan §2.5). 다른 모델에 든 같은 그림(원본과 옵션이 같음)을 줄마다 보이고, 누르면 브라우저가 그것을 찾아 준다.
        Array<const AssetRecord*> same;
        m_editor->FindSameImages(record.id, same);
        if (same.Size() > 0)
        {
            Widget::FormLayout layout("##sameImage");
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorSameImage, "Same Image")),
                [&]() {
                    for (std::size_t index = 0; index < same.Size(); ++index)
                    {
                        ImGui::PushID(static_cast<int>(index));
                        String label = same[index]->relativePath;
                        label += " / ";
                        label += same[index]->name;
                        if (Widget::LinkText(label.c_str()))
                        {
                            m_editor->RevealAssetInBrowser(same[index]->id);
                        }
                        ImGui::PopID();
                    }
                });
        }

        ImGui::PushID("##subTexture");
        if (Widget::CollapsingSection(Loc::TextOr(LocKeys::InspectorTextureImportOptions, "Texture Import Options")))
        {
            Widget::FormLayout layout("##import");
            const TypeDescriptor& type = TypeDescriptorOf<TextureImportOptions>::Get();
            if (type.fields != nullptr)
            {
                DrawFieldsInto(layout, *type.fields, &entry.textureOptions, context);
            }
        }
        ImGui::PopID();
        // 묶인 그림의 옵션을 고치면 그 그림만 따로 읽는다. 막지 않고 알린다 - 일부러 다르게 읽고 싶을 수 있다.
        if (same.Size() > 0)
        {
            Widget::WrappedHintText(Loc::TextOr(LocKeys::InspectorSameImageSplitNote, "Changing the options reads this image apart from its same images"));
        }
    }

    void InspectorPanel::ReleaseSocketAsset()
    {
        if (AssetSystem* assets = m_editor != nullptr ? m_editor->GetAssetSystem() : nullptr; assets != nullptr && m_socketAsset.generation != 0)
        {
            assets->Release(m_socketAsset);
        }
        m_socketAsset = {};
        m_socketAssetId = {};
        m_boneNames.Clear();
    }

    void InspectorPanel::DrawSubAssetSockets(const AssetMetaFile& meta, const AssetRecord& record, std::size_t subIndex)
    {
        m_socketDrawn = true;
        // **뼈 이름은 그 뼈대에서 온다.** 고른 하위 에셋(뼈대나 메시)을 실어 들고 있는다 - 모델 디코드는 워커라(D-455) 처음 몇 프레임은 뼈대가 없고,
        // 그동안 `bone` 칸은 글자 칸이다. 다른 것을 고르면 놓는다.
        AssetSystem* assets = m_editor->GetAssetSystem();
        if (assets != nullptr && m_socketAssetId != record.id)
        {
            ReleaseSocketAsset();
            m_socketAssetId = record.id;
            m_socketAsset = assets->Load(record.id);
        }
        m_boneNames.Clear();
        const SkeletonData* skeleton = nullptr;
        if (assets != nullptr && m_socketAsset.generation != 0)
        {
            if (record.type == AssetType::Skeleton)
            {
                skeleton = assets->GetSkeleton(m_socketAsset);
            }
            else if (const MeshData* mesh = assets->GetMesh(m_socketAsset); mesh != nullptr && mesh->skeletonAsset.generation != 0)
            {
                skeleton = assets->GetSkeleton(mesh->skeletonAsset);
            }
        }
        for (std::size_t bone = 0; skeleton != nullptr && bone < skeleton->skeleton.bones.Size(); ++bone)
        {
            m_boneNames.Add(skeleton->skeleton.bones[bone].name);
        }

        AssetMetaFile scratch = meta;
        AssetSubEntry& entry = scratch.subAssets[subIndex];
        AssetEditScope scope;
        scope.scratch = &scratch;
        scope.block = AssetEditScope::Block::SubSockets;
        scope.subIndex = subIndex;
        scope.boneNames = m_boneNames.IsEmpty() ? nullptr : &m_boneNames;
        Context context;
        context.asset = &scope;
        ImGui::PushID("##subSockets");
        if (Widget::CollapsingSection(Loc::TextOr(LocKeys::InspectorSockets, "Sockets")))
        {
            Widget::WrappedHintText(record.type == AssetType::Skeleton
                    ? Loc::TextOr(LocKeys::InspectorSkeletonSocketsHint, "Every mesh that uses this skeleton shares these sockets")
                    : Loc::TextOr(LocKeys::InspectorMeshSocketsHint, "These sockets belong to this mesh only"));
            Widget::FormLayout layout("##sockets");
            const TypeDescriptor& type = TypeDescriptorOf<AssetSubEntrySockets>::Get();
            if (type.fields != nullptr)
            {
                DrawFieldsInto(layout, *type.fields, &entry, context);
            }
        }
        ImGui::PopID();
    }

    void InspectorPanel::DrawSocketBoneField(void* address, Context& context)
    {
        String& bone = *static_cast<String*>(address);
        const Array<String>& names = *context.asset->boneNames;
        m_keyNames.Clear();
        Int32 current = -1;
        for (std::size_t index = 0; index < names.Size(); ++index)
        {
            m_keyNames.Add(names[index].c_str());
            if (names[index] == bone)
            {
                current = static_cast<Int32>(index);
            }
        }
        const Bool picked = Widget::FilterCombo("##value", ArrayView<const char* const>(m_keyNames.Data(), m_keyNames.Size()), current)
                                .EmptyText(bone.empty() ? Loc::TextOr(LocKeys::InspectorNoSocket, "(none)") : bone.c_str())
                                .Draw();
        if (picked && current >= 0 && names[static_cast<std::size_t>(current.Get())] != bone)
        {
            bone = names[static_cast<std::size_t>(current.Get())];
            CommitAssetEdit(context);
        }
    }

    void InspectorPanel::DrawSocketNameField(const TypeDescriptor& type, void* address, Bool editable, Context& context)
    {
        TextId& id = *static_cast<TextId*>(address);
        const ArrayView<const char> stored = TextStore::Get().GetText(id);
        String draft(stored.Data() != nullptr ? stored.Data() : "", stored.Size());
        // 가장 가까운 조상의 애니메이터가 고를 이름을 준다(붙이기 시스템과 같은 조상이다).
        m_socketNames.Clear();
        Canvas* canvas = m_editor->GetCanvas();
        const Component::Animator3D* animator = nullptr;
        for (Object::GameObject* above = context.owner != nullptr ? context.owner->GetParent() : nullptr; canvas != nullptr && above != nullptr && animator == nullptr;
             above = above->GetParent())
        {
            animator = canvas->FindComponentRaw<Component::Animator3D>(above);
        }
        if (const auto* animation = canvas != nullptr && animator != nullptr ? canvas->GetSystems().FindSystem<System::Animation3DSystem>() : nullptr)
        {
            animation->CollectSocketNames(*canvas, *animator, m_socketNames);
        }
        if (m_socketNames.IsEmpty())
        {
            const Bool finished = Widget::TextField("##value", draft).CommitOnFinish().Draw();
            String before;
            if (finished && editable && SetPropertyCommand::ReadValue(context.attached.GetInstance(), context.typeId, context.path, before))
            {
                TextStore::Get().Assign(id, draft.c_str(), draft.size());
                CommitEdit(type, address, before, context);
            }
            return;
        }
        m_socketNameViews.Clear();
        Int32 current = -1;
        for (std::size_t index = 0; index < m_socketNames.Size(); ++index)
        {
            m_socketNameViews.Add(m_socketNames[index].c_str());
            if (m_socketNames[index] == draft)
            {
                current = static_cast<Int32>(index);
            }
        }
        const Bool picked = Widget::FilterCombo("##value", ArrayView<const char* const>(m_socketNameViews.Data(), m_socketNameViews.Size()), current)
                                .EmptyText(draft.empty() ? Loc::TextOr(LocKeys::InspectorNoSocket, "(none)") : draft.c_str())
                                .Draw();
        if (picked && editable && current >= 0)
        {
            String before;
            if (SetPropertyCommand::ReadValue(context.attached.GetInstance(), context.typeId, context.path, before))
            {
                const String& chosen = m_socketNames[static_cast<std::size_t>(current.Get())];
                TextStore::Get().Assign(id, chosen.c_str(), chosen.size());
                CommitEdit(type, address, before, context);
            }
        }
    }

    void InspectorPanel::DrawLayerMaskField(const TypeDescriptor& type, void* address, Bool editable, Context& context)
    {
        TextId& id = *static_cast<TextId*>(address);
        const ArrayView<const char> stored = TextStore::Get().GetText(id);
        String draft(stored.Data() != nullptr ? stored.Data() : "", stored.Size());
        // 이 애니메이터가 쓰는 뼈대(시스템이 푼 것 - 비어 있으면 자식 스킨 메시의 것)의 마스크다.
        m_socketNames.Clear();
        Canvas* canvas = m_editor->GetCanvas();
        const Component::Animator3D* animator = canvas != nullptr && context.owner != nullptr ? canvas->FindComponentRaw<Component::Animator3D>(context.owner) : nullptr;
        const auto* animation = canvas != nullptr && animator != nullptr ? canvas->GetSystems().FindSystem<System::Animation3DSystem>() : nullptr;
        const System::Animation3DSystem::State* state = animation != nullptr ? animation->FindState(animator->GetInstanceId()) : nullptr;
        const AssetSystem* assets = m_editor->GetAssetSystem();
        const SkeletonData* skeleton = state != nullptr && assets != nullptr ? assets->GetSkeleton(state->skeleton) : nullptr;
        if (skeleton != nullptr)
        {
            for (const ModelBoneMask& mask : skeleton->masks)
            {
                m_socketNames.Add(mask.name);
            }
        }
        if (m_socketNames.IsEmpty())
        {
            const Bool finished = Widget::TextField("##value", draft).CommitOnFinish().Draw();
            String before;
            if (finished && editable && SetPropertyCommand::ReadValue(context.attached.GetInstance(), context.typeId, context.path, before))
            {
                TextStore::Get().Assign(id, draft.c_str(), draft.size());
                CommitEdit(type, address, before, context);
            }
            return;
        }
        // 첫 항목은 마스크 없음(모든 뼈)이다.
        m_socketNameViews.Clear();
        m_socketNameViews.Add(Loc::TextOr(LocKeys::InspectorNoSocket, "(none)"));
        Int32 current = draft.empty() ? Int32(0) : Int32(-1);
        for (std::size_t index = 0; index < m_socketNames.Size(); ++index)
        {
            m_socketNameViews.Add(m_socketNames[index].c_str());
            if (m_socketNames[index] == draft)
            {
                current = static_cast<Int32>(index + 1);
            }
        }
        const Bool picked = Widget::FilterCombo("##value", ArrayView<const char* const>(m_socketNameViews.Data(), m_socketNameViews.Size()), current)
                                .EmptyText(draft.c_str())
                                .Draw();
        if (picked && editable && current >= 0)
        {
            String before;
            if (SetPropertyCommand::ReadValue(context.attached.GetInstance(), context.typeId, context.path, before))
            {
                const String chosen = current == 0 ? String() : m_socketNames[static_cast<std::size_t>(current.Get() - 1)];
                TextStore::Get().Assign(id, chosen.c_str(), chosen.size());
                CommitEdit(type, address, before, context);
            }
        }
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
        using Block = AssetEditScope::Block;
        const auto drawBlock = [&](const char* title, const TypeDescriptor& type, void* options, Block block) {
            // 컴포넌트와 같은 모양이다: 슬롯 번호 → 접는 머리 → 줄 배치 `##import`.
            ImGui::PushID(slot++);
            scope.block = block;
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
                TypeDescriptorOf<TextureImportOptions>::Get(), &scratch.textureOptions, Block::Texture);
        }
        if (image)
        {
            drawBlock(Loc::TextOr(LocKeys::InspectorSpriteImportOptions, "Sprite Import Options"),
                TypeDescriptorOf<SpriteImportOptions>::Get(), &scratch.spriteOptions, Block::Sprite);
        }
        if (meta.type == AssetType::Audio)
        {
            drawBlock(Loc::TextOr(LocKeys::InspectorAudioImportOptions, "Audio Import Options"),
                TypeDescriptorOf<AudioImportOptions>::Get(), &scratch.audioOptions, Block::Audio);
        }
        // 폰트의 PPU·필터(D-200). 고치면 제자리 재로드로 글자가 새 크기로 다시 뜬다.
        if (meta.type == AssetType::Font)
        {
            drawBlock(Loc::TextOr(LocKeys::InspectorFontImportOptions, "Font Import Options"),
                TypeDescriptorOf<FontImportOptions>::Get(), &scratch.fontOptions, Block::Font);
        }
        // 폰트 패밀리의 네 칸(D-225). 고치면 제자리 재로드로 칸의 폰트가 바뀌고, 그 패밀리를 쓰는 텍스트가 다시 레이아웃된다.
        if (meta.type == AssetType::FontFamily)
        {
            drawBlock(Loc::TextOr(LocKeys::InspectorFontFamilyFaces, "Font Family"),
                TypeDescriptorOf<FontFamilyOptions>::Get(), &scratch.fontFamilyOptions, Block::FontFamily);
        }
        // 문자열 표의 로케일(D-226). 고치면 제자리 재로드로 표가 다른 언어의 것이 되고, 키가 있는 텍스트가 다시 레이아웃된다.
        if (meta.type == AssetType::StringTable)
        {
            drawBlock(Loc::TextOr(LocKeys::InspectorStringTable, "String Table"),
                TypeDescriptorOf<StringTableOptions>::Get(), &scratch.stringTableOptions, Block::StringTable);
        }
        // **재질 파일의 값**(material-plan 4 단계). 메타가 아니라 `.jmat` 본문이다 - 고치면 그 파일을 쓰는 커맨드다(되돌리면 옛 글자).
        // 그림 칸은 `...TextureId` 라 Texture 목록이 붙고(칸 이름의 마지막 낱말), 기본색·발광은 색 고르개가 견본이다.
        MaterialDesc material;
        if (meta.type == AssetType::Material && m_editor->GetSelectedMaterial() != nullptr && false == m_editor->IsSelectedMaterialReadOnly())
        {
            material = *m_editor->GetSelectedMaterial();
            scope.material = &material;
            drawBlock(Loc::TextOr(LocKeys::InspectorMaterial, "Material"),
                TypeDescriptorOf<MaterialDesc>::Get(), &material, Block::Material);
        }
        // 모델의 임포트 옵션(D-392). 고치면 하위 목록을 새 옵션으로 다시 맞춰 같은 커맨드에 담는다 - `importMaterials` 를 끄면 재질 하위 에셋이 빠지고, 되돌리면 같은 아이디로 돌아온다.
        if (meta.type == AssetType::Model)
        {
            drawBlock(Loc::TextOr(LocKeys::InspectorModelImportOptions, "Model Import Options"),
                TypeDescriptorOf<ModelImportOptions>::Get(), &scratch.modelOptions, Block::Model);
        }
    }

    void InspectorPanel::DrawPostProcessProfile(const AssetMetaFile& meta)
    {
        // **후처리 프로필은 인스펙터에서 고친다**(D-344, 사용자 결정 "인스펙터"). 고치면 바로 파일에 적고 실린 에셋을 제자리에서 다시 읽는다 -
        // 그 프로필을 쓰는 카메라가 다음 프레임에 바뀐다. 되돌리기는 에디터의 것이다.
        String text;
        if (false == m_editor->ReadAssetSourceText(meta.id, text))
        {
            Widget::HintText(Loc::TextOr(LocKeys::InspectorPostProcessUnreadable, "This profile could not be read"));
            return;
        }
        if (m_profileAsset != meta.id || m_profileText != text)
        {
            m_profileAsset = meta.id;
            m_profileText = text;
            m_profileError.clear();
            if (false == ParsePostProcessProfile(text.data(), text.size(), m_profileDocument, &m_profileError))
            {
                m_profileDocument = {};
            }
        }
        if (false == m_profileError.empty())
        {
            // 깨진 YAML 을 덮어쓰면 손으로 고친 것을 잃는다. 고칠 때까지 손대지 않는다.
            Widget::HintText(Loc::TextOr(LocKeys::InspectorPostProcessBroken, "The profile's text is broken. Fix the file to edit it here"));
            Widget::WrappedText(m_profileError.c_str());
            return;
        }

        // 편집본이다. 위젯이 고친 값은 커밋이 파일에 쓰고, 다음 프레임에 그 글자를 다시 읽는다 - 쓰는 길이 하나다.
        PostProcessProfileDocument scratch = m_profileDocument;
        AssetEditScope scope;
        scope.profile = &scratch;
        Context context;
        context.asset = &scope;
        const Bool editable = false == m_editor->IsAtScriptBreak();

        Widget::SectionHeader(Loc::TextOr(LocKeys::InspectorPostProcessEffects, "Post-Process Effects")).Draw();
        if (scratch.entries.IsEmpty())
        {
            Widget::HintText(Loc::TextOr(LocKeys::InspectorPostProcessEmpty, "No effects yet"));
        }
        const ImGuiID listOwner = ImGui::GetID("##post_effects");
        for (std::size_t index = 0; index < scratch.entries.Size(); ++index)
        {
            PostProcessProfileEntry& entry = scratch.entries[index];
            ImGui::PushID(static_cast<JBro::Int32>(index));
            // 종류 이름은 번역하지 않는다(컴포넌트 이름과 같다).
            const Bool opened = Widget::CollapsingSection(entry.typeName.c_str(), true, true);
            const ImVec2 headerMin = ImGui::GetItemRectMin();
            const ImVec2 headerMax = ImGui::GetItemRectMax();
            // **머리를 끌어 순서를 바꾼다**(컴포넌트 머리와 같은 손짓, D-307). 같은 지점의 효과끼리만 차례가 뜻이 있다.
            if (editable && Widget::BeginDragSource())
            {
                Widget::ListReorderPayload payload;
                payload.owner = listOwner;
                payload.index = static_cast<JBro::Int32>(index);
                Widget::SetDragValue(Widget::DragKind::ListReorder, payload);
                Widget::Text(entry.typeName.c_str());
                Widget::EndDragSource();
            }
            if (editable && Widget::BeginDropTarget())
            {
                const Widget::DropPayload drop = Widget::AcceptDrop(Widget::DragKind::ListReorder, Widget::DropFeedback::None);
                Widget::ListReorderPayload dragged;
                if (Widget::ReadDropValue(drop, dragged) && dragged.owner == listOwner && dragged.index >= 0
                    && static_cast<std::size_t>(dragged.index.Get()) < scratch.entries.Size())
                {
                    const Float height = (std::max)(1.0f, headerMax.y - headerMin.y);
                    const Bool above = (ImGui::GetIO().MousePos.y - headerMin.y) / height < 0.5f;
                    const std::size_t from = static_cast<std::size_t>(dragged.index.Get());
                    const std::size_t to = ComponentDropSlot(from, index, above);
                    if (to != from)
                    {
                        Widget::DrawDropLine(headerMin.x, headerMax.x, above ? headerMin.y : headerMax.y);
                    }
                    if (drop.delivered && to != from)
                    {
                        PostProcessProfileEntry moved = std::move(scratch.entries[from]);
                        scratch.entries.RemoveAt(from);
                        scratch.entries.Insert(to, std::move(moved));
                        Widget::EndDropTarget();
                        ImGui::PopID();
                        CommitProfile(meta.id, scratch);
                        return;
                    }
                }
                Widget::EndDropTarget();
            }
            Bool removed = false;
            if (Widget::BeginContextMenu("##PostEffectMenu"))
            {
                if (Widget::MenuItem(Loc::TextOr(LocKeys::InspectorPostProcessRemove, "Remove Effect"), nullptr, editable, nullptr, Icons::Delete))
                {
                    removed = true;
                }
                Widget::EndContextMenu();
            }
            {
                const ImVec2 cursor = ImGui::GetCursorScreenPos();
                const Float side = headerMax.y - headerMin.y;
                ImGui::SetCursorScreenPos(ImVec2(headerMax.x - side, headerMin.y));
                if (Widget::IconButton("##post_effect_menu", Icons::Menu)
                        .Size(ImVec2(side, side))
                        .Flat()
                        .Tooltip(Loc::TextOr(LocKeys::InspectorPostProcessMenu, "Effect menu"))
                        .Draw())
                {
                    Widget::OpenContextMenu("##PostEffectMenu");
                }
                ImGui::SetCursorScreenPos(cursor);
            }
            if (removed)
            {
                scratch.entries.RemoveAt(index);
                ImGui::PopID();
                CommitProfile(meta.id, scratch);
                return;
            }
            if (opened)
            {
                Widget::FormLayout layout("##post_effect");
                layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorEnabled, "Enabled")), [&]() {
                    Widget::DisableScope locked(false == editable);
                    Bool enabled = entry.enabled;
                    if (Widget::Checkbox("##enabled", enabled))
                    {
                        entry.enabled = enabled;
                        CommitProfile(meta.id, scratch);
                    }
                });
                const PostProcessEffectType* type = PostProcessEffectRegistry::Get(entry.type);
                if (type == nullptr || type->parameters == nullptr)
                {
                    if (type == nullptr)
                    {
                        layout.FullRow([&]() {
                            Widget::HintText(Loc::TextOr(LocKeys::InspectorPostProcessUnknown,
                                "This engine does not know this effect. It is kept as it was read"));
                        });
                    }
                }
                else
                {
                    const PropertyTable& table = *type->parameters;
                    for (UInt32 field = 0; field < table.count; ++field)
                    {
                        const PropertyInfo& property = table.properties[field.Get()];
                        if (false == IsPostProcessFieldEditable(entry.type, field)
                            || (property.edit != nullptr && false == property.edit->visible))
                        {
                            continue;
                        }
                        const char* label = NameTable::Get().Resolve(property.name);
                        const Bool overridden = (entry.overrides & (UInt64(1) << field.Get())) != 0;
                        Widget::IdScope id(static_cast<JBro::Int32>(field.Get()));
                        // **필드 앞의 체크가 덮어쓰기다.** 켠 필드만 파일에 적히고, 끈 필드는 효과의 기본값이다 - 볼륨이 섞을 때도 켠 필드만 섞는다.
                        layout.Row(Widget::FieldLabel(label != nullptr ? label : "?").Disabled(false == overridden), [&]() {
                            Widget::DisableScope locked(false == editable);
                            Bool on = overridden;
                            if (Widget::Checkbox("##override", on))
                            {
                                SetPostProcessOverride(entry, field, on);
                                CommitProfile(meta.id, scratch);
                                return;
                            }
                            Widget::HoveredTooltip(Loc::TextOr(LocKeys::InspectorPostProcessOverrideTooltip,
                                "Write this value into the profile. Off uses the effect's default"));
                            ImGui::SameLine();
                            Widget::DisableScope unset(false == overridden);
                            scope.profileEntry = static_cast<JBro::UInt32>(index);
                            scope.profileField = field;
                            DrawValue(label != nullptr ? label : "?", *property.type, property.Address(entry.block.Data()), property.edit, context);
                        });
                    }
                }
            }
            ImGui::PopID();
        }

        // **효과 추가**(컴포넌트 추가와 같은 자리). 종류는 효과 종류 표의 것이다 - 사용자 효과도 표에 들면 여기 나온다.
        ImGui::Spacing();
        Widget::DisableScope locked(false == editable);
        if (Widget::IconButton("##add_post_effect", Icons::Plus)
                .Caption(Loc::TextOr(LocKeys::InspectorPostProcessAdd, "Add Effect"))
                .Draw())
        {
            Widget::OpenContextMenu("##AddPostEffect");
        }
        if (Widget::BeginContextMenu("##AddPostEffect"))
        {
            for (UInt32 type = 0; type < PostProcessEffectRegistry::GetCount(); ++type)
            {
                const PostProcessEffectType* effect = PostProcessEffectRegistry::Get(type);
                if (effect != nullptr && Widget::MenuItem(effect->name))
                {
                    scratch.entries.Add(MakePostProcessProfileEntry(type));
                    CommitProfile(meta.id, scratch);
                }
            }
            Widget::EndContextMenu();
        }
    }

    void InspectorPanel::CommitProfile(AssetId asset, const PostProcessProfileDocument& document)
    {
        AssetSourceTarget target;
        if (false == m_editor->DescribeAssetSource(asset, target))
        {
            return;
        }
        const String after = WritePostProcessProfile(document);
        if (after == m_profileText)
        {
            return;
        }
        m_editor->GetCommands().Execute(MakeOwnerPtr<SetAssetSourceCommand>(target, m_profileText, after, "Edit Post-Process Profile"));
    }

    void InspectorPanel::DrawRenderTexture(const AssetMetaFile& meta)
    {
        String text;
        RenderTextureSettings settings;
        if (false == m_editor->ReadAssetSourceText(meta.id, text) || false == ParseRenderTexture(text.data(), text.size(), settings))
        {
            Widget::HintText(Loc::TextOr(LocKeys::InspectorRenderTextureUnreadable, "This render texture could not be read"));
            return;
        }
        Widget::WrappedHintText(Loc::TextOr(LocKeys::InspectorRenderTextureHint, "Set it as a camera's renderTextureId to draw into it, and as a sprite's renderTextureId to show it"));
        Bool changed = false;
        {
            Widget::FormLayout layout("##render_texture");
            // 필드 이름 그대로 보인다(컴포넌트 필드와 같다).
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorRenderTextureWidth, "width")), [&]() {
                changed = Widget::DragField<UInt32>("##width", settings.width).Range(1u, RenderTextureSettings::MaxSide).Draw() || changed;
            });
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorRenderTextureHeight, "height")), [&]() {
                changed = Widget::DragField<UInt32>("##height", settings.height).Range(1u, RenderTextureSettings::MaxSide).Draw() || changed;
            });
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::InspectorRenderTextureFilter, "filter")), [&]() {
                changed = Widget::EnumCombo("##filter", *TypeDescriptorOf<TextureFilter>::Get().enumNames, &settings.filter) || changed;
            });
        }
        if (false == changed)
        {
            return;
        }
        AssetSourceTarget target;
        if (false == m_editor->DescribeAssetSource(meta.id, target))
        {
            return;
        }
        const String after = WriteRenderTexture(settings);
        if (after != text)
        {
            m_editor->GetCommands().Execute(MakeOwnerPtr<SetAssetSourceCommand>(target, text, after, "Edit Render Texture"));
        }
    }

    void InspectorPanel::DrawMaterial(const AssetMetaFile& meta)
    {
        // **머티리얼은 인스펙터에서 고친다**(D-347, 프로필과 같은 길). 고치면 바로 파일에 적고 실린 에셋을 제자리에서 다시 읽는다 - 그 머티리얼을 쓰는
        // 스프라이트·효과가 다음 프레임에 바뀐다.
        String text;
        if (false == m_editor->ReadAssetSourceText(meta.id, text))
        {
            Widget::HintText(Loc::TextOr(LocKeys::InspectorMaterialUnreadable, "This material could not be read"));
            return;
        }
        if (m_materialAsset != meta.id || m_materialText != text)
        {
            m_materialAsset = meta.id;
            m_materialText = text;
            m_materialError.clear();
            if (false == ParseMaterial(text.data(), text.size(), m_materialDocument, &m_materialError))
            {
                m_materialDocument = {};
            }
        }
        if (false == m_materialError.empty())
        {
            // 깨진 YAML 을 덮어쓰면 손으로 고친 것을 잃는다. 고칠 때까지 손대지 않는다.
            Widget::HintText(Loc::TextOr(LocKeys::InspectorMaterialBroken, "The material's text is broken. Fix the file to edit it here"));
            Widget::WrappedText(m_materialError.c_str());
            return;
        }

        // 편집본이다. 위젯이 고친 값은 커밋이 파일에 쓰고, 다음 프레임에 그 글자를 다시 읽는다 - 쓰는 길이 하나다.
        MaterialDocument scratch = m_materialDocument;
        const Bool editable = false == m_editor->IsAtScriptBreak();
        // 줄 이름은 파일의 키다(필드 이름처럼 번역하지 않는다).
        {
            Widget::FormLayout layout("##material");
            layout.Row(Widget::FieldLabel("Shader"), [&]() {
                Widget::DisableScope locked(false == editable);
                const AssetChoices& choices = ChoicesFor(AssetType::Shader);
                AssetId shader = scratch.shader;
                if (Widget::AssetField("##shader", ArrayView<const char* const>(choices.namePointers.Data(), choices.namePointers.Size()),
                        ArrayView<const AssetId>(choices.ids.Data(), choices.ids.Size()), shader)
                        .Draw())
                {
                    scratch.shader = shader;
                    CommitMaterial(meta.id, scratch);
                }
            });
            layout.Row(Widget::FieldLabel("Domain"), [&]() {
                Widget::DisableScope locked(false == editable);
                MaterialDomain domain = scratch.domain;
                if (Widget::EnumCombo("##domain", *TypeDescriptorOf<MaterialDomain>::Get().enumNames, &domain))
                {
                    scratch.domain = domain;
                    CommitMaterial(meta.id, scratch);
                }
            });
        }
        if (scratch.shader.IsNull())
        {
            Widget::HintText(Loc::TextOr(LocKeys::InspectorMaterialNoShader, "Pick a shader to see its parameters and textures"));
            return;
        }
        const String* error = nullptr;
        const CompiledMaterialShader* shader = m_editor->GetMaterialShader(scratch.shader, scratch.domain, &error);
        if (shader == nullptr)
        {
            Widget::HintText(Loc::TextOr(LocKeys::InspectorMaterialShaderError, "The shader could not be compiled"));
            if (error != nullptr)
            {
                Widget::WrappedText(error->c_str());
            }
            return;
        }

        Widget::SectionHeader(Loc::TextOr(LocKeys::InspectorMaterialParameters, "Parameters")).Draw();
        if (shader->parameters.IsEmpty())
        {
            Widget::HintText(Loc::TextOr(LocKeys::InspectorMaterialNoParameters, "The shader has no parameters"));
        }
        else
        {
            Widget::FormLayout layout("##material_parameters");
            for (std::size_t index = 0; index < shader->parameters.Size(); ++index)
            {
                const MaterialParameterInfo& parameter = shader->parameters[index];
                const UInt32 components = GetMaterialParameterComponents(parameter.type);
                const MaterialParameterValue* written = scratch.FindParameter(parameter.name.View());
                const Bool overridden = written != nullptr;
                Float values[4] = {parameter.defaults[0], parameter.defaults[1], parameter.defaults[2], parameter.defaults[3]};
                if (overridden)
                {
                    for (UInt32 at = 0; at < 4; ++at)
                    {
                        values[at.Get()] = at < written->count ? written->values[at.Get()] : Float(0.0f);
                    }
                }
                Widget::IdScope id(static_cast<JBro::Int32>(index));
                // **이름 앞의 체크가 덮어쓰기다**(프로필과 같다). 켠 파라미터만 파일에 적히고, 끈 것은 셰이더의 초기값이다.
                layout.Row(Widget::FieldLabel(parameter.name.c_str()).Disabled(false == overridden), [&]() {
                    Widget::DisableScope locked(false == editable);
                    Bool on = overridden;
                    if (Widget::Checkbox("##override", on))
                    {
                        if (on)
                        {
                            scratch.SetParameter(parameter.name.View(), parameter.defaults, components);
                        }
                        else
                        {
                            scratch.RemoveParameter(parameter.name.View());
                        }
                        CommitMaterial(meta.id, scratch);
                        return;
                    }
                    Widget::HoveredTooltip(Loc::TextOr(LocKeys::InspectorMaterialOverrideTooltip,
                        "Write this value into the material. Off uses the shader's initial value"));
                    ImGui::SameLine();
                    Widget::DisableScope unset(false == overridden);
                    Bool changed = false;
                    if (parameter.color)
                    {
                        if (components == 3)
                        {
                            values[3] = 1.0f;
                        }
                        changed = Widget::ColorField("##value", values);
                    }
                    else if (parameter.type == MaterialParameterType::Bool)
                    {
                        Bool flag = values[0] > 0.0f;
                        changed = Widget::Checkbox("##value", flag);
                        values[0] = flag ? 1.0f : 0.0f;
                    }
                    else if (parameter.type == MaterialParameterType::Int || parameter.type == MaterialParameterType::UInt)
                    {
                        Int32 integer = static_cast<JBro::Int32>(values[0].Get() < 0.0f ? values[0].Get() - 0.5f : values[0].Get() + 0.5f);
                        changed = parameter.type == MaterialParameterType::UInt
                            ? Widget::DragField<Int32>("##value", integer).Range(0, 2147483647).Draw()
                            : Widget::DragField<Int32>("##value", integer).Draw();
                        values[0] = static_cast<JBro::Float>(integer.Get());
                    }
                    else if (components == 1)
                    {
                        changed = Widget::DragField<Float>("##value", values[0]).Speed(0.01f).Draw();
                    }
                    else
                    {
                        changed = Widget::ScalarRunField("##value", values, static_cast<JBro::Int32>(components.Get()), 0.01f, false, 0.0f, 0.0f);
                    }
                    if (changed)
                    {
                        scratch.SetParameter(parameter.name.View(), values, components);
                        CommitMaterial(meta.id, scratch);
                    }
                });
            }
        }
        if (shader->textures.IsEmpty())
        {
            return;
        }
        Widget::SectionHeader(Loc::TextOr(LocKeys::InspectorMaterialTextures, "Textures")).Draw();
        Widget::FormLayout layout("##material_textures");
        for (std::size_t index = 0; index < shader->textures.Size(); ++index)
        {
            const MaterialTextureInfo& slot = shader->textures[index];
            Widget::IdScope id(static_cast<JBro::Int32>(index));
            layout.Row(Widget::FieldLabel(slot.name.c_str()), [&]() {
                Widget::DisableScope locked(false == editable);
                const AssetChoices& choices = ChoicesFor(AssetType::Texture);
                const MaterialTextureValue* written = scratch.FindTexture(slot.name.View());
                AssetId texture = written != nullptr ? written->texture : AssetId{};
                if (Widget::AssetField("##texture", ArrayView<const char* const>(choices.namePointers.Data(), choices.namePointers.Size()),
                        ArrayView<const AssetId>(choices.ids.Data(), choices.ids.Size()), texture)
                        .Draw())
                {
                    scratch.SetTexture(slot.name.View(), texture);
                    CommitMaterial(meta.id, scratch);
                }
            });
        }
    }

    void InspectorPanel::CommitMaterial(AssetId asset, const MaterialDocument& document)
    {
        AssetSourceTarget target;
        if (false == m_editor->DescribeAssetSource(asset, target))
        {
            return;
        }
        const String after = WriteMaterial(document);
        if (after == m_materialText)
        {
            return;
        }
        m_editor->GetCommands().Execute(MakeOwnerPtr<SetAssetSourceCommand>(target, m_materialText, after, "Edit Material"));
    }

    void InspectorPanel::DrawShader(const AssetMetaFile& meta)
    {
        // **셰이더 에셋 자체를 본다**(D-348). 전에는 굽기 오류가 그 셰이더를 쓰는 머티리얼에서만 보여, 머티리얼을 만들기 전에는 셰이더가 되는지 알 길이 없었다.
        // 자리마다 따로 굽는다 - 스프라이트로는 되고 후처리로는 안 되는 셰이더가 있다. 고치는 것은 편집기다(인스펙터는 글자를 고치지 않는다).
        static const char* const TypeNames[] = {"float", "float2", "float3", "float4", "int", "uint", "bool"};
        UInt32 errorLine = 0;
        const EnumNames& domains = *TypeDescriptorOf<MaterialDomain>::Get().enumNames;
        for (UInt32 domain = 0; domain < MaterialDomainCount; ++domain)
        {
            const char* domainName = domain < domains.count ? domains.names[domain.Get()] : nullptr;
            Widget::IdScope id(static_cast<JBro::Int32>(domain.Get()));
            // 자리 이름은 머티리얼 파일의 `Domain` 값 그대로다(번역하지 않는다).
            Widget::SectionHeader(domainName != nullptr ? domainName : "?").Draw();
            const String* error = nullptr;
            const CompiledMaterialShader* shader = m_editor->GetMaterialShader(meta.id, static_cast<MaterialDomain>(domain.Get()), &error);
            if (shader == nullptr)
            {
                Widget::HintText(Loc::TextOr(LocKeys::InspectorShaderDomainError, "Could not be compiled for this domain"));
                if (error != nullptr)
                {
                    Widget::WrappedText(error->c_str());
                    // fxc 의 오류는 `경로(줄,칸): error ...` 다. 첫 오류의 줄을 편집기에서 연다.
                    const std::size_t open = error->find('(');
                    if (errorLine == 0 && open != String::npos)
                    {
                        UInt32 line = 0;
                        for (std::size_t at = open + 1; at < error->size() && (*error)[at] >= '0' && (*error)[at] <= '9'; ++at)
                        {
                            line = line * 10u + static_cast<std::uint32_t>((*error)[at] - '0');
                        }
                        errorLine = line;
                    }
                }
                continue;
            }
            if (shader->parameters.IsEmpty() && shader->textures.IsEmpty())
            {
                Widget::HintText(Loc::TextOr(LocKeys::InspectorShaderEmpty, "No parameters or textures"));
                continue;
            }
            // 이름은 셰이더의 변수 이름이고, 값은 HLSL 의 형과 초기값이다 - 머티리얼이 덮어쓰지 않으면 이 값으로 그린다.
            Widget::FormLayout layout("##shader_reflection");
            for (std::size_t index = 0; index < shader->parameters.Size(); ++index)
            {
                const MaterialParameterInfo& parameter = shader->parameters[index];
                const UInt32 components = GetMaterialParameterComponents(parameter.type);
                char text[160] = {};
                JBro::Int32 written = std::snprintf(text, sizeof(text), "%s ", TypeNames[static_cast<std::size_t>(parameter.type)]);
                for (UInt32 at = 0; at < components && written > 0 && written < static_cast<JBro::Int32>(sizeof(text)); ++at)
                {
                    written += std::snprintf(text + written, sizeof(text) - static_cast<std::size_t>(written), at == 0 ? "%g" : ", %g",
                        static_cast<double>(parameter.defaults[at.Get()].Get()));
                }
                layout.Row(Widget::FieldLabel(parameter.name.c_str()), [&]() { Widget::Text(text); });
            }
            for (std::size_t index = 0; index < shader->textures.Size(); ++index)
            {
                const MaterialTextureInfo& slot = shader->textures[index];
                char text[32] = {};
                std::snprintf(text, sizeof(text), "Texture2D t%u", static_cast<unsigned>(slot.slot.Get()));
                layout.Row(Widget::FieldLabel(slot.name.c_str()), [&]() { Widget::Text(text); });
            }
        }
        AssetSourceTarget target;
        if (m_editor->DescribeAssetSource(meta.id, target))
        {
            if (Widget::IconButton("##open_shader", Icons::OpenExternal)
                    .Caption(Loc::TextOr(LocKeys::InspectorShaderOpen, "Open in Editor"))
                    .Draw())
            {
                m_editor->OpenScriptAt(target.path, errorLine == 0 ? UInt32(1) : errorLine);
            }
        }
    }

    void InspectorPanel::CommitAssetEdit(Context& context)
    {
        // 후처리 프로필의 필드다(D-344). 값을 고친 필드는 덮어쓴 필드가 된다.
        if (context.asset != nullptr && context.asset->profile != nullptr)
        {
            PostProcessProfileDocument& profile = *context.asset->profile;
            if (context.asset->profileEntry < profile.entries.Size())
            {
                PostProcessProfileEntry& entry = profile.entries[context.asset->profileEntry.Get()];
                entry.overrides = entry.overrides | (UInt64(1) << context.asset->profileField.Get());
                CommitProfile(m_profileAsset, profile);
            }
            return;
        }
        // 재질 파일은 메타가 아니라 그 파일을 쓴다. 모델의 재질(편집본이 없다)은 읽기 전용이라 아무 일도 없다.
        if (context.asset != nullptr && context.asset->block == AssetEditScope::Block::Material)
        {
            AssetFileTarget target;
            String before;
            String after;
            if (context.asset->material == nullptr || false == m_editor->DescribeSelectedMaterialFile(target, before)
                || false == FormatMaterialFile(*context.asset->material, after) || before == after)
            {
                return;
            }
            m_editor->GetCommands().Execute(MakeOwnerPtr<SetAssetFileCommand>(target, before, after));
            return;
        }
        const AssetMetaFile* original = m_editor->GetSelectedAssetMeta();
        AssetFileTarget target;
        if (original == nullptr || context.asset == nullptr || context.asset->scratch == nullptr
            || false == m_editor->DescribeSelectedAssetMeta(target))
        {
            return;
        }
        // 고친 블록은 이제 파일에 있어야 한다.
        AssetMetaFile& scratch = *context.asset->scratch;
        using Block = AssetEditScope::Block;
        switch (context.asset->block)
        {
        case Block::Sprite:
            scratch.hasSpriteOptions = true;
            break;
        case Block::Audio:
            scratch.hasAudioOptions = true;
            break;
        case Block::Font:
            scratch.hasFontOptions = true;
            break;
        case Block::FontFamily:
            scratch.hasFontFamilyOptions = true;
            break;
        case Block::StringTable:
            scratch.hasStringTableOptions = true;
            break;
        case Block::Model:
        {
            scratch.hasModelOptions = true;
            // 하위 목록을 새 옵션으로 맞춘다. 읽히지 않으면 옵션만 바꾸고 목록은 둔다 - 고치는 손을 막지 않는다.
            const AssetRecord* record = m_editor->GetAssetRegistry().Find(target.id);
            Bool changed = false;
            String error;
            if (record != nullptr && target.platform != nullptr)
            {
                RefreshModelSubAssets(*target.platform, m_editor->GetAssetRoot().c_str(), record->relativePath, scratch, changed, error);
            }
            break;
        }
        case Block::SubTexture:
            if (context.asset->subIndex < scratch.subAssets.Size())
            {
                scratch.subAssets[context.asset->subIndex].hasTextureOptions = true;
            }
            break;
        case Block::SubSockets:
            // 소켓 목록은 항목에 그대로 있다(빈 목록은 적지 않는다). 켤 블록이 없다.
            break;
        case Block::Texture:
        default:
            scratch.hasTextureOptions = true;
            break;
        }
        const String before = FormatAssetMetaFile(*original);
        const String after = FormatAssetMetaFile(scratch);
        if (before == after)
        {
            return;
        }
        m_editor->GetCommands().Execute(MakeOwnerPtr<SetAssetFileCommand>(target, before, after));
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
                context.attached.GetInstance(), context.typeId, context.path, after)
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
        SetPropertyCommand::ApplyValue(context.attached.GetInstance(), context.typeId, context.path, before);
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
            const AttachedRef target = targets[index].attached;
            ComponentAddress address;
            if (false == MakeAttachedAddress(m_editor->GetObjectIds(),
                *targets[index].owner, target, address))
            {
                continue;
            }
            String targetBefore;
            if (false == SetPropertyCommand::ReadValue(
                target.GetInstance(), context.typeId, context.path, targetBefore))
            {
                continue;
            }

            String targetAfter = after;
            if (numeric && false == (target == context.attached))
            {
                // 그 대상의 값에 같은 델타를 얹고, 그 결과를 글자로 뜬다.
                // **뜬 뒤에는 도로 돌려놓는다** - 쓰는 것은 커맨드의 몫이다.
                void* targetAddress = nullptr;
                const TypeDescriptor* targetType = nullptr;
                if (false == SetPropertyCommand::ResolveLeaf(target.GetInstance(), context.typeId,
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
                    target.GetInstance(), context.typeId, context.path, targetAfter);
                SetPropertyCommand::ApplyValue(
                    target.GetInstance(), context.typeId, context.path, targetBefore);
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
        // **에셋 편집본의 목록**(모델 메타의 소켓, D-460)이다. 컴포넌트가 없으니 편집을 편집본 배열에 곧장 적용하고 메타 전체를 쓰는 커맨드 하나로 남긴다.
        // 하나라도 맞지 않으면 아무것도 쓰지 않는다 - 편집본은 다음 프레임에 원본에서 새로 뜬다.
        if (context.asset != nullptr)
        {
            for (std::size_t index = 0; index < edits.Size(); ++index)
            {
                if (false == ApplyListEdit(type, address, edits[index]))
                {
                    return;
                }
            }
            CommitAssetEdit(context);
            return;
        }
        const Array<EditTarget> targets = CollectEditTargets(context);
        Array<ComponentAddress> addresses;
        for (std::size_t index = 0; index < targets.Size(); ++index)
        {
            ComponentAddress target;
            if (MakeAttachedAddress(m_editor->GetObjectIds(), *targets[index].owner,
                targets[index].attached, target))
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

    namespace
    {
        // 그 분류의 필드가 모두 읽기 전용인가. 실행 중에 엔진이 채우는 캐시(`Transform2D` 의 `World cache`)가 그렇다.
        Bool IsReadOnlyCategory(const PropertyTable& table, const char* category)
        {
            Bool any = false;
            for (UInt32 index = 0; index < table.count; ++index)
            {
                const PropertyEditInfo* edit = table.properties[index].edit;
                if (edit == nullptr || edit->category == nullptr || std::strcmp(edit->category, category) != 0)
                {
                    continue;
                }
                if (edit->editable)
                {
                    return false;
                }
                any = true;
            }
            return any;
        }
    }

    void InspectorPanel::DrawFieldsInto(
        Widget::FormLayout& layout,
        const PropertyTable& table,
        void* owner,
        Context& context)
    {
        // **읽기 전용 분류는 접은 줄 아래에 둔다**(사용자 시나리오 A). `Transform2D` 의 월드 캐시(행렬 여섯 칸·월드 위치·회전·크기·유효)가
        // 펼친 채로 위치·회전·크기 바로 밑을 차지해, 정작 고칠 컴포넌트와 `컴포넌트 추가` 가 화면 밖으로 밀렸다.
        // 고칠 수 있는 필드가 섞인 분류(`AudioSource` 의 `Spatial`)는 지금처럼 그대로 늘어놓는다.
        const char* foldCategory = nullptr;
        Bool foldOpen = false;
        // 접은 줄은 Id 를 쌓지 않는다(`NoTreePushOnOpen`) - 쌓으면 안쪽 필드의 Id 가 바뀌어 가이드 포커스와 시험이 가리키는 자리가 어긋난다.
        // 안쪽임은 들여쓰기로만 보인다.
        const auto closeFold = [&]() {
            if (foldCategory != nullptr && foldOpen)
            {
                --context.openTrees;
                ImGui::Unindent();
            }
            foldCategory = nullptr;
            foldOpen = false;
        };
        for (UInt32 index = 0; index < table.count; ++index)
        {
            const PropertyInfo& property = table.properties[index];
            if (property.type == nullptr || property.Address == nullptr)
            {
                continue;
            }
            const char* category = property.edit != nullptr ? property.edit->category : nullptr;
            const Bool folded = category != nullptr && IsReadOnlyCategory(table, category);
            if (false == folded || foldCategory == nullptr || std::strcmp(foldCategory, category) != 0)
            {
                closeFold();
                if (folded)
                {
                    foldCategory = category;
                    layout.FullRow([&]() {
                        Widget::IdScope id(category);
                        foldOpen = Widget::FoldNode(EditorNames::FieldCategoryLabel(category),
                            ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_NoTreePushOnOpen);
                    });
                    if (foldOpen)
                    {
                        ++context.openTrees;
                        ImGui::Indent();
                    }
                }
            }
            if (folded && false == foldOpen)
            {
                continue;
            }
            void* address = property.Address(owner);
            if (address == nullptr)
            {
                continue;
            }
            // 숨긴 필드는 줄을 두지 않는다(D-296). 저장은 되고 다른 손짓이 고친다.
            if (property.edit != nullptr && false == property.edit->visible)
            {
                continue;
            }
            // 다른 `Bool` 필드가 꺼져 있으면 줄을 두지 않는다(D-309, `VisibleWhen`). 여럿 고르면 주된 것의 값을 본다.
            if (property.edit != nullptr && property.edit->visibleWhen != nullptr
                && false == IsSwitchOn(table, owner, property.edit->visibleWhen))
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
                && (false == inElement || property.serialize) && false == m_editor->IsAtScriptBreak();
            const char* tooltip =
                property.edit != nullptr ? property.edit->tooltip : nullptr;

            // **목록은 표를 끊고 줄 전체를 쓴다**(D-89, D-309). 값 칸 안에 두면 손잡이·번호·지우기 단추가 값 칸의 폭을 먼저 쓰고,
            // 펼친 구조체 원소는 필드 표가 또 라벨 칸을 가져, 좁은 패널에서 값이 몇 픽셀만 남았다(`100` 이 `1` 로 보였다,
            // `audioEffectIds` 의 에셋 칸이 8 픽셀이었다). 라벨은 목록 한 줄 위에 선다. 처음에는 구조체 원소의 목록만 끊었다.
            //
            // 끊기는 이 필드의 Id 를 쌓기 전에 한다 - 쌓은 채로 표를 닫으면 표가 제 Id 대신 그것을
            // 뺀다. 트리 마디가 열린 자리도 같은 이유로 끊지 못해 값 칸에 둔다.
            if (false == inElement && context.openTrees == 0
                && property.type->arrayOps != nullptr && property.type->element != nullptr)
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
                        if (false == inElement && context.path.depth == 1 && context.attached.IsValid() && context.guideTarget)

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
            if (false == inElement && context.path.depth == 1 && context.attached.IsValid() && context.guideTarget)

            {
                Widget::ReportGuideTarget(GuideFocusTargets::InspectorField(context.typeId, property.name),
                    layout.GetLastRowMin(), layout.GetLastRowMax(), false, ImGui::IsItemDeactivatedAfterEdit());
            }

            // **이 필드에 붙는 줄이 있으면 바로 밑에 그린다**(D-165). 무엇을 붙일지는 인스펙터가 모른다 - 표가 안다.
            if (false == inElement && context.path.depth == 1 && context.asset == nullptr
                && context.attached.component != nullptr && context.owner != nullptr)
            {
                // 붙는 줄은 빌트인 컴포넌트의 것이다 - 표의 항목이 컴포넌트 타입을 안다.
                if (const FieldExtraDraw extra = FindFieldExtra(context.typeId, property.name))
                {
                    FieldExtraContext extraContext;
                    extraContext.editor = m_editor;
                    extraContext.owner = context.owner;
                    extraContext.component = context.attached.component;
                    extraContext.typeId = context.typeId;
                    extra(layout, extraContext);
                }
            }

            leave();
        }
        closeFold();
    }

    void InspectorPanel::DrawValue(
        const char* label,
        const TypeDescriptor& type,
        void* address,
        const PropertyEditInfo* edit,
        Context& context)
    {
        const Bool editable = (edit == nullptr || edit->editable) && false == m_editor->IsAtScriptBreak();
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
            const char* outerList = context.listField;
            context.listField = label;
            DrawArray(type, address, editable, context);
            context.listField = outerList;
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
            const Bool snapped = context.attached.IsValid()
                ? SetPropertyCommand::ReadValue(context.attached.GetInstance(), context.typeId, context.path, before)
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
        if (context.element == nullptr && context.attached.IsValid() && SameName(type.typeName, "JBro.Handle.GameObject"))
        {
            DrawObjectField(type, address, context);
            return;
        }
        // 컴포넌트 참조는 그 타입이 붙은 오브젝트의 목록이다(D-302).
        if (context.element == nullptr && context.attached.IsValid() && type.refTarget != nullptr
            && SameName(type.typeName, "JBro.ComponentHandle"))
        {
            DrawComponentHandleField(type, address, context);
            return;
        }
        // **`AssetId` 는 드롭다운이다**(D-116). 목록 원소도 그렇다(D-306): `audioEffectIds` 의 원소는 `audioEffectId` 처럼
        // 읽어 그 타입만 고른다. 고른 값은 원소 편집으로 적힌다(`CommitEdit` → `RecordElementEdit`, D-89) - 쓰는 길은 하나다.
        if (context.element == nullptr && SameName(type.typeName, "JBro.Uuid")
            && IsAssetIdName(label))
        {
            DrawAssetField(label, type, address, context);
            return;
        }
        if (context.element != nullptr && context.element->fieldDepth == 0 && SameName(type.typeName, "JBro.Uuid")
            && context.listField != nullptr)
        {
            const std::size_t length = std::strlen(context.listField);
            if (length > 3 && length < 64 && std::strcmp(context.listField + length - 3, "Ids") == 0)
            {
                char elementName[64] = {};
                std::memcpy(elementName, context.listField, length - 1);
                DrawAssetField(elementName, type, address, context);
                return;
            }
        }

        // 텍스트의 글자는 줄바꿈을 그대로 치는 여러 줄 칸이다. 한 줄 칸은 줄바꿈을 이스케이프 글자로 보여 주고
        // 512 바이트에서 끊겼다.
        // 뼈에 붙이기의 소켓 칸은 그 애니메이터의 소켓·뼈 이름에서 고른다(D-460). 문자열 표의 키 고르기보다 먼저다.
        if (context.element == nullptr && context.attached.IsValid() && SameName(type.typeName, "JBro.TextId") && label != nullptr
            && std::strcmp(label, "socket") == 0 && context.typeId == MakeStableTypeId(Component::BoneAttachment3D::StaticTypeName()))
        {
            DrawSocketNameField(type, address, editable, context);
            return;
        }
        // 애니메이터의 위 레이어 마스크 칸은 그 뼈대의 마스크 이름에서 고른다(D-464).
        if (context.element == nullptr && context.attached.IsValid() && SameName(type.typeName, "JBro.TextId") && label != nullptr
            && (std::strcmp(label, "layer1Mask") == 0 || std::strcmp(label, "layer2Mask") == 0 || std::strcmp(label, "layer3Mask") == 0)
            && context.typeId == MakeStableTypeId(Component::Animator3D::StaticTypeName()))
        {
            DrawLayerMaskField(type, address, editable, context);
            return;
        }
        // 모델 메타 소켓의 뼈는 그 뼈대의 뼈 이름에서 고른다(D-460).
        if (context.asset != nullptr && context.asset->block == AssetEditScope::Block::SubSockets && context.asset->boneNames != nullptr
            && label != nullptr && std::strcmp(label, "bone") == 0 && SameName(type.typeName, "string") && editable)
        {
            DrawSocketBoneField(address, context);
            return;
        }
        if (context.element == nullptr && context.attached.IsValid() && SameName(type.typeName, "JBro.TextId"))
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

#include "pch.h"
#include "InspectorTool.h"
#include "AssetInspectorPreview.h"
#include "EffectEditorWindow.h"
#include "Editor/Main/ProjectSettingsWindow.h"

#include "Editor/ImItem/ImAssetField.h"
#include "Editor/ImItem/ImText.h"
#include "Editor/ImItem/ImSplitter.h"
#include "Editor/ImItem/ImList.h"
#include "Engine/Editor/ImGuiUtillity.h"
#include "Editor/Script/ScriptSchema.h"
#include "Editor/Script/ScriptSchemaWidgets.h"

#include "Editor/Editor.h"
#include "Editor/EditorContext.h"
#include "Editor/Command/EditorCanvasCommands.h"
#include "Editor/Command/EditorLayerCommands.h"
#include "Editor/Command/EditorSceneCommands.h"
#include "Editor/EditorDragDrop.h"
#include "Editor/Gui/EditorGuiActions.h"
#include "Editor/Localization/EditorReflectionLabels.h"
#include "Engine/GameFramework/Object/GameObject.h"
#include "Engine/GameFramework/Object/Ref.h"
#include "Engine/GameFramework/Reflection/ReflectionRegistry.h"
#include "Engine/Core/Asset/AssetMetaFile.h"
#include "Engine/Core/Asset/AssetTypeRules.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/IAssetRegistry.h"
#include "Engine/Core/Asset/MaterialAsset.h"
#include "Engine/Core/Asset/SpriteAsset.h"
#include "Engine/Core/Asset/AudioAsset.h"
#include "Engine/Core/Asset/FontAsset.h"
#include "Engine/Core/RuntimeConfig.h"
#include "Engine/GameFramework/Component/Text2D.h"
#include "Engine/Core/ScriptCore.h"
#include "Engine/Core/Renderer/IRenderResourceCache.h"
#include "Engine/Core/Resource/ResourceRegistry.h"
#include "Engine/Core/RHI/IRHITexture.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/GameFramework/Component/AudioComponents.h"
#include "Engine/GameFramework/Component/Camera2D.h"
#include "Engine/GameFramework/Component/Physics2DComponents.h"
#include "Engine/GameFramework/Scripting/GameScript.h"
#include "Engine/GameFramework/Component/Transform2D.h"
#include "Engine/GameFramework/Physics2D/Physics2DSystem.h"
#include "Engine/GameFramework/Physics2D/Physics2DTypes.h"
#include "Engine/GameFramework/Canvas/CanvasTransformUtils.h"
#include "Engine/GameFramework/Canvas/CanvasRuntimeAccess.h"
#include "Utillity/Math/RectT.h"
#include "Utillity/Types/EngineTypes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>

namespace
{
	constexpr std::size_t GUID_BUFFER_LENGTH = 128;

	using EditorReflectionLabels::GetComponentLabel;
	using EditorReflectionLabels::GetPropertyLabel;
	using EditorReflectionLabels::GetScriptDisplayName;

	// 컴포넌트 타입 이름 → ResourceRegistry 아이콘 키.
	// 매핑이 없으면 nullptr — 호출부에서 자리(Dummy)만 비우고 이미지는 그리지 않는다.
	const char* GetComponentIconKey(const char* typeName)
	{
		if (nullptr == typeName) return nullptr;
		if (0 == strcmp(typeName, "Transform2D"))            return "icon-transform";
		if (0 == strcmp(typeName, "Camera2D"))               return "icon-camera";
		if (0 == strcmp(typeName, "Rigidbody2D"))            return "icon-rigidbody";
		if (0 == strcmp(typeName, "CircleCollider2D"))       return "icon-circle-collider";
		if (0 == strcmp(typeName, "PolygonCollider2D"))      return "icon-polygon-collider";
		if (0 == strcmp(typeName, "Script"))                 return "icon-script";
		if (0 == strcmp(typeName, "GameObject"))             return "icon-object";
		return nullptr;
	}

	ImTextureID GetComponentIconTexture(const char* typeName)
	{
		if (false == Engine.ResourceRegistry.IsValid()) return 0;
		const char* key = GetComponentIconKey(typeName);
		if (nullptr == key) return 0;
CSpriteAsset* sprite = Engine.ResourceRegistry->GetSprite(key);
		if (nullptr == sprite) return 0;
		if (false == Engine.RenderResourceCache.IsValid()) return 0;
		SafePtr<IRHITexture> tex = Engine.RenderResourceCache->AcquireSpriteTexture(sprite->GetGuid(), *sprite);
		if (false == tex.IsValid()) return 0;
		return reinterpret_cast<ImTextureID>(tex->GetNativeHandle().ShaderResourceView);
	}

	// 스크립트 컴포넌트에서 등록된 타입의 표시 이름을 가져온다.
	// 미등록(INVALID_TYPE_ID) 또는 reflection 미준비 시 nullptr.
	const char* GetScriptInstanceDisplayName(void* compInstance)
	{
		if (nullptr == compInstance || false == Engine.Reflection.IsValid()) return nullptr;
		CGameScript* script = static_cast<CGameScript*>(compInstance);
		if (INVALID_TYPE_ID == script->GetTypeId()) return nullptr;
		const ScriptTypeInfo* info = Engine.Reflection->FindScript(script->GetTypeId());
		return info ? GetScriptDisplayName(info) : nullptr;
	}

	File::Guid GetComponentGuid(void* component)
	{
		return component ? static_cast<CComponent*>(component)->GetInstanceGuid() : File::Guid();
	}

	// ── Ref<T> 프로퍼티 헬퍼 ──────────────────────────────────────────────────
	// 드래그된 HIERARCHY_COMPONENT 페이로드의 타입명(컴포넌트/스크립트) 조회.
	// 오브젝트를 컴포넌트/스크립트 Ref 프로퍼티에 드롭했을 때, 타입이 맞는 첫 컴포넌트를 찾는다.
	//  · Script Ref     : RefTypeName 과 등록 스크립트 타입명이 일치하는 첫 CGameScript.
	//  · Component Ref  : GetTypeName() 이 RefTypeName 과 일치하는 첫 컴포넌트.
	// 없으면 nullptr. RefTypeName 이 비어 있으면 카테고리만 맞으면 첫 인스턴스를 돌려준다.
	File::Guid FindFirstAttachmentForRef(CGameObject& object, const ReflectPropertyInfo& property)
	{
		const bool wantScript = (ERefCategory::Script == property.RefCategory);
		const char* wantType  = property.RefTypeName;
		if (wantScript)
		{
			CGameCanvas* scene = object.GetCanvas();
			if (nullptr == scene)
			{
				return File::Guid();
			}
			for (const SafePtr<CComponent>& componentRef : object.GetComponents())
			{
				CGameScript* script = CCanvasRuntimeAccess::AsScript(*scene, componentRef.TryGet());
				if (nullptr == script) continue;
				if (nullptr == wantType || '\0' == wantType[0]) return script->GetInstanceGuid();
				if (Engine.Reflection.IsValid())
				{
					const ScriptTypeInfo* info = Engine.Reflection->FindScript(script->GetTypeId());
					if (info && info->Type.Name && 0 == strcmp(info->Type.Name, wantType)) return script->GetInstanceGuid();
				}
			}
			return File::Guid();
		}

		for (const SafePtr<CComponent>& cref : object.GetComponents())
		{
			CComponent* comp = cref.TryGet();
			if (nullptr == comp)
			{
				continue;
			}

			if (false == wantScript)
			{
				const char* tn = comp->GetTypeName();
				if (nullptr == wantType || '\0' == wantType[0] || (tn && 0 == strcmp(tn, wantType)))
				{
					return comp->GetInstanceGuid();
				}
			}
		}
		return File::Guid();
	}

	// Ref 의 현재 대상 표시 라벨.
	std::string BuildRefDisplayLabel(const RefBase& ref, const ReflectPropertyInfo& property)
	{
		const char* typeName = property.RefTypeName ? property.RefTypeName : "Ref";
		if (ref.IsNull())
		{
			return std::string(Loc::Text(EditorLocKeys::InspectorRefNone)) + "  [" + typeName + "]";
		}
		const File::Guid guid(ref.GuidText());
		if (ERefCategory::Asset == property.RefCategory)
		{
			const File::Path& path = File::ResolvePath(guid);
			const std::string name = path.IsNull()
				? std::string(ref.GuidText())
				: path.filename().generic_string();
			return name + "  [" + typeName + "]";
		}
		// 오브젝트/컴포넌트/스크립트 — InstanceGuid → 오브젝트 이름.
		if (CGameCanvas* scene = EditorContext::TryGetActiveScene())
		{
			if (CGameObject* obj = scene->FindByInstanceGuid(guid).TryGet())
			{
				return std::string(obj->GetName()) + "  [" + typeName + "]";
			}
		}
		return std::string(Loc::Text(EditorLocKeys::InspectorRefMissing)) + "  [" + typeName + "]";
	}

	EAssetType ResolveExpectedAssetType(const ReflectPropertyInfo& property)
	{
		if (EAssetType::Unknown != property.ExpectedAssetType)
		{
			return property.ExpectedAssetType;
		}
		if (property.RefTypeName)
		{
			return CAssetTypeRules::ParseTypeName(property.RefTypeName);
		}
		return EAssetType::Unknown;
	}

	// Ref 의 드롭 타깃 처리. 변경되면 true. (호출 시점은 위젯 바로 다음.)
	bool ApplyRefDrop(RefBase& ref, const ReflectPropertyInfo& property)
	{
		// 에셋 참조 — 에셋 브라우저 페이로드(AcceptAssetDragDropPayload 가 자체 Begin/End).
		if (ERefCategory::Asset == property.RefCategory)
		{
			EditorDragDrop::AssetPayload payload;
			if (EditorDragDrop::AcceptAssetDragDropPayload(payload))
			{
				const EAssetType expectedType = ResolveExpectedAssetType(property);
				if (false == CAssetTypeRules::IsAssignableTo(expectedType, payload.Type, EditorDragDrop::GetRelativePath(payload)))
				{
					return false;
				}
				ref.SetGuidText(EditorDragDrop::GetGuid(payload).generic_string().c_str());
				return true;
			}
			return false;
		}

		// 오브젝트/컴포넌트/스크립트 — 하이어라키 페이로드.
		CGameCanvas* scene = EditorContext::TryGetActiveScene();
		if (nullptr == scene || false == ImGui::BeginDragDropTarget())
		{
			return false;
		}
		bool changed = false;

		const bool wantsGameObject =
			property.RefTypeName && 0 == strcmp(property.RefTypeName, "GameObject");

		// 드롭 전에 타입 적합성을 먼저 검사한다. 적합할 때만 AcceptDragDropPayload 를 호출해야
		// ImGui 가 부적합 드롭을 "수락 가능"(초록 테두리)으로 표시하지 않는다. 호버 중인
		// 페이로드를 GetDragDropPayload 로 읽기전용 조회해 타입을 미리 판정한다.
		const ImGuiPayload* hovering = ImGui::GetDragDropPayload();
		File::Guid match;
		bool acceptable = false;
		if (hovering && hovering->IsDataType(EditorDragDrop::HIERARCHY_ENTITY_PAYLOAD)
			&& hovering->Data)
		{
			CGameObject* obj = *static_cast<CGameObject* const*>(hovering->Data);
			// GameObject Ref 는 오브젝트면 항상 적합. Component/Script Ref 는 그 오브젝트에
			// 타입이 맞는 컴포넌트가 있을 때만 적합(없으면 드롭 거부).
			acceptable = (nullptr != obj) &&
				(wantsGameObject || false == (match = FindFirstAttachmentForRef(*obj, property)).IsNull());
		}

		// 적합할 때만 실제 수락 대상으로 등록한다. 부적합이면 Accept 를 호출하지 않으므로
		// ImGui 가 드롭 불가로 표시하고, 드롭해도 아무 일도 일어나지 않는다.
		if (acceptable)
		{
			if (const ImGuiPayload* p =
				ImGui::AcceptDragDropPayload(EditorDragDrop::HIERARCHY_ENTITY_PAYLOAD))
			{
				CGameObject* obj = *static_cast<CGameObject* const*>(p->Data);
				ref.SetGuidText(obj->GetInstanceGuid().generic_string().c_str());
				ref.SetComponentGuidText(wantsGameObject ? "" : match.generic_string().c_str());
				changed = true;
			}
		}

		ImGui::EndDragDropTarget();
		return changed;
	}

	// 공유 참조-필드 위젯 — 에셋/오브젝트/컴포넌트/스크립트 참조 모두 같은 모양.
	// 시각(ReadOnly 버튼 + X 클리어)은 여기 한 곳. 드롭 수락/클리어는 저장부별 콜백으로 주입.
	//   accept(): 자체 Begin/End 드롭 타깃 처리, 변경 시 true.
	//   clear():  값 비우기.
	//   revealAssetGuid: null 이 아니면 더블클릭 시 그 에셋을 브라우저에서 드러낸다(에셋 Ref
	//   전용 — 오브젝트/컴포넌트/스크립트 참조는 브라우저에 없으므로 넘기지 않는다).
	template <typename AcceptFn, typename ClearFn>
	bool DrawReferenceField(const std::string& label, bool isNull, AcceptFn&& accept, ClearFn&& clear,
	                        const File::Guid* revealAssetGuid = nullptr)
	{
		ImReferenceField widget("##reference_field", label, isNull);
		widget.OnAcceptDrop(std::forward<AcceptFn>(accept))
			.OnClear(std::forward<ClearFn>(clear));
		if (nullptr != revealAssetGuid && false == revealAssetGuid->IsNull())
		{
			const File::Guid guid = *revealAssetGuid;
			widget.OnActivate([guid]() {
				if (Editor::AssetBrowser.IsValid())
				{
					Editor::AssetBrowser->RevealAsset(guid);
				}
			});
		}
		return widget.Draw();
	}

	bool DrawPropertyEditor(void* field, const ReflectPropertyInfo& property)
	{
		if (nullptr == field || false == property.IsEditable)
		{
			return false;
		}

		ImGui::Utillity::IDGroup idGroup(field);

		switch (property.Type)
		{
		case EReflectPropertyType::Bool:
			return ImGui::Checkbox("", static_cast<bool*>(field));
		case EReflectPropertyType::Int32:
			if (property.HasRange)
			{
				return ImGui::SliderInt("", static_cast<int*>(field),
					static_cast<int>(property.RangeMin), static_cast<int>(property.RangeMax));
			}
			return ImGui::InputScalar("", ImGuiDataType_S32, field);
		case EReflectPropertyType::Int64:
			if (property.HasRange)
			{
				std::int64_t min = static_cast<std::int64_t>(property.RangeMin);
				std::int64_t max = static_cast<std::int64_t>(property.RangeMax);
				return ImGui::SliderScalar("", ImGuiDataType_S64, field, &min, &max);
			}
			return ImGui::InputScalar("", ImGuiDataType_S64, field);
		case EReflectPropertyType::UInt32:
			return ImGui::InputScalar("", ImGuiDataType_U32, field);
		case EReflectPropertyType::UInt64:
			return ImGui::InputScalar("", ImGuiDataType_U64, field);
		case EReflectPropertyType::Float:
			if (property.HasRange)
			{
				// JPROP(Range(min,max)) → 슬라이더 + 클램프.
				return ImGui::SliderFloat("", static_cast<float*>(field), property.RangeMin, property.RangeMax);
			}
			return ImGui::DragFloat("", static_cast<float*>(field), 0.01f);
		case EReflectPropertyType::Degree:
			return ImGui::DragFloat("", static_cast<float*>(field), 0.5f, 0.0f, 0.0f, "%.2f deg");
		case EReflectPropertyType::Radian:
			return ImGui::DragFloat("", static_cast<float*>(field), 0.01f, 0.0f, 0.0f, "%.2f rad");
		case EReflectPropertyType::AngleDegrees:
		{
			// 내부 저장값은 Radians, Inspector에서는 Degrees로 표시/편집.
			float* rad = static_cast<float*>(field);
			constexpr float kRad2Deg = RAD_TO_DEG;
			constexpr float kDeg2Rad = DEG_TO_RAD;
			float deg = *rad * kRad2Deg;
			if (ImGui::DragFloat("", &deg, 0.5f, 0.0f, 0.0f, "%.2f deg"))
			{
				*rad = deg * kDeg2Rad;
				return true;
			}
			return false;
		}
		case EReflectPropertyType::Vector2Float:
			return ImGui::DragFloat2("", static_cast<float*>(field), 0.01f);
		case EReflectPropertyType::RectFloat:
		{
			Rect* rect = static_cast<Rect*>(field);
			float values[4] = { rect->Left, rect->Top, rect->Right, rect->Bottom };
			if (ImGui::DragFloat4("", values, 0.01f))
			{
				rect->Left = values[0];
				rect->Top = values[1];
				rect->Right = values[2];
				rect->Bottom = values[3];
				return true;
			}
			return false;
		}
		case EReflectPropertyType::ColorFloat4:
			return ImGui::ColorEdit4("", static_cast<float*>(field));
		case EReflectPropertyType::String:
			if (property.ElementCount > 1)
			{
				return ImGui::InputText("", static_cast<char*>(field), property.ElementCount);
			}
			if (property.Name && 0 == std::strcmp(property.Name, "Text"))
			{
				return ImGui::InputTextMultiline("", static_cast<std::string*>(field), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.5f));
			}
			return ImGui::InputText("", static_cast<std::string*>(field));
		case EReflectPropertyType::AssetGuid:
		{
			// 에셋 참조(엔진 컴포넌트의 File::Guid). AssetField 가 표시/클리어/드롭 수락을 통합한다.
			File::Guid* guid = static_cast<File::Guid*>(field);
			ImAssetField fieldWidget("##asset_guid", *guid);
			if (EAssetType::Unknown != property.ExpectedAssetType)
			{
				fieldWidget.Type(property.ExpectedAssetType);
			}
			return fieldWidget.Draw();
		}
		case EReflectPropertyType::Ref:
		{
			// Ref<T>(스크립트 POD RefBase). 공유 위젯 + 카테고리별 페이로드 수락(ApplyRefDrop).
			RefBase* ref = static_cast<RefBase*>(field);
			// 에셋 Ref 만 더블클릭 reveal 대상 — guid 를 넘긴다(오브젝트/컴포넌트/스크립트 참조는
			// 브라우저에 없으므로 제외).
			File::Guid revealGuid;
			if (ERefCategory::Asset == property.RefCategory && false == ref->IsNull())
			{
				revealGuid = File::Guid(ref->GuidText());
			}
			return DrawReferenceField(
				BuildRefDisplayLabel(*ref, property), ref->IsNull(),
				[&]() { return ApplyRefDrop(*ref, property); },
				[&]() { ref->Clear(); },
				revealGuid.IsNull() ? nullptr : &revealGuid);
		}
		case EReflectPropertyType::Enum:
		{
			// 메타가 있으면 이름 드롭다운(Combo). 없으면(스크립트 enum 등 미등록) Int 폴백.
			if (const EnumTypeMeta* meta = property.Enum; meta && meta->Names && meta->Count > 0)
			{
				int index = meta->ToIndex(field, property.Size);
				const char* rawPreview = (index >= 0 && index < meta->Count) ? meta->Names[index] : "?";
				const std::string previewKey = std::string("editor.enum.") + (property.Name ? property.Name : "") + "." + rawPreview;
				const std::string preview = Loc::TextOr(previewKey.c_str(), rawPreview);
				bool changed = false;
				if (ImGui::BeginCombo("", preview.c_str()))
				{
					for (int i = 0; i < meta->Count; ++i)
					{
						const bool selected = (i == index);
						const std::string itemKey = std::string("editor.enum.") + (property.Name ? property.Name : "") + "." + meta->Names[i];
						const std::string itemLabel = Loc::TextOr(itemKey.c_str(), meta->Names[i]);
						if (ImGui::Selectable(itemLabel.c_str(), selected))
						{
							meta->SetIndex(field, property.Size, i);
							changed = true;
						}
						if (selected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
				return changed;
			}

			std::int32_t value = 0;
			const std::size_t copySize = std::min(property.Size, sizeof(value));
			std::memcpy(&value, field, copySize);
			if (ImGui::InputInt("", &value))
			{
				std::memcpy(field, &value, copySize);
				return true;
			}
			return false;
		}
		case EReflectPropertyType::Layout2D:
		{
			// Layout2D 편집 UI:
			//   Normalized (nx, ny) × 해상도 + Pixel (px, py) = 실제 픽셀
			Layout2D* layout = static_cast<Layout2D*>(field);
			bool changed = false;

			ImGui::PushID("");
			ImGui::TextUnformatted("");
			ImGui::Indent(8.0f);

			const std::string normLabel = std::string("N##") + "";
			const std::string pixLabel  = std::string("P##") + "";

			float norm[2] = { layout->Normalized.x, layout->Normalized.y };
			if (ImGui::DragFloat2(normLabel.c_str(), norm, 0.01f,
			                      0.0f, 0.0f, "%.3f"))
			{
				layout->Normalized.x = norm[0];
				layout->Normalized.y = norm[1];
				changed = true;
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(%s)", Loc::Text(EditorLocKeys::InspectorNormalized));

			float pix[2] = { layout->Pixel.x, layout->Pixel.y };
			if (ImGui::DragFloat2(pixLabel.c_str(), pix, 1.0f,
			                      0.0f, 0.0f, "%.1f"))
			{
				layout->Pixel.x = pix[0];
				layout->Pixel.y = pix[1];
				changed = true;
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(%s)", Loc::Text(EditorLocKeys::InspectorPixel));

			ImGui::Unindent(8.0f);
			ImGui::PopID();
			return changed;
		}
		default:
			ImGui::TextDisabled("%s: %s", "", Loc::Text(EditorLocKeys::InspectorUnsupported));
			return false;
		}
	}

	void DrawReflectedPropertyRow(
		CGameCanvas& scene,
		CGameObject* selectedObject,
		CComponent& component,
		const ReflectPropertyInfo& property,
		ImText& labelText)
	{
		void* field = CReflectionRegistry::GetPropertyAddress(&component, property);
		if (nullptr == field)
		{
			return;
		}

		std::vector<std::uint8_t> oldValue(property.Size);
		const bool canRawUndo = !(property.Type == EReflectPropertyType::String && property.ElementCount <= 1);
		const bool canStringUndo = property.Type == EReflectPropertyType::String && property.ElementCount <= 1;
		const std::string oldString = canStringUndo ? *static_cast<std::string*>(field) : std::string();
		if (canRawUndo && property.Size > 0)
		{
			std::memcpy(oldValue.data(), field, property.Size);
		}

		ImGui::Utillity::FormLayout layout("##reflected_properties", 4.0f, { 2.0f, 1.0f }, 60.0f);
		const std::string label = GetPropertyLabel(property);
		layout.Row([&]() { labelText(label.c_str()); }, [&]()
		{
			const bool changed = DrawPropertyEditor(field, property);
			if (changed && canRawUndo && property.Size > 0)
			{
				std::vector<std::uint8_t> newValue(property.Size);
				std::memcpy(newValue.data(), field, property.Size);
				if (oldValue != newValue)
				{
					Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CSetComponentPropertyCommand>(
						scene.SafeFromThis(), selectedObject, component.GetTypeId(), property.Offset,
						std::move(oldValue), std::move(newValue), component.GetInstanceGuid()));
				}
			}
			else if (changed && canStringUndo)
			{
				const std::string& newString = *static_cast<std::string*>(field);
				if (oldString != newString)
				{
					Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CSetComponentStringPropertyCommand>(
						scene.SafeFromThis(), selectedObject, component.GetTypeId(), property.Offset,
						oldString, newString, component.GetInstanceGuid()));
				}
			}
		});
	}

	void DrawTransformMatrixReadOnly(const Transform2D& transform)
	{
		const Matrix3x2 matrix = transform.ToMatrix3x2();
		float row0[3] = { matrix.M11, matrix.M21, matrix.Dx };
		float row1[3] = { matrix.M12, matrix.M22, matrix.Dy };

		ImGui::SeparatorText(Loc::Text(EditorLocKeys::InspectorMatrix3x2));
		ImGui::BeginDisabled();
		ImGui::InputFloat3("Row 0", row0, "%.3f", ImGuiInputTextFlags_ReadOnly);
		ImGui::InputFloat3("Row 1", row1, "%.3f", ImGuiInputTextFlags_ReadOnly);
		ImGui::EndDisabled();
	}

	void DrawReadOnlyVector2(const char* label, const Vector2& value)
	{
		float vector[2] = { value.x, value.y };
		ImGui::BeginDisabled();
		ImGui::InputFloat2(label, vector, "%.3f", ImGuiInputTextFlags_ReadOnly);
		ImGui::EndDisabled();
	}

	void DrawReadOnlyFloat(const char* label, float value)
	{
		ImGui::BeginDisabled();
		ImGui::InputFloat(label, &value, 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_ReadOnly);
		ImGui::EndDisabled();
	}

	void DrawReadOnlyUInt(const char* label, std::uint32_t value)
	{
		ImGui::BeginDisabled();
		ImGui::InputScalar(label, ImGuiDataType_U32, &value, nullptr, nullptr, nullptr, ImGuiInputTextFlags_ReadOnly);
		ImGui::EndDisabled();
	}

	void DrawPhysicsContactDebug(const CGameCanvas& scene, const CGameObject* selectedObject)
	{
		const CPhysics2DSystem* physicsSystem = scene.GetPhysics2DSystem();
		if (nullptr == physicsSystem)
		{
			return;
		}

		std::uint32_t contactCount = 0;
		Physics2DManifold lastManifold;
		for (const Physics2DManifold& manifold : physicsSystem->GetManifolds())
		{
			const bool involvesSelected =
				(manifold.A == selectedObject) ||
				(manifold.B == selectedObject);
			if (false == involvesSelected)
			{
				continue;
			}

			++contactCount;
			lastManifold = manifold;
		}

		ImGui::SeparatorText(Loc::Text(EditorLocKeys::InspectorPhysicsContacts));
		DrawReadOnlyUInt(Loc::Text(EditorLocKeys::InspectorContactsDetected), contactCount);
		if (contactCount > 0)
		{
			DrawReadOnlyVector2(Loc::Text(EditorLocKeys::InspectorContactsLastNormal), lastManifold.Normal);
			DrawReadOnlyVector2(Loc::Text(EditorLocKeys::InspectorContactsLastPoint), lastManifold.ContactPoints[0]);
			DrawReadOnlyFloat(Loc::Text(EditorLocKeys::InspectorContactsLastPenetration), lastManifold.Penetration);
		}
	}

	void DrawRigidbodyDebug(const CGameCanvas& scene, const CGameObject* selectedObject, const Rigidbody2D& rigidbody)
	{
		ImGui::SeparatorText(Loc::Text(EditorLocKeys::InspectorRigidbodyDebug));
		const float inverseMass = rigidbody.Mass > 0.0f ? 1.0f / rigidbody.Mass : 0.0f;
		const float inverseInertia = false == rigidbody.FreezeRotation && rigidbody.Inertia > 0.0f ? 1.0f / rigidbody.Inertia : 0.0f;
		DrawReadOnlyFloat(Loc::Text(EditorLocKeys::InspectorRigidbodyInverseMass), inverseMass);
		DrawReadOnlyFloat(Loc::Text(EditorLocKeys::InspectorRigidbodyInverseInertia), inverseInertia);
		DrawReadOnlyUInt(Loc::Text(EditorLocKeys::InspectorRigidbodyImpulseContacts), rigidbody.LastContactCount);
		DrawReadOnlyVector2(Loc::Text(EditorLocKeys::InspectorRigidbodyLastImpulseNormal), rigidbody.LastContactNormal);
		DrawReadOnlyVector2(Loc::Text(EditorLocKeys::InspectorRigidbodyLastImpulsePoint), rigidbody.LastContactPoint);
		DrawReadOnlyFloat(Loc::Text(EditorLocKeys::InspectorRigidbodyLastNormalImpulse), rigidbody.LastNormalImpulse);
		DrawReadOnlyFloat(Loc::Text(EditorLocKeys::InspectorRigidbodyLastFrictionImpulse), rigidbody.LastFrictionImpulse);
		DrawReadOnlyFloat(Loc::Text(EditorLocKeys::InspectorRigidbodyLastAngularImpulse), rigidbody.LastAngularImpulse);
		DrawPhysicsContactDebug(scene, selectedObject);
	}

	void DrawCircleColliderDebug(const CGameCanvas& scene, const CGameObject* selectedObject, const CircleCollider2D& collider)
	{
		(void)scene;
		const Matrix3x2 worldTransform = selectedObject ? GetWorldTransform(*selectedObject) : Matrix3x2::Identity();
		const Vector2 worldCenter = worldTransform.TransformPoint(Vector2(0.0f, 0.0f));
		const float scaleX = std::sqrt(worldTransform.M11 * worldTransform.M11 + worldTransform.M12 * worldTransform.M12);
		const float scaleY = std::sqrt(worldTransform.M21 * worldTransform.M21 + worldTransform.M22 * worldTransform.M22);
		const float worldRadius = collider.Radius * std::max(scaleX, scaleY);

		ImGui::SeparatorText(Loc::Text(EditorLocKeys::InspectorCircleColliderDebug));
		DrawReadOnlyVector2(Loc::Text(EditorLocKeys::InspectorColliderWorldCenter), worldCenter);
		DrawReadOnlyFloat(Loc::Text(EditorLocKeys::InspectorColliderWorldRadius), worldRadius);
	}

	void DrawPolygonColliderDebug(const CGameCanvas& scene, const CGameObject* selectedObject, const PolygonCollider2D& collider)
	{
		(void)scene;
		std::vector<Vector2> generatedPoints;
		const std::vector<Vector2>* localPoints = &collider.LocalPoints;
		if (localPoints->empty())
		{
			collider.BuildLocalPoints(generatedPoints);
			localPoints = &generatedPoints;
		}

		PhysicsAABB2D aabb;
		if (false == localPoints->empty())
		{
			const Matrix3x2 worldTransform = selectedObject ? GetWorldTransform(*selectedObject) : Matrix3x2::Identity();
			Vector2 firstPoint = worldTransform.TransformPoint((*localPoints)[0]);
			aabb.Min = firstPoint;
			aabb.Max = firstPoint;
			for (const Vector2& localPoint : *localPoints)
			{
				const Vector2 worldPoint = worldTransform.TransformPoint(localPoint);
				aabb.Min.x = std::min(aabb.Min.x, worldPoint.x);
				aabb.Min.y = std::min(aabb.Min.y, worldPoint.y);
				aabb.Max.x = std::max(aabb.Max.x, worldPoint.x);
				aabb.Max.y = std::max(aabb.Max.y, worldPoint.y);
			}
		}

		ImGui::SeparatorText(Loc::Text(EditorLocKeys::InspectorPolygonColliderDebug));
		DrawReadOnlyUInt(Loc::Text(EditorLocKeys::InspectorColliderLocalPoints), static_cast<std::uint32_t>(localPoints->size()));
		DrawReadOnlyUInt(Loc::Text(EditorLocKeys::InspectorColliderConvexPieces), static_cast<std::uint32_t>(collider.ConvexPieces.size()));
		DrawReadOnlyVector2(Loc::Text(EditorLocKeys::InspectorColliderWorldAabbMin), aabb.Min);
		DrawReadOnlyVector2(Loc::Text(EditorLocKeys::InspectorColliderWorldAabbMax), aabb.Max);
	}

	void DrawCamera2DDebug(const CGameObject* selectedObject)
	{
		SafePtr<CProjectManager> projectManager = EditorContext::GetProjectManager();
		if (false == projectManager.IsValid() || false == projectManager->IsDebugModeEnabled())
		{
			return;
		}

		ImGui::SeparatorText(Loc::Text(EditorLocKeys::InspectorCameraDebug));

		RenderCullingStats stats;
		if (false == Editor::ImEditor.IsValid() || false == Editor::ImEditor->TryGetCameraCullingStats(selectedObject, stats))
		{
			ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::InspectorCameraDebugNoStats));
			return;
		}

		DrawReadOnlyUInt(Loc::Text(EditorLocKeys::InspectorCameraDebugSubmitted), stats.SubmittedCount);
		DrawReadOnlyUInt(Loc::Text(EditorLocKeys::InspectorCameraDebugCulled), stats.CulledCount);
		DrawReadOnlyUInt(Loc::Text(EditorLocKeys::InspectorCameraDebugDrawn), stats.DrawnCount);
	}

	// ── GetComponentIsEnabled ────────────────────────────────────────────────
	bool GetComponentIsEnabled(void* component)
	{
		const CComponent* value = static_cast<const CComponent*>(component);
		return nullptr == value || value->IsEnabled();
	}

	// ── DrawIsEnabledCheckbox ─────────────────────────────────────────────────
	//   sameLineAfter=true  → "##enabled" 체크박스 + SameLine() (CollapsingHeader 왼쪽)
	//   sameLineAfter=false → "IsEnabled" 라벨 체크박스 + Separator  (탭 최상단 단독)
	void DrawIsEnabledCheckbox(
		CGameCanvas& scene, CGameObject* selectedObject,
		const ComponentTypeInfo& componentType,
		std::size_t instanceIdx, void* component, bool sameLineAfter)
	{
		CComponent* target = static_cast<CComponent*>(component);
		if (nullptr == target) return;

		bool oldEnabled = target->IsEnabled();
		bool newEnabled = oldEnabled;
		const std::string enabledLabel = sameLineAfter ? "##enabled" : (std::string(Loc::Text(EditorLocKeys::EditorPropertyIsEnabled)) + "##enabled");
		if (ImGui::Checkbox(enabledLabel.c_str(), &newEnabled) && newEnabled != oldEnabled)
		{
			target->SetEnabled(newEnabled);
			Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CSetComponentEnabledCommand>(
				scene.SafeFromThis(), selectedObject, GetComponentGuid(component), oldEnabled, newEnabled));
		}
		if (sameLineAfter)
			ImGui::SameLine();
		else
			ImGui::Separator();
	}

	// ── DrawComponentProperties ───────────────────────────────────────────────
	// IsEnabled·non-editable 프로퍼티를 제외하고 에디터 + 특수 디버그 섹션 렌더링.
	void DrawComponentProperties(
		CGameCanvas& scene, CGameObject* selectedObject,
		const ComponentTypeInfo& componentType,
		std::size_t instanceIdx, void* component)
	{
		ImText leftText;
		leftText.SetHoveredTooltip(true);

		for (const ReflectPropertyInfo& property : componentType.Properties)
		{
			if (property.Name && 0 == strcmp(property.Name, "IsEnabled"))
				continue;
			if (!property.IsEditable)
				continue;
			if (componentType.Type.Id == CReflectionRegistry::MakeTypeId("Text2D"))
			{
				const Text2D& text = *static_cast<const Text2D*>(component);
				if (property.Name && 0 == std::strcmp(property.Name, "HeightPixels") && ETextOverflowMode::Clip != text.OverflowMode) continue;
				if (property.Name && (0 == std::strcmp(property.Name, "AutoSizeEnabled")
					|| 0 == std::strcmp(property.Name, "MinFontSizePixels") || 0 == std::strcmp(property.Name, "MaxFontSizePixels"))
					&& ETextOverflowMode::Clip != text.OverflowMode) continue;
				if (property.Name && (0 == std::strcmp(property.Name, "MinFontSizePixels") || 0 == std::strcmp(property.Name, "MaxFontSizePixels"))
					&& false == text.AutoSizeEnabled) continue;
			}

			DrawReflectedPropertyRow(scene, selectedObject, *static_cast<CComponent*>(component), property, leftText);
		}

		// 특수 디버그 섹션
		if (componentType.Type.Id == CReflectionRegistry::MakeTypeId("Transform2D"))
			DrawTransformMatrixReadOnly(*static_cast<Transform2D*>(component));
		else if (componentType.Type.Id == CReflectionRegistry::MakeTypeId("Rigidbody2D"))
			DrawRigidbodyDebug(scene, selectedObject, *static_cast<Rigidbody2D*>(component));
		else if (componentType.Type.Id == CReflectionRegistry::MakeTypeId("CircleCollider2D"))
			DrawCircleColliderDebug(scene, selectedObject, *static_cast<CircleCollider2D*>(component));
		else if (componentType.Type.Id == CReflectionRegistry::MakeTypeId("PolygonCollider2D"))
			DrawPolygonColliderDebug(scene, selectedObject, *static_cast<PolygonCollider2D*>(component));
		else if (componentType.Type.Id == CReflectionRegistry::MakeTypeId("Camera2D"))
			DrawCamera2DDebug(selectedObject);
		else if (componentType.Type.Id == CReflectionRegistry::MakeTypeId("Text2D"))
		{
			const Text2D& text = *static_cast<const Text2D*>(component);
			const AssetGuid effective = text.FontFamilyGuid.IsNull() ? Runtime.DefaultFontFamilyGuid : text.FontFamilyGuid;
			if (effective.IsNull())
			{
				ImGui::Spacing();
				ImValidationMessage(Loc::Text(EditorLocKeys::InspectorText2dFontMissing), EImValidationSeverity::Warning).Draw();
				if (ImActionButton(Loc::Text(EditorLocKeys::InspectorText2dOpenFontSettings))
					.Severity(EImValidationSeverity::Warning)
					.Draw() && Editor::ProjectSettings)
				{
					Editor::ProjectSettings->SetVisible(true);
					Editor::ProjectSettings->Focus();
				}
			}
		}
	}

	bool SaveSpriteImportOptions(const AssetMetaData& metaData, const SpriteImportOptions& options)
	{
		SafePtr<IAssetManager> assetManager = EditorContext::GetAssetManager();
		if (false == assetManager.IsValid())
		{
			return false;
		}

		File::Path resolvedMetaPath;
		if (false == assetManager->ResolveAssetPath(metaData.MetaPath, resolvedMetaPath))
		{
			return false;
		}

		AssetMetaData updatedMetaData = metaData;
		updatedMetaData.ImportOptionsYaml = CSpriteImportOptions::ToYaml(options);
		if (false == CAssetMetaFile::Save(resolvedMetaPath, updatedMetaData))
		{
			return false;
		}

		// 자산이 이미 로드되어 있으면 자산이 자기 ImportOptions 를 in-place 갱신.
		// 자산 객체는 destroy 되지 않으므로 외부 SafePtr(씬/인스펙터 미리보기 등) 가 살아남는다.
		if (AssetRef<IAsset> loaded = assetManager->FindLoadedAsset(updatedMetaData.Guid))
		{
			loaded->ApplyImportOptions(updatedMetaData.ImportOptionsYaml);
		}
		return true;
	}

	void DrawSpriteImportOptions(const AssetMetaData& metaData)
	{
		// 매 프레임 디스크 yaml 로 덮어쓰면 사용자가 ImGui 에서 만진 값이 1프레임 만에 reset 된다.
		// 자산이 바뀔 때(혹은 처음)만 디스크 값에서 로드하고, 그 외에는 편집 중 값을 그대로 유지.
		// SaveSpriteImportOptions 가 디스크에 쓴 뒤에도 캐시 값과 디스크 값이 동일하므로 무해.
		static AssetGuid           s_cachedGuid;
		static SpriteImportOptions s_options;
		static bool                s_dirty = false;
		if (s_cachedGuid != metaData.Guid)
		{
			s_cachedGuid = metaData.Guid;
			s_options    = CSpriteImportOptions::FromYaml(metaData.ImportOptionsYaml);
			s_dirty      = false;
		}
		SpriteImportOptions& options = s_options;
		int rowCount    = static_cast<int>(options.RowCount);
		int columnCount = static_cast<int>(options.ColumnCount);
		int cellWidth   = static_cast<int>(options.CellWidth);
		int cellHeight  = static_cast<int>(options.CellHeight);
		int marginX     = static_cast<int>(options.MarginX);
		int marginY     = static_cast<int>(options.MarginY);
		int gapX        = static_cast<int>(options.GapX);
		int gapY        = static_cast<int>(options.GapY);

		ImSectionHeader(Loc::Text(EditorLocKeys::InspectorSpriteImportOptions)).Draw();

		ImGui::Utillity::FormLayout layout("##sprite_import_options");
		bool changed = false;

		// ── 슬라이스 모드 콤보 ─────────────────────────────────────────────────
		const char* sliceItems[] = {
			Loc::Text(EditorLocKeys::InspectorSliceTypeNone),
			Loc::Text(EditorLocKeys::InspectorSliceTypeAutomatic),
			Loc::Text(EditorLocKeys::InspectorSliceTypeCellSize),
			Loc::Text(EditorLocKeys::InspectorSliceTypeCellCount),
		};
		int sliceTypeIndex = static_cast<int>(options.SliceType);
		layout.Row(
			[&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorSliceType)); },
			[&]()
			{
				if (ImGui::Combo("##inspector.slice_type", &sliceTypeIndex, sliceItems, IM_ARRAYSIZE(sliceItems)))
				{
					changed = true;
				}
			});
		options.SliceType = static_cast<ESpriteSliceType>(sliceTypeIndex);

		// ── 모드별 입력란 ─────────────────────────────────────────────────────
		if (ESpriteSliceType::CellCount == options.SliceType)
		{
			layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorRowCount)); },    [&]() { changed |= ImGui::InputInt("##inspector.row_count", &rowCount); });
			layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorColumnCount)); }, [&]() { changed |= ImGui::InputInt("##inspector.column_count", &columnCount); });
		}
		else if (ESpriteSliceType::CellSize == options.SliceType)
		{
			layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorCellWidth)); },  [&]() { changed |= ImGui::InputInt("##inspector.cell_width", &cellWidth); });
			layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorCellHeight)); }, [&]() { changed |= ImGui::InputInt("##inspector.cell_height", &cellHeight); });
		}

		// ── 그리드/여백 (None/Automatic 에서는 의미 없으므로 슬라이스 모드일 때만) ─
		if (ESpriteSliceType::CellSize == options.SliceType || ESpriteSliceType::CellCount == options.SliceType)
		{
			layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorMarginX)); }, [&]() { changed |= ImGui::InputInt("##inspector.margin_x", &marginX); });
			layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorMarginY)); }, [&]() { changed |= ImGui::InputInt("##inspector.margin_y", &marginY); });
			layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorGapX)); },    [&]() { changed |= ImGui::InputInt("##inspector.gap_x", &gapX); });
			layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorGapY)); },    [&]() { changed |= ImGui::InputInt("##inspector.gap_y", &gapY); });
		}

		SafePtr<CProjectManager> projectManager = EditorContext::GetProjectManager();
		const float projectPPU = projectManager ? projectManager->GetPixelsPerUnit() : 0.0f;

		// ── 공용: 피벗/PPU ────────────────────────────────────────────────────
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorPivotX)); },         [&]() { changed |= ImGui::DragFloat("##inspector.pivot_x", &options.PivotX, 0.01f, 0.0f, 1.0f); });
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorPivotY)); },         [&]() { changed |= ImGui::DragFloat("##inspector.pivot_y", &options.PivotY, 0.01f, 0.0f, 1.0f); });
		layout.Row(
			[&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorPixelsPerUnit)); },
			[&]() {
				// 0 = 프로젝트 기본값 사용. 0 보다 큰 값이면 그 값으로 오버라이드.
				changed |= ImGui::DragFloat("##inspector.pixels_per_unit", &options.PixelsPerUnit, 1.0f, 0.0f, 10000.0f);
				if (options.PixelsPerUnit <= 0.0f)
				{
					ImGui::SameLine();
					ImGui::TextDisabled("%.1f %s", projectPPU, Loc::Text(EditorLocKeys::InspectorPpuProjectDefaultSuffix));
				}
			}
		);

		options.RowCount    = static_cast<std::uint32_t>(std::max(1, rowCount));
		options.ColumnCount = static_cast<std::uint32_t>(std::max(1, columnCount));
		options.CellWidth   = static_cast<std::uint32_t>(std::max(1, cellWidth));
		options.CellHeight  = static_cast<std::uint32_t>(std::max(1, cellHeight));
		options.MarginX     = static_cast<std::uint32_t>(std::max(0, marginX));
		options.MarginY     = static_cast<std::uint32_t>(std::max(0, marginY));
		options.GapX        = static_cast<std::uint32_t>(std::max(0, gapX));
		options.GapY        = static_cast<std::uint32_t>(std::max(0, gapY));

		SafePtr<IAssetManager> assetManager = EditorContext::GetAssetManager();
		std::uint32_t textureWidth = 0;
		std::uint32_t textureHeight = 0;
		if (assetManager)
		{
			if (AssetRef<IAsset> loadedAsset = assetManager->LoadAsset(metaData.Guid))
			{
				if (EAssetType::Sprite == loadedAsset->GetAssetType())
				{
					CSpriteAsset* spriteAsset = static_cast<CSpriteAsset*>(loadedAsset.Get());
					textureWidth = spriteAsset ? spriteAsset->GetWidth() : 0;
					textureHeight = spriteAsset ? spriteAsset->GetHeight() : 0;
				}
			}
		}

		const std::vector<SpriteFrame> previewFrames = CSpriteImportOptions::BuildFrames(textureWidth, textureHeight, options);
		DrawReadOnlyUInt(Loc::Text(EditorLocKeys::InspectorSpritePreviewFrameCount), static_cast<std::uint32_t>(previewFrames.size()));
		if (false == previewFrames.empty())
		{
			DrawReadOnlyUInt(Loc::Text(EditorLocKeys::InspectorSpriteFrameWidth), previewFrames.front().Width);
			DrawReadOnlyUInt(Loc::Text(EditorLocKeys::InspectorSpriteFrameHeight), previewFrames.front().Height);
		}

		if (changed)
		{
			s_dirty = true;
		}

		ImGui::BeginDisabled(false == s_dirty);
		if (ImActionButton(Loc::Text(EditorLocKeys::InspectorApplySpriteImportOptions))
			.Severity(EImValidationSeverity::Success)
			.Draw())
		{
			if (SaveSpriteImportOptions(metaData, options))
			{
				s_dirty = false;
			}
		}
		ImGui::EndDisabled();
	}

	// ── 사운드 자산 임포트 옵션 ──────────────────────────────────────────────
	bool SaveAudioImportOptions(const AssetMetaData& metaData, const AudioImportOptions& options)
	{
		SafePtr<IAssetManager> assetManager = EditorContext::GetAssetManager();
		if (false == assetManager.IsValid()) return false;

		File::Path resolvedMetaPath;
		if (false == assetManager->ResolveAssetPath(metaData.MetaPath, resolvedMetaPath)) return false;

		AssetMetaData updatedMetaData = metaData;
		updatedMetaData.ImportOptionsYaml = CAudioImportOptions::ToYaml(options);
		if (false == CAssetMetaFile::Save(resolvedMetaPath, updatedMetaData)) return false;

		// 자산이 이미 로드되어 있으면 자산이 자기 ImportOptions 를 in-place 갱신.
		// 자산 객체는 destroy 되지 않으므로 외부 SafePtr(미리듣기 등) 가 살아남는다.
		if (AssetRef<IAsset> loaded = assetManager->FindLoadedAsset(updatedMetaData.Guid))
		{
			loaded->ApplyImportOptions(updatedMetaData.ImportOptionsYaml);
		}
		return true;
	}

	void DrawAudioImportOptions(const AssetMetaData& metaData)
	{
		// SpriteImportOptions 와 동일한 캐시 + dirty 패턴.
		static AssetGuid          s_cachedGuid;
		static AudioImportOptions s_options;
		static bool               s_dirty = false;
		if (s_cachedGuid != metaData.Guid)
		{
			s_cachedGuid = metaData.Guid;
			s_options    = CAudioImportOptions::FromYaml(metaData.ImportOptionsYaml);
			s_dirty      = false;
		}
		AudioImportOptions& options = s_options;

		ImSectionHeader(Loc::Text(EditorLocKeys::InspectorAudioImportOptions)).Draw();

		ImGui::Utillity::FormLayout layout("##audio_import_options");
		bool changed = false;

		// ── 임포트 모드 (Decompressed / Streaming) ─────────────────────────
		const char* modeItems[] = {
			Loc::Text(EditorLocKeys::InspectorAudioModeDecompressed),
			Loc::Text(EditorLocKeys::InspectorAudioModeStreaming),
		};
		int modeIndex = static_cast<int>(options.Mode);
		layout.Row(
			[&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorAudioMode)); },
			[&]()
			{
				if (ImGui::Combo("##inspector.audio.mode", &modeIndex, modeItems, IM_ARRAYSIZE(modeItems)))
				{
					changed = true;
				}
			});
		options.Mode = static_cast<EAudioImportMode>(modeIndex);

		// ── 기본 버스 (Master / Music / SFX / Voice / UI / Custom) ─────────
		const char* busItems[] = {
			Loc::Text(EditorLocKeys::InspectorAudioBusMaster),
			Loc::Text(EditorLocKeys::InspectorAudioBusMusic),
			Loc::Text(EditorLocKeys::InspectorAudioBusSfx),
			Loc::Text(EditorLocKeys::InspectorAudioBusVoice),
			Loc::Text(EditorLocKeys::InspectorAudioBusUi),
			Loc::Text(EditorLocKeys::InspectorAudioBusCustom),
		};
		int busIndex = static_cast<int>(options.DefaultBus);
		layout.Row(
			[&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorAudioDefaultBus)); },
			[&]()
			{
				if (ImGui::Combo("##inspector.audio.bus", &busIndex, busItems, IM_ARRAYSIZE(busItems)))
				{
					changed = true;
				}
			});
		options.DefaultBus = static_cast<EAudioBusKind>(busIndex);

		// ── 기본 볼륨 / 루프 ──────────────────────────────────────────────
		layout.Row(
			[&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorAudioDefaultVolume)); },
			[&]() { changed |= ImGui::DragFloat("##inspector.audio.default_volume", &options.DefaultVolume, 0.01f, 0.0f, 2.0f); });
		layout.Row(
			[&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorAudioLoop)); },
			[&]() { changed |= ImGui::Checkbox("##inspector.audio.loop", &options.Loop); });

		// ── 3D 음향 ────────────────────────────────────────────────────────
		layout.Row(
			[&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorAudioIs3d)); },
			[&]() { changed |= ImGui::Checkbox("##inspector.audio.is_3d", &options.Is3D); });
		if (options.Is3D)
		{
			layout.Row(
				[&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorAudioMinDistance)); },
				[&]() { changed |= ImGui::DragFloat("##inspector.audio.min_distance", &options.MinDistance, 0.1f, 0.0f, 10000.0f); });
			layout.Row(
				[&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorAudioMaxDistance)); },
				[&]() { changed |= ImGui::DragFloat("##inspector.audio.max_distance", &options.MaxDistance, 0.1f, 0.0f, 10000.0f); });
			if (options.MinDistance < 0.0f) options.MinDistance = 0.0f;
			if (options.MaxDistance < options.MinDistance) options.MaxDistance = options.MinDistance;
		}

		// ── 정보 (포맷 / 길이) ─────────────────────────────────────────────
		SafePtr<IAssetManager> assetManager = EditorContext::GetAssetManager();
		if (assetManager)
		{
			if (AssetRef<IAsset> loadedAsset = assetManager->LoadAsset(metaData.Guid))
			{
				if (EAssetType::Audio == loadedAsset->GetAssetType())
				{
					CAudioAsset* audioAsset = static_cast<CAudioAsset*>(loadedAsset.Get());
					if (audioAsset)
					{
						const AudioFormatInfo& fmt = audioAsset->GetFormat();
						ImGui::TextDisabled("%s: %u Hz / %u ch",
							Loc::Text(EditorLocKeys::InspectorAudioFormat),
							fmt.SampleRate,
							static_cast<unsigned int>(fmt.Channels));
						ImGui::TextDisabled("%s: %.2f s",
							Loc::Text(EditorLocKeys::InspectorAudioDuration),
							audioAsset->GetDurationSeconds());
					}
				}
			}
		}

		if (changed) s_dirty = true;

		ImGui::BeginDisabled(false == s_dirty);
		if (ImActionButton(Loc::Text(EditorLocKeys::InspectorApplyAudioImportOptions))
			.Severity(EImValidationSeverity::Success)
			.Draw())
		{
			if (SaveAudioImportOptions(metaData, options))
			{
				s_dirty = false;
			}
		}
		ImGui::EndDisabled();
	}

	bool SaveMaterialImportOptions(const AssetMetaData& metaData, const MaterialImportOptions& options)
	{
		SafePtr<IAssetManager> assetManager = EditorContext::GetAssetManager();
		if (false == assetManager.IsValid()) return false;

		File::Path resolvedMetaPath;
		if (false == assetManager->ResolveAssetPath(metaData.MetaPath, resolvedMetaPath)) return false;

		AssetMetaData updatedMetaData = metaData;
		updatedMetaData.ImportOptionsYaml = CMaterialImportOptions::ToYaml(options);
		if (false == CAssetMetaFile::Save(resolvedMetaPath, updatedMetaData)) return false;

		if (AssetRef<IAsset> loaded = assetManager->FindLoadedAsset(updatedMetaData.Guid))
		{
			loaded->ApplyImportOptions(updatedMetaData.ImportOptionsYaml);
		}
		return true;
	}

	void DrawMaterialImportOptions(const AssetMetaData& metaData)
	{
		static AssetGuid             s_cachedGuid;
		static MaterialImportOptions s_options;
		static bool                  s_dirty = false;
		if (s_cachedGuid != metaData.Guid)
		{
			s_cachedGuid = metaData.Guid;
			s_options = CMaterialImportOptions::FromYaml(metaData.ImportOptionsYaml);
			s_dirty = false;
		}

		MaterialImportOptions& options = s_options;
		ImSectionHeader(Loc::Text(EditorLocKeys::InspectorMaterialImportOptions)).Draw();

		ImGui::Utillity::FormLayout layout("##material_import_options");
		bool changed = false;

		const char* queueItems[] = {
			CMaterialImportOptions::QueueToString(ERenderQueue::Background),
			CMaterialImportOptions::QueueToString(ERenderQueue::Opaque),
			CMaterialImportOptions::QueueToString(ERenderQueue::Transparent),
			CMaterialImportOptions::QueueToString(ERenderQueue::Overlay),
		};
		int queueIndex = 2;
		switch (options.Queue)
		{
		case ERenderQueue::Background:
			queueIndex = 0;
			break;
		case ERenderQueue::Opaque:
			queueIndex = 1;
			break;
		case ERenderQueue::Overlay:
			queueIndex = 3;
			break;
		case ERenderQueue::Transparent:
		default:
			queueIndex = 2;
			break;
		}

		layout.Row(
			[&]() {
				ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorMaterialQueue));
				ImGui::Utillity::HoveredToolTip(Loc::Text(EditorLocKeys::InspectorMaterialQueueDesc));
			},
			[&]() {
				if (ImGui::Combo("##inspector.material.queue", &queueIndex, queueItems, IM_ARRAYSIZE(queueItems)))
				{
					changed = true;
				}
			});

		switch (queueIndex)
		{
		case 0:
			options.Queue = ERenderQueue::Background;
			break;
		case 1:
			options.Queue = ERenderQueue::Opaque;
			break;
		case 3:
			options.Queue = ERenderQueue::Overlay;
			break;
		case 2:
		default:
			options.Queue = ERenderQueue::Transparent;
			break;
		}

		if (changed) s_dirty = true;

		ImGui::BeginDisabled(false == s_dirty);
		if (ImActionButton(Loc::Text(EditorLocKeys::InspectorApplyMaterialImportOptions))
			.Severity(EImValidationSeverity::Success)
			.Draw())
		{
			if (SaveMaterialImportOptions(metaData, options))
			{
				s_dirty = false;
			}
		}
		ImGui::EndDisabled();
	}


	// 이름이 무효(식별자 아님/예약어/목록 내 중복)인지.
	bool IsSchemaNameInvalid(const std::string& name, const std::vector<ScriptSchema::Property>& all)
	{
		if (false == ScriptSchema::IsValidIdentifier(name) || ScriptSchema::IsReservedName(name))
		{
			return true;
		}
		int sameCount = 0;
		for (const ScriptSchema::Property& o : all) { if (o.Name == name) ++sameCount; }
		return sameCount > 1;
	}

	// 스크립트 .h 선택 시 — 프로퍼티 스키마 편집(타입/이름/⋮메뉴/순서/추가삭제) + Apply.
	bool DrawSelectedScriptInspector()
	{
		const File::Path& path = Editor::GetSelectedScriptPath();
		if (path.IsNull())
		{
			return false;
		}

		// 선택이 바뀐 프레임에만 .h 를 파싱해 작업본을 채운다(편집 중 덮어쓰기 방지).
		static File::Path                          s_loadedPath;
		static bool                                s_parseOk = false;
		static std::string                         s_className;
		static std::vector<ScriptSchema::Property> s_props;
		static std::string                         s_status;

		if (path != s_loadedPath)
		{
			s_loadedPath = path;
			s_status.clear();
			const ScriptSchema::ParsedScript parsed = ScriptSchema::ParseHeaderFile(path);
			s_parseOk   = parsed.Found;
			s_className = parsed.ClassName;
			s_props     = parsed.Properties;
		}

		ImGui::Text("%s: %s", Loc::Text(EditorLocKeys::CommonPath), path.filename().generic_string().c_str());
		if (false == s_parseOk)
		{
			ImGui::TextDisabled(Loc::Text(EditorLocKeys::InspectorScriptNotParsed));
			return true;
		}
		ImGui::Text("%s: %s", Loc::Text(EditorLocKeys::InspectorScriptClass), s_className.c_str());
		ImGui::Separator();
		ImGui::TextUnformatted(Loc::Text(EditorLocKeys::AssetBrowserScriptPopupProperties));

		// ── 프로퍼티 목록(공유 행 위젯). 인스펙터에선 이름 read-only ──
		ImList<ScriptSchema::Property>(
			"##script_schema", s_props,
			[](ScriptSchema::Property& p, int /*idx*/)
			{
				ScriptSchemaUI::DrawPropertyRow(p, IsSchemaNameInvalid(p.Name, s_props), /*nameReadOnly*/ true);
			},
			ScriptSchema::Property{});

		// 전체 유효성(모든 이름이 유효+유일).
		bool allValid = true;
		for (const ScriptSchema::Property& p : s_props)
		{
			if (IsSchemaNameInvalid(p.Name, s_props)) { allValid = false; break; }
		}
		if (false == allValid)
		{
			ImValidationMessage(Loc::Text(EditorLocKeys::AssetBrowserScriptPopupInvalidProps), EImValidationSeverity::Error).Draw();
		}

		ImGui::Spacing();
		ImGui::Separator();

		// ── Apply / Reload ────────────────────────────────────────────────────
		if (ImActionButton(Loc::Text(EditorLocKeys::CommonApply))
			.Severity(EImValidationSeverity::Success)
			.Disabled(false == allValid)
			.Draw())
		{
			if (ScriptSchema::WriteHeaderFile(path, s_className, s_props))
			{
				if (SafePtr<CProjectManager> pm = EditorContext::GetProjectManager())
				{
					pm->RegenerateScriptProject();
				}
				s_status     = Loc::Text(EditorLocKeys::InspectorScriptApplyOk);
				s_loadedPath = File::NULL_PATH;   // 정규화된 파일 재파싱
			}
			else
			{
				s_status = Loc::Text(EditorLocKeys::InspectorScriptApplyFail);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button(Loc::Text(EditorLocKeys::CommonRefresh)))
		{
			s_loadedPath = File::NULL_PATH;   // 디스크에서 다시 읽기(편집 취소)
		}
		if (false == s_status.empty())
		{
			ImGui::TextDisabled("%s", s_status.c_str());
		}
		return true;
	}

	void DrawFontFamilyEditor(const AssetMetaData& metaData, IAssetManager& assetManager)
	{
		static AssetGuid cachedGuid;
		static FontFamilyData data;
		static bool dirty = false;
		if (cachedGuid != metaData.Guid)
		{
			cachedGuid = metaData.Guid; data = {}; dirty = false;
			if (AssetRef<IAsset> asset = assetManager.LoadAsset(metaData.Guid))
			{
				if (asset->GetAssetType() == EAssetType::FontFamily)
					data = static_cast<CFontFamilyAsset*>(asset.Get())->GetData();
			}
		}
		auto drawAsset = [](const char* id, AssetGuid& guid, EAssetType expected)
		{
			return ImAssetField(id, guid)
				.Type(expected)
				.Draw();
		};

		ImSectionHeader(Loc::Text(EditorLocKeys::InspectorFontFamilyTitle)).Draw();
		ImGui::Utillity::FormLayout layout("##font_family");
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorFontFamilyRegular)); }, [&]() { ImGui::PushID("regular"); dirty |= drawAsset("##regular", data.Regular, EAssetType::FontFace); ImGui::PopID(); });
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorFontFamilyBold)); }, [&]() { ImGui::PushID("bold"); dirty |= drawAsset("##bold", data.Bold, EAssetType::FontFace); ImGui::PopID(); });
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorFontFamilyItalic)); }, [&]() { ImGui::PushID("italic"); dirty |= drawAsset("##italic", data.Italic, EAssetType::FontFace); ImGui::PopID(); });
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorFontFamilyBoldItalic)); }, [&]() { ImGui::PushID("bolditalic"); dirty |= drawAsset("##bolditalic", data.BoldItalic, EAssetType::FontFace); ImGui::PopID(); });
		dirty |= ImGui::Checkbox(Loc::Text(EditorLocKeys::InspectorFontFamilyUseProjectFallbacks), &data.UseProjectFallbacks);
		ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorFontFamilyLeadingFallbacks));
		for (std::size_t i = 0; i < data.FallbackFamilies.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i)); dirty |= drawAsset("##family_fallback", data.FallbackFamilies[i], EAssetType::FontFamily);
			ImGui::SameLine();
			if (ImGui::SmallButton(Loc::Text(EditorLocKeys::CommonRemove))) { data.FallbackFamilies.erase(data.FallbackFamilies.begin() + static_cast<std::ptrdiff_t>(i)); dirty = true; ImGui::PopID(); break; }
			ImGui::PopID();
		}
		if (ImGui::Button(Loc::Text(EditorLocKeys::InspectorFontFamilyAddFallback))) { data.FallbackFamilies.push_back(INVALID_ASSET_GUID); dirty = true; }
		ImGui::BeginDisabled(false == dirty);
		if (ImActionButton(Loc::Text(EditorLocKeys::InspectorFontFamilyApply))
			.Severity(EImValidationSeverity::Success)
			.Draw())
		{
			std::ostringstream out;
			out << "Regular: " << data.Regular.generic_string() << '\n';
			out << "Bold: " << data.Bold.generic_string() << '\n';
			out << "Italic: " << data.Italic.generic_string() << '\n';
			out << "BoldItalic: " << data.BoldItalic.generic_string() << '\n';
			out << "UseProjectFallbacks: " << (data.UseProjectFallbacks ? "true" : "false") << '\n';
			out << "FallbackFamilies:\n";
			for (const AssetGuid& guid : data.FallbackFamilies) if (false == guid.IsNull()) out << "  - " << guid.generic_string() << '\n';
			const File::Path path = File::ResolvePath(metaData.Guid);
			std::ofstream stream(path, std::ios::binary | std::ios::trunc);
			if (stream.is_open()) { stream << out.str(); stream.close(); assetManager.ReloadAsset(metaData.Guid); dirty = false; }
		}
		ImGui::EndDisabled();
	}

	void DrawFontFaceImportOptions(const AssetMetaData& metaData, IAssetManager& assetManager)
	{
		static AssetGuid cachedGuid;
		static int faceIndex = 0;
		static bool dirty = false;
		static bool saved = false;
		static std::string familyStatus;
		if (cachedGuid != metaData.Guid)
		{
			cachedGuid = metaData.Guid; faceIndex = 0; dirty = false; saved = false; familyStatus.clear();
			const std::string& yaml = metaData.ImportOptionsYaml;
			const std::size_t key = yaml.find("FaceIndex:");
			if (key != std::string::npos)
			{
				try { faceIndex = std::max(0, std::stoi(yaml.substr(key + 10))); } catch (...) { faceIndex = 0; }
			}
		}
		ImSectionHeader(Loc::Text(EditorLocKeys::InspectorFontFaceTitle)).Draw();
		ImGui::Utillity::FormLayout layout("##font_face_import");
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorFontFaceFaceIndex)); },
			[&]() { if (ImGui::InputInt("##font_face.face_index", &faceIndex)) { faceIndex = std::max(0, faceIndex); dirty = true; saved = false; } });
		ImGui::BeginDisabled(false == dirty);
		if (ImActionButton(Loc::Text(EditorLocKeys::InspectorFontFaceApply))
			.Severity(EImValidationSeverity::Success)
			.Draw())
		{
			File::Path resolvedMetaPath;
			if (assetManager.ResolveAssetPath(metaData.MetaPath, resolvedMetaPath))
			{
				AssetMetaData updated = metaData;
				updated.ImportOptionsYaml = "FaceIndex: " + std::to_string(faceIndex);
				if (CAssetMetaFile::Save(resolvedMetaPath, updated)) { dirty = false; saved = true; }
			}
		}
		ImGui::EndDisabled();
		if (saved) ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::InspectorFontFaceApplied));

		ImGui::Spacing();
		if (ImGui::Button(Loc::Text(EditorLocKeys::InspectorFontFaceCreateFamily), ImVec2(-FLT_MIN, 0.0f)))
		{
			familyStatus = Loc::Text(EditorLocKeys::InspectorFontFaceFamilyCreateFailed);
			File::Path fontPath;
			SafePtr<CProjectManager> projectManager = EditorContext::GetProjectManager();
			if (projectManager && assetManager.ResolveAssetPath(metaData.Path, fontPath))
			{
				const std::filesystem::path source(fontPath);
				std::filesystem::path familyPath = source.parent_path() / (source.stem().string() + ".jfontfamily");
				for (int suffix = 2; std::filesystem::exists(familyPath) && suffix < 1000; ++suffix)
					familyPath = source.parent_path() / (source.stem().string() + " " + std::to_string(suffix) + ".jfontfamily");
				std::ofstream familyFile(familyPath, std::ios::binary | std::ios::trunc);
				if (familyFile.is_open())
				{
					familyFile << "Regular: " << metaData.Guid.generic_string() << "\n"
						<< "Bold: \nItalic: \nBoldItalic: \nUseProjectFallbacks: true\nFallbackFamilies: []\n";
					familyFile.close();
					std::string relativePath;
					AssetMetaData familyMeta;
					if (projectManager->TryMakeProjectAssetRelativePath(File::Path(familyPath), relativePath))
					{
						const std::string displayName = familyPath.stem().string();
						AssetImportDesc desc; desc.Type = EAssetType::FontFamily; desc.Path = File::Path(relativePath);
						desc.DisplayName = displayName.c_str(); desc.Importer = "FontFamily";
						if (assetManager.ImportAsset(desc, &familyMeta))
						{
							Editor::SelectAsset(familyMeta.Guid, File::Path(familyPath));
							familyStatus = Loc::Text(EditorLocKeys::InspectorFontFaceFamilyCreated);
						}
					}
				}
			}
		}
		if (false == familyStatus.empty()) ImGui::TextWrapped("%s", familyStatus.c_str());
	}

	// 경고 한 줄 — 이 파일의 캔버스/레이어 패널이 공유한다. 위젯으로 뺄 만큼 자라면
	// ImItem 으로 옮길 것(지금은 호출부가 둘뿐이고 기존 코드도 TextColored 를 직접 쓴다).
	void DrawInspectorWarning(const char* text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.25f, 1.0f));
		ImGui::TextWrapped("%s", text);
		ImGui::PopStyleColor();
	}

	// 뷰포트 카메라 콤보 항목. 카메라를 가진 오브젝트 전부(비활성 포함 — 지금 꺼져 있어도
	// 런타임에 켜질 수 있으니 지목 자체는 막지 않는다).
	struct CameraChoice
	{
		File::Guid  Guid;
		std::string Label;
	};

	std::vector<CameraChoice> CollectCameraChoices(CGameCanvas& scene)
	{
		std::vector<CameraChoice> choices;
		scene.ForEachObject([&choices](CGameObject& object)
		{
			if (nullptr == object.GetComponent<Camera2D>())
			{
				return;
			}
			CameraChoice choice;
			choice.Guid = object.GetInstanceGuid();
			choice.Label = object.GetName();
			choices.push_back(std::move(choice));
		});
		return choices;
	}

	// 폴백(뷰포트가 카메라를 안 고른 경우)이 "임의로 하나"를 고르게 되는지 판단용.
	int CountActiveCameras(CGameCanvas& scene)
	{
		int count = 0;
		scene.ForEach<Camera2D>([&count](Camera2D& camera)
		{
			if (IsActiveComponent(camera))
			{
				++count;
			}
		});
		return count;
	}

	// Static 레이어는 RT 를 한 번 그린 뒤 재그리기를 건너뛴다 — 시뮬은 계속 도니까 오브젝트가
	// 움직여도 화면은 그대로다. "움직인다" 판정: 스크립트가 붙어 있거나(무엇이든 할 수 있다),
	// Rigidbody2D 가 Static 이 아니거나(물리가 옮긴다).
	// 스크립트 판정에 dynamic_cast 대신 리플렉션 조회를 쓴다 — 인스펙터는 매 프레임 돈다.
	int CountMovingObjectsInLayer(CGameCanvas& scene, const CGameLayer& layer, const CReflectionRegistry& reflection)
	{
		int count = 0;
		scene.ForEachObject([&](CGameObject& object)
		{
			if (object.GetLayer().TryGet() != &layer)
			{
				return;
			}

			if (const Rigidbody2D* body = object.GetComponent<Rigidbody2D>())
			{
				if (EPhysics2DBodyType::Static != body->BodyType)
				{
					++count;
					return;
				}
			}

			for (const SafePtr<CComponent>& componentRef : object.GetComponents())
			{
				const CComponent* component = componentRef.TryGet();
				if (component && reflection.FindScript(component->GetTypeId()))
				{
					++count;
					return;
				}
			}
		});
		return count;
	}

	// 뷰포트 하나의 속성 편집. 레이어 패널과 같은 규칙 — 라이브 대입이 아니라 스냅샷 교체다
	// (커맨드가 "적용 직전 값"을 old 로 캡처하므로 먼저 라이브로 바꾸면 undo 가 무효가 된다).
	void DrawViewportProperties(
		CGameCanvas& scene,
		std::size_t viewportIndex,
		const std::vector<CameraChoice>& cameras,
		int activeCameraCount)
	{
		CanvasViewport* viewport = scene.GetViewportAt(viewportIndex);
		if (nullptr == viewport)
		{
			return;
		}

		using EField = CSetViewportPropertyCommand::EField;
		auto apply = [&scene, viewportIndex](EField field, const ViewportSnapshot& properties)
		{
			EditorCanvasActions::SetViewportProperty(scene, viewportIndex, field, properties);
		};

		ImGui::Utillity::FormLayout layout("##viewport_properties", 4.0f, { 2.0f, 1.0f });

		// 이름은 편집이 끝날 때 1회만 커밋(레이어 이름과 같은 이유 — 글자마다 undo 금지).
		ImInputText nameInput("##viewport_name");
		nameInput.SetHintText(Loc::TextOr(EditorLocKeys::EditorPropertyName, "Name"));
		nameInput.SetSourceText(viewport->Name);
		layout.Row([&]() { ImGui::TextUnformatted(Loc::TextOr(EditorLocKeys::EditorPropertyName, "Name")); }, [&]() {
			nameInput();
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				ViewportSnapshot properties = ViewportSnapshot::Capture(*viewport);
				properties.Name = nameInput.GetString();
				apply(EField::Name, properties);
			}
		});

		bool active = viewport->Active;
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorViewportActive)); }, [&]() {
			if (ImGui::Checkbox("##inspector.viewport.active", &active))
			{
				ViewportSnapshot properties = ViewportSnapshot::Capture(*viewport);
				properties.Active = active;
				apply(EField::Active, properties);
			}
		});

		// 카메라 — 0번은 "자동"(폴백). 나머지는 카메라를 가진 오브젝트.
		std::vector<const char*> cameraItems;
		cameraItems.reserve(cameras.size() + 1);
		cameraItems.push_back(Loc::Text(EditorLocKeys::InspectorViewportCameraAuto));
		for (const CameraChoice& choice : cameras)
		{
			cameraItems.push_back(choice.Label.c_str());
		}

		int cameraIndex = 0;
		bool cameraResolved = viewport->CameraObjectGuid.IsNull();
		for (std::size_t i = 0; i < cameras.size(); ++i)
		{
			if (cameras[i].Guid == viewport->CameraObjectGuid)
			{
				cameraIndex = static_cast<int>(i) + 1;
				cameraResolved = true;
				break;
			}
		}

		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorViewportCamera)); }, [&]() {
			if (ImGui::Combo("##inspector.viewport.camera", &cameraIndex, cameraItems.data(), static_cast<int>(cameraItems.size())))
			{
				ViewportSnapshot properties = ViewportSnapshot::Capture(*viewport);
				properties.CameraObjectGuid = (0 == cameraIndex)
					? File::Guid()
					: cameras[static_cast<std::size_t>(cameraIndex) - 1].Guid;
				apply(EField::Camera, properties);
			}
		});

		// 렉트 = Layout2D 2개(비율 × 해상도 + 픽셀). 비율/픽셀을 각각 한 줄로 편집한다.
		auto drawLayoutRows = [&](const char* labelKey, const Layout2D& current, bool isSize)
		{
			char rowLabel[128];

			Vector2 normalized = current.Normalized;
			std::snprintf(rowLabel, sizeof(rowLabel), "%s (%s)",
				Loc::Text(labelKey), Loc::Text(EditorLocKeys::InspectorViewportRatio));
			layout.Row([&]() { ImGui::TextUnformatted(rowLabel); }, [&]() {
				if (ImGui::DragFloat2(isSize ? "##inspector.viewport.size.normalized" : "##inspector.viewport.pos.normalized",
					&normalized.x, 0.01f))
				{
					ViewportSnapshot properties = ViewportSnapshot::Capture(*viewport);
					(isSize ? properties.Size : properties.Position).Normalized = normalized;
					apply(EField::Rect, properties);
				}
			});

			Vector2 pixel = current.Pixel;
			std::snprintf(rowLabel, sizeof(rowLabel), "%s (%s)",
				Loc::Text(labelKey), Loc::Text(EditorLocKeys::InspectorViewportPixel));
			layout.Row([&]() { ImGui::TextUnformatted(rowLabel); }, [&]() {
				if (ImGui::DragFloat2(isSize ? "##inspector.viewport.size.pixel" : "##inspector.viewport.pos.pixel",
					&pixel.x, 1.0f))
				{
					ViewportSnapshot properties = ViewportSnapshot::Capture(*viewport);
					(isSize ? properties.Size : properties.Position).Pixel = pixel;
					apply(EField::Rect, properties);
				}
			});
		};

		drawLayoutRows(EditorLocKeys::InspectorViewportPosition, viewport->Position, false);
		drawLayoutRows(EditorLocKeys::InspectorViewportSize, viewport->Size, true);

		// 레이어 필터 — 비면 전체. 표시는 캔버스 뷰와 같은 포토샵식 역순(위 = 화면 최전면).
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorViewportLayerFilter)); }, [&]() {
			if (viewport->LayerFilter.empty())
			{
				ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::InspectorViewportLayerFilterAll));
			}
			else
			{
				ImGui::Text("%d / %d", static_cast<int>(viewport->LayerFilter.size()),
					static_cast<int>(scene.GetLayerCount()));
			}
		});

		for (std::size_t i = scene.GetLayerCount(); i > 0; --i)
		{
			CGameLayer* layer = scene.GetLayerAt(i - 1);
			if (nullptr == layer)
			{
				continue;
			}

			const File::Guid& layerGuid = layer->GetInstanceGuid();
			const auto found = std::find(viewport->LayerFilter.begin(), viewport->LayerFilter.end(), layerGuid);
			// 빈 필터 = 전체이므로 전부 체크된 것으로 보여준다.
			bool included = viewport->LayerFilter.empty() || found != viewport->LayerFilter.end();

			ImGui::Utillity::IDGroup layerIdGroup(static_cast<const void*>(layer));
			layout.Row([&]() { ImGui::TextDisabled("%s", layer->GetName()); }, [&]() {
				if (false == ImGui::Checkbox("##inspector.viewport.layer_filter.item", &included))
				{
					return;
				}

				ViewportSnapshot properties = ViewportSnapshot::Capture(*viewport);
				if (properties.LayerFilter.empty() && false == included)
				{
					// 전체(빈 목록)에서 하나를 빼는 순간 명시 목록으로 굳는다 — 나머지 전부.
					for (std::size_t other = 0; other < scene.GetLayerCount(); ++other)
					{
						CGameLayer* otherLayer = scene.GetLayerAt(other);
						if (otherLayer && otherLayer != layer)
						{
							properties.LayerFilter.push_back(otherLayer->GetInstanceGuid());
						}
					}
				}
				else if (included)
				{
					properties.LayerFilter.push_back(layerGuid);
				}
				else
				{
					const auto it = std::find(properties.LayerFilter.begin(), properties.LayerFilter.end(), layerGuid);
					if (it != properties.LayerFilter.end())
					{
						properties.LayerFilter.erase(it);
					}
				}
				apply(EField::LayerFilter, properties);
			});
		}

		if (false == cameraResolved)
		{
			DrawInspectorWarning(Loc::Text(EditorLocKeys::InspectorCanvasWarningCameraUnresolved));
		}
		else if (viewport->CameraObjectGuid.IsNull() && activeCameraCount > 1)
		{
			char warning[256];
			std::snprintf(warning, sizeof(warning),
				Loc::Text(EditorLocKeys::InspectorCanvasWarningCameraAmbiguous), activeCameraCount);
			DrawInspectorWarning(warning);
		}
	}

	// 캔버스 설정 — 배경색 + 뷰포트 목록. 캔버스 뷰의 캔버스 노드와 에셋 브라우저의 활성
	// `.jcanvas` 선택이 같은 패널로 들어온다.
	void DrawCanvasInspector(CGameCanvas& scene)
	{
		ImSectionHeader(Loc::Text(EditorLocKeys::InspectorCanvasProperties)).Draw();

		{
			ImGui::Utillity::FormLayout layout("##canvas_properties", 4.0f, { 2.0f, 1.0f });

			const float* current = scene.GetBackgroundColor();
			float background[4] = { current[0], current[1], current[2], current[3] };
			layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorCanvasBackgroundColor)); }, [&]() {
				if (ImGui::ColorEdit4("##inspector.canvas.background", background, ImGuiColorEditFlags_NoInputs))
				{
					EditorCanvasActions::SetBackgroundColor(scene, background);
				}
			});
		}

		ImGui::Spacing();
		ImSectionHeader(Loc::Text(EditorLocKeys::InspectorCanvasViewports)).Draw();

		const std::vector<CameraChoice> cameras = CollectCameraChoices(scene);
		const int activeCameraCount = CountActiveCameras(scene);

		// 삭제는 순회가 끝난 뒤에 — 목록을 순회 중에 줄이면 이후 인덱스가 어긋난다.
		bool        hasPendingDelete = false;
		std::size_t pendingDeleteIndex = 0;

		for (std::size_t i = 0; i < scene.GetViewportCount(); ++i)
		{
			const CanvasViewport* viewport = scene.GetViewportAt(i);
			if (nullptr == viewport)
			{
				continue;
			}

			// 이름이 겹쳐도 헤더 id 가 충돌하지 않게 인덱스로 스코프를 판다.
			ImGui::Utillity::IDGroup idGroup(i);
			if (ImGui::CollapsingHeader(viewport->Name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				DrawViewportProperties(scene, i, cameras, activeCameraCount);

				// 마지막 뷰포트는 삭제 불가 — 씬이 "뷰포트 0개"를 허용하지 않는다(레이어와 같은 규칙).
				const bool canDelete = scene.GetViewportCount() > 1;
				ImGui::BeginDisabled(false == canDelete);
				if (ImGui::Button(Loc::Text(EditorLocKeys::InspectorCanvasDeleteViewport)))
				{
					hasPendingDelete = true;
					pendingDeleteIndex = i;
				}
				ImGui::EndDisabled();
				ImGui::Spacing();
			}
		}

		if (hasPendingDelete)
		{
			auto command = MakeOwnerPtr<CDeleteViewportCommand>(scene.SafeFromThis(), pendingDeleteIndex);
			Editor::CommandManager.ExecuteCommand(std::move(command));
		}

		if (ImGui::Button(Loc::Text(EditorLocKeys::InspectorCanvasAddViewport), ImVec2(-FLT_MIN, 0.0f)))
		{
			auto command = MakeOwnerPtr<CCreateViewportCommand>(scene.SafeFromThis());
			Editor::CommandManager.ExecuteCommand(std::move(command));
		}
	}

	// 레이어 선택 시 컴포짓 속성 편집. 표시 중이면 true(호출자는 다른 패널을 그리지 않는다).
	// 편집은 라이브 대입이 아니라 스냅샷 교체로 한다 — 커맨드가 "적용 직전 값"을 old 로 캡처하므로
	// 먼저 라이브로 바꿔버리면 undo 가 새 값으로 되돌아간다(=무효).
	bool DrawSelectedLayerInspector(CGameCanvas& scene)
	{
		CGameLayer* layer = Editor::GetSelectedLayer();
		if (nullptr == layer)
		{
			return false;
		}

		ImSectionHeader(Loc::Text(EditorLocKeys::InspectorLayerProperties)).Draw();

		ImGui::Utillity::FormLayout layout("##layer_properties", 4.0f, { 2.0f, 1.0f });

		// 이 레이어가 `.jlayer` 에서 왔으면 어떤 에셋인지 보여준다. 읽기 전용이다 — 여기서
		// 에셋을 바꾸면 "다른 레이어로 갈아끼우기"가 되는데, 그건 오브젝트 교체까지 뜻하므로
		// 필드 하나로 처리할 일이 아니다(레이어 에셋 드롭 = 새 레이어 추가가 그 경로).
		if (false == layer->SourceAssetGuid.IsNull())
		{
			AssetGuid sourceAsset = layer->SourceAssetGuid;
			layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorLayerSourceAsset)); }, [&]() {
				// 읽기 전용이되 더블클릭 reveal 은 살린다 — BeginDisabled 로 감싸면 hover 판정이
				// 죽어 더블클릭이 안 먹으므로 AllowDrop(false)/AllowClear(false) 로 표현한다.
				ImAssetField("##inspector.layer.source_asset", sourceAsset)
					.Type(EAssetType::Layer)
					.AllowClear(false)
					.AllowDrop(false)
					.Draw();
			});
		}

		using EField = CSetLayerPropertyCommand::EField;
		auto apply = [&scene, layer](EField field, const LayerPropertySnapshot& properties)
		{
			EditorLayerActions::SetLayerProperty(scene, *layer, field, properties);
		};

		// 이름은 편집이 끝날 때 1회만 커맨드로 커밋한다 — 글자마다 커밋하면 undo 가 글자 수만큼
		// 쌓인다(드래그 병합은 마우스 기준이라 타이핑에는 걸리지 않는다).
		ImInputText nameInput("##layer_name");
		nameInput.SetHintText(Loc::TextOr(EditorLocKeys::EditorPropertyName, "Name"));
		nameInput.SetSourceText(layer->GetName());
		layout.Row([&]() { ImGui::TextUnformatted(Loc::TextOr(EditorLocKeys::EditorPropertyName, "Name")); }, [&]() {
			nameInput();
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				LayerPropertySnapshot properties = LayerPropertySnapshot::Capture(*layer);
				properties.Name = nameInput.GetString();
				apply(EField::Name, properties);
			}
		});

		// 블렌드 항목 순서 = ELayerBlendMode 값 순서(Normal/Additive/Multiply/Screen).
		const char* blendItems[] = {
			Loc::Text(EditorLocKeys::InspectorLayerBlendNormal),
			Loc::Text(EditorLocKeys::InspectorLayerBlendAdditive),
			Loc::Text(EditorLocKeys::InspectorLayerBlendMultiply),
			Loc::Text(EditorLocKeys::InspectorLayerBlendScreen),
		};
		int blendIndex = static_cast<int>(layer->BlendMode);
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorLayerBlendMode)); }, [&]() {
			if (ImGui::Combo("##inspector.layer.blend", &blendIndex, blendItems, IM_ARRAYSIZE(blendItems)))
			{
				LayerPropertySnapshot properties = LayerPropertySnapshot::Capture(*layer);
				properties.BlendMode = static_cast<ELayerBlendMode>(blendIndex);
				apply(EField::BlendMode, properties);
			}
		});

		float opacity = layer->Opacity;
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorLayerOpacity)); }, [&]() {
			if (ImGui::SliderFloat("##inspector.layer.opacity", &opacity, 0.0f, 1.0f))
			{
				LayerPropertySnapshot properties = LayerPropertySnapshot::Capture(*layer);
				properties.Opacity = opacity;
				apply(EField::Opacity, properties);
			}
		});

		bool visible = layer->Visible;
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorLayerVisible)); }, [&]() {
			if (ImGui::Checkbox("##inspector.layer.visible", &visible))
			{
				LayerPropertySnapshot properties = LayerPropertySnapshot::Capture(*layer);
				properties.Visible = visible;
				apply(EField::Visible, properties);
			}
		});

		bool isStatic = layer->Static;
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorLayerStatic)); }, [&]() {
			if (ImGui::Checkbox("##inspector.layer.static", &isStatic))
			{
				LayerPropertySnapshot properties = LayerPropertySnapshot::Capture(*layer);
				properties.Static = isStatic;
				apply(EField::Static, properties);
			}
		});

		bool forceOwnTexture = layer->ForceOwnTexture;
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorLayerForceOwnTexture)); }, [&]() {
			if (ImGui::Checkbox("##inspector.layer.force_own_texture", &forceOwnTexture))
			{
				LayerPropertySnapshot properties = LayerPropertySnapshot::Capture(*layer);
				properties.ForceOwnTexture = forceOwnTexture;
				apply(EField::ForceOwnTexture, properties);
			}
		});

		float parallax = layer->ParallaxFactor;
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorLayerParallax)); }, [&]() {
			if (ImGui::DragFloat("##inspector.layer.parallax", &parallax, 0.01f))
			{
				LayerPropertySnapshot properties = LayerPropertySnapshot::Capture(*layer);
				properties.ParallaxFactor = parallax;
				apply(EField::ParallaxFactor, properties);
			}
		});

		// 승계는 `.jlayer` 에서 온 레이어만 가능하다 — 인라인 레이어는 전환 후 그 인스턴스를
		// 지목할 방법이 없어(파일 신원이 없다) 언제나 새로 로드된다. 그래서 에셋 레이어에만 띄운다.
		if (false == layer->SourceAssetGuid.IsNull())
		{
			bool keepOnCanvasChange = layer->KeepOnCanvasChange;
			layout.Row([&]() { ImGui::TextUnformatted(Loc::Text(EditorLocKeys::InspectorLayerKeepOnCanvasChange)); }, [&]() {
				if (ImGui::Checkbox("##inspector.layer.keep_on_canvas_change", &keepOnCanvasChange))
				{
					LayerPropertySnapshot properties = LayerPropertySnapshot::Capture(*layer);
					properties.KeepOnCanvasChange = keepOnCanvasChange;
					apply(EField::KeepOnCanvasChange, properties);
				}
				ImGui::SetItemTooltip("%s", Loc::Text(EditorLocKeys::InspectorLayerKeepOnCanvasChangeTooltip));
			});
		}

		// Static 은 렌더 동결이라 그 안의 움직이는 오브젝트는 화면에 반영되지 않는다.
		if (layer->Static && Engine.Reflection.IsValid())
		{
			const int movingCount = CountMovingObjectsInLayer(scene, *layer, *Engine.Reflection);
			if (movingCount > 0)
			{
				char warning[256];
				std::snprintf(warning, sizeof(warning),
					Loc::Text(EditorLocKeys::InspectorLayerWarningStaticDynamic), movingCount);
				DrawInspectorWarning(warning);
			}
		}

		return true;
	}

	// 선택한 `.jcanvas` 가 지금 열려 있는 캔버스인지. 캔버스 설정은 런타임 객체(활성 씬)를
	// 편집하는 것이라, 열지 않은 파일은 편집 대상이 없다 — 안내 + 열기 버튼만 준다.
	bool IsActiveCanvasAsset(const AssetMetaData& metaData)
	{
		const File::Path& activePath = Editor::GetActiveScenePath();
		if (activePath.empty())
		{
			return false;
		}

		SafePtr<CProjectManager> projectManager = EditorContext::GetProjectManager();
		if (false == projectManager.IsValid())
		{
			return false;
		}

		std::string activeRelative;
		if (false == projectManager->TryMakeProjectAssetRelativePath(activePath, activeRelative))
		{
			return false;
		}
		return activeRelative == metaData.Path.generic_string();
	}

	bool DrawSelectedAssetInspector(CGameCanvas& scene)
	{
		const File::Guid& selectedGuid = Editor::GetSelectedAssetGuid();
		if (selectedGuid.IsNull())
		{
			return false;
		}

		SafePtr<IAssetManager> assetManager = EditorContext::GetAssetManager();
		if (false == assetManager.IsValid())
		{
			ImGui::TextDisabled(Loc::Text(EditorLocKeys::InspectorAssetManagerUnavailable));
			return true;
		}

		AssetMetaData metaData;
		if (false == assetManager->GetRegistry().TryGetAsset(selectedGuid, metaData))
		{
			ImGui::TextDisabled(Loc::Text(EditorLocKeys::InspectorSelectedAssetNotRegistered));
			return true;
		}

		// ── 맨 위 미리보기 영역 (Sprite=이미지, Audio=Play/Stop 등) ─────────
		// DrawTopPreview 가 핸들러의 Enter/Stay/Exit 라이프사이클을 자체적으로 관리.
		if (AssetInspectorPreview::DrawTopPreview(metaData))
		{
			ImGui::Separator();
		}

		ImGui::Text("%s: %s", Loc::Text(EditorLocKeys::CommonAsset), metaData.DisplayName.c_str());
		ImGui::Text("%s: %s", Loc::Text(EditorLocKeys::CommonGuid), metaData.Guid.generic_string().c_str());
		ImGui::Text("%s: %s", Loc::Text(EditorLocKeys::CommonPath), metaData.Path.generic_string().c_str());
		ImGui::Text("%s: %s", Loc::Text(EditorLocKeys::CommonImporter), metaData.Importer.c_str());
		ImGui::Separator();

		if (EAssetType::Sprite == metaData.Type)
		{
			DrawSpriteImportOptions(metaData);
		}
		else if (EAssetType::Audio == metaData.Type)
		{
			DrawAudioImportOptions(metaData);
		}
		else if (EAssetType::Material == metaData.Type)
		{
			DrawMaterialImportOptions(metaData);
		}
		else if (EAssetType::AudioEffect == metaData.Type)
		{
			// 효과 편집은 전용 독윈도우에서 한다. 인스펙터엔 안내 + 에디터 열기 버튼.
			ImGui::TextDisabled(Loc::Text(EditorLocKeys::InspectorEffectOpenInWindow));
			ImGui::Spacing();
			if (ImGui::Button(Loc::Text(EditorLocKeys::InspectorEffectOpenEditor), ImVec2(-FLT_MIN, 0.0f)))
			{
				EffectEditorWindow::Open(metaData.Guid, metaData.DisplayName);
			}
		}
		else if (EAssetType::FontFace == metaData.Type)
		{
			DrawFontFaceImportOptions(metaData, *assetManager);
		}
		else if (EAssetType::FontFamily == metaData.Type)
		{
			DrawFontFamilyEditor(metaData, *assetManager);
		}
		else if (EAssetType::Scene == metaData.Type)
		{
			// 캔버스 = 임포트 옵션이 아니라 저작 데이터. 열려 있으면 캔버스 뷰의 캔버스 노드와
			// 같은 패널을 여기서도 띄운다(사용자 확정: 선택 경로는 양쪽 다).
			if (IsActiveCanvasAsset(metaData))
			{
				DrawCanvasInspector(scene);
			}
			else
			{
				// 여는 경로는 에셋 브라우저 더블클릭(CCanvasAssetOpenHandler) 하나뿐이다 —
				// 여기 열기 버튼을 두려면 그 로직을 AssetBrowserEntry 에서 떼어내야 해서
				// 지금은 안내만 한다.
				ImGui::TextWrapped("%s", Loc::Text(EditorLocKeys::InspectorCanvasOpenToEdit));
			}
		}
		else
		{
			ImGui::TextDisabled(Loc::Text(EditorLocKeys::InspectorNoEditableImportOptions));
		}

		return true;
	}
}

void CInspectorTool::OnCreate()
{
	SetLocalizedTitleKey(EditorLocKeys::WindowInspector);
	// 스크롤은 허용하되 스크롤바는 표시하지 않음
	m_imguiFlags |= ImGuiWindowFlags_NoScrollbar;
}

void CInspectorTool::OnDestroy()
{
	// 미리보기 핸들러가 잡고 있던 리소스(특히 오디오 디바이스/플레이어) 해제.
	AssetInspectorPreview::ShutdownAll();
}

void CInspectorTool::OnRenderStay()
{
	// 매 프레임 초기화: 컴포넌트 미표시 상태가 기본값
	m_activeComponentTypeName = nullptr;

	CGameCanvas* scene = EditorContext::TryGetActiveScene();
	if (nullptr == scene)
	{
		AssetInspectorPreview::NotifyInspectionLost();
		ImGui::TextDisabled(Loc::Text(EditorLocKeys::InspectorNoActiveScene));
		return;
	}

	CGameObject* selectedObject = Editor::GetSelectedEntity();
	if (nullptr == selectedObject)
	{
		// 캔버스 노드 선택 — 캔버스 설정 패널(에셋 브라우저 경로는 아래 에셋 패널이 분기).
		if (Editor::IsCanvasSelected())
		{
			AssetInspectorPreview::NotifyInspectionLost();
			DrawCanvasInspector(*scene);
			return;
		}
		// 레이어 선택 — 컴포짓 속성 패널.
		if (DrawSelectedLayerInspector(*scene))
		{
			AssetInspectorPreview::NotifyInspectionLost();
			return;
		}
		// 스크립트 .h 선택 — 스키마 에디터.
		if (DrawSelectedScriptInspector())
		{
			AssetInspectorPreview::NotifyInspectionLost();
			return;
		}
		if (DrawSelectedAssetInspector(*scene))
			return;
		// 자산도 엔티티도 없음 — 활성 미리보기 핸들러 정리.
		AssetInspectorPreview::NotifyInspectionLost();
		ImGui::TextDisabled(Loc::Text(EditorLocKeys::InspectorNoEntitySelected));
		return;
	}

	// 엔티티가 선택됐다 — 자산 미리보기 영역은 그리지 않는다. 활성 핸들러 정리.
	AssetInspectorPreview::NotifyInspectionLost();

	if (false == Engine.Reflection.IsValid())
	{
		ImGui::TextDisabled(Loc::Text(EditorLocKeys::InspectorReflectionUnavailable));
		return;
	}

	CReflectionRegistry& reflection = *Engine.Reflection;

	// ── 컴포넌트 수집 ─────────────────────────────────────────────────────────
	// GameObject/Transform 은 더 이상 컴포넌트가 아님(CGameObject 멤버) → 상단 인라인.
	// 나머지 전체: 좌측 목록에 표시. 단일 인스턴스(타입당 1개).
	struct ComponentEntry
	{
		const ComponentTypeInfo* typeInfo;
		std::size_t              typeIndex;
		std::vector<void*>       instances;
	};

	std::vector<ComponentEntry> allEntries; // 임시 보관 (포인터 안정성)

	for (std::size_t i = 0; i < reflection.GetComponentTypeCount(); ++i)
	{
		const ComponentTypeInfo* ct = reflection.GetComponentType(i);
		if (!ct)
			continue;

		// 멀티 컴포넌트: 같은 타입 인스턴스를 전부 모은다(없으면 스킵).
		std::vector<void*> instances = reflection.GetComponentAddresses(*selectedObject, ct->Type.Id);
		if (instances.empty())
			continue;

		allEntries.push_back({ ct, i, std::move(instances) });
	}

	// ── GameObject 인라인 표시 (CGameObject 직접 편집) ──────────────────────────
	{
		ImGui::Utillity::FormLayout layout("##game_object_inline", 4.0f, { 2.0f, 1.0f });
		layout.Row([&]() { ImGui::TextUnformatted(Loc::TextOr(EditorLocKeys::EditorPropertyIsActive, "Active")); }, [&]() {
			ImGui::Checkbox("##editor.property.IsActive", &selectedObject->IsActive);
		});

		ImInputText nameInput("##go_name");
		nameInput.SetHintText(Loc::TextOr(EditorLocKeys::EditorPropertyName, "Name"));
		nameInput.SetText(selectedObject->GetName());
		layout.Row([&]() { ImGui::TextUnformatted(Loc::TextOr(EditorLocKeys::EditorPropertyName, "Name")); }, [&]() {
			if (nameInput())
			{
				selectedObject->SetName(nameInput);
			}
		});

		ImInputText tagInput("##go_tag");
		tagInput.SetHintText(Loc::TextOr(EditorLocKeys::EditorPropertyTag, "Tag"));
		tagInput.SetText(selectedObject->Tag.c_str());
		layout.Row([&]() { ImGui::TextUnformatted(Loc::TextOr(EditorLocKeys::EditorPropertyTag, "Tag")); }, [&]() {
			if (tagInput())
			{
				selectedObject->Tag = tagInput;
			}
		});

		ImGui::Spacing();

		Transform2D& t = selectedObject->GetTransform();

		// Transform 편집은 전용 커맨드로 기록(컴포넌트 아님) + 다중 선택 전체에 같은 델타 적용.
		// 편집 전 스냅샷(before) ↔ 편집 후(t=after)에서 델타를 구하고, primary 의 라이브 변경을
		// 되돌린 뒤 커맨드가 선택 전체에 델타를 적용한다. 드래그는 CommandManager 가 병합 → undo 1개.
		auto pushTransform = [&](const Transform2D& before)
		{
			Transform2D delta;
			delta.Position.x          = t.Position.x - before.Position.x;
			delta.Position.y          = t.Position.y - before.Position.y;
			delta.RotationRadians.Value = t.RotationRadians.Value - before.RotationRadians.Value;
			delta.Scale.x             = t.Scale.x - before.Scale.x;
			delta.Scale.y             = t.Scale.y - before.Scale.y;

			t = before; // primary 라이브 변경 취소 → 커맨드가 전체에 균일 적용.
			// 부모+자식 동시 선택 시 부모만(자식은 부모 따라 이동) — 최상위만 타깃.
			std::vector<CGameObject*> targets = Editor::GetSelectedTopLevel();
			Editor::CommandManager.ExecuteCommand(
				MakeOwnerPtr<CSetObjectTransformCommand>(scene->SafeFromThis(), targets, delta));
		};

		// Position
		layout.Row([&]() { ImGui::TextUnformatted(Loc::TextOr(EditorLocKeys::EditorPropertyPosition, "Position")); }, [&]() {
			const Transform2D before = t;
			if (ImGui::DragFloat2(Loc::TextOr(EditorLocKeys::EditorPropertyPosition, "Position"), &t.Position.x, 0.01f))
				pushTransform(before);
		});

		// Rotation - 회전은 내부 radian, 표시/편집은 degree.
		layout.Row([&]() { ImGui::TextUnformatted(Loc::TextOr(EditorLocKeys::EditorPropertyRotationRadians, "Rotation")); }, [&]() {
			const Transform2D before = t;
			Degree degrees = t.RotationRadians.ToDegree();
			if (ImGui::DragFloat(Loc::TextOr(EditorLocKeys::EditorPropertyRotationRadians, "Rotation"), &degrees.Value, 0.5f))
			{
				t.RotationRadians = degrees.ToRadian();
				pushTransform(before);
			}
		});

		// Scale
		layout.Row([&]() { ImGui::TextUnformatted(Loc::TextOr(EditorLocKeys::EditorPropertyScale, "Scale")); }, [&]() {
			const Transform2D before = t;
			if (ImGui::DragFloat2(Loc::TextOr(EditorLocKeys::EditorPropertyScale, "Scale"), &t.Scale.x, 0.01f))
				pushTransform(before);
			});
	}

	ImText separatorText;
	separatorText.UseSeparator(true);
	separatorText(Loc::Text(EditorLocKeys::EditorCategoryComponents));

	// ── 목록 항목 빌드 (GameObject 제외) ──────────────────────────────────────
	struct ListEntry
	{
		std::string     label;
		ComponentEntry* compEntry = nullptr;
		std::size_t     instIdx   = 0;
		CGameScript*    script = nullptr;
		File::Guid      componentGuid;
	};

	std::vector<ListEntry> listItems;
	for (const SafePtr<CComponent>& componentRef : selectedObject->GetComponents())
	{
		CComponent* component = componentRef.TryGet();
		if (nullptr == component) continue;
		if (CGameScript* script = CCanvasRuntimeAccess::AsScript(*scene, component))
		{
			listItems.push_back({ GetScriptInstanceDisplayName(script) ? GetScriptInstanceDisplayName(script) : Loc::Text(EditorLocKeys::InspectorUnknownScript), nullptr, 0, script, script->GetInstanceGuid() });
			continue;
		}
		for (ComponentEntry& entry : allEntries)
		{
			if (nullptr == entry.typeInfo || 0 != strcmp(component->GetTypeName(), entry.typeInfo->Type.Name)) continue;
			for (std::size_t i = 0; i < entry.instances.size(); ++i)
			{
				if (entry.instances[i] == component)
				{
					listItems.push_back({ GetComponentLabel(*entry.typeInfo), &entry, i, nullptr, component->GetInstanceGuid() });
					break;
				}
			}
			break;
		}
	}

	// 엔티티 변경 시 인덱스 범위 보정
	if (listItems.empty() || m_selectedTabIndex >= static_cast<int>(listItems.size()))
	{
		m_selectedTabIndex = 0;
	}

	// 하이어라키에서 컴포넌트를 클릭하면(포커스 힌트) 해당 탭으로 전환한다.
	if (false == Editor::GetFocusComponent().empty())
	{
		for (int idx = 0; idx < static_cast<int>(listItems.size()); ++idx)
		{
			const ListEntry& focusItem = listItems[static_cast<std::size_t>(idx)];
			const char* typeName = focusItem.script ? "Script" : focusItem.compEntry->typeInfo->Type.Name;
			if (typeName && Editor::GetFocusComponent() == typeName)
			{
				m_selectedTabIndex = idx;
				break;
			}
		}
		Editor::ClearFocusComponent();
	}

	// 현재 선택된 탭의 컴포넌트 타입 이름 캐시 (SceneViewTool 등 외부 시스템이 참조)
	if (false == listItems.empty())
	{
		const ListEntry& activeItem = listItems[static_cast<std::size_t>(m_selectedTabIndex)];
		m_activeComponentTypeName = activeItem.script ? "Script" : activeItem.compEntry->typeInfo->Type.Name;
	}

	// ── 레이아웃: 좌측 리스트 | 드래그 구분선 | 우측 컨텐츠 ──────────────────
	constexpr float SPLITTER_W = 3.0f;
	constexpr float MIN_RATIO  = 0.15f;
	constexpr float MAX_RATIO  = 0.80f;

	const ImVec2 availSpace = ImGui::GetContentRegionAvail();
	const float leftW		= availSpace.x * m_splitRatio - SPLITTER_W * 0.5f;
	const float rightW		= availSpace.x - leftW - SPLITTER_W;

	// ── 좌측 패널: 컴포넌트 이름 목록 ──────────────────────────────────────────
	const ImVec4 disabledTextCol = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);

	ImGui::BeginChild("##InspectorList",
	    ImVec2(leftW, availSpace.y), true, ImGuiWindowFlags_NoScrollbar);
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));
		if (listItems.empty())
		{
			ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::InspectorNoOtherComponents));
		}
		else
		{
			for (int idx = 0; idx < static_cast<int>(listItems.size()); ++idx)
			{
				const ListEntry& item     = listItems[static_cast<std::size_t>(idx)];
				const bool       selected = (m_selectedTabIndex == idx);

				// IsEnabled 확인 → 비활성화 컴포넌트는 dim 색상
				const ComponentEntry* ce = item.compEntry;
				void* firstInst = ce && item.instIdx < ce->instances.size() ? ce->instances[item.instIdx] : nullptr;
				const bool isEnabled = item.script ? item.script->IsEnabled()
					: GetComponentIsEnabled(firstInst);

				if (!isEnabled)
					ImGui::PushStyleColor(ImGuiCol_Text, disabledTextCol);

				// 아이콘 + 이름. 아이콘이 없으면 이름만 표시(자리는 비워 정렬 유지).
				const ImTextureID iconTex = GetComponentIconTexture(item.script ? "Script" : ce->typeInfo->Type.Name);
				const float       lineH   = ImGui::GetTextLineHeight();
				if (0 != iconTex)
				{
					ImGui::Image(iconTex, ImVec2(lineH, lineH));
				}
				else
				{
					ImGui::Dummy(ImVec2(lineH, lineH));
				}
				ImGui::SameLine();

				// ID 충돌 방지: 인덱스 접미사
				char selLabel[256];
				snprintf(selLabel, sizeof(selLabel), "%s##si%d", item.label.c_str(), idx);

				if (ImGui::Selectable(selLabel, selected,
				        ImGuiSelectableFlags_SpanAllColumns,
				        ImVec2(0.0f, 0.0f)))
				{
					m_selectedTabIndex = idx;
				}
				if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
				{
					ImGui::SetDragDropPayload("INSPECTOR_ATTACHMENT", &idx, sizeof(idx));
					ImGui::TextUnformatted(item.label.c_str());
					ImGui::EndDragDropSource();
				}
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INSPECTOR_ATTACHMENT"))
					{
						const int sourceIndex = *static_cast<const int*>(payload->Data);
						if (sourceIndex >= 0 && sourceIndex < static_cast<int>(listItems.size()) && sourceIndex != idx)
						{
							Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CReorderComponentCommand>(scene->SafeFromThis(), selectedObject, listItems[static_cast<std::size_t>(sourceIndex)].componentGuid, static_cast<std::size_t>(sourceIndex), static_cast<std::size_t>(idx)));
							m_selectedTabIndex = idx;
						}
					}
					ImGui::EndDragDropTarget();
				}

				if (!isEnabled)
					ImGui::PopStyleColor();

				// 우클릭 → 컴포넌트 제거 (style color 복원 후, item 은 직전 Selectable).
				char ctxId[32];
				snprintf(ctxId, sizeof(ctxId), "##compctx%d", idx);
				if (ImGui::BeginPopupContextItem(ctxId))
				{
					// 복사/붙여넣기 — firstInst 는 단일 상속이라 곧 CComponent* 다.
					CComponent* menuComponent = item.script
						? static_cast<CComponent*>(item.script)
						: static_cast<CComponent*>(firstInst);
					if (menuComponent) EditorGuiActions::DrawCopyComponentMenuItem(*menuComponent);
					EditorGuiActions::DrawPasteComponentMenuItem(*selectedObject);
					ImGui::Separator();
					if (ImGui::MenuItem(Loc::Text(EditorLocKeys::InspectorRemoveComponent)))
					{
						if (item.script) Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CRemoveScriptCommand>(scene->SafeFromThis(), selectedObject, item.script->GetInstanceGuid()));
						else Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CRemoveComponentCommand>(scene->SafeFromThis(), selectedObject, ce->typeInfo->Type.Id, GetComponentGuid(firstInst)));
					}
					ImGui::EndPopup();
				}
			}
		}

		ImGui::Separator();
		const std::string addComponentLabel =
			std::string("+ ") + Loc::Text(EditorLocKeys::InspectorAddComponent);
		EditorGuiActions::DrawAddComponentButton(*scene, selectedObject, addComponentLabel.c_str());
		ImGui::PopStyleVar();
	}
	ImGui::EndChild();

	// ── 드래그 구분선 ───────────────────────────────────────────────────────────
    {
        const ImVec2 regionMin = ImGui::GetCursorScreenPos();
        float splitPos = availSpace.x * m_splitRatio;
        if (::VerticalSplitter("##InspSplitter", splitPos, regionMin, availSpace, SPLITTER_W))
        {
            m_splitRatio = std::clamp(splitPos / std::max(availSpace.x, 1.0f), MIN_RATIO, MAX_RATIO);
        }
    }

	// ── 우측 패널: 선택된 컴포넌트 내용 ──────────────────────────────────────────
	ImGui::BeginChild("##InspectorContent",
	    ImVec2(rightW, availSpace.y), false, ImGuiWindowFlags_NoScrollbar);
	{
		if (listItems.empty())
		{
			ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::InspectorNoOtherComponents));
		}
		else
		{
			const ListEntry&  sel     = listItems[static_cast<std::size_t>(m_selectedTabIndex)];
			ComponentEntry*   e       = sel.compEntry;
			const std::size_t instIdx = sel.instIdx;
			void* comp = e && instIdx < e->instances.size() ? e->instances[instIdx] : nullptr;

			if (sel.script)
			{
				CGameScript* script = sel.script;
				ImGui::PushID(script);
				bool enabled = script->IsEnabled();
				if (ImGui::Checkbox(Loc::TextOr(EditorLocKeys::EditorPropertyIsEnabled, "Enabled"), &enabled))
				{
					const bool oldEnabled = script->IsEnabled();
					script->SetEnabled(enabled);
					Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CSetComponentEnabledCommand>(
						scene->SafeFromThis(), selectedObject, script->GetInstanceGuid(), oldEnabled, enabled));
				}
				const ScriptTypeInfo* scriptInfo = Engine.Reflection.IsValid() ? Engine.Reflection->FindScript(script->GetTypeId()) : nullptr;
				if (scriptInfo)
				{
					ImText labelText;
					labelText.SetHoveredTooltip(true);
					for (const ReflectPropertyInfo& prop : scriptInfo->Properties)
					{
						if (prop.IsEditable)
						{
							DrawReflectedPropertyRow(*scene, selectedObject, *script, prop, labelText);
						}
					}
				}
				ImGui::PopID();
			}
			else if (comp)
			{
				ImGui::PushID(static_cast<int>(e->typeIndex * 1000 + instIdx));
				DrawIsEnabledCheckbox(*scene, selectedObject, *e->typeInfo, instIdx, comp, false);
				DrawComponentProperties(*scene, selectedObject, *e->typeInfo, instIdx, comp);

			// ── AudioPlayer: EffectGuids 효과 체인 (리플렉션 밖, 커스텀 ImList) ──
			// EffectGuids 는 가변 길이라 리플렉션 자동 그리기에서 빠진다. 효과 에셋을
			// 드래그&드롭으로 추가/삭제하고, 리스트 순서대로 적용된다(ImList 가 재정렬 제공).
			if (e->typeInfo->Type.Name && 0 == strcmp(e->typeInfo->Type.Name, "AudioPlayer"))
			{
				AudioPlayer* audioPlayer = static_cast<AudioPlayer*>(comp);
				if (audioPlayer)
				{
					ImGui::Spacing();
					ImSectionHeader(Loc::Text(EditorLocKeys::InspectorAudioEffectChain)).Draw();
					ImList<AssetGuid>(
						"##audio_effect_chain", audioPlayer->EffectGuids,
						[](AssetGuid& effectGuid, int /*idx*/)
						{
							ImAssetField("##audio_effect_asset", effectGuid)
								.Type(EAssetType::AudioEffect)
								.Draw();
						},
						INVALID_ASSET_GUID);
				}
			}

			ImGui::PopID();
		}
	}
	}
	ImGui::EndChild();
}

#include "pch.h"
#include "InspectorTool.h"
#include "AssetInspectorPreview.h"
#include "EffectEditorWindow.h"

#include "Engine/Editor/ImItem/ImText.h"
#include "Engine/Editor/ImItem/ImSplitter.h"
#include "Engine/Editor/ImItem/ImList.h"
#include "Engine/Editor/ImGuiUtillity.h"
#include "Editor/Script/ScriptSchema.h"
#include "Editor/Script/ScriptSchemaWidgets.h"

#include "Editor/Editor.h"
#include "Editor/Command/EditorSceneCommands.h"
#include "Editor/EditorDragDrop.h"
#include "Editor/Helper/EditorGuiDrawHelpers.h"
#include "Engine/GameFramework/Object/GameObject.h"
#include "Engine/GameFramework/Object/Ref.h"
#include "Engine/GameFramework/Reflection/ReflectionRegistry.h"
#include "Engine/Core/Asset/AssetMetaFile.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/IAssetRegistry.h"
#include "Engine/Core/Asset/SpriteAsset.h"
#include "Engine/Core/Asset/AudioAsset.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Core/Renderer/IRenderResourceCache.h"
#include "Engine/Core/Resource/ResourceRegistry.h"
#include "Engine/Core/RHI/IRHITexture.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/GameFramework/Component/Physics2DComponents.h"
#include "Engine/GameFramework/Component/ScriptComponent.h"
#include "Engine/GameFramework/Scripting/ScriptSystem.h"
#include "Engine/GameFramework/Component/Transform2D.h"
#include "Engine/GameFramework/Physics2D/Physics2DSystem.h"
#include "Engine/GameFramework/Physics2D/Physics2DTypes.h"
#include "Engine/GameFramework/Scene/SceneTransformUtils.h"
#include "Utillity/Math/RectT.h"
#include "Utillity/Types/EngineTypes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
	constexpr std::size_t GUID_BUFFER_LENGTH = 128;

	using EditorGuiDrawHelpers::GetScriptDisplayName;
	using EditorGuiDrawHelpers::LocalizedComponentLabel;
	using EditorGuiDrawHelpers::LocalizedPropertyLabel;

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
		if (0 == strcmp(typeName, "ScriptComponent"))        return "icon-script";
		if (0 == strcmp(typeName, "GameObject"))             return "icon-object";
		return nullptr;
	}

	ImTextureID GetComponentIconTexture(const char* typeName)
	{
		if (false == Core::ResourceRegistry.IsValid()) return 0;
		const char* key = GetComponentIconKey(typeName);
		if (nullptr == key) return 0;
CSpriteAsset* sprite = Core::ResourceRegistry->GetSprite(key);
		if (nullptr == sprite) return 0;
		if (false == Engine.RenderResourceCache.IsValid()) return 0;
		SafePtr<IRHITexture> tex = Engine.RenderResourceCache->AcquireSpriteTexture(sprite->GetGuid(), *sprite);
		if (false == tex.IsValid()) return 0;
		return reinterpret_cast<ImTextureID>(tex->GetNativeHandle().ShaderResourceView);
	}

	// ScriptComponent 인스턴스에서 등록된 스크립트의 표시 이름을 가져온다.
	// 미등록(INVALID_TYPE_ID) 또는 reflection 미준비 시 nullptr.
	const char* GetScriptInstanceDisplayName(void* compInstance)
	{
		if (nullptr == compInstance || false == Core::Reflection.IsValid()) return nullptr;
		ScriptComponent* sc = static_cast<ScriptComponent*>(compInstance);
		if (INVALID_TYPE_ID == sc->ScriptTypeId) return nullptr;
		const ScriptTypeInfo* info = Core::Reflection->FindScript(sc->ScriptTypeId);
		return info ? GetScriptDisplayName(info) : nullptr;
	}

	void DrawScriptTypeSelector(CScene& scene, CGameObject* object, std::size_t instanceIndex, ScriptComponent& scriptComponent)
	{
		if (false == Core::Reflection.IsValid())
		{
			ImGui::TextDisabled(Loc::Text("inspector.script_registry_unavailable"));
			return;
		}

		CReflectionRegistry& reflection = *Core::Reflection;
		const ScriptTypeInfo* currentType = reflection.FindScript(scriptComponent.ScriptTypeId);
		const char* preview = currentType ? GetScriptDisplayName(currentType) : Loc::Text("inspector.unknown_script");

		ImGui::Utillity::FormLayout layout("##script_type_selector", 4.0f, {2.0f, 1.0f}, 80.0f);
		layout.Row(
			[]() { ImGui::TextUnformatted(Loc::Text("inspector.script_type")); },
			[&]()
			{
				if (0 == reflection.GetScriptTypeCount())
				{
					ImGui::TextDisabled(Loc::Text("inspector.no_scripts_registered"));
					return;
				}

				if (ImGui::BeginCombo("##script_type", preview))
				{
					for (std::size_t i = 0; i < reflection.GetScriptTypeCount(); ++i)
					{
						const ScriptTypeInfo* scriptType = reflection.GetScriptType(i);
						if (nullptr == scriptType || scriptType->Type.Id == INVALID_TYPE_ID)
						{
							continue;
						}

						const bool selected = scriptComponent.ScriptTypeId == scriptType->Type.Id;
						const std::string label =
							std::string(GetScriptDisplayName(scriptType)) + "##" + std::to_string(scriptType->Type.Id);
						if (ImGui::Selectable(label.c_str(), selected) && false == selected)
						{
							Editor::CommandManager.ExecuteCommand(
								MakeOwnerPtr<CSetScriptTypeCommand>(
									scene.SafeFromThis(), object, instanceIndex, scriptType->Type.Id));
						}
						if (selected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
			});
	}

	// ── Ref<T> 프로퍼티 헬퍼 ──────────────────────────────────────────────────
	// 드래그된 HIERARCHY_COMPONENT 페이로드의 타입명(컴포넌트/스크립트) 조회.
	// 오브젝트를 컴포넌트/스크립트 Ref 프로퍼티에 드롭했을 때, 타입이 맞는 첫 컴포넌트를 찾는다.
	//  · Script Ref     : RefTypeName 과 등록 스크립트 타입명이 일치하는 첫 ScriptComponent.
	//  · Component Ref  : GetTypeName() 이 RefTypeName 과 일치하는 첫 컴포넌트.
	// 없으면 nullptr. RefTypeName 이 비어 있으면 카테고리만 맞으면 첫 인스턴스를 돌려준다.
	CComponent* FindFirstComponentForRef(CGameObject& object, const ReflectPropertyInfo& property)
	{
		const bool wantScript = (ERefCategory::Script == property.RefCategory);
		const char* wantType  = property.RefTypeName;

		for (const SafePtr<CComponent>& cref : object.GetComponents())
		{
			CComponent* comp = cref.TryGet();
			if (nullptr == comp)
			{
				continue;
			}

			if (wantScript)
			{
				ScriptComponent* sc = dynamic_cast<ScriptComponent*>(comp);
				if (nullptr == sc)
				{
					continue;
				}
				if (nullptr == wantType || '\0' == wantType[0])
				{
					return sc;   // 타입 미지정 — 첫 스크립트.
				}
				if (Core::Reflection.IsValid())
				{
					const ScriptTypeInfo* info = Core::Reflection->FindScript(sc->ScriptTypeId);
					if (info && info->Type.Name && 0 == strcmp(info->Type.Name, wantType))
					{
						return sc;
					}
				}
			}
			else
			{
				if (dynamic_cast<ScriptComponent*>(comp))
				{
					continue;   // 컴포넌트 Ref 는 스크립트 컨테이너를 건너뛴다.
				}
				const char* tn = comp->GetTypeName();
				if (nullptr == wantType || '\0' == wantType[0] || (tn && 0 == strcmp(tn, wantType)))
				{
					return comp;
				}
			}
		}
		return nullptr;
	}

	// Ref 의 현재 대상 표시 라벨.
	std::string BuildRefDisplayLabel(const RefBase& ref, const ReflectPropertyInfo& property)
	{
		const char* typeName = property.RefTypeName ? property.RefTypeName : "Ref";
		if (ref.IsNull())
		{
			return std::string(Loc::Text("inspector.ref_none")) + "  [" + typeName + "]";
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
		if (CScene* scene = GetActiveScene())
		{
			if (CGameObject* obj = scene->FindByInstanceGuid(guid).TryGet())
			{
				return std::string(obj->GetName()) + "  [" + typeName + "]";
			}
		}
		return std::string(Loc::Text("inspector.ref_missing")) + "  [" + typeName + "]";
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
				ref.SetGuidText(EditorDragDrop::GetGuid(payload).generic_string().c_str());
				return true;
			}
			return false;
		}

		// 오브젝트/컴포넌트/스크립트 — 하이어라키 페이로드.
		CScene* scene = GetActiveScene();
		if (nullptr == scene || false == ImGui::BeginDragDropTarget())
		{
			return false;
		}
		bool changed = false;

		const bool wantsGameObject =
			property.RefTypeName && 0 == strcmp(property.RefTypeName, "GameObject");

		if (wantsGameObject)
		{
			// 오브젝트 자체를 드래그(HIERARCHY_ENTITY).
			if (const ImGuiPayload* p =
				ImGui::AcceptDragDropPayload(EditorDragDrop::HIERARCHY_ENTITY_PAYLOAD))
			{
				if (CGameObject* obj = *static_cast<CGameObject* const*>(p->Data))
				{
					ref.SetGuidText(obj->InstanceGuid.generic_string().c_str());
					changed = true;
				}
			}
		}
		else if (const ImGuiPayload* p =
			ImGui::AcceptDragDropPayload(EditorDragDrop::HIERARCHY_ENTITY_PAYLOAD))
		{
			// 컴포넌트/스크립트 Ref — 오브젝트를 드롭하면 그 오브젝트의 타입이 맞는 첫
			// 컴포넌트를 참조로 설정한다. (오브젝트 guid + 컴포넌트 guid) 쌍을 저장한다.
			if (CGameObject* obj = *static_cast<CGameObject* const*>(p->Data))
			{
				CComponent* match = FindFirstComponentForRef(*obj, property);
				if (match)
				{
					ref.SetGuidText(obj->InstanceGuid.generic_string().c_str());
					ref.SetComponentGuidText(match->InstanceGuid.generic_string().c_str());
					changed = true;
				}
			}
		}

		ImGui::EndDragDropTarget();
		return changed;
	}

	// 공유 참조-필드 위젯 — 에셋/오브젝트/컴포넌트/스크립트 참조 모두 같은 모양.
	// 시각(ReadOnly 버튼 + X 클리어)은 여기 한 곳. 드롭 수락/클리어는 저장부별 콜백으로 주입.
	//   accept(): 자체 Begin/End 드롭 타깃 처리, 변경 시 true.
	//   clear():  값 비우기.
	template <typename AcceptFn, typename ClearFn>
	bool DrawReferenceField(const std::string& label, bool isNull, AcceptFn&& accept, ClearFn&& clear)
	{
		bool changed = false;
		const float clearW = ImGui::GetFrameHeight();
		const float fullW  = ImGui::GetContentRegionAvail().x;
		const float fieldW = std::max(1.0f, fullW - clearW - ImGui::GetStyle().ItemSpacing.x);

		// 직접 편집 불가 표시 — 버튼처럼 보이되 클릭 동작 없음(드롭 타깃 전용).
		ImGui::PushStyleColor(ImGuiCol_Button,        ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
		ImGui::Button((label + "##reffield").c_str(), ImVec2(fieldW, 0.0f));
		ImGui::PopStyleColor(3);

		if (accept()) changed = true;   // 콜백이 Begin/EndDragDropTarget 까지 처리

		ImGui::SameLine();
		if (ImGui::Button("X", ImVec2(clearW, 0.0f)) && false == isNull)
		{
			clear();
			changed = true;
		}
		return changed;
	}

	bool DrawPropertyEditor(void* field, const ReflectPropertyInfo& property, bool drawLabel = true)
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
			return ImGui::InputText("", static_cast<std::string*>(field));
		case EReflectPropertyType::AssetGuid:
		{
			// 에셋 참조(엔진 컴포넌트의 File::Guid). 공유 위젯 + 에셋 페이로드만 수락.
			File::Guid* guid = static_cast<File::Guid*>(field);
			std::string label;
			if (guid->IsNull())
			{
				label = Loc::Text("inspector.ref_none");
			}
			else
			{
				const File::Path& path = File::ResolvePath(*guid);
				label = path.IsNull() ? guid->generic_string() : path.filename().generic_string();
			}
			return DrawReferenceField(
				label, guid->IsNull(),
				[&]() -> bool
				{
					EditorDragDrop::AssetPayload payload;
					if (EditorDragDrop::AcceptAssetDragDropPayload(payload))
					{
						*guid = EditorDragDrop::GetGuid(payload);
						return true;
					}
					return false;
				},
				[&]() { *guid = File::NULL_GUID; });
		}
		case EReflectPropertyType::Ref:
		{
			// Ref<T>(스크립트 POD RefBase). 공유 위젯 + 카테고리별 페이로드 수락(ApplyRefDrop).
			RefBase* ref = static_cast<RefBase*>(field);
			return DrawReferenceField(
				BuildRefDisplayLabel(*ref, property), ref->IsNull(),
				[&]() { return ApplyRefDrop(*ref, property); },
				[&]() { ref->Clear(); });
		}
		case EReflectPropertyType::Enum:
		{
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
			ImGui::TextDisabled("(%s)", Loc::Text("inspector.normalized"));

			float pix[2] = { layout->Pixel.x, layout->Pixel.y };
			if (ImGui::DragFloat2(pixLabel.c_str(), pix, 1.0f,
			                      0.0f, 0.0f, "%.1f"))
			{
				layout->Pixel.x = pix[0];
				layout->Pixel.y = pix[1];
				changed = true;
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(%s)", Loc::Text("inspector.pixel"));

			ImGui::Unindent(8.0f);
			ImGui::PopID();
			return changed;
		}
		default:
			ImGui::TextDisabled("%s: %s", "", Loc::Text("inspector.unsupported"));
			return false;
		}
	}

	void DrawTransformMatrixReadOnly(const Transform2D& transform)
	{
		const Matrix3x2 matrix = transform.ToMatrix3x2();
		float row0[3] = { matrix.M11, matrix.M21, matrix.Dx };
		float row1[3] = { matrix.M12, matrix.M22, matrix.Dy };

		ImGui::SeparatorText(Loc::Text("inspector.matrix_3x2"));
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

	void DrawPhysicsContactDebug(const CScene& scene, const CGameObject* selectedObject)
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

		ImGui::SeparatorText(Loc::Text("inspector.physics_contacts"));
		DrawReadOnlyUInt(Loc::Text("inspector.contacts.detected"), contactCount);
		if (contactCount > 0)
		{
			DrawReadOnlyVector2(Loc::Text("inspector.contacts.last_normal"), lastManifold.Normal);
			DrawReadOnlyVector2(Loc::Text("inspector.contacts.last_point"), lastManifold.ContactPoints[0]);
			DrawReadOnlyFloat(Loc::Text("inspector.contacts.last_penetration"), lastManifold.Penetration);
		}
	}

	void DrawRigidbodyDebug(const CScene& scene, const CGameObject* selectedObject, const Rigidbody2D& rigidbody)
	{
		ImGui::SeparatorText(Loc::Text("inspector.rigidbody_debug"));
		const float inverseMass = rigidbody.Mass > 0.0f ? 1.0f / rigidbody.Mass : 0.0f;
		const float inverseInertia = false == rigidbody.FreezeRotation && rigidbody.Inertia > 0.0f ? 1.0f / rigidbody.Inertia : 0.0f;
		DrawReadOnlyFloat(Loc::Text("inspector.rigidbody.inverse_mass"), inverseMass);
		DrawReadOnlyFloat(Loc::Text("inspector.rigidbody.inverse_inertia"), inverseInertia);
		DrawReadOnlyUInt(Loc::Text("inspector.rigidbody.impulse_contacts"), rigidbody.LastContactCount);
		DrawReadOnlyVector2(Loc::Text("inspector.rigidbody.last_impulse_normal"), rigidbody.LastContactNormal);
		DrawReadOnlyVector2(Loc::Text("inspector.rigidbody.last_impulse_point"), rigidbody.LastContactPoint);
		DrawReadOnlyFloat(Loc::Text("inspector.rigidbody.last_normal_impulse"), rigidbody.LastNormalImpulse);
		DrawReadOnlyFloat(Loc::Text("inspector.rigidbody.last_friction_impulse"), rigidbody.LastFrictionImpulse);
		DrawReadOnlyFloat(Loc::Text("inspector.rigidbody.last_angular_impulse"), rigidbody.LastAngularImpulse);
		DrawPhysicsContactDebug(scene, selectedObject);
	}

	void DrawCircleColliderDebug(const CScene& scene, const CGameObject* selectedObject, const CircleCollider2D& collider)
	{
		(void)scene;
		const Matrix3x2 worldTransform = selectedObject ? GetWorldTransform(*selectedObject) : Matrix3x2::Identity();
		const Vector2 worldCenter = worldTransform.TransformPoint(Vector2(0.0f, 0.0f));
		const float scaleX = std::sqrt(worldTransform.M11 * worldTransform.M11 + worldTransform.M12 * worldTransform.M12);
		const float scaleY = std::sqrt(worldTransform.M21 * worldTransform.M21 + worldTransform.M22 * worldTransform.M22);
		const float worldRadius = collider.Radius * std::max(scaleX, scaleY);

		ImGui::SeparatorText(Loc::Text("inspector.circle_collider_debug"));
		DrawReadOnlyVector2(Loc::Text("inspector.collider.world_center"), worldCenter);
		DrawReadOnlyFloat(Loc::Text("inspector.collider.world_radius"), worldRadius);
	}

	void DrawPolygonColliderDebug(const CScene& scene, const CGameObject* selectedObject, const PolygonCollider2D& collider)
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

		ImGui::SeparatorText(Loc::Text("inspector.polygon_collider_debug"));
		DrawReadOnlyUInt(Loc::Text("inspector.collider.local_points"), static_cast<std::uint32_t>(localPoints->size()));
		DrawReadOnlyUInt(Loc::Text("inspector.collider.convex_pieces"), static_cast<std::uint32_t>(collider.ConvexPieces.size()));
		DrawReadOnlyVector2(Loc::Text("inspector.collider.world_aabb_min"), aabb.Min);
		DrawReadOnlyVector2(Loc::Text("inspector.collider.world_aabb_max"), aabb.Max);
	}

	// ── GetComponentIsEnabled ────────────────────────────────────────────────
	// 컴포넌트의 IsEnabled 값을 반환. IsEnabled 프로퍼티가 없으면 true.
	bool GetComponentIsEnabled(void* component, const ComponentTypeInfo& typeInfo)
	{
		for (const ReflectPropertyInfo& prop : typeInfo.Properties)
		{
			if (prop.Type == EReflectPropertyType::Bool &&
			    prop.Name && 0 == strcmp(prop.Name, "IsEnabled"))
			{
				void* field = CReflectionRegistry::GetPropertyAddress(component, prop);
				if (field) return *static_cast<bool*>(field);
				break;
			}
		}
		return true;
	}

	// ── DrawIsEnabledCheckbox ─────────────────────────────────────────────────
	//   sameLineAfter=true  → "##enabled" 체크박스 + SameLine() (CollapsingHeader 왼쪽)
	//   sameLineAfter=false → "IsEnabled" 라벨 체크박스 + Separator  (탭 최상단 단독)
	void DrawIsEnabledCheckbox(
		CScene& scene, CGameObject* selectedObject,
		const ComponentTypeInfo& componentType,
		std::size_t instanceIdx, void* component, bool sameLineAfter)
	{
		const ReflectPropertyInfo* enabledProp = nullptr;
		for (const ReflectPropertyInfo& prop : componentType.Properties)
		{
			if (prop.Type == EReflectPropertyType::Bool &&
				prop.Name && 0 == strcmp(prop.Name, "IsEnabled"))
			{
				enabledProp = &prop;
				break;
			}
		}
		if (!enabledProp) return;

		void* enabledField = CReflectionRegistry::GetPropertyAddress(component, *enabledProp);
		if (!enabledField) return;

		bool oldEnabled = *static_cast<bool*>(enabledField);
		bool newEnabled = oldEnabled;
		const std::string enabledLabel = sameLineAfter ? "##enabled" : (std::string(Loc::Text("editor.property.IsEnabled")) + "##enabled");
		if (ImGui::Checkbox(enabledLabel.c_str(), &newEnabled) && newEnabled != oldEnabled)
		{
			*static_cast<bool*>(enabledField) = newEnabled;
			std::vector<std::uint8_t> oldVal = { static_cast<std::uint8_t>(oldEnabled) };
			std::vector<std::uint8_t> newVal = { static_cast<std::uint8_t>(newEnabled) };
			Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CSetComponentPropertyCommand>(
				scene.SafeFromThis(), selectedObject,
				componentType.Type.Id, enabledProp->Offset,
				std::move(oldVal), std::move(newVal), instanceIdx));
		}
		if (sameLineAfter)
			ImGui::SameLine();
		else
			ImGui::Separator();
	}

	// ── DrawComponentProperties ───────────────────────────────────────────────
	// IsEnabled·non-editable 프로퍼티를 제외하고 에디터 + 특수 디버그 섹션 렌더링.
	void DrawComponentProperties(
		CScene& scene, CGameObject* selectedObject,
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

			ImGui::Utillity::FormLayout layout("##component_properties", 4.0f, {2.0f, 1.0f}, 60.0f);

			void* field = CReflectionRegistry::GetPropertyAddress(component, property);
			std::vector<std::uint8_t> oldValue(property.Size);
			const bool canRawUndo = !(property.Type == EReflectPropertyType::String && property.ElementCount <= 1);
			if (canRawUndo && field && property.Size > 0)
				std::memcpy(oldValue.data(), field, property.Size);

			const std::string label = LocalizedPropertyLabel(property);
			layout.Row([&]() { leftText(label.c_str()); }, [&]() {
					const bool	changed = DrawPropertyEditor(field, property);
					if (changed && canRawUndo && field && property.Size > 0)
					{
						std::vector<std::uint8_t> newValue(property.Size);
						std::memcpy(newValue.data(), field, property.Size);
						if (oldValue != newValue)
						{
							Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CSetComponentPropertyCommand>(
								scene.SafeFromThis(), selectedObject,
								componentType.Type.Id, property.Offset,
								std::move(oldValue), std::move(newValue), instanceIdx));
						}
					}
				}
			);
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
	}

	bool SaveSpriteImportOptions(const AssetMetaData& metaData, const SpriteImportOptions& options)
	{
		SafePtr<CProjectManager> projectManager = GetProjectManager();
		SafePtr<IAssetManager> assetManager = projectManager ? projectManager->GetAssetManager() : nullptr;
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

		ImGui::SeparatorText(Loc::Text("inspector.sprite_import_options"));

		ImGui::Utillity::FormLayout layout("##sprite_import_options");
		bool changed = false;

		// ── 슬라이스 모드 콤보 ─────────────────────────────────────────────────
		const char* sliceItems[] = {
			Loc::Text("inspector.slice_type.none"),
			Loc::Text("inspector.slice_type.automatic"),
			Loc::Text("inspector.slice_type.cell_size"),
			Loc::Text("inspector.slice_type.cell_count"),
		};
		int sliceTypeIndex = static_cast<int>(options.SliceType);
		layout.Row(
			[&]() { ImGui::TextUnformatted(Loc::Text("inspector.slice_type")); },
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
			layout.Row([&]() { ImGui::TextUnformatted(Loc::Text("inspector.row_count")); },    [&]() { changed |= ImGui::InputInt("##inspector.row_count", &rowCount); });
			layout.Row([&]() { ImGui::TextUnformatted(Loc::Text("inspector.column_count")); }, [&]() { changed |= ImGui::InputInt("##inspector.column_count", &columnCount); });
		}
		else if (ESpriteSliceType::CellSize == options.SliceType)
		{
			layout.Row([&]() { ImGui::TextUnformatted(Loc::Text("inspector.cell_width")); },  [&]() { changed |= ImGui::InputInt("##inspector.cell_width", &cellWidth); });
			layout.Row([&]() { ImGui::TextUnformatted(Loc::Text("inspector.cell_height")); }, [&]() { changed |= ImGui::InputInt("##inspector.cell_height", &cellHeight); });
		}

		// ── 그리드/여백 (None/Automatic 에서는 의미 없으므로 슬라이스 모드일 때만) ─
		if (ESpriteSliceType::CellSize == options.SliceType || ESpriteSliceType::CellCount == options.SliceType)
		{
			layout.Row([&]() { ImGui::TextUnformatted(Loc::Text("inspector.margin_x")); }, [&]() { changed |= ImGui::InputInt("##inspector.margin_x", &marginX); });
			layout.Row([&]() { ImGui::TextUnformatted(Loc::Text("inspector.margin_y")); }, [&]() { changed |= ImGui::InputInt("##inspector.margin_y", &marginY); });
			layout.Row([&]() { ImGui::TextUnformatted(Loc::Text("inspector.gap_x")); },    [&]() { changed |= ImGui::InputInt("##inspector.gap_x", &gapX); });
			layout.Row([&]() { ImGui::TextUnformatted(Loc::Text("inspector.gap_y")); },    [&]() { changed |= ImGui::InputInt("##inspector.gap_y", &gapY); });
		}

		SafePtr<CProjectManager> projectManager = GetProjectManager();
		const float projectPPU = projectManager ? projectManager->GetPixelsPerUnit() : 0.0f;

		// ── 공용: 피벗/PPU ────────────────────────────────────────────────────
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text("inspector.pivot_x")); },         [&]() { changed |= ImGui::DragFloat("##inspector.pivot_x", &options.PivotX, 0.01f, 0.0f, 1.0f); });
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text("inspector.pivot_y")); },         [&]() { changed |= ImGui::DragFloat("##inspector.pivot_y", &options.PivotY, 0.01f, 0.0f, 1.0f); });
		layout.Row(
			[&]() { ImGui::TextUnformatted(Loc::Text("inspector.pixels_per_unit")); },
			[&]() {
				// 0 = 프로젝트 기본값 사용. 0 보다 큰 값이면 그 값으로 오버라이드.
				changed |= ImGui::DragFloat("##inspector.pixels_per_unit", &options.PixelsPerUnit, 1.0f, 0.0f, 10000.0f);
				if (options.PixelsPerUnit <= 0.0f)
				{
					ImGui::SameLine();
					ImGui::TextDisabled("%.1f %s", projectPPU, Loc::Text("inspector.ppu.project_default_suffix"));
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

		SafePtr<IAssetManager> assetManager = projectManager ? projectManager->GetAssetManager() : nullptr;
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
		DrawReadOnlyUInt(Loc::Text("inspector.sprite.preview_frame_count"), static_cast<std::uint32_t>(previewFrames.size()));
		if (false == previewFrames.empty())
		{
			DrawReadOnlyUInt(Loc::Text("inspector.sprite.frame_width"), previewFrames.front().Width);
			DrawReadOnlyUInt(Loc::Text("inspector.sprite.frame_height"), previewFrames.front().Height);
		}

		if (changed)
		{
			s_dirty = true;
		}

		ImGui::BeginDisabled(false == s_dirty);
		if (ImGui::Button(Loc::Text("inspector.apply_sprite_import_options")))
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
		SafePtr<CProjectManager> projectManager = GetProjectManager();
		SafePtr<IAssetManager>   assetManager   = projectManager ? projectManager->GetAssetManager() : nullptr;
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

		ImGui::SeparatorText(Loc::Text("inspector.audio_import_options"));

		ImGui::Utillity::FormLayout layout("##audio_import_options");
		bool changed = false;

		// ── 임포트 모드 (Decompressed / Streaming) ─────────────────────────
		const char* modeItems[] = {
			Loc::Text("inspector.audio.mode.decompressed"),
			Loc::Text("inspector.audio.mode.streaming"),
		};
		int modeIndex = static_cast<int>(options.Mode);
		layout.Row(
			[&]() { ImGui::TextUnformatted(Loc::Text("inspector.audio.mode")); },
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
			Loc::Text("inspector.audio.bus.master"),
			Loc::Text("inspector.audio.bus.music"),
			Loc::Text("inspector.audio.bus.sfx"),
			Loc::Text("inspector.audio.bus.voice"),
			Loc::Text("inspector.audio.bus.ui"),
			Loc::Text("inspector.audio.bus.custom"),
		};
		int busIndex = static_cast<int>(options.DefaultBus);
		layout.Row(
			[&]() { ImGui::TextUnformatted(Loc::Text("inspector.audio.default_bus")); },
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
			[&]() { ImGui::TextUnformatted(Loc::Text("inspector.audio.default_volume")); },
			[&]() { changed |= ImGui::DragFloat("##inspector.audio.default_volume", &options.DefaultVolume, 0.01f, 0.0f, 2.0f); });
		layout.Row(
			[&]() { ImGui::TextUnformatted(Loc::Text("inspector.audio.loop")); },
			[&]() { changed |= ImGui::Checkbox("##inspector.audio.loop", &options.Loop); });

		// ── 3D 음향 ────────────────────────────────────────────────────────
		layout.Row(
			[&]() { ImGui::TextUnformatted(Loc::Text("inspector.audio.is_3d")); },
			[&]() { changed |= ImGui::Checkbox("##inspector.audio.is_3d", &options.Is3D); });
		if (options.Is3D)
		{
			layout.Row(
				[&]() { ImGui::TextUnformatted(Loc::Text("inspector.audio.min_distance")); },
				[&]() { changed |= ImGui::DragFloat("##inspector.audio.min_distance", &options.MinDistance, 0.1f, 0.0f, 10000.0f); });
			layout.Row(
				[&]() { ImGui::TextUnformatted(Loc::Text("inspector.audio.max_distance")); },
				[&]() { changed |= ImGui::DragFloat("##inspector.audio.max_distance", &options.MaxDistance, 0.1f, 0.0f, 10000.0f); });
			if (options.MinDistance < 0.0f) options.MinDistance = 0.0f;
			if (options.MaxDistance < options.MinDistance) options.MaxDistance = options.MinDistance;
		}

		// ── 정보 (포맷 / 길이) ─────────────────────────────────────────────
		SafePtr<CProjectManager> projectManager = GetProjectManager();
		SafePtr<IAssetManager>   assetManager   = projectManager ? projectManager->GetAssetManager() : nullptr;
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
							Loc::Text("inspector.audio.format"),
							fmt.SampleRate,
							static_cast<unsigned int>(fmt.Channels));
						ImGui::TextDisabled("%s: %.2f s",
							Loc::Text("inspector.audio.duration"),
							audioAsset->GetDurationSeconds());
					}
				}
			}
		}

		if (changed) s_dirty = true;

		ImGui::BeginDisabled(false == s_dirty);
		if (ImGui::Button(Loc::Text("inspector.apply_audio_import_options")))
		{
			if (SaveAudioImportOptions(metaData, options))
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

		ImGui::Text("%s: %s", Loc::Text("common.path"), path.filename().generic_string().c_str());
		if (false == s_parseOk)
		{
			ImGui::TextDisabled(Loc::Text("inspector.script_not_parsed"));
			return true;
		}
		ImGui::Text("%s: %s", Loc::Text("inspector.script_class"), s_className.c_str());
		ImGui::Separator();
		ImGui::TextUnformatted(Loc::Text("asset_browser.script_popup.properties"));

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
			ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "%s",
				Loc::Text("asset_browser.script_popup.invalid_props"));
		}

		ImGui::Spacing();
		ImGui::Separator();

		// ── Apply / Reload ────────────────────────────────────────────────────
		ImGui::BeginDisabled(false == allValid);
		if (ImGui::Button(Loc::Text("common.apply")))
		{
			if (ScriptSchema::WriteHeaderFile(path, s_className, s_props))
			{
				if (SafePtr<CProjectManager> pm = GetProjectManager())
				{
					pm->RegenerateScriptProject();
				}
				s_status     = Loc::Text("inspector.script_apply_ok");
				s_loadedPath = File::NULL_PATH;   // 정규화된 파일 재파싱
			}
			else
			{
				s_status = Loc::Text("inspector.script_apply_fail");
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button(Loc::Text("common.refresh")))
		{
			s_loadedPath = File::NULL_PATH;   // 디스크에서 다시 읽기(편집 취소)
		}
		if (false == s_status.empty())
		{
			ImGui::TextDisabled("%s", s_status.c_str());
		}
		return true;
	}

	bool DrawSelectedAssetInspector()
	{
		const File::Guid& selectedGuid = Editor::GetSelectedAssetGuid();
		if (selectedGuid.IsNull())
		{
			return false;
		}

		SafePtr<CProjectManager> projectManager = GetProjectManager();
		SafePtr<IAssetManager> assetManager = projectManager ? projectManager->GetAssetManager() : nullptr;
		if (false == assetManager.IsValid())
		{
			ImGui::TextDisabled(Loc::Text("inspector.asset_manager_unavailable"));
			return true;
		}

		const AssetMetaData* metaData = assetManager->GetRegistry().FindAsset(selectedGuid);
		if (nullptr == metaData)
		{
			ImGui::TextDisabled(Loc::Text("inspector.selected_asset_not_registered"));
			return true;
		}

		// ── 맨 위 미리보기 영역 (Sprite=이미지, Audio=Play/Stop 등) ─────────
		// DrawTopPreview 가 핸들러의 Enter/Stay/Exit 라이프사이클을 자체적으로 관리.
		if (AssetInspectorPreview::DrawTopPreview(*metaData))
		{
			ImGui::Separator();
		}

		ImGui::Text("%s: %s", Loc::Text("common.asset"), metaData->DisplayName.c_str());
		ImGui::Text("%s: %s", Loc::Text("common.guid"), metaData->Guid.generic_string().c_str());
		ImGui::Text("%s: %s", Loc::Text("common.path"), metaData->Path.generic_string().c_str());
		ImGui::Text("%s: %s", Loc::Text("common.importer"), metaData->Importer.c_str());
		ImGui::Separator();

		if (EAssetType::Sprite == metaData->Type)
		{
			DrawSpriteImportOptions(*metaData);
		}
		else if (EAssetType::Audio == metaData->Type)
		{
			DrawAudioImportOptions(*metaData);
		}
		else if (EAssetType::AudioEffect == metaData->Type)
		{
			// 효과 편집은 전용 독윈도우에서 한다. 인스펙터엔 안내 + 에디터 열기 버튼.
			ImGui::TextDisabled(Loc::Text("inspector.effect.open_in_window"));
			ImGui::Spacing();
			if (ImGui::Button(Loc::Text("inspector.effect.open_editor"), ImVec2(-FLT_MIN, 0.0f)))
			{
				EffectEditorWindow::Open(metaData->Guid, metaData->DisplayName);
			}
		}
		else
		{
			ImGui::TextDisabled(Loc::Text("inspector.no_editable_import_options"));
		}

		return true;
	}
}

void CInspectorTool::OnCreate()
{
	SetLocalizedTitleKey("window.inspector");
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

	CScene* scene = GetActiveScene();
	if (nullptr == scene)
	{
		AssetInspectorPreview::NotifyInspectionLost();
		ImGui::TextDisabled(Loc::Text("inspector.no_active_scene"));
		return;
	}

	CGameObject* selectedObject = Editor::GetSelectedEntity();
	if (nullptr == selectedObject)
	{
		// 스크립트 .h 선택 — 스키마 에디터.
		if (DrawSelectedScriptInspector())
		{
			AssetInspectorPreview::NotifyInspectionLost();
			return;
		}
		if (DrawSelectedAssetInspector())
			return;
		// 자산도 엔티티도 없음 — 활성 미리보기 핸들러 정리.
		AssetInspectorPreview::NotifyInspectionLost();
		ImGui::TextDisabled(Loc::Text("inspector.no_entity_selected"));
		return;
	}

	// 엔티티가 선택됐다 — 자산 미리보기 영역은 그리지 않는다. 활성 핸들러 정리.
	AssetInspectorPreview::NotifyInspectionLost();

	if (false == Core::Reflection.IsValid())
	{
		ImGui::TextDisabled(Loc::Text("inspector.reflection_unavailable"));
		return;
	}

	CReflectionRegistry& reflection = *Core::Reflection;

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
		layout.Row([&]() { ImGui::TextUnformatted(Loc::TextOr("editor.property.IsActive", "Active")); }, [&]() {
			ImGui::Checkbox("##editor.property.IsActive", &selectedObject->IsActive);
		});

		ImInputText nameInput("##go_name");
		nameInput.SetHintText(Loc::TextOr("editor.property.Name", "Name"));
		nameInput.SetText(selectedObject->GetName());
		layout.Row([&]() { ImGui::TextUnformatted(Loc::TextOr("editor.property.Name", "Name")); }, [&]() {
			if (nameInput())
			{
				selectedObject->SetName(nameInput);
			}
		});

		ImInputText tagInput("##go_tag");
		tagInput.SetHintText(Loc::TextOr("editor.property.Tag", "Tag"));
		tagInput.SetText(selectedObject->Tag.c_str());
		layout.Row([&]() { ImGui::TextUnformatted(Loc::TextOr("editor.property.Tag", "Tag")); }, [&]() {
			if (tagInput())
			{
				selectedObject->Tag = tagInput;
			}
		});

		ImGui::Spacing();

		Transform2D& t = selectedObject->GetTransform();

		// Transform 편집은 전용 커맨드로 기록(컴포넌트 아님). 편집 전 스냅샷 → 변경 시 push.
		// 드래그 매 프레임 push 되지만 CommandManager 가 좌버튼 유지 중 병합 → undo 1개.
		auto pushTransform = [&](const Transform2D& before)
		{
			Editor::CommandManager.ExecuteCommand(
				MakeOwnerPtr<CSetObjectTransformCommand>(scene->SafeFromThis(), selectedObject, before, t));
		};

		// Position
		layout.Row([&]() { ImGui::TextUnformatted(Loc::TextOr("editor.property.Position", "Position")); }, [&]() {
			const Transform2D before = t;
			if (ImGui::DragFloat2(Loc::TextOr("editor.property.Position", "Position"), &t.Position.x, 0.01f))
				pushTransform(before);
		});

		// Rotation - 회전은 내부 radian, 표시/편집은 degree.
		layout.Row([&]() { ImGui::TextUnformatted(Loc::TextOr("editor.property.RotationRadians", "Rotation")); }, [&]() {
			const Transform2D before = t;
			Degree degrees = t.RotationRadians.ToDegree();
			if (ImGui::DragFloat(Loc::TextOr("editor.property.RotationRadians", "Rotation"), &degrees.Value, 0.5f))
			{
				t.RotationRadians = degrees.ToRadian();
				pushTransform(before);
			}
		});

		// Scale
		layout.Row([&]() { ImGui::TextUnformatted(Loc::TextOr("editor.property.Scale", "Scale")); }, [&]() {
			const Transform2D before = t;
			if (ImGui::DragFloat2(Loc::TextOr("editor.property.Scale", "Scale"), &t.Scale.x, 0.01f))
				pushTransform(before);
			});
	}

	ImText separatorText;
	separatorText.UseSeparator(true);
	separatorText(Loc::Text("editor.category.Components"));

	// ── 목록 항목 빌드 (GameObject 제외) ──────────────────────────────────────
	struct ListEntry
	{
		std::string     label;
		ComponentEntry* compEntry = nullptr;
		std::size_t     instIdx   = 0;
	};

	std::vector<ListEntry> listItems;
	for (auto& e : allEntries)
	{
		const char* name = e.typeInfo->Type.Name ? e.typeInfo->Type.Name : "";
		if (strcmp(name, "GameObject") == 0)
			continue; // 상단 인라인 처리됨

		const std::string baseDisplayName = LocalizedComponentLabel(*e.typeInfo);
		const bool hasMultiple = e.instances.size() > 1;
		const bool isScriptComponent = (name && 0 == strcmp(name, "ScriptComponent"));

		for (std::size_t instIdx = 0; instIdx < e.instances.size(); ++instIdx)
		{
			std::string displayName = baseDisplayName;
			// ScriptComponent: "ScriptComponent" 대신 등록된 스크립트 이름을 라벨로.
			if (isScriptComponent && instIdx < e.instances.size())
			{
				if (const char* scriptName = GetScriptInstanceDisplayName(e.instances[instIdx]))
				{
					displayName = scriptName;
				}
			}

			ListEntry le;
			if (hasMultiple)
				le.label = displayName + " [" + std::to_string(instIdx) + "]";
			else
				le.label = displayName;
			le.compEntry = &e;
			le.instIdx   = instIdx;
			listItems.push_back(std::move(le));
		}
	}

	if (listItems.empty())
	{
		ImGui::TextDisabled(Loc::Text("inspector.no_other_components"));
		return;
	}

	// 엔티티 변경 시 인덱스 범위 보정
	if (m_selectedTabIndex >= static_cast<int>(listItems.size()))
		m_selectedTabIndex = 0;

	// 하이어라키에서 컴포넌트를 클릭하면(포커스 힌트) 해당 탭으로 전환한다.
	if (false == Editor::GetFocusComponent().empty())
	{
		for (int idx = 0; idx < static_cast<int>(listItems.size()); ++idx)
		{
			const char* typeName = listItems[static_cast<std::size_t>(idx)].compEntry->typeInfo->Type.Name;
			if (typeName && Editor::GetFocusComponent() == typeName)
			{
				m_selectedTabIndex = idx;
				break;
			}
		}
		Editor::ClearFocusComponent();
	}

	// 현재 선택된 탭의 컴포넌트 타입 이름 캐시 (SceneViewTool 등 외부 시스템이 참조)
	m_activeComponentTypeName =
		listItems[static_cast<std::size_t>(m_selectedTabIndex)].compEntry->typeInfo->Type.Name;

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
		for (int idx = 0; idx < static_cast<int>(listItems.size()); ++idx)
		{
			const ListEntry& item     = listItems[static_cast<std::size_t>(idx)];
			const bool       selected = (m_selectedTabIndex == idx);

			// IsEnabled 확인 → 비활성화 컴포넌트는 dim 색상
			const ComponentEntry& ce = *item.compEntry;
			void* firstInst = (item.instIdx < ce.instances.size()) ? ce.instances[item.instIdx] : nullptr;
			const bool isEnabled = firstInst
			    ? GetComponentIsEnabled(firstInst, *ce.typeInfo)
			    : true;

			if (!isEnabled)
				ImGui::PushStyleColor(ImGuiCol_Text, disabledTextCol);

			// 아이콘 + 이름. 아이콘이 없으면 이름만 표시(자리는 비워 정렬 유지).
			const ImTextureID iconTex = GetComponentIconTexture(ce.typeInfo->Type.Name);
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

			if (!isEnabled)
				ImGui::PopStyleColor();

			// 우클릭 → 컴포넌트 제거 (style color 복원 후, item 은 직전 Selectable).
			char ctxId[32];
			snprintf(ctxId, sizeof(ctxId), "##compctx%d", idx);
			if (ImGui::BeginPopupContextItem(ctxId))
			{
				// 복사/붙여넣기 — firstInst 는 단일 상속이라 곧 CComponent* 다.
				if (CComponent* comp = static_cast<CComponent*>(firstInst))
				{
					EditorGuiDrawHelpers::DrawCopyComponentMenuItem(*comp);
				}
				EditorGuiDrawHelpers::DrawPasteComponentMenuItem(*selectedObject);
				ImGui::Separator();
				if (ImGui::MenuItem(Loc::Text("inspector.remove_component")))
				{
					Editor::CommandManager.ExecuteCommand(
						MakeOwnerPtr<CRemoveComponentCommand>(
							scene->SafeFromThis(), selectedObject, ce.typeInfo->Type.Id));
				}
				ImGui::EndPopup();
			}
		}
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
		const ListEntry&  sel     = listItems[static_cast<std::size_t>(m_selectedTabIndex)];
		ComponentEntry&   e       = *sel.compEntry;
		const std::size_t instIdx = sel.instIdx;
		void* comp = (instIdx < e.instances.size()) ? e.instances[instIdx] : nullptr;

		if (comp)
		{
			ImGui::PushID(static_cast<int>(e.typeIndex * 1000 + instIdx));
			DrawIsEnabledCheckbox(*scene, selectedObject, *e.typeInfo, instIdx, comp, false);
			DrawComponentProperties(*scene, selectedObject, *e.typeInfo, instIdx, comp);

			// ── ScriptComponent: REFLECT_FIELD 자동 표시 ──────────────────────
			// ScriptComponent 의 ComponentTypeInfo::Properties 에는 IsEnabled 만 있다.
			// 실제 스크립트 필드는 ScriptTypeInfo::Properties 에 있으므로
			// 인스턴스 포인터 + 오프셋으로 직접 렌더링한다.
			if (e.typeInfo->Type.Name && 0 == strcmp(e.typeInfo->Type.Name, "ScriptComponent"))
			{
				ScriptComponent* scriptComp = static_cast<ScriptComponent*>(comp);
				if (scriptComp)
				{
					DrawScriptTypeSelector(*scene, selectedObject, instIdx, *scriptComp);

					// 에디트 모드에서도 프로퍼티를 보이고 편집할 수 있도록 인스턴스를 보장한다.
					// (DLL 로드 + 타입 등록이 되어 있으면 생성됨. Bind/Start 는 플레이 때만.)
					if (nullptr == scriptComp->Instance)
					{
						CScriptSystem::EnsureEditTimeInstance(*scriptComp);
					}
				}

				if (scriptComp && scriptComp->Instance && Core::Reflection.IsValid())
				{
					const ScriptTypeInfo* scriptInfo =
						Core::Reflection->FindScript(scriptComp->ScriptTypeId);

					if (scriptInfo && !scriptInfo->Properties.empty())
					{
						const char* displayName = GetScriptDisplayName(scriptInfo);

						ImGui::Spacing();
						ImGui::SeparatorText(displayName);

						ImText leftLabel;
						leftLabel.SetHoveredTooltip(true);

						const char* currentCategory = nullptr;
						for (const ReflectPropertyInfo& prop : scriptInfo->Properties)
						{
							void* field = CReflectionRegistry::GetPropertyAddress(scriptComp->Instance, prop);
						if (nullptr == field) { continue; }

							// JPROP(Category("..")) → 카테고리가 바뀔 때 구분 헤더.
							if (prop.Category && (nullptr == currentCategory || 0 != strcmp(currentCategory, prop.Category)))
							{
								currentCategory = prop.Category;
								ImGui::SeparatorText(currentCategory);
							}

							ImGui::Utillity::FormLayout layout("##script_fields", 4.0f, {2.0f, 1.0f}, 60.0f);
							const std::string label = prop.DisplayName
								? prop.DisplayName
								: (prop.Name ? prop.Name : "");

							layout.Row(
								[&]() {
									leftLabel(label.c_str());
									// JPROP(Tooltip("..")) → 라벨 호버 시 설명.
									if (prop.Tooltip && ImGui::IsItemHovered())
									{
										ImGui::SetTooltip("%s", prop.Tooltip);
									}
								},
								[&]() { DrawPropertyEditor(field, prop, false); }
							);
						}
					}
				}
				else if (scriptComp && scriptComp->ScriptTypeId != INVALID_TYPE_ID
				         && !scriptComp->Instance && Core::Reflection.IsValid())
				{
					// 인스턴스가 아직 없는 경우 (DLL 미로드 등)
					const ScriptTypeInfo* scriptInfo =
						Core::Reflection->FindScript(scriptComp->ScriptTypeId);
					const char* typeName = GetScriptDisplayName(scriptInfo);
					ImGui::Spacing();
					ImGui::TextDisabled(Loc::Text("inspector.script_instance_waiting_format"), typeName);
				}
			}

			ImGui::PopID();
		}
	}
	ImGui::EndChild();
}

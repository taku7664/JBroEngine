#include "pch.h"
#include "InspectorTool.h"

#include "Editor/Editor.h"
#include "Editor/Command/EditorSceneCommands.h"
#include "Editor/Helper/EditorGuiDrawHelpers.h"
#include "Engine/Core/Asset/AssetMetaFile.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/IAssetRegistry.h"
#include "Engine/Core/Asset/SpriteAsset.h"
#include "Engine/Core/Resource/ResourceRegistry.h"
#include "Engine/Core/RHI/IRHITexture.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/GameFramework/Component/Physics2DComponents.h"
#include "Engine/GameFramework/Component/ScriptComponent.h"
#include "Engine/GameFramework/Component/Transform2D.h"
#include "Engine/GameFramework/Physics2D/Physics2DSystem.h"
#include "Engine/GameFramework/Physics2D/Physics2DTypes.h"
#include "Engine/GameFramework/Scene/SceneTransformUtils.h"

#include <algorithm>
#include <cmath>
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
		SafePtr<CSpriteAsset> sprite = Core::ResourceRegistry->GetSprite(key);
		if (false == sprite.IsValid()) return 0;
		SafePtr<IRHITexture> tex = sprite->GetGpuTexture();
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

	void DrawScriptTypeSelector(CScene& scene, EntityId entity, std::size_t instanceIndex, ScriptComponent& scriptComponent)
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
									scene.SafeFromThis(), entity, instanceIndex, scriptType->Type.Id));
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
			return ImGui::InputScalar("", ImGuiDataType_S32, field);
		case EReflectPropertyType::UInt32:
			return ImGui::InputScalar("", ImGuiDataType_U32, field);
		case EReflectPropertyType::Float:
			return ImGui::DragFloat("", static_cast<float*>(field), 0.01f);
		case EReflectPropertyType::AngleDegrees:
		{
			// 내부 저장값은 Radians, Inspector에서는 Degrees로 표시/편집.
			float* rad = static_cast<float*>(field);
			constexpr float kRad2Deg = 180.0f / 3.14159265358979323846f;
			constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
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
		case EReflectPropertyType::ColorFloat4:
			return ImGui::ColorEdit4("", static_cast<float*>(field));
		case EReflectPropertyType::String:
			if (property.ElementCount > 0)
			{
				return ImGui::InputText("", static_cast<char*>(field), property.ElementCount);
			}
			return false;
		case EReflectPropertyType::AssetGuid:
		{
			File::Guid* guid = static_cast<File::Guid*>(field);
			char buffer[GUID_BUFFER_LENGTH] = {};
			const std::string guidText = guid->generic_string();
			strncpy_s(buffer, guidText.c_str(), GUID_BUFFER_LENGTH - 1);
			if (ImGui::InputText("", buffer, GUID_BUFFER_LENGTH))
			{
				*guid = File::Guid(buffer);
				return true;
			}
			return false;
		}
		case EReflectPropertyType::EntityId:
			return ImGui::InputScalar("", ImGuiDataType_U64, field);
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

	void DrawReadOnlyVector2(const char* label, const Vector2<float>& value)
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

	void DrawPhysicsContactDebug(const CScene& scene, EntityId selectedEntity)
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
			if (manifold.A != selectedEntity && manifold.B != selectedEntity)
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

	void DrawRigidbodyDebug(const CScene& scene, EntityId selectedEntity, const Rigidbody2D& rigidbody)
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
		DrawPhysicsContactDebug(scene, selectedEntity);
	}

	void DrawCircleColliderDebug(const CScene& scene, EntityId selectedEntity, const CircleCollider2D& collider)
	{
		const Matrix3x2 worldTransform = GetWorldTransform(scene, selectedEntity);
		const Vector2<float> worldCenter = worldTransform.TransformPoint(Vector2<float>(0.0f, 0.0f));
		const float scaleX = std::sqrt(worldTransform.M11 * worldTransform.M11 + worldTransform.M12 * worldTransform.M12);
		const float scaleY = std::sqrt(worldTransform.M21 * worldTransform.M21 + worldTransform.M22 * worldTransform.M22);
		const float worldRadius = collider.Radius * std::max(scaleX, scaleY);

		ImGui::SeparatorText(Loc::Text("inspector.circle_collider_debug"));
		DrawReadOnlyVector2(Loc::Text("inspector.collider.world_center"), worldCenter);
		DrawReadOnlyFloat(Loc::Text("inspector.collider.world_radius"), worldRadius);
	}

	void DrawPolygonColliderDebug(const CScene& scene, EntityId selectedEntity, const PolygonCollider2D& collider)
	{
		std::vector<Vector2<float>> generatedPoints;
		const std::vector<Vector2<float>>* localPoints = &collider.LocalPoints;
		if (localPoints->empty())
		{
			collider.BuildLocalPoints(generatedPoints);
			localPoints = &generatedPoints;
		}

		PhysicsAABB2D aabb;
		if (false == localPoints->empty())
		{
			const Matrix3x2 worldTransform = GetWorldTransform(scene, selectedEntity);
			Vector2<float> firstPoint = worldTransform.TransformPoint((*localPoints)[0]);
			aabb.Min = firstPoint;
			aabb.Max = firstPoint;
			for (const Vector2<float>& localPoint : *localPoints)
			{
				const Vector2<float> worldPoint = worldTransform.TransformPoint(localPoint);
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
		CScene& scene, EntityId selectedEntity,
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
				scene.SafeFromThis(), selectedEntity,
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
		CScene& scene, EntityId selectedEntity,
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
			if (field && property.Size > 0)
				std::memcpy(oldValue.data(), field, property.Size);

			const std::string label = LocalizedPropertyLabel(property);
			layout.Row([&]() { leftText(label.c_str()); }, [&]() {
					const bool	changed = DrawPropertyEditor(field, property);
					if (changed && field && property.Size > 0)
					{
						std::vector<std::uint8_t> newValue(property.Size);
						std::memcpy(newValue.data(), field, property.Size);
						if (oldValue != newValue)
						{
							Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CSetComponentPropertyCommand>(
								scene.SafeFromThis(), selectedEntity,
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
			DrawRigidbodyDebug(scene, selectedEntity, *static_cast<Rigidbody2D*>(component));
		else if (componentType.Type.Id == CReflectionRegistry::MakeTypeId("CircleCollider2D"))
			DrawCircleColliderDebug(scene, selectedEntity, *static_cast<CircleCollider2D*>(component));
		else if (componentType.Type.Id == CReflectionRegistry::MakeTypeId("PolygonCollider2D"))
			DrawPolygonColliderDebug(scene, selectedEntity, *static_cast<PolygonCollider2D*>(component));
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

		assetManager->RefreshAssetRegistry();
		assetManager->ReloadAsset(updatedMetaData.Guid);
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

		// ── 공용: 피벗/PPU ────────────────────────────────────────────────────
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text("inspector.pivot_x")); },         [&]() { changed |= ImGui::DragFloat("##inspector.pivot_x", &options.PivotX, 0.01f, 0.0f, 1.0f); });
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text("inspector.pivot_y")); },         [&]() { changed |= ImGui::DragFloat("##inspector.pivot_y", &options.PivotY, 0.01f, 0.0f, 1.0f); });
		layout.Row([&]() { ImGui::TextUnformatted(Loc::Text("inspector.pixels_per_unit")); }, [&]() { changed |= ImGui::DragFloat("##inspector.pixels_per_unit", &options.PixelsPerUnit, 1.0f, 1.0f, 10000.0f); });

		options.RowCount    = static_cast<std::uint32_t>(std::max(1, rowCount));
		options.ColumnCount = static_cast<std::uint32_t>(std::max(1, columnCount));
		options.CellWidth   = static_cast<std::uint32_t>(std::max(1, cellWidth));
		options.CellHeight  = static_cast<std::uint32_t>(std::max(1, cellHeight));
		options.MarginX     = static_cast<std::uint32_t>(std::max(0, marginX));
		options.MarginY     = static_cast<std::uint32_t>(std::max(0, marginY));
		options.GapX        = static_cast<std::uint32_t>(std::max(0, gapX));
		options.GapY        = static_cast<std::uint32_t>(std::max(0, gapY));

		SafePtr<CProjectManager> projectManager = GetProjectManager();
		SafePtr<IAssetManager> assetManager = projectManager ? projectManager->GetAssetManager() : nullptr;
		std::uint32_t textureWidth = 0;
		std::uint32_t textureHeight = 0;
		if (assetManager)
		{
			if (SafePtr<IAsset> loadedAsset = assetManager->LoadAsset(metaData.Guid))
			{
				if (EAssetType::Sprite == loadedAsset->GetAssetType())
				{
					CSpriteAsset* spriteAsset = static_cast<CSpriteAsset*>(loadedAsset.TryGet());
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

		ImGui::Text("%s: %s", Loc::Text("common.asset"), metaData->DisplayName.c_str());
		ImGui::Text("%s: %s", Loc::Text("common.guid"), metaData->Guid.generic_string().c_str());
		ImGui::Text("%s: %s", Loc::Text("common.path"), metaData->Path.generic_string().c_str());
		ImGui::Text("%s: %s", Loc::Text("common.importer"), metaData->Importer.c_str());
		ImGui::Separator();

		if (EAssetType::Sprite == metaData->Type)
		{
			DrawSpriteImportOptions(*metaData);
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
}

void CInspectorTool::OnUpdate()
{
}

void CInspectorTool::OnRenderStay()
{
	// 매 프레임 초기화: 컴포넌트 미표시 상태가 기본값
	m_activeComponentTypeName = nullptr;

	CScene* scene = GetActiveScene();
	if (nullptr == scene)
	{
		ImGui::TextDisabled(Loc::Text("inspector.no_active_scene"));
		return;
	}

	const EntityId selectedEntity = Editor::GetSelectedEntity();
	if (INVALID_ENTITY_ID == selectedEntity || false == scene->IsAlive(selectedEntity))
	{
		if (DrawSelectedAssetInspector())
			return;
		ImGui::TextDisabled(Loc::Text("inspector.no_entity_selected"));
		return;
	}

	if (false == Core::Reflection.IsValid())
	{
		ImGui::TextDisabled(Loc::Text("inspector.reflection_unavailable"));
		return;
	}

	CReflectionRegistry& reflection = *Core::Reflection;

	// ── 컴포넌트 수집 ─────────────────────────────────────────────────────────
	// TransformHierarchy2D / WorldTransform2D: 엔진 내부 → 비노출
	// GameObject: 상단 인라인 표시, 목록 제외
	// 나머지 전체: 좌측 목록에 표시
	struct ComponentEntry
	{
		const ComponentTypeInfo* typeInfo;
		std::size_t              typeIndex;
		std::vector<void*>       instances;
	};

	ComponentEntry*             goEntry = nullptr; // GameObject (목록 밖)
	std::vector<ComponentEntry> allEntries;        // 임시 보관 (포인터 안정성)

	for (std::size_t i = 0; i < reflection.GetComponentTypeCount(); ++i)
	{
		const ComponentTypeInfo* ct = reflection.GetComponentType(i);
		if (!ct || !reflection.HasComponent(*scene, selectedEntity, ct->Type.Id))
			continue;

		std::vector<void*> instances;
		reflection.GetAllComponentAddresses(*scene, selectedEntity, ct->Type.Id, instances);
		if (instances.empty())
			continue;

		const char* name = ct->Type.Name ? ct->Type.Name : "";
		if (strcmp(name, "TransformHierarchy2D") == 0 ||
		    strcmp(name, "WorldTransform2D") == 0)
			continue;

		allEntries.push_back({ ct, i, std::move(instances) });
	}

	// GameObject 분리
	for (auto& e : allEntries)
	{
		const char* name = e.typeInfo->Type.Name ? e.typeInfo->Type.Name : "";
		if (strcmp(name, "GameObject") == 0)
		{
			goEntry = &e;
			break;
		}
	}

	// ── GameObject 인라인 표시 (헤더 없음) ─────────────────────────────────────
	ImGui::Text("%s: %llu", Loc::Text("common.entity"), static_cast<unsigned long long>(selectedEntity));
	EditorGuiDrawHelpers::DrawAddComponentButton(*scene, selectedEntity);

	if (goEntry && !goEntry->instances.empty())
	{
		void* comp = goEntry->instances[0];
		if (comp)
		{
			ImGui::Spacing();
			ImGui::PushID(static_cast<int>(goEntry->typeIndex * 1000));
			DrawIsEnabledCheckbox(*scene, selectedEntity, *goEntry->typeInfo, 0, comp, false);
			DrawComponentProperties(*scene, selectedEntity, *goEntry->typeInfo, 0, comp);
			ImGui::PopID();
		}
	}
	ImGui::Separator();
	ImGui::Spacing();

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
		}
		ImGui::PopStyleVar();
	}
	ImGui::EndChild();

	// ── 드래그 구분선 ───────────────────────────────────────────────────────────
	ImGui::Utillity::VerticalSplitter("##InspSplitter", m_splitRatio, availSpace, MIN_RATIO, MAX_RATIO, SPLITTER_W);

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
			DrawIsEnabledCheckbox(*scene, selectedEntity, *e.typeInfo, instIdx, comp, false);
			DrawComponentProperties(*scene, selectedEntity, *e.typeInfo, instIdx, comp);

			// ── ScriptComponent: REFLECT_FIELD 자동 표시 ──────────────────────
			// ScriptComponent 의 ComponentTypeInfo::Properties 에는 IsEnabled 만 있다.
			// 실제 스크립트 필드는 ScriptTypeInfo::Properties 에 있으므로
			// 인스턴스 포인터 + 오프셋으로 직접 렌더링한다.
			if (e.typeInfo->Type.Name && 0 == strcmp(e.typeInfo->Type.Name, "ScriptComponent"))
			{
				ScriptComponent* scriptComp = static_cast<ScriptComponent*>(comp);
				if (scriptComp)
				{
					DrawScriptTypeSelector(*scene, selectedEntity, instIdx, *scriptComp);
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

						for (const ReflectPropertyInfo& prop : scriptInfo->Properties)
						{
							void* field = CReflectionRegistry::GetPropertyAddress(scriptComp->Instance, prop);
						if (nullptr == field) { continue; }

							ImGui::Utillity::FormLayout layout("##script_fields", 4.0f, {2.0f, 1.0f}, 60.0f);
							const std::string label = prop.DisplayName
								? prop.DisplayName
								: (prop.Name ? prop.Name : "");

							layout.Row(
								[&]() { leftLabel(label.c_str()); },
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

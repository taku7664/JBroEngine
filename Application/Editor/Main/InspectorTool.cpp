#include "pch.h"
#include "InspectorTool.h"

#include "Editor/Editor.h"
#include "Editor/Command/EditorSceneCommands.h"
#include "Editor/EditorComponentMenu.h"
#include "Engine/Core/Asset/AssetMetaFile.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/IAssetRegistry.h"
#include "Engine/Core/Asset/SpriteAsset.h"
#include "Engine/Core/Asset/TextureAsset.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/GameFramework/Component/Physics2DComponents.h"
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

	CScene* GetActiveScene()
	{
		if (false == Core::SceneManager.IsValid())
		{
			return nullptr;
		}

		SafePtr<CScene> activeScene = Core::SceneManager->GetActiveScene();
		return activeScene.TryGet();
	}

	SafePtr<CProjectManager> GetProjectManager()
	{
		return Editor::ImEditor ? Editor::ImEditor->GetProjectManager() : nullptr;
	}

	bool DrawPropertyEditor(void* field, const ReflectPropertyInfo& property)
	{
		if (nullptr == field || false == property.IsEditable)
		{
			return false;
		}

		const char* label = property.DisplayName ? property.DisplayName : property.Name;
		switch (property.Type)
		{
		case EReflectPropertyType::Bool:
			return ImGui::Checkbox(label, static_cast<bool*>(field));
		case EReflectPropertyType::Int32:
			return ImGui::InputScalar(label, ImGuiDataType_S32, field);
		case EReflectPropertyType::UInt32:
			return ImGui::InputScalar(label, ImGuiDataType_U32, field);
		case EReflectPropertyType::Float:
			return ImGui::DragFloat(label, static_cast<float*>(field), 0.01f);
		case EReflectPropertyType::AngleDegrees:
		{
			// 내부 저장값은 Radians, Inspector에서는 Degrees로 표시/편집.
			float* rad = static_cast<float*>(field);
			constexpr float kRad2Deg = 180.0f / 3.14159265358979323846f;
			constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
			float deg = *rad * kRad2Deg;
			if (ImGui::DragFloat(label, &deg, 0.5f, 0.0f, 0.0f, "%.2f deg"))
			{
				*rad = deg * kDeg2Rad;
				return true;
			}
			return false;
		}
		case EReflectPropertyType::Vector2Float:
			return ImGui::DragFloat2(label, static_cast<float*>(field), 0.01f);
		case EReflectPropertyType::ColorFloat4:
			return ImGui::ColorEdit4(label, static_cast<float*>(field));
		case EReflectPropertyType::String:
			if (property.ElementCount > 0)
			{
				return ImGui::InputText(label, static_cast<char*>(field), property.ElementCount);
			}
			return false;
		case EReflectPropertyType::AssetGuid:
		{
			File::Guid* guid = static_cast<File::Guid*>(field);
			char buffer[GUID_BUFFER_LENGTH] = {};
			const std::string guidText = guid->generic_string();
			strncpy_s(buffer, guidText.c_str(), GUID_BUFFER_LENGTH - 1);
			if (ImGui::InputText(label, buffer, GUID_BUFFER_LENGTH))
			{
				*guid = File::Guid(buffer);
				return true;
			}
			return false;
		}
		case EReflectPropertyType::EntityId:
			return ImGui::InputScalar(label, ImGuiDataType_U64, field);
		case EReflectPropertyType::Enum:
		{
			std::int32_t value = 0;
			const std::size_t copySize = std::min(property.Size, sizeof(value));
			std::memcpy(&value, field, copySize);
			if (ImGui::InputInt(label, &value))
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

			ImGui::PushID(label);
			ImGui::TextUnformatted(label);
			ImGui::Indent(8.0f);

			const std::string normLabel = std::string("N##") + label;
			const std::string pixLabel  = std::string("P##") + label;

			float norm[2] = { layout->Normalized.x, layout->Normalized.y };
			if (ImGui::DragFloat2(normLabel.c_str(), norm, 0.01f,
			                      0.0f, 0.0f, "%.3f"))
			{
				layout->Normalized.x = norm[0];
				layout->Normalized.y = norm[1];
				changed = true;
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(Normalized)");

			float pix[2] = { layout->Pixel.x, layout->Pixel.y };
			if (ImGui::DragFloat2(pixLabel.c_str(), pix, 1.0f,
			                      0.0f, 0.0f, "%.1f"))
			{
				layout->Pixel.x = pix[0];
				layout->Pixel.y = pix[1];
				changed = true;
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(Pixel)");

			ImGui::Unindent(8.0f);
			ImGui::PopID();
			return changed;
		}
		default:
			ImGui::TextDisabled("%s: unsupported", label);
			return false;
		}
	}

	void DrawTransformMatrixReadOnly(const Transform2D& transform)
	{
		const Matrix3x2 matrix = transform.ToMatrix3x2();
		float row0[3] = { matrix.M11, matrix.M21, matrix.Dx };
		float row1[3] = { matrix.M12, matrix.M22, matrix.Dy };

		ImGui::SeparatorText("Matrix 3x2");
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

		ImGui::SeparatorText("Physics Contacts");
		DrawReadOnlyUInt("Detected Contacts", contactCount);
		if (contactCount > 0)
		{
			DrawReadOnlyVector2("Last Contact Normal", lastManifold.Normal);
			DrawReadOnlyVector2("Last Contact Point", lastManifold.ContactPoints[0]);
			DrawReadOnlyFloat("Last Penetration", lastManifold.Penetration);
		}
	}

	void DrawRigidbodyDebug(const CScene& scene, EntityId selectedEntity, const Rigidbody2D& rigidbody)
	{
		ImGui::SeparatorText("Rigidbody Debug");
		const float inverseMass = rigidbody.Mass > 0.0f ? 1.0f / rigidbody.Mass : 0.0f;
		const float inverseInertia = false == rigidbody.FreezeRotation && rigidbody.Inertia > 0.0f ? 1.0f / rigidbody.Inertia : 0.0f;
		DrawReadOnlyFloat("Inverse Mass", inverseMass);
		DrawReadOnlyFloat("Inverse Inertia", inverseInertia);
		DrawReadOnlyUInt("Impulse Contacts", rigidbody.LastContactCount);
		DrawReadOnlyVector2("Last Impulse Normal", rigidbody.LastContactNormal);
		DrawReadOnlyVector2("Last Impulse Point", rigidbody.LastContactPoint);
		DrawReadOnlyFloat("Last Normal Impulse", rigidbody.LastNormalImpulse);
		DrawReadOnlyFloat("Last Friction Impulse", rigidbody.LastFrictionImpulse);
		DrawReadOnlyFloat("Last Angular Impulse", rigidbody.LastAngularImpulse);
		DrawPhysicsContactDebug(scene, selectedEntity);
	}

	void DrawCircleColliderDebug(const CScene& scene, EntityId selectedEntity, const CircleCollider2D& collider)
	{
		const Matrix3x2 worldTransform = GetWorldTransform(scene, selectedEntity);
		const Vector2<float> worldCenter = worldTransform.TransformPoint(collider.LocalCenter);
		const float scaleX = std::sqrt(worldTransform.M11 * worldTransform.M11 + worldTransform.M12 * worldTransform.M12);
		const float scaleY = std::sqrt(worldTransform.M21 * worldTransform.M21 + worldTransform.M22 * worldTransform.M22);
		const float worldRadius = collider.Radius * std::max(scaleX, scaleY);

		ImGui::SeparatorText("Circle Collider Debug");
		DrawReadOnlyVector2("World Center", worldCenter);
		DrawReadOnlyFloat("World Radius", worldRadius);
	}

	void DrawPolygonColliderDebug(const CScene& scene, EntityId selectedEntity, const PolygonCollider2D& collider)
	{
		std::vector<Vector2<float>> localPoints;
		collider.BuildLocalPoints(localPoints);

		PhysicsAABB2D aabb;
		if (false == localPoints.empty())
		{
			const Matrix3x2 worldTransform = GetWorldTransform(scene, selectedEntity);
			Vector2<float> firstPoint = worldTransform.TransformPoint(localPoints[0]);
			aabb.Min = firstPoint;
			aabb.Max = firstPoint;
			for (const Vector2<float>& localPoint : localPoints)
			{
				const Vector2<float> worldPoint = worldTransform.TransformPoint(localPoint);
				aabb.Min.x = std::min(aabb.Min.x, worldPoint.x);
				aabb.Min.y = std::min(aabb.Min.y, worldPoint.y);
				aabb.Max.x = std::max(aabb.Max.x, worldPoint.x);
				aabb.Max.y = std::max(aabb.Max.y, worldPoint.y);
			}
		}

		ImGui::SeparatorText("Polygon Collider Debug");
		DrawReadOnlyUInt("Generated Points", static_cast<std::uint32_t>(localPoints.size()));
		DrawReadOnlyVector2("Bounds Size", collider.Size);
		DrawReadOnlyVector2("World AABB Min", aabb.Min);
		DrawReadOnlyVector2("World AABB Max", aabb.Max);
	}

	bool SaveSpriteImportOptions(const AssetMetaData& metaData, const SpriteImportOptions& options)
	{
		SafePtr<CProjectManager> projectManager = GetProjectManager();
		SafePtr<IAssetManager> assetManager = projectManager ? projectManager->GetAssetManager() : nullptr;
		if (false == assetManager.IsValid())
		{
			return false;
		}

		std::string resolvedMetaPath;
		if (false == assetManager->ResolveAssetPath(metaData.MetaPath.generic_string().c_str(), resolvedMetaPath))
		{
			return false;
		}

		AssetMetaData updatedMetaData = metaData;
		updatedMetaData.ImportOptionsYaml = CSpriteImportOptions::ToYaml(options);
		if (false == CAssetMetaFile::Save(resolvedMetaPath.c_str(), updatedMetaData))
		{
			return false;
		}

		assetManager->RefreshAssetRegistry();
		assetManager->ReloadAsset(updatedMetaData.Guid);
		return true;
	}

	void DrawSpriteImportOptions(const AssetMetaData& metaData)
	{
		SpriteImportOptions options = CSpriteImportOptions::FromYaml(metaData.ImportOptionsYaml);
		int rowCount = static_cast<int>(options.RowCount);
		int columnCount = static_cast<int>(options.ColumnCount);
		int marginX = static_cast<int>(options.MarginX);
		int marginY = static_cast<int>(options.MarginY);
		int gapX = static_cast<int>(options.GapX);
		int gapY = static_cast<int>(options.GapY);

		ImGui::SeparatorText("Sprite Import Options");
		bool changed = false;
		changed |= ImGui::InputInt("Row Count", &rowCount);
		changed |= ImGui::InputInt("Column Count", &columnCount);
		changed |= ImGui::InputInt("Margin X", &marginX);
		changed |= ImGui::InputInt("Margin Y", &marginY);
		changed |= ImGui::InputInt("Gap X", &gapX);
		changed |= ImGui::InputInt("Gap Y", &gapY);
		changed |= ImGui::DragFloat("Pivot X", &options.PivotX, 0.01f, 0.0f, 1.0f);
		changed |= ImGui::DragFloat("Pivot Y", &options.PivotY, 0.01f, 0.0f, 1.0f);
		changed |= ImGui::DragFloat("Pixels Per Unit", &options.PixelsPerUnit, 1.0f, 1.0f, 10000.0f);

		options.RowCount = static_cast<std::uint32_t>(std::max(1, rowCount));
		options.ColumnCount = static_cast<std::uint32_t>(std::max(1, columnCount));
		options.MarginX = static_cast<std::uint32_t>(std::max(0, marginX));
		options.MarginY = static_cast<std::uint32_t>(std::max(0, marginY));
		options.GapX = static_cast<std::uint32_t>(std::max(0, gapX));
		options.GapY = static_cast<std::uint32_t>(std::max(0, gapY));

		SafePtr<CProjectManager> projectManager = GetProjectManager();
		SafePtr<IAssetManager> assetManager = projectManager ? projectManager->GetAssetManager() : nullptr;
		std::uint32_t textureWidth = 0;
		std::uint32_t textureHeight = 0;
		if (assetManager)
		{
			if (SafePtr<IAsset> loadedAsset = assetManager->LoadAsset(metaData.Guid))
			{
				if (EAssetType::Texture == loadedAsset->GetAssetType())
				{
					CTextureAsset* textureAsset = static_cast<CTextureAsset*>(loadedAsset.TryGet());
					textureWidth = textureAsset ? textureAsset->GetWidth() : 0;
					textureHeight = textureAsset ? textureAsset->GetHeight() : 0;
				}
			}
		}

		const std::vector<SpriteFrame> previewFrames = CSpriteImportOptions::BuildFrames(textureWidth, textureHeight, options);
		DrawReadOnlyUInt("Preview Frame Count", static_cast<std::uint32_t>(previewFrames.size()));
		if (false == previewFrames.empty())
		{
			DrawReadOnlyUInt("Frame Width", previewFrames.front().Width);
			DrawReadOnlyUInt("Frame Height", previewFrames.front().Height);
		}

		if (changed)
		{
			ImGui::TextDisabled("Options changed. Apply to write .Jmeta.");
		}

		if (ImGui::Button("Apply Sprite Import Options"))
		{
			SaveSpriteImportOptions(metaData, options);
		}
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
			ImGui::TextDisabled("Asset manager is not available.");
			return true;
		}

		const AssetMetaData* metaData = assetManager->GetRegistry().FindAsset(selectedGuid);
		if (nullptr == metaData)
		{
			ImGui::TextDisabled("Selected asset is not registered.");
			return true;
		}

		ImGui::Text("Asset: %s", metaData->DisplayName.c_str());
		ImGui::Text("Guid: %s", metaData->Guid.generic_string().c_str());
		ImGui::Text("Path: %s", metaData->Path.generic_string().c_str());
		ImGui::Text("Importer: %s", metaData->Importer.c_str());
		ImGui::Separator();

		if (EAssetType::Texture == metaData->Type || EAssetType::Sprite == metaData->Type)
		{
			DrawSpriteImportOptions(*metaData);
		}
		else
		{
			ImGui::TextDisabled("No editable import options for this asset type.");
		}

		return true;
	}
}

void CInspectorTool::OnCreate()
{
	SetTitle("Inspector");
}

void CInspectorTool::OnDestroy()
{
}

void CInspectorTool::OnUpdate()
{
}

void CInspectorTool::OnRenderStay()
{
	CScene* scene = GetActiveScene();
	if (nullptr == scene)
	{
		ImGui::TextDisabled("No active scene.");
		return;
	}

	const EntityId selectedEntity = Editor::GetSelectedEntity();
	if (INVALID_ENTITY_ID == selectedEntity || false == scene->IsAlive(selectedEntity))
	{
		if (DrawSelectedAssetInspector())
		{
			return;
		}

		ImGui::TextDisabled("No entity selected.");
		return;
	}

	ImGui::Text("Entity: %llu", static_cast<unsigned long long>(selectedEntity));
	ImGui::Separator();

	if (false == Core::Reflection.IsValid())
	{
		ImGui::TextDisabled("Reflection registry is not available.");
		return;
	}

	CReflectionRegistry& reflection = *Core::Reflection;

	EditorComponentMenu::DrawAddComponentButton(*scene, selectedEntity);

	ImGui::Separator();

	for (std::size_t i = 0; i < reflection.GetComponentTypeCount(); ++i)
	{
		const ComponentTypeInfo* componentType = reflection.GetComponentType(i);
		if (nullptr == componentType || false == reflection.HasComponent(*scene, selectedEntity, componentType->Type.Id))
		{
			continue;
		}

		// Collect all instances (1 for normal components, N for AllowDuplicates types).
		std::vector<void*> allInstances;
		reflection.GetAllComponentAddresses(*scene, selectedEntity, componentType->Type.Id, allInstances);
		if (allInstances.empty())
		{
			continue;
		}

		const char* displayName = componentType->Type.DisplayName ? componentType->Type.DisplayName : componentType->Type.Name;
		const bool hasMultiple = allInstances.size() > 1;

		for (std::size_t instanceIdx = 0; instanceIdx < allInstances.size(); ++instanceIdx)
		{
			void* component = allInstances[instanceIdx];
			if (nullptr == component)
			{
				continue;
			}

			// Unique ImGui ID per component type + instance.
			ImGui::PushID(static_cast<int>(i * 1000 + instanceIdx));

			// --- IsEnabled inline checkbox (Unity-style: left of the header) ---
			const ReflectPropertyInfo* enabledProp = nullptr;
			for (const ReflectPropertyInfo& prop : componentType->Properties)
			{
				if (prop.Type == EReflectPropertyType::Bool && prop.Name && 0 == strcmp(prop.Name, "IsEnabled"))
				{
					enabledProp = &prop;
					break;
				}
			}

			if (enabledProp)
			{
				void* enabledField = CReflectionRegistry::GetPropertyAddress(component, *enabledProp);
				if (enabledField)
				{
					bool oldEnabled = *static_cast<bool*>(enabledField);
					bool newEnabled = oldEnabled;
					if (ImGui::Checkbox("##enabled", &newEnabled) && newEnabled != oldEnabled)
					{
						*static_cast<bool*>(enabledField) = newEnabled;
						std::vector<std::uint8_t> oldVal = { static_cast<std::uint8_t>(oldEnabled) };
						std::vector<std::uint8_t> newVal = { static_cast<std::uint8_t>(newEnabled) };
						Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CSetComponentPropertyCommand>(
							scene->SafeFromThis(),
							selectedEntity,
							componentType->Type.Id,
							enabledProp->Offset,
							std::move(oldVal),
							std::move(newVal),
							instanceIdx));
					}
					ImGui::SameLine();
				}
			}

			// --- Collapsing header with optional [N] suffix ---
			char headerLabel[256];
			if (hasMultiple)
			{
				snprintf(headerLabel, sizeof(headerLabel), "%s [%zu]", displayName, instanceIdx);
			}
			else
			{
				snprintf(headerLabel, sizeof(headerLabel), "%s", displayName);
			}

			ImGui::Utillity::StyleBuilder styleBuilder;
			styleBuilder.PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
			if (ImGui::CollapsingHeader(headerLabel, ImGuiTreeNodeFlags_DefaultOpen))
			{
				styleBuilder.PopStyle();
				for (const ReflectPropertyInfo& property : componentType->Properties)
				{
					// IsEnabled is rendered as the inline header checkbox; skip it here.
					if (property.Name && 0 == strcmp(property.Name, "IsEnabled"))
					{
						continue;
					}

					if (false == property.IsEditable)
					{
						continue;
					}

					void* field = CReflectionRegistry::GetPropertyAddress(component, property);
					std::vector<std::uint8_t> oldValue(property.Size);
					if (field && property.Size > 0)
					{
						std::memcpy(oldValue.data(), field, property.Size);
					}

					const bool changed = DrawPropertyEditor(field, property);
					if (changed && field && property.Size > 0)
					{
						std::vector<std::uint8_t> newValue(property.Size);
						std::memcpy(newValue.data(), field, property.Size);
						if (oldValue != newValue)
						{
							Editor::CommandManager.ExecuteCommand(MakeOwnerPtr<CSetComponentPropertyCommand>(
								scene->SafeFromThis(),
								selectedEntity,
								componentType->Type.Id,
								property.Offset,
								std::move(oldValue),
								std::move(newValue),
								instanceIdx));
						}
					}
				}

				if (componentType->Type.Id == CReflectionRegistry::MakeTypeId("Transform2D"))
				{
					DrawTransformMatrixReadOnly(*static_cast<Transform2D*>(component));
				}
				else if (componentType->Type.Id == CReflectionRegistry::MakeTypeId("Rigidbody2D"))
				{
					DrawRigidbodyDebug(*scene, selectedEntity, *static_cast<Rigidbody2D*>(component));
				}
				else if (componentType->Type.Id == CReflectionRegistry::MakeTypeId("CircleCollider2D"))
				{
					DrawCircleColliderDebug(*scene, selectedEntity, *static_cast<CircleCollider2D*>(component));
				}
				else if (componentType->Type.Id == CReflectionRegistry::MakeTypeId("PolygonCollider2D"))
				{
					DrawPolygonColliderDebug(*scene, selectedEntity, *static_cast<PolygonCollider2D*>(component));
				}
			}

			ImGui::PopID();
		}
	}
}

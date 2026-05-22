#include "pch.h"
#include "InspectorTool.h"

#include "Editor/Editor.h"

#include <algorithm>
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
		default:
			ImGui::TextDisabled("%s: unsupported", label);
			return false;
		}
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

	if (ImGui::BeginCombo("Add Component", "Add Component"))
	{
		for (std::size_t i = 0; i < reflection.GetComponentTypeCount(); ++i)
		{
			const ComponentTypeInfo* componentType = reflection.GetComponentType(i);
			if (nullptr == componentType || false == componentType->CanAddToEntity)
			{
				continue;
			}
			if (reflection.HasComponent(*scene, selectedEntity, componentType->Type.Id))
			{
				continue;
			}

			const char* displayName = componentType->Type.DisplayName ? componentType->Type.DisplayName : componentType->Type.Name;
			if (ImGui::Selectable(displayName))
			{
				reflection.AddComponent(*scene, selectedEntity, componentType->Type.Id);
			}
		}
		ImGui::EndCombo();
	}

	ImGui::Separator();

	for (std::size_t i = 0; i < reflection.GetComponentTypeCount(); ++i)
	{
		const ComponentTypeInfo* componentType = reflection.GetComponentType(i);
		if (nullptr == componentType || false == reflection.HasComponent(*scene, selectedEntity, componentType->Type.Id))
		{
			continue;
		}

		void* component = reflection.GetComponentAddress(*scene, selectedEntity, componentType->Type.Id);
		if (nullptr == component)
		{
			continue;
		}

		const char* displayName = componentType->Type.DisplayName ? componentType->Type.DisplayName : componentType->Type.Name;
		if (ImGui::CollapsingHeader(displayName, ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::PushID(static_cast<int>(i));
			for (const ReflectPropertyInfo& property : componentType->Properties)
			{
				if (false == property.IsEditable)
				{
					continue;
				}

				void* field = CReflectionRegistry::GetPropertyAddress(component, property);
				DrawPropertyEditor(field, property);
			}
			ImGui::PopID();
		}
	}
}

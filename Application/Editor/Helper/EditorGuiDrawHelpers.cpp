#include "pch.h"
#include "EditorGuiDrawHelpers.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Command/EditorSceneCommands.h"
#include "Editor/Editor.h"
#include "Engine/Core/Core.h"
#include "Engine/GameFramework/Reflection/ReflectionRegistry.h"
#include "Engine/GameFramework/Scene/Scene.h"

namespace
{
	bool DrawComponentList(CScene& scene, EntityId entity)
	{
		if (false == Core::Reflection.IsValid() || false == scene.IsAlive(entity))
		{
			ImGui::TextDisabled("No reflected component registry.");
			return false;
		}

		bool added = false;
		CReflectionRegistry& reflection = *Core::Reflection;

		// 추가 가능한 컴포넌트: AllowDuplicates 이거나 아직 없는 타입
		auto isAvailable = [&](const ComponentTypeInfo* t) -> bool
		{
			return t->CanAddToEntity &&
			       (t->AllowDuplicates ||
			        false == reflection.HasComponent(scene, entity, t->Type.Id));
		};

		std::vector<std::string> categories;
		for (std::size_t i = 0; i < reflection.GetComponentTypeCount(); ++i)
		{
			const ComponentTypeInfo* componentType = reflection.GetComponentType(i);
			if (nullptr == componentType || false == isAvailable(componentType)) continue;

			const char* category = componentType->Type.Category ? componentType->Type.Category : "Components";
			if (std::find(categories.begin(), categories.end(), category) == categories.end())
				categories.emplace_back(category);
		}

		for (const std::string& category : categories)
		{
			if (ImGui::BeginMenu(category.c_str()))
			{
				for (std::size_t i = 0; i < reflection.GetComponentTypeCount(); ++i)
				{
					const ComponentTypeInfo* componentType = reflection.GetComponentType(i);
					if (nullptr == componentType || false == isAvailable(componentType)) continue;

					const char* componentCategory =
					    componentType->Type.Category ? componentType->Type.Category : "Components";
					if (category != componentCategory) continue;

					const char* displayName =
					    componentType->Type.DisplayName
					        ? componentType->Type.DisplayName
					        : componentType->Type.Name;
					if (ImGui::MenuItem(displayName))
					{
						Editor::CommandManager.ExecuteCommand(
						    MakeOwnerPtr<CAddComponentCommand>(
						        scene.SafeFromThis(), entity, componentType->Type.Id));
						added = true;
					}
				}
				ImGui::EndMenu();
			}
		}
		return added;
	}
} // namespace

bool EditorGuiDrawHelpers::DrawAddComponentMenu(CScene& scene, EntityId entity)
{
	if (ImGui::BeginMenu("Add Component"))
	{
		const bool added = DrawComponentList(scene, entity);
		ImGui::EndMenu();
		return added;
	}
	return false;
}

bool EditorGuiDrawHelpers::DrawAddComponentButton(CScene& scene, EntityId entity)
{
	bool added = false;
	if (ImGui::Button("Add Component"))
	{
		ImGui::OpenPopup("AddComponentPopup");
	}

	if (ImGui::BeginPopup("AddComponentPopup"))
	{
		added = DrawComponentList(scene, entity);
		ImGui::EndPopup();
	}
	return added;
}

bool EditorGuiDrawHelpers::DrawAddObjectMenu(CScene& scene, EntityId parent)
{
	// parent 유무에 따라 레이블 변경
	const char* label = (parent != INVALID_ENTITY_ID)
	                        ? Utillity::U8(u8"Add Child Object")
	                        : Utillity::U8(u8"Add Object");

	if (ImGui::MenuItem(label))
	{
		OwnerPtr<CCreateGameObjectCommand> cmd =
		    MakeOwnerPtr<CCreateGameObjectCommand>(scene.SafeFromThis(), "GameObject", parent);
		CCreateGameObjectCommand* rawCmd = cmd.Get();
		if (Editor::CommandManager.ExecuteCommand(std::move(cmd)) && rawCmd)
		{
			Editor::SelectEntity(rawCmd->GetEntity());
		}
		return true;
	}
	return false;
}

#endif

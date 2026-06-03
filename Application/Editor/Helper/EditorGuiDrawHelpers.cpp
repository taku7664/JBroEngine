#include "pch.h"
#include "EditorGuiDrawHelpers.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Command/EditorSceneCommands.h"
#include "Editor/Editor.h"
#include "Engine/Core/Core.h"
#include "Engine/GameFramework/Reflection/ReflectionRegistry.h"
#include "Engine/GameFramework/Scene/Scene.h"

#include <cstring>

namespace
{
	bool IsScriptComponent(const ComponentTypeInfo* componentType)
	{
		return componentType &&
		       componentType->Type.Name &&
		       0 == std::strcmp(componentType->Type.Name, "ScriptComponent");
	}

	bool DrawScriptList(CScene& scene, CGameObject* object, CReflectionRegistry& reflection)
	{
		if (nullptr == object)
		{
			return false;
		}

		if (0 == reflection.GetScriptTypeCount())
		{
			ImGui::TextDisabled(Loc::Text("inspector.no_scripts_registered"));
			return false;
		}

		bool added = false;
		for (std::size_t i = 0; i < reflection.GetScriptTypeCount(); ++i)
		{
			const ScriptTypeInfo* scriptType = reflection.GetScriptType(i);
			if (nullptr == scriptType || scriptType->Type.Id == INVALID_TYPE_ID)
			{
				continue;
			}

			const std::string label =
				std::string(EditorGuiDrawHelpers::GetScriptDisplayName(scriptType)) + "##" + std::to_string(scriptType->Type.Id);
			if (ImGui::MenuItem(label.c_str()))
			{
				added = Editor::CommandManager.ExecuteCommand(
					MakeOwnerPtr<CAddScriptComponentCommand>(
						scene.SafeFromThis(), object, scriptType->Type.Id));
			}
		}

		return added;
	}

	bool DrawComponentList(CScene& scene, CGameObject* object)
	{
		if (false == Core::Reflection.IsValid() || nullptr == object)
		{
			ImGui::TextDisabled(Loc::Text("inspector.no_component_registry"));
			return false;
		}

		bool added = false;
		CReflectionRegistry& reflection = *Core::Reflection;

		// 단일 인스턴스: 아직 부착되지 않은 타입만 메뉴에 노출
		auto isAvailable = [&](const ComponentTypeInfo* t) -> bool
		{
			return t->CanAddToObject &&
			       false == reflection.HasComponent(*object, t->Type.Id);
		};

		std::vector<std::string> categories;
		for (std::size_t i = 0; i < reflection.GetComponentTypeCount(); ++i)
		{
			const ComponentTypeInfo* componentType = reflection.GetComponentType(i);
			if (nullptr == componentType || IsScriptComponent(componentType) || false == isAvailable(componentType)) continue;

			const char* category = componentType->Type.Category ? componentType->Type.Category : "Components";
			if (std::find(categories.begin(), categories.end(), category) == categories.end())
				categories.emplace_back(category);
		}

		for (const std::string& category : categories)
		{
			const std::string categoryLabel = EditorGuiDrawHelpers::LocalizedCategoryLabel(category.c_str());
			if (ImGui::BeginMenu(categoryLabel.c_str()))
			{
				for (std::size_t i = 0; i < reflection.GetComponentTypeCount(); ++i)
				{
					const ComponentTypeInfo* componentType = reflection.GetComponentType(i);
					if (nullptr == componentType || IsScriptComponent(componentType) || false == isAvailable(componentType)) continue;

					const char* componentCategory =
					    componentType->Type.Category ? componentType->Type.Category : "Components";
					if (category != componentCategory) continue;

					const std::string componentLabel = EditorGuiDrawHelpers::LocalizedComponentLabel(*componentType);
					if (ImGui::MenuItem(componentLabel.c_str()))
					{
						Editor::CommandManager.ExecuteCommand(
						    MakeOwnerPtr<CAddComponentCommand>(
						        scene.SafeFromThis(), object, componentType->Type.Id));
						added = true;
					}
				}
				ImGui::EndMenu();
			}
		}

		if (ImGui::BeginMenu(Loc::Text("inspector.script_menu")))
		{
			added = DrawScriptList(scene, object, reflection) || added;
			ImGui::EndMenu();
		}

		return added;
	}
} // namespace

// ── 리플렉션 → 로컬라이즈 라벨 헬퍼 ────────────────────────────────────────
const char* EditorGuiDrawHelpers::GetScriptDisplayName(const ScriptTypeInfo* scriptType)
{
	if (nullptr == scriptType)
	{
		return Loc::Text("inspector.unknown_script");
	}
	if (scriptType->Type.DisplayName && scriptType->Type.DisplayName[0] != '\0')
	{
		return scriptType->Type.DisplayName;
	}
	return scriptType->Type.Name ? scriptType->Type.Name : Loc::Text("inspector.unknown_script");
}

std::string EditorGuiDrawHelpers::LocalizedComponentLabel(const ComponentTypeInfo& componentType)
{
	const char* fallback = componentType.Type.DisplayName ? componentType.Type.DisplayName : componentType.Type.Name;
	const std::string key = std::string("editor.component.") + (componentType.Type.Name ? componentType.Type.Name : "");
	return Loc::TextOr(key.c_str(), fallback ? fallback : "");
}

std::string EditorGuiDrawHelpers::LocalizedPropertyLabel(const ReflectPropertyInfo& property)
{
	const char* fallback = property.DisplayName ? property.DisplayName : property.Name;
	const std::string key = std::string("editor.property.") + (property.Name ? property.Name : "");
	return Loc::TextOr(key.c_str(), fallback ? fallback : "");
}

std::string EditorGuiDrawHelpers::LocalizedCategoryLabel(const char* category)
{
	const char* safe = category ? category : "Components";
	const std::string key = std::string("editor.category.") + safe;
	return Loc::TextOr(key.c_str(), safe);
}

bool EditorGuiDrawHelpers::DrawAddComponentMenu(CScene& scene, CGameObject* object)
{
	if (ImGui::BeginMenu(Loc::Text("inspector.add_component")))
	{
		const bool added = DrawComponentList(scene, object);
		ImGui::EndMenu();
		return added;
	}
	return false;
}

bool EditorGuiDrawHelpers::DrawAddComponentButton(CScene& scene, CGameObject* object)
{
	bool added = false;
	if (ImGui::Button(Loc::Text("inspector.add_component")))
	{
		ImGui::OpenPopup("AddComponentPopup");
	}

	if (ImGui::BeginPopup("AddComponentPopup"))
	{
		added = DrawComponentList(scene, object);
		ImGui::EndPopup();
	}
	return added;
}

bool EditorGuiDrawHelpers::DrawAddObjectMenu(CScene& scene, CGameObject* parent)
{
	// parent 유무에 따라 레이블 변경
	const char* label = (nullptr != parent)
	                        ? Loc::Text("inspector.add_child_object")
	                        : Loc::Text("inspector.add_object");

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

bool EditorGuiDrawHelpers::DrawRemoveObjectMenu(CScene& scene, CGameObject* object)
{
	(void)scene; (void)object;
	return false;
}

#endif

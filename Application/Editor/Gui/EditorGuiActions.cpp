#include "pch.h"
#include "EditorGuiActions.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Command/EditorSceneCommands.h"
#include "Editor/Editor.h"
#include "Editor/Localization/EditorReflectionLabels.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/GameFramework/Component/Component.h"
#include "Engine/GameFramework/Object/GameObject.h"
#include "Engine/GameFramework/Reflection/ReflectionRegistry.h"
#include "Engine/GameFramework/Scene/Scene.h"
#include "Engine/GameFramework/Scene/SceneRuntimeAccess.h"
#include "Engine/GameFramework/Serialization/ComponentSerializer.h"
#include "Engine/GameFramework/Serialization/ObjectSerializer.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace
{
	std::string ToLowerAscii(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return value;
	}

	bool DrawScriptList(CGameScene& scene, CGameObject* object, CReflectionRegistry& reflection)
	{
		if (nullptr == object)
		{
			return false;
		}

		if (0 == reflection.GetScriptTypeCount())
		{
			ImGui::TextDisabled(Loc::Text(EditorLocKeys::InspectorNoScriptsRegistered));
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
				std::string(EditorReflectionLabels::GetScriptDisplayName(scriptType)) + "##" + std::to_string(scriptType->Type.Id);
			if (ImGui::MenuItem(label.c_str()))
			{
				added = Editor::CommandManager.ExecuteCommand(
					MakeOwnerPtr<CAddScriptCommand>(
						scene.SafeFromThis(), object, scriptType->Type.Id));
			}
		}

		return added;
	}

	bool AddComponent(CGameScene& scene, CGameObject* object, const ComponentTypeInfo& componentType)
	{
		return Editor::CommandManager.ExecuteCommand(
			MakeOwnerPtr<CAddComponentCommand>(
				scene.SafeFromThis(), object, componentType.Type.Id));
	}

	bool AddScript(CGameScene& scene, CGameObject* object, const ScriptTypeInfo& scriptType)
	{
		return Editor::CommandManager.ExecuteCommand(
			MakeOwnerPtr<CAddScriptCommand>(
				scene.SafeFromThis(), object, scriptType.Type.Id));
	}

	bool DrawComponentList(CGameScene& scene, CGameObject* object, const char* filterText = "")
	{
		if (false == Engine.Reflection.IsValid() || nullptr == object)
		{
			ImGui::TextDisabled(Loc::Text(EditorLocKeys::InspectorNoComponentRegistry));
			return false;
		}

		bool added = false;
		CReflectionRegistry& reflection = *Engine.Reflection;

		auto canShowComponent = [](const ComponentTypeInfo* t) -> bool
		{
			return t && t->CanAddToObject;
		};

		auto canAddComponent = [&](const ComponentTypeInfo* t) -> bool
		{
			return canShowComponent(t) &&
			       reflection.CanAddComponent(*object, t->Type.Id);
		};

		const std::string filter = ToLowerAscii(filterText ? filterText : "");
		if (false == filter.empty())
		{
			int resultCount = 0;
			for (std::size_t i = 0; i < reflection.GetComponentTypeCount(); ++i)
			{
				const ComponentTypeInfo* componentType = reflection.GetComponentType(i);
				if (false == canShowComponent(componentType)) continue;

				const std::string componentLabel = EditorReflectionLabels::GetComponentLabel(*componentType);
				const char* category = componentType->Type.Category ? componentType->Type.Category : "Components";
				const char* typeName = componentType->Type.Name ? componentType->Type.Name : "";
				const std::string haystack = ToLowerAscii(componentLabel + " " + typeName + " " + category);
				if (haystack.find(filter) == std::string::npos)
				{
					continue;
				}

				const std::string label = componentLabel + "##component." + std::to_string(componentType->Type.Id);
				const bool canAdd = canAddComponent(componentType);
				++resultCount;
				ImGui::BeginDisabled(false == canAdd);
				if (ImGui::Selectable(label.c_str()) && canAdd)
				{
					added = AddComponent(scene, object, *componentType);
					if (added)
					{
						ImGui::CloseCurrentPopup();
					}
				}
				ImGui::EndDisabled();
				if (false == canAdd && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				{
					ImGui::SetTooltip("%s", Loc::Text(EditorLocKeys::CommonAlreadyAdded));
				}
			}

			for (std::size_t i = 0; i < reflection.GetScriptTypeCount(); ++i)
			{
				const ScriptTypeInfo* scriptType = reflection.GetScriptType(i);
				if (nullptr == scriptType || scriptType->Type.Id == INVALID_TYPE_ID)
				{
					continue;
				}

				const std::string scriptLabel = EditorReflectionLabels::GetScriptDisplayName(scriptType);
				const char* typeName = scriptType->Type.Name ? scriptType->Type.Name : "";
				const std::string haystack = ToLowerAscii(scriptLabel + " " + typeName + " Script");
				if (haystack.find(filter) == std::string::npos)
				{
					continue;
				}

				const std::string label = scriptLabel + "##script." + std::to_string(scriptType->Type.Id);
				++resultCount;
				if (ImGui::Selectable(label.c_str()))
				{
					added = AddScript(scene, object, *scriptType);
					if (added)
					{
						ImGui::CloseCurrentPopup();
					}
				}
			}

			if (0 == resultCount)
			{
				ImGui::TextDisabled("%s", Loc::Text(EditorLocKeys::CommonNoResults));
			}
			return added;
		}

		std::vector<std::string> categories;
		for (std::size_t i = 0; i < reflection.GetComponentTypeCount(); ++i)
		{
			const ComponentTypeInfo* componentType = reflection.GetComponentType(i);
			if (false == canShowComponent(componentType)) continue;

			const char* category = componentType->Type.Category ? componentType->Type.Category : "Components";
			if (std::find(categories.begin(), categories.end(), category) == categories.end())
				categories.emplace_back(category);
		}

		for (const std::string& category : categories)
		{
			const std::string categoryLabel = EditorReflectionLabels::GetCategoryLabel(category.c_str());
			if (ImGui::BeginMenu(categoryLabel.c_str()))
			{
				for (std::size_t i = 0; i < reflection.GetComponentTypeCount(); ++i)
				{
					const ComponentTypeInfo* componentType = reflection.GetComponentType(i);
					if (false == canShowComponent(componentType)) continue;

					const char* componentCategory =
					    componentType->Type.Category ? componentType->Type.Category : "Components";
					if (category != componentCategory) continue;

					const std::string componentLabel = EditorReflectionLabels::GetComponentLabel(*componentType);
					const bool canAdd = canAddComponent(componentType);
					if (ImGui::MenuItem(componentLabel.c_str(), nullptr, false, canAdd))
					{
						added = AddComponent(scene, object, *componentType);
					}
					if (false == canAdd && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					{
						ImGui::SetTooltip("%s", Loc::Text(EditorLocKeys::CommonAlreadyAdded));
					}
				}
				ImGui::EndMenu();
			}
		}

		if (ImGui::BeginMenu(Loc::Text(EditorLocKeys::InspectorScriptMenu)))
		{
			added = DrawScriptList(scene, object, reflection) || added;
			ImGui::EndMenu();
		}

		return added;
	}
} // namespace

bool EditorGuiActions::DrawAddComponentMenu(CGameScene& scene, CGameObject* object)
{
	if (ImGui::BeginMenu(Loc::Text(EditorLocKeys::InspectorAddComponent)))
	{
		const bool added = DrawComponentList(scene, object);
		ImGui::EndMenu();
		return added;
	}
	return false;
}

bool EditorGuiActions::DrawAddComponentButton(CGameScene& scene, CGameObject* object, const char* buttonLabel)
{
	bool added = false;
	const char* label = buttonLabel ? buttonLabel : Loc::Text(EditorLocKeys::InspectorAddComponent);
	if (ImGui::Selectable(label, false, ImGuiSelectableFlags_SpanAvailWidth))
	{
		ImGui::OpenPopup("AddComponentPopup");
	}

	if (ImGui::BeginPopup("AddComponentPopup"))
	{
		static char filter[128] = {};
		ImGui::SetNextItemWidth(130.0f);
		ImGui::InputTextWithHint("##component_filter", Loc::Text(EditorLocKeys::CommonFilter), filter, sizeof(filter));
		ImGui::Separator();
		added = DrawComponentList(scene, object, filter);
		if (added)
		{
			filter[0] = '\0';
		}
		ImGui::EndPopup();
	}
	return added;
}

bool EditorGuiActions::DrawAddObjectMenu(CGameScene& scene, CGameObject* parent, const Vector2* spawnWorldPos,
                                         CGameLayer* layer)
{
	// parent 유무에 따라 레이블 변경
	const char* label = (nullptr != parent)
	                        ? Loc::Text(EditorLocKeys::InspectorAddChildObject)
	                        : Loc::Text(EditorLocKeys::InspectorAddObject);

	if (ImGui::MenuItem(label))
	{
		OwnerPtr<CCreateGameObjectCommand> cmd =
		    MakeOwnerPtr<CCreateGameObjectCommand>(scene.SafeFromThis(), "GameObject", parent, spawnWorldPos, layer);
		CCreateGameObjectCommand* rawCmd = cmd.Get();
		if (Editor::CommandManager.ExecuteCommand(std::move(cmd)) && rawCmd)
		{
			Editor::SelectEntity(rawCmd->GetEntity());
		}
		return true;
	}
	return false;
}

bool EditorGuiActions::DrawRemoveObjectMenu(CGameScene& scene, CGameObject* object)
{
	if (nullptr == object)
	{
		return false;
	}

	if (ImGui::MenuItem(Loc::Text(EditorLocKeys::HierarchyDeleteObject)))
	{
		const bool wasSelected = Editor::IsSelected(object);
		Editor::CommandManager.ExecuteCommand(
			MakeOwnerPtr<CDeleteGameObjectCommand>(scene.SafeFromThis(), object));
		// 삭제된 오브젝트가 선택돼 있었으면 선택 해제(다음 프레임 파괴 → SafePtr null 이지만
		// 즉시 정리해 인스펙터 잔상 방지).
		if (wasSelected)
		{
			Editor::RemoveFromSelection(object);
		}
		return true;
	}
	return false;
}

// ── 복사 / 붙여넣기 ──────────────────────────────────────────────────────────

bool EditorGuiActions::DrawCopyObjectMenuItem(const CGameObject& object)
{
	if (ImGui::MenuItem(Loc::TextOr(EditorLocKeys::EditorMenuCopyObject, "Copy Object")))
	{
		const std::string text = Serialization::SerializeObject(object);
		ImGui::SetClipboardText(text.c_str());
		return true;
	}
	return false;
}

bool EditorGuiActions::DrawPasteObjectMenuItem(CGameScene& scene, CGameObject* parent)
{
	if (false == HasObjectClipboardData())
	{
		return false;   // 클립보드가 오브젝트가 아니면 메뉴를 숨긴다.
	}
	if (ImGui::MenuItem(Loc::TextOr(EditorLocKeys::EditorMenuPasteObject, "Paste Object")))
	{
		// 원본 위치 유지(메뉴 붙여넣기). undo/새 guid 발급은 커맨드가 처리.
		return PasteObjectsFromClipboard(scene, nullptr, parent);
	}
	return false;
}

bool EditorGuiActions::HasObjectClipboardData()
{
	const char* clipboardText = ImGui::GetClipboardText();
	return nullptr != clipboardText && Serialization::LooksLikeObject(clipboardText);
}

bool EditorGuiActions::CopySelectedObjectsToClipboard()
{
	const std::vector<CGameObject*> roots = Editor::GetSelectedTopLevel();
	if (roots.empty())
	{
		return false;
	}
	const std::vector<const CGameObject*> objects(roots.begin(), roots.end());
	const std::string text = Serialization::SerializeObjects(objects);
	if (text.empty())
	{
		return false;
	}
	ImGui::SetClipboardText(text.c_str());
	return true;
}

bool EditorGuiActions::PasteObjectsFromClipboard(CGameScene& scene, const Vector2* spawnWorldPos, CGameObject* parent)
{
	const char* clip = ImGui::GetClipboardText();
	if (nullptr == clip || false == Serialization::LooksLikeObject(clip))
	{
		return false;
	}
	OwnerPtr<CPasteObjectsCommand> cmd =
	    MakeOwnerPtr<CPasteObjectsCommand>(scene.SafeFromThis(), std::string(clip), spawnWorldPos, parent);
	CPasteObjectsCommand* rawCmd = cmd.Get();
	if (false == Editor::CommandManager.ExecuteCommand(std::move(cmd)) || nullptr == rawCmd)
	{
		return false;
	}
	Editor::SelectEntities(rawCmd->GetPastedRoots());
	return true;
}

bool EditorGuiActions::DeleteSelectedObjects(CGameScene& scene)
{
	const std::vector<CGameObject*> roots = Editor::GetSelectedTopLevel();
	if (roots.empty())
	{
		return false;
	}

	if (false == Editor::CommandManager.ExecuteCommand(
		MakeOwnerPtr<CDeleteGameObjectsCommand>(scene.SafeFromThis(), roots)))
	{
		return false;
	}

	Editor::ClearSelection();
	return true;
}

bool EditorGuiActions::DrawCopyComponentMenuItem(const CComponent& component)
{
	if (ImGui::MenuItem(Loc::TextOr(EditorLocKeys::EditorMenuCopyComponent, "Copy Component")))
	{
		const std::string text = Serialization::SerializeComponent(component);
		if (false == text.empty())
		{
			ImGui::SetClipboardText(text.c_str());
		}
		return true;
	}
	return false;
}

bool EditorGuiActions::DrawPasteComponentMenuItem(CGameObject& object)
{
	const char* clip = ImGui::GetClipboardText();
	if (nullptr == clip || false == Serialization::LooksLikeComponent(clip))
	{
		return false;   // 클립보드가 컴포넌트가 아니면 메뉴를 숨긴다.
	}
	if (ImGui::MenuItem(Loc::TextOr(EditorLocKeys::EditorMenuPasteComponent, "Paste Component")))
	{
		if (Serialization::DeserializeComponentInto(object, clip))
		{
			// 새 컴포넌트(방금 부착된 마지막 것)는 새 InstanceGuid 를 받아야 한다 —
			// 원본 guid 를 그대로 쓰면 Ref 지목이 원본/복사본 사이에서 충돌한다.
			const std::vector<SafePtr<CComponent>>& components = object.GetComponents();
			if (false == components.empty())
			{
				if (CComponent* component = components.back().TryGet())
				{
					CSceneRuntimeAccess::SetComponentInstanceGuid(*component, File::GenerateGuid());
				}
			}
			return true;
		}
	}
	return false;
}

#endif

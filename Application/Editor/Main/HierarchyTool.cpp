#include "pch.h"
#include "HierarchyTool.h"

#include "Editor/Command/EditorSceneCommands.h"
#include "Editor/EditorComponentMenu.h"
#include "Editor/Editor.h"
#include "Editor/Main/SceneViewTool.h"
#include "Engine/Core/Core.h"

#include <functional>
#include <unordered_map>

void CHierarchyTool::OnCreate()
{
	SetTitle("Hierarchy");
}

void CHierarchyTool::OnDestroy()
{
}

void CHierarchyTool::OnUpdate()
{
}

void CHierarchyTool::OnRenderStay()
{
	if (false == Core::SceneManager.IsValid())
	{
		ImGui::TextDisabled("SceneManager is not available.");
		return;
	}

	SafePtr<CScene> activeScene = Core::SceneManager->GetActiveScene();
	if (false == activeScene.IsValid())
	{
		ImGui::TextDisabled("No active scene.");
		Editor::ClearSelection();
		return;
	}

	auto drawBackgroundPopup = [&]()
	{
		if (ImGui::BeginPopupContextWindow("HierarchyBackgroundContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			if (ImGui::MenuItem("Add Object"))
			{
				OwnerPtr<CCreateGameObjectCommand> command = MakeOwnerPtr<CCreateGameObjectCommand>(activeScene, "GameObject");
				CCreateGameObjectCommand* rawCommand = command.Get();
				if (Editor::CommandManager.ExecuteCommand(std::move(command)) && rawCommand)
				{
					Editor::SelectEntity(rawCommand->GetEntity());
				}
			}
			ImGui::EndPopup();
		}
	};

	SceneSnapshot snapshot;
	activeScene->BuildSnapshot(snapshot);
	if (snapshot.Objects.empty())
	{
		ImGui::TextDisabled("Scene is empty.");
		Editor::ClearSelection();
		drawBackgroundPopup();
		return;
	}

	std::unordered_map<EntityId, std::vector<std::size_t>> childrenByParent;
	std::vector<std::size_t> rootIndices;
	for (std::size_t i = 0; i < snapshot.Objects.size(); ++i)
	{
		const SceneObjectSnapshot& object = snapshot.Objects[i];
		if (INVALID_ENTITY_ID == object.Parent)
		{
			rootIndices.push_back(i);
		}
		else
		{
			childrenByParent[object.Parent].push_back(i);
		}
	}

	std::function<void(std::size_t)> drawObject = [&](std::size_t objectIndex)
	{
		const SceneObjectSnapshot& object = snapshot.Objects[objectIndex];
		const auto childIt = childrenByParent.find(object.Entity);
		const bool hasChildren = childIt != childrenByParent.end() && false == childIt->second.empty();
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (false == hasChildren)
		{
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}
		if (Editor::GetSelectedEntity() == object.Entity)
		{
			flags |= ImGuiTreeNodeFlags_Selected;
		}

		const bool isOpen = ImGui::TreeNodeEx(
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(object.Entity)),
			flags,
			"%s",
			object.Name[0] ? object.Name : "GameObject");

		if (ImGui::IsItemClicked())
		{
			Editor::SelectEntity(object.Entity);
		}

		// 더블클릭 → 에디터 카메라가 해당 오브젝트를 포커싱
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			Editor::SelectEntity(object.Entity);
			if (Editor::SceneView)
			{
				Editor::SceneView->FocusOnEntity(object.Entity, *activeScene);
			}
		}

		if (ImGui::BeginPopupContextItem("HierarchyObjectContext"))
		{
			Editor::SelectEntity(object.Entity);
			EditorComponentMenu::DrawAddComponentMenu(*activeScene, object.Entity);
			ImGui::EndPopup();
		}

		if (hasChildren && isOpen)
		{
			for (std::size_t childIndex : childIt->second)
			{
				drawObject(childIndex);
			}
			ImGui::TreePop();
		}
	};

	for (std::size_t rootIndex : rootIndices)
	{
		drawObject(rootIndex);
	}

	drawBackgroundPopup();
}

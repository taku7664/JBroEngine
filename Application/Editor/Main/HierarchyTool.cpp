#include "pch.h"
#include "HierarchyTool.h"

#include "Editor/Editor.h"

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

	SceneSnapshot snapshot;
	activeScene->BuildSnapshot(snapshot);
	if (snapshot.Objects.empty())
	{
		ImGui::TextDisabled("Scene is empty.");
		Editor::ClearSelection();
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
}

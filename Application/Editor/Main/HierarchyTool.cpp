#include "pch.h"
#include "HierarchyTool.h"

#include "Editor/Command/EditorSceneCommands.h"
#include "Editor/Helper/EditorGuiDrawHelpers.h"
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

	// ── 빈 영역 우클릭 컨텍스트 메뉴 ────────────────────────────────────────────
	auto drawBackgroundPopup = [&]()
	{
		if (ImGui::BeginPopupContextWindow("HierarchyBackgroundContext",
		    ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			EditorGuiDrawHelpers::DrawAddObjectMenu(*activeScene, INVALID_ENTITY_ID);
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

	// ── 계층 인덱스 빌드 ────────────────────────────────────────────────────────
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

	// ── 드래그 중 "루트로 이동" 드롭 존 (상단) ─────────────────────────────────
	const bool isDragging = (ImGui::GetDragDropPayload() != nullptr);
	if (isDragging)
	{
		ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.25f, 0.45f, 0.25f, 0.55f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.60f, 0.30f, 0.80f));
		ImGui::Button("  Drop here to unparent  ", ImVec2(-1.0f, 0.0f));
		ImGui::PopStyleColor(2);

		if (ImGui::BeginDragDropTarget())
		{
			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY");
			if (payload)
			{
				EntityId dragged = *static_cast<const EntityId*>(payload->Data);
				if (activeScene->GetParent(dragged) != INVALID_ENTITY_ID)
				{
					auto cmd = MakeOwnerPtr<CSetParentCommand>(activeScene, dragged, INVALID_ENTITY_ID);
					Editor::CommandManager.ExecuteCommand(std::move(cmd));
					Editor::SelectEntity(dragged);
				}
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::Separator();
	}

	// ── 트리 노드 재귀 렌더링 ────────────────────────────────────────────────────
	std::function<void(std::size_t)> drawObject = [&](std::size_t objectIndex)
	{
		ImGui::Utillity::IDGroup idGroup(objectIndex); // 고유 ID 스코프 (인덱스 기반)

		const SceneObjectSnapshot& object = snapshot.Objects[objectIndex];
		const auto childIt  = childrenByParent.find(object.Entity);
		const bool hasChildren = (childIt != childrenByParent.end() && !childIt->second.empty());

		ImGuiTreeNodeFlags flags =
		    ImGuiTreeNodeFlags_OpenOnArrow |
		    ImGuiTreeNodeFlags_SpanAvailWidth;
		if (!hasChildren)
		{
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}
		if (Editor::IsSelected(object.Entity))
		{
			flags |= ImGuiTreeNodeFlags_Selected;
		}

		const char* name = object.Name[0] ? object.Name : "GameObject";
		const bool isOpen = ImGui::TreeNodeEx(
		    reinterpret_cast<void*>(static_cast<std::uintptr_t>(object.Entity)),
		    flags, "%s", name);

		// ── 클릭으로 선택 (트리 토글 클릭 제외) ─────────────────────────────
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
		{
			Editor::SelectEntity(object.Entity);
		}

		// ── 더블클릭 → 씬뷰 포커스 컨텍스트 전환 ──────────────────────────────
		// A가 포커스 컨텍스트여도 관계없이 C로 전환 가능.
		// FocusOnEntity(카메라만) 대신 SetFocusContext(컨텍스트 + 카메라)를 호출.
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			Editor::SelectEntity(object.Entity);
			if (Editor::SceneView)
			{
				Editor::SceneView->SetFocusContext(object.Entity, *activeScene);
			}
		}

		// ── Drag Source ───────────────────────────────────────────────────────
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
		{
			ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &object.Entity, sizeof(EntityId));
			ImGui::Text("Move: %s", name);
			ImGui::EndDragDropSource();
		}

		// ── Drop Target: 이 노드의 자식으로 ──────────────────────────────────
		if (ImGui::BeginDragDropTarget())
		{
			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY");
			if (payload)
			{
				EntityId dragged = *static_cast<const EntityId*>(payload->Data);
				// 자기 자신 및 사이클 방지
				const bool valid = (dragged != object.Entity)
				    && !activeScene->IsDescendantOf(object.Entity, dragged);
				if (valid)
				{
					auto cmd = MakeOwnerPtr<CSetParentCommand>(activeScene, dragged, object.Entity);
					Editor::CommandManager.ExecuteCommand(std::move(cmd));
					Editor::SelectEntity(dragged);
				}
			}
			ImGui::EndDragDropTarget();
		}

		// ── 우클릭 컨텍스트 메뉴 ──────────────────────────────────────────────
		if (ImGui::BeginPopupContextItem("HierarchyObjectContext"))
		{
			Editor::SelectEntity(object.Entity);

			if (object.Parent != INVALID_ENTITY_ID)
			{
				if (ImGui::MenuItem("Unparent"))
				{
					auto cmd = MakeOwnerPtr<CSetParentCommand>(activeScene, object.Entity, INVALID_ENTITY_ID);
					Editor::CommandManager.ExecuteCommand(std::move(cmd));
				}
				ImGui::Separator();
			}

			EditorGuiDrawHelpers::DrawAddComponentMenu(*activeScene, object.Entity);
			ImGui::EndPopup();
		}

		// ── 자식 노드 재귀 렌더링 ────────────────────────────────────────────
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

	// ── 빈 영역 드롭 존: 부모 해제 ───────────────────────────────────────────────
	// 트리 아래 남은 공간을 드롭 타깃으로 사용한다.
	// 드래그 중일 때만 활성화해 일반 클릭에 영향을 주지 않는다.
	if (isDragging)
	{
		const float remainingY = ImGui::GetContentRegionAvail().y;
		if (remainingY > 4.0f)
		{
			ImGui::InvisibleButton("##HierarchyRootDrop", ImVec2(-1.0f, remainingY));
			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY");
				if (payload)
				{
					EntityId dragged = *static_cast<const EntityId*>(payload->Data);
					if (activeScene->GetParent(dragged) != INVALID_ENTITY_ID)
					{
						auto cmd = MakeOwnerPtr<CSetParentCommand>(activeScene, dragged, INVALID_ENTITY_ID);
						Editor::CommandManager.ExecuteCommand(std::move(cmd));
						Editor::SelectEntity(dragged);
					}
				}
				ImGui::EndDragDropTarget();
			}
		}
	}

	drawBackgroundPopup();
}

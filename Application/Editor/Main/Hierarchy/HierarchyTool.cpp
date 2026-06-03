#include "pch.h"
#include "HierarchyTool.h"

#include "Engine/Editor/ImItem/ImTree.h"

#include "Editor/Command/EditorSceneCommands.h"
#include "Editor/Helper/EditorGuiDrawHelpers.h"
#include "Editor/Editor.h"
#include "Editor/EditorDragDrop.h"
#include "Editor/Main/SceneView/SceneViewTool.h"
#include "Engine/Core/Core.h"
#include "Engine/GameFramework/Reflection/ReflectionRegistry.h"
#include "Engine/GameFramework/Component/ScriptComponent.h"
#include "Engine/GameFramework/Object/GameObject.h"
#include "Engine/GameFramework/Scene/Scene.h"

#include <vector>

#include <cstdint>
#include <cstring>
#include <functional>
#include <unordered_map>

void CHierarchyTool::OnCreate()
{
	SetLocalizedTitleKey("window.hierarchy");
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
		ImGui::TextDisabled(Loc::Text("hierarchy.scene_manager_unavailable"));
		return;
	}

	SafePtr<CScene> activeScene = Core::SceneManager->GetActiveScene();
	if (false == activeScene.IsValid())
	{
		ImGui::TextDisabled(Loc::Text("hierarchy.no_active_scene"));
		Editor::ClearSelection();
		return;
	}

	// ── 빈 영역 우클릭 컨텍스트 메뉴 ────────────────────────────────────────────
	auto drawBackgroundPopup = [&]()
	{
		if (ImGui::BeginPopupContextWindow("HierarchyBackgroundContext",
		    ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			EditorGuiDrawHelpers::DrawAddObjectMenu(*activeScene, nullptr);
			ImGui::EndPopup();
		}
	};

	// ── 활성 오브젝트 수집 + 계층 인덱스 빌드(id 기반) ──────────────────────────
	std::vector<CGameObject*> objects;
	activeScene->ForEachObject([&objects](CGameObject& o){ objects.push_back(&o); });
	if (objects.empty())
	{
		ImGui::TextDisabled(Loc::Text("hierarchy.scene_empty"));
		Editor::ClearSelection();
		drawBackgroundPopup();
		return;
	}

	std::unordered_map<const CGameObject*, std::vector<std::size_t>> childrenByParent;
	std::vector<std::size_t> rootIndices;
	for (std::size_t i = 0; i < objects.size(); ++i)
	{
		if (CGameObject* parent = objects[i]->GetParent().TryGet())
		{
			childrenByParent[parent].push_back(i);
		}
		else
		{
			rootIndices.push_back(i);
		}
	}

	// ── 드래그 중 "루트로 이동" 드롭 존 (상단) ─────────────────────────────────
	// 다른 위젯의 드래그(예: ImGui::Utillity::List 의 reorder)가 진행 중일 때도
	// GetDragDropPayload 는 nullptr 이 아니다. payload 타입이 HIERARCHY_ENTITY 인
	// 경우에만 unparent 드롭 존을 표시한다.
	const ImGuiPayload* currentDragPayload = ImGui::GetDragDropPayload();
	const bool isDragging = currentDragPayload != nullptr
	                      && currentDragPayload->IsDataType("HIERARCHY_ENTITY");
	if (isDragging)
	{
		ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.25f, 0.45f, 0.25f, 0.55f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.60f, 0.30f, 0.80f));
		ImGui::Button(Loc::Text("hierarchy.drop_here_to_unparent"), ImVec2(-1.0f, 0.0f));
		ImGui::PopStyleColor(2);

		if (ImGui::BeginDragDropTarget())
		{
			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY");
			if (payload)
			{
				CGameObject* draggedObj = *static_cast<CGameObject* const*>(payload->Data);
				if (draggedObj && draggedObj->GetParent().IsValid())
				{
					auto cmd = MakeOwnerPtr<CSetParentCommand>(activeScene, draggedObj, nullptr);
					Editor::CommandManager.ExecuteCommand(std::move(cmd));
					Editor::SelectEntity(draggedObj);
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

		CGameObject* obj = objects[objectIndex];
		const auto childIt  = childrenByParent.find(obj);
		const bool hasChildren = (childIt != childrenByParent.end() && !childIt->second.empty());

		ImGuiTreeNodeFlags flags =
		    ImGuiTreeNodeFlags_OpenOnArrow |
		    ImGuiTreeNodeFlags_SpanAvailWidth;
		if (!hasChildren)
		{
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}
		if (Editor::IsSelected(obj))
		{
			flags |= ImGuiTreeNodeFlags_Selected;
		}
		const char* objName = obj->GetName();
		const char* name = (objName && objName[0]) ? objName : "GameObject";
		const bool isOpen = ImTree(name, flags);

		// ── 클릭으로 선택 (Release 기준) ───────────────────────────────────
		// Press 가 아니라 Release 에서 선택한다. Press 로 선택하면 드래그를 시작하는
		// 순간 선택이 바뀌어 인스펙터가 갱신되고, 드래그-드랍 대상(Ref 프로퍼티)이
		// 사라져 드롭을 못 한다. Release 기준이면: 단순 클릭은 선택, 드래그는
		// 아이템 밖에서 떼지므로(IsItemHovered=false) 선택이 일어나지 않는다.
		if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
			&& !ImGui::IsItemToggledOpen())
		{
			Editor::SelectEntity(obj);
		}

		// ── 더블클릭 → 씬뷰 포커스 컨텍스트 전환 ──────────────────────────────
		// A가 포커스 컨텍스트여도 관계없이 C로 전환 가능.
		// FocusOnEntity(카메라만) 대신 SetFocusContext(컨텍스트 + 카메라)를 호출.
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			Editor::SelectEntity(obj);
			if (Editor::SceneView)
			{
				Editor::SceneView->SetFocusContext(obj, *activeScene);
			}
		}

		// ── Drag Source ───────────────────────────────────────────────────────
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
		{
			ImGui::SetDragDropPayload(EditorDragDrop::HIERARCHY_ENTITY_PAYLOAD, &obj, sizeof(CGameObject*));
			ImGui::Text(Loc::Text("hierarchy.move_format"), name);
			ImGui::EndDragDropSource();
		}

		// ── Drop Target: 이 노드의 자식으로 ──────────────────────────────────
		if (ImGui::BeginDragDropTarget())
		{
			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY");
			if (payload)
			{
				CGameObject* draggedObj = *static_cast<CGameObject* const*>(payload->Data);
				// 자기 자신 및 사이클 방지
				const bool valid = (draggedObj != obj) && draggedObj
				    && !obj->IsDescendantOf(*draggedObj);
				if (valid)
				{
					auto cmd = MakeOwnerPtr<CSetParentCommand>(activeScene, draggedObj, obj);
					Editor::CommandManager.ExecuteCommand(std::move(cmd));
					Editor::SelectEntity(draggedObj);
				}
			}
			ImGui::EndDragDropTarget();
		}

		// ── 우클릭 컨텍스트 메뉴 ──────────────────────────────────────────────
		if (ImGui::BeginPopupContextItem("HierarchyObjectContext"))
		{
			Editor::SelectEntity(obj);

			if (obj->GetParent().IsValid())
			{
				if (ImGui::MenuItem(Loc::Text("hierarchy.unparent")))
				{
					auto cmd = MakeOwnerPtr<CSetParentCommand>(activeScene, obj, nullptr);
					Editor::CommandManager.ExecuteCommand(std::move(cmd));
				}
				ImGui::Separator();
			}

			EditorGuiDrawHelpers::DrawAddComponentMenu(*activeScene, obj);
			ImGui::Separator();
			EditorGuiDrawHelpers::DrawRemoveObjectMenu(*activeScene, obj);
			ImGui::EndPopup();
		}

		// ── 선택된 오브젝트: 컴포넌트 리스트를 자식보다 "위"에 표시 ───────────
		// 트리를 펼치지 않아도(선택만 돼도) 펼쳐진 것처럼 아래에 컴포넌트 리스트를
		// 나열한다. 트리를 펼쳤으면 컴포넌트 리스트가 자식 목록보다 위에 온다.
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
					CGameObject* draggedObj = *static_cast<CGameObject* const*>(payload->Data);
					if (draggedObj && draggedObj->GetParent().IsValid())
					{
						auto cmd = MakeOwnerPtr<CSetParentCommand>(activeScene, draggedObj, nullptr);
						Editor::CommandManager.ExecuteCommand(std::move(cmd));
						Editor::SelectEntity(draggedObj);
					}
				}
				ImGui::EndDragDropTarget();
			}
		}
	}

	drawBackgroundPopup();
}

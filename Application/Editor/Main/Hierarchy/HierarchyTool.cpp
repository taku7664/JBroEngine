#include "pch.h"
#include "HierarchyTool.h"

#include "Editor/ImItem/ImTree.h"
#include "Editor/ImItem/ImButton.h"

#include "Editor/Command/EditorSceneCommands.h"
#include "Editor/Icons/FontAwesomeIcons.h"
#include "Editor/Gui/EditorGuiActions.h"
#include "Editor/Editor.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorDragDrop.h"
#include "Editor/Main/SceneView/SceneViewTool.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/GameFramework/Reflection/ReflectionRegistry.h"
#include "Engine/GameFramework/Object/GameObject.h"
#include "Engine/GameFramework/Scene/Scene.h"

#include <vector>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <unordered_map>

void CHierarchyTool::OnCreate()
{
	SetLocalizedTitleKey(EditorLocKeys::WindowHierarchy);
}

void CHierarchyTool::OnDestroy()
{
}

void CHierarchyTool::OnUpdate()
{
}

void CHierarchyTool::OnRenderStay()
{
	if (false == Engine.SceneManager.IsValid())
	{
		ImGui::TextDisabled(Loc::Text(EditorLocKeys::HierarchySceneManagerUnavailable));
		return;
	}

	SafePtr<CGameScene> activeScene = EditorContext::GetActiveScene();
	if (false == activeScene.IsValid())
	{
		ImGui::TextDisabled(Loc::Text(EditorLocKeys::HierarchyNoActiveScene));
		Editor::ClearSelection();
		return;
	}

	// ── 빈 영역 우클릭 컨텍스트 메뉴 ────────────────────────────────────────────
	auto drawBackgroundPopup = [&]()
	{
		if (ImGui::BeginPopupContextWindow("HierarchyBackgroundContext",
		    ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			EditorGuiActions::DrawAddObjectMenu(*activeScene, nullptr);
			EditorGuiActions::DrawPasteObjectMenuItem(*activeScene);
			ImGui::EndPopup();
		}
	};

	// ── 활성 오브젝트 수집 + 계층 인덱스 빌드(id 기반) ──────────────────────────
	std::vector<CGameObject*> objects;
	activeScene->ForEachObject([&objects](CGameObject& o){ objects.push_back(&o); });
	if (objects.empty())
	{
		ImGui::TextDisabled(Loc::Text(EditorLocKeys::HierarchySceneEmpty));
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

	// 표시 순서 = 생성순서. 풀 슬롯 순회 순서는 생성순서와 무관(할당 역순·슬롯 재사용)하므로
	// 루트와 각 형제 그룹을 CreationOrder 로 정렬한다 → 새 오브젝트는 맨 아래, 시뮬 정지/재사용
	// 후에도 순서 불변.
	auto byCreationOrder = [&objects](std::size_t a, std::size_t b)
	{
		return objects[a]->CreationOrder < objects[b]->CreationOrder;
	};
	std::sort(rootIndices.begin(), rootIndices.end(), byCreationOrder);
	for (auto& entry : childrenByParent)
	{
		std::sort(entry.second.begin(), entry.second.end(), byCreationOrder);
	}

	// ── Hierarchy 엔티티 드래그 상태 ──────────────────────────────────────────
	// 다른 위젯의 드래그(예: ImGui::Utillity::List 의 reorder)가 진행 중일 때도
	// GetDragDropPayload 는 nullptr 이 아니다. payload 타입이 HIERARCHY_ENTITY 인
	// 경우에만 계층 드롭 UI를 표시한다.
	const ImGuiPayload* currentDragPayload = ImGui::GetDragDropPayload();
	const bool isDragging = currentDragPayload != nullptr
	                      && currentDragPayload->IsDataType("HIERARCHY_ENTITY");

	auto canMoveObject = [](CGameObject* draggedObj, CGameObject* newParent, CGameObject* insertNear) -> bool
	{
		if (nullptr == draggedObj || draggedObj == newParent || draggedObj == insertNear)
		{
			return false;
		}
		return nullptr == newParent || false == newParent->IsDescendantOf(*draggedObj);
	};

	auto executeHierarchyMove = [&](CGameObject* draggedObj, CGameObject* newParent,
	                                CGameObject* insertNear, bool insertAfter) -> bool
	{
		if (false == canMoveObject(draggedObj, newParent, insertNear))
		{
			return false;
		}

		auto cmd = MakeOwnerPtr<CMoveGameObjectInHierarchyCommand>(
			activeScene, draggedObj, newParent, insertNear, insertAfter);
		if (false == Editor::CommandManager.ExecuteCommand(std::move(cmd)))
		{
			return false;
		}
		Editor::SelectEntity(draggedObj);
		return true;
	};

	// ── 트리 노드 재귀 렌더링 ────────────────────────────────────────────────────
	struct PendingSelectionClick
	{
		CGameObject* Object = nullptr;
		bool Ctrl = false;
		bool Shift = false;
	};
	std::vector<CGameObject*> visibleObjects;
	visibleObjects.reserve(objects.size());
	PendingSelectionClick pendingSelection;

	std::function<void(std::size_t)> drawObject = [&](std::size_t objectIndex)
	{
		ImGui::Utillity::IDGroup idGroup(objectIndex); // 고유 ID 스코프 (인덱스 기반)

		CGameObject* obj = objects[objectIndex];
		visibleObjects.push_back(obj);
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
		const ImRect treeRowRect = GImGui->LastItemData.DisplayRect;
		const float nodeH = ImGui::GetItemRectSize().y; // 실제 트리노드 줄 높이(눈 버튼 정렬용)

		// ── 클릭으로 선택 (Release 기준) ───────────────────────────────────
		// Press 가 아니라 Release 에서 선택한다. Press 로 선택하면 드래그를 시작하는
		// 순간 선택이 바뀌어 인스펙터가 갱신되고, 드래그-드랍 대상(Ref 프로퍼티)이
		// 사라져 드롭을 못 한다. Release 기준이면: 단순 클릭은 선택, 드래그는
		// 아이템 밖에서 떼지므로(IsItemHovered=false) 선택이 일어나지 않는다.
		if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
			&& !ImGui::IsItemToggledOpen())
		{
			pendingSelection.Object = obj;
			pendingSelection.Ctrl = ImGui::GetIO().KeyCtrl;
			pendingSelection.Shift = ImGui::GetIO().KeyShift;
		}

		// ── 더블클릭 → 씬뷰 포커스 컨텍스트 전환 ──────────────────────────────
		// A가 포커스 컨텍스트여도 관계없이 C로 전환 가능.
		// FocusOnEntity(카메라만) 대신 SetFocusContext(컨텍스트 + 카메라)를 호출.
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			Editor::SelectEntity(obj);
			m_selectionAnchorGuid = obj->InstanceGuid;
			if (Editor::SceneView)
			{
				Editor::SceneView->SetFocusContext(obj, *activeScene);
			}
		}

		// ── Drag Source ───────────────────────────────────────────────────────
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
		{
			ImGui::SetDragDropPayload(EditorDragDrop::HIERARCHY_ENTITY_PAYLOAD, &obj, sizeof(CGameObject*));
			ImGui::Text(Loc::Text(EditorLocKeys::HierarchyMoveFormat), name);
			ImGui::EndDragDropSource();
		}

		// ── Drop Target ──────────────────────────────────────────────────────
		// 행 위쪽 25%  : 이 오브젝트 앞에 삽입
		// 행 중앙 50%  : 이 오브젝트의 자식으로 이동
		// 행 아래쪽 25%: 이 오브젝트 뒤에 삽입
		if (isDragging)
		{
			const ImVec2 cursorAfterTree = ImGui::GetCursorScreenPos();
			ImGui::SetCursorScreenPos(treeRowRect.Min);
			ImGui::InvisibleButton("##HierarchyFullRowDrop", treeRowRect.GetSize());
			ImGui::SetCursorScreenPos(cursorAfterTree);
		}

		if (isDragging && ImGui::BeginDragDropTarget())
		{
			const ImVec2 rowMin = treeRowRect.Min;
			const ImVec2 rowMax = treeRowRect.Max;
			const float rowHeight = std::max(1.0f, rowMax.y - rowMin.y);
			const float mouseY = ImGui::GetIO().MousePos.y;
			const float localY = std::clamp((mouseY - rowMin.y) / rowHeight, 0.0f, 1.0f);
			const bool dropBefore = localY < 0.25f;
			const bool dropAfter = localY > 0.75f;

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImU32 color = ImGui::GetColorU32(ImVec4(0.25f, 0.70f, 1.00f, 1.00f));
			if (dropBefore || dropAfter)
			{
				const float y = dropBefore ? rowMin.y : rowMax.y;
				drawList->AddLine(ImVec2(rowMin.x, y), ImVec2(rowMax.x, y), color, 2.0f);
			}
			else
			{
				drawList->AddRect(rowMin, rowMax, color);
			}

			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY");
			if (payload)
			{
				CGameObject* draggedObj = *static_cast<CGameObject* const*>(payload->Data);
				if (dropBefore || dropAfter)
				{
					executeHierarchyMove(draggedObj, obj->GetParent().TryGet(), obj, dropAfter);
				}
				else
				{
					executeHierarchyMove(draggedObj, obj, nullptr, true);
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
				if (ImGui::MenuItem(Loc::Text(EditorLocKeys::HierarchyUnparent)))
				{
					auto cmd = MakeOwnerPtr<CSetParentCommand>(activeScene, obj, nullptr);
					Editor::CommandManager.ExecuteCommand(std::move(cmd));
				}
				ImGui::Separator();
			}

			EditorGuiActions::DrawAddComponentMenu(*activeScene, obj);
			ImGui::Separator();
			EditorGuiActions::DrawCopyObjectMenuItem(*obj);
			EditorGuiActions::DrawPasteObjectMenuItem(*activeScene);
			ImGui::Separator();
			EditorGuiActions::DrawRemoveObjectMenu(*activeScene, obj);
			ImGui::EndPopup();
		}

		// ── 가시성 토글(눈 아이콘, 우측 정렬). 씬뷰 전용 EditorHidden 플래그. ──
		// 트리노드 상호작용(선택/드래그/드롭/컨텍스트) 처리 뒤에 그려야 IsItem* 이
		// 트리노드를 가리킨다. SameLine(절대x)로 같은 행 우측에 배치.
		{
			const bool  hidden = obj->IsEditorHidden();
			// 버튼 높이를 실제 트리노드 줄 높이(nodeH)에 정확히 맞춘다(오버플로 방지).
			// 글리프만 0.7 로 축소 → 버튼 안 세로 중앙(=줄 중앙).
			ImGui::SameLine(ImGui::GetContentRegionMax().x - nodeH);
			const char* icon = hidden ? EditorIcons::ICON_EYE_SLASH : EditorIcons::ICON_EYE;
			ImText eyeText;
			eyeText.SetScale(0.7f).SetAlign(ImText::Align::Center);
			if (ImTextButton(eyeText, icon, ImVec2(nodeH, nodeH)))
			{
				obj->SetEditorHidden(!hidden);
			}
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

	if (pendingSelection.Object)
	{
		auto findVisibleObjectByGuid = [&visibleObjects](const File::Guid& guid) -> CGameObject*
		{
			if (guid.IsNull())
			{
				return nullptr;
			}

			for (CGameObject* visibleObject : visibleObjects)
			{
				if (visibleObject && visibleObject->InstanceGuid == guid)
				{
					return visibleObject;
				}
			}
			return nullptr;
		};

		if (pendingSelection.Shift)
		{
			CGameObject* anchor = findVisibleObjectByGuid(m_selectionAnchorGuid);
			if (nullptr == anchor)
			{
				anchor = Editor::GetSelectedEntity();
			}
			if (nullptr == anchor)
			{
				anchor = pendingSelection.Object;
			}

			const auto anchorIt = std::find(visibleObjects.begin(), visibleObjects.end(), anchor);
			const auto clickedIt = std::find(visibleObjects.begin(), visibleObjects.end(), pendingSelection.Object);
			if (anchorIt != visibleObjects.end() && clickedIt != visibleObjects.end())
			{
				const auto rangeBegin = anchorIt < clickedIt ? anchorIt : clickedIt;
				const auto rangeEnd = anchorIt < clickedIt ? clickedIt : anchorIt;
				std::vector<CGameObject*> range(rangeBegin, rangeEnd + 1);
				if (pendingSelection.Ctrl)
				{
					for (CGameObject* object : range)
					{
						Editor::AddToSelection(object);
					}
				}
				else
				{
					Editor::SelectEntities(range);
				}
			}
		}
		else if (pendingSelection.Ctrl)
		{
			if (Editor::IsSelected(pendingSelection.Object))
			{
				Editor::RemoveFromSelection(pendingSelection.Object);
			}
			else
			{
				Editor::AddToSelection(pendingSelection.Object);
			}
			m_selectionAnchorGuid = pendingSelection.Object->InstanceGuid;
		}
		else
		{
			Editor::SelectEntity(pendingSelection.Object);
			m_selectionAnchorGuid = pendingSelection.Object->InstanceGuid;
		}
	}

	drawBackgroundPopup();
}

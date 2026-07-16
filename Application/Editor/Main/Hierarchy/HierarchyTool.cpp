#include "pch.h"
#include "HierarchyTool.h"

#include "Editor/ImItem/ImTree.h"
#include "Editor/ImItem/ImButton.h"
#include "Editor/ImItem/ImLayerHeader.h"
#include "Engine/Editor/ImEditor.h"

#include "Editor/Command/EditorLayerCommands.h"
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
#include "Engine/GameFramework/Scene/GameLayer.h"
#include "Engine/GameFramework/Scene/Scene.h"

#include <vector>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>

void CLayerTool::OnCreate()
{
	SetLocalizedTitleKey(EditorLocKeys::WindowLayers);
}

void CLayerTool::OnDestroy()
{
	// 레이어 썸네일은 이 창만 쓴다 — 창이 사라지면 RT 도 놓는다.
	if (Editor::ImEditor.IsValid())
	{
		Editor::ImEditor->RequestLayerThumbnails(0);
	}
}

void CLayerTool::OnUpdate()
{
}

void CLayerTool::OnRenderStay()
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

	// 레이어 생성은 배경 팝업과 레이어 컨텍스트 메뉴 양쪽에서 쓴다.
	auto drawAddLayerMenuItem = [&]()
	{
		if (ImGui::MenuItem(Loc::Text(EditorLocKeys::HierarchyAddLayer)))
		{
			auto cmd = MakeOwnerPtr<CCreateLayerCommand>(activeScene, nullptr);
			CCreateLayerCommand* rawCmd = cmd.Get();
			if (Editor::CommandManager.ExecuteCommand(std::move(cmd)) && rawCmd)
			{
				Editor::SelectLayer(rawCmd->GetLayer());
			}
		}
	};

	// ── 빈 영역 우클릭 컨텍스트 메뉴 ────────────────────────────────────────────
	// 오브젝트 추가 대상 레이어 = 현재 선택 레이어(없으면 씬 기본 레이어).
	auto drawBackgroundPopup = [&]()
	{
		if (ImGui::BeginPopupContextWindow("HierarchyBackgroundContext",
		    ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			EditorGuiActions::DrawAddObjectMenu(*activeScene, nullptr, nullptr, Editor::GetSelectedLayer());
			EditorGuiActions::DrawPasteObjectMenuItem(*activeScene);
			ImGui::Separator();
			drawAddLayerMenuItem();
			ImGui::EndPopup();
		}
	};

	// ── 활성 오브젝트 수집 + 계층 인덱스 빌드(id 기반) ──────────────────────────
	// 오브젝트가 0개여도 레이어는 그린다(레이어는 오브젝트와 독립된 캔버스 구성 요소).
	std::vector<CGameObject*> objects;
	activeScene->ForEachObject([&objects](CGameObject& o){ objects.push_back(&o); });

	std::unordered_map<const CGameObject*, std::vector<std::size_t>> childrenByParent;
	std::unordered_map<const CGameLayer*, std::vector<std::size_t>> rootsByLayer;
	for (std::size_t i = 0; i < objects.size(); ++i)
	{
		if (CGameObject* parent = objects[i]->GetParent().TryGet())
		{
			childrenByParent[parent].push_back(i);
		}
		else
		{
			rootsByLayer[objects[i]->GetLayer().TryGet()].push_back(i);
		}
	}

	// 표시 순서 = 생성순서. 풀 슬롯 순회 순서는 생성순서와 무관(할당 역순·슬롯 재사용)하므로
	// 루트와 각 형제 그룹을 CreationOrder 로 정렬한다 → 새 오브젝트는 맨 아래, 시뮬 정지/재사용
	// 후에도 순서 불변.
	auto byCreationOrder = [&objects](std::size_t a, std::size_t b)
	{
		return objects[a]->GetCreationOrder() < objects[b]->GetCreationOrder();
	};
	for (auto& entry : rootsByLayer)
	{
		std::sort(entry.second.begin(), entry.second.end(), byCreationOrder);
	}
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
	                      && currentDragPayload->IsDataType(EditorDragDrop::HIERARCHY_ENTITY_PAYLOAD);
	const bool isDraggingLayer = currentDragPayload != nullptr
	                      && currentDragPayload->IsDataType(EditorDragDrop::HIERARCHY_LAYER_PAYLOAD);

	auto canMoveObject = [](CGameObject* draggedObj, CGameObject* newParent, CGameObject* insertNear) -> bool
	{
		if (nullptr == draggedObj || draggedObj == newParent || draggedObj == insertNear)
		{
			return false;
		}
		return nullptr == newParent || false == newParent->IsDescendantOf(*draggedObj);
	};

	// newLayer = nullptr 이면 레이어 유지. 루트로 떨어지는 드롭에서만 의미가 있다
	// (자식으로 들어가면 부모 레이어를 따라간다).
	auto executeHierarchyMove = [&](CGameObject* draggedObj, CGameObject* newParent,
	                                CGameObject* insertNear, bool insertAfter,
	                                CGameLayer* newLayer = nullptr) -> bool
	{
		if (false == canMoveObject(draggedObj, newParent, insertNear))
		{
			return false;
		}

		auto cmd = MakeOwnerPtr<CMoveGameObjectInHierarchyCommand>(
			activeScene, draggedObj, newParent, insertNear, insertAfter, newLayer);
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
			m_selectionAnchorGuid = obj->GetInstanceGuid();
			if (Editor::CanvasView)
			{
				Editor::CanvasView->SetFocusContext(obj, *activeScene);
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

			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorDragDrop::HIERARCHY_ENTITY_PAYLOAD);
			if (payload)
			{
				CGameObject* draggedObj = *static_cast<CGameObject* const*>(payload->Data);
				if (dropBefore || dropAfter)
				{
					// 형제로 삽입 = 대상과 같은 레이어. 대상이 자식이면 레이어 인자는 무시된다.
					executeHierarchyMove(draggedObj, obj->GetParent().TryGet(), obj, dropAfter,
					                     obj->GetLayer().TryGet());
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

	// ── 레이어 노드 렌더링 ──────────────────────────────────────────────────────
	// 표시 순서는 포토샵식(위 = 컴포짓 마지막 = 화면 최전면)이므로 모델 인덱스의 역순으로
	// 그린다. 모델(m_layers)은 0 = 맨 아래 그대로 두고 표시만 뒤집는다.
	const std::size_t layerCount = activeScene->GetLayerCount();

	// 썸네일 크기 = 씬뷰 종횡비 × 요청 높이. ImEditor 가 씬뷰 레이어 합성 중에 채운다.
	const float thumbnailHeight = ImGui::GetTextLineHeight() * 2.0f;
	const bool hasImEditor = Editor::ImEditor.IsValid();
	if (hasImEditor)
	{
		Editor::ImEditor->RequestLayerThumbnails(static_cast<std::uint32_t>(thumbnailHeight));
	}

	auto drawLayer = [&](std::size_t layerIndex)
	{
		CGameLayer* layer = activeScene->GetLayerAt(layerIndex);
		if (nullptr == layer)
		{
			return;
		}

		ImGui::Utillity::IDGroup idGroup(static_cast<const void*>(layer)); // 오브젝트 인덱스 id 와 충돌 방지

		// 오브젝트 행 눈 버튼과 같은 폭(= ImTree 한 줄 행 높이) — 아이콘 x 가 어긋나지 않게.
		// ImTreeRender 의 한 줄 높이 계산과 같은 식이다(텍스트 높이 + 3).
		const float eyeButtonWidth = ImGui::GetTextLineHeight() + 3.0f;

		ImLayerHeaderDesc headerDesc;
		headerDesc.Label = layer->GetName();
		headerDesc.Selected = (Editor::GetSelectedLayer() == layer);
		headerDesc.ReservedRightWidth = eyeButtonWidth;
		if (hasImEditor && Editor::ImEditor->GetLayerThumbnailHeight() > 0)
		{
			headerDesc.Thumbnail = reinterpret_cast<ImTextureID>(
				Editor::ImEditor->GetLayerThumbnailTextureID(layer->GetIndex()));
			headerDesc.ThumbnailSize = ImVec2(
				static_cast<float>(Editor::ImEditor->GetLayerThumbnailWidth()),
				static_cast<float>(Editor::ImEditor->GetLayerThumbnailHeight()));
		}

		const ImLayerHeaderResult header = ImLayerHeader("##LayerHeader", headerDesc);
		const bool isOpen = header.IsOpen;
		const ImRect layerRowRect = header.RowRect;
		const float nodeH = header.RowHeight;

		if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
			&& !ImGui::IsItemToggledOpen())
		{
			Editor::SelectLayer(layer);
		}

		// ── Drag Source(컴포짓 순서 재배치) ───────────────────────────────────
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
		{
			ImGui::SetDragDropPayload(EditorDragDrop::HIERARCHY_LAYER_PAYLOAD, &layer, sizeof(CGameLayer*));
			ImGui::Text(Loc::Text(EditorLocKeys::HierarchyLayerMoveFormat), layer->GetName());
			ImGui::EndDragDropSource();
		}

		// ── Drop Target ──────────────────────────────────────────────────────
		// 오브젝트 드롭 = 이 레이어의 루트로 이동. 레이어 드롭 = 이 레이어 위/아래로 재배치.
		if (isDragging || isDraggingLayer)
		{
			const ImVec2 cursorAfterTree = ImGui::GetCursorScreenPos();
			ImGui::SetCursorScreenPos(layerRowRect.Min);
			ImGui::InvisibleButton("##HierarchyLayerRowDrop", layerRowRect.GetSize());
			ImGui::SetCursorScreenPos(cursorAfterTree);

			if (ImGui::BeginDragDropTarget())
			{
				const ImU32 color = ImGui::GetColorU32(ImVec4(0.25f, 0.70f, 1.00f, 1.00f));
				ImDrawList* drawList = ImGui::GetWindowDrawList();

				if (isDraggingLayer)
				{
					// 행 위 절반 = 이 레이어보다 앞(위)에, 아래 절반 = 뒤(아래)에 배치.
					const float rowHeight = std::max(1.0f, layerRowRect.Max.y - layerRowRect.Min.y);
					const float localY = std::clamp(
						(ImGui::GetIO().MousePos.y - layerRowRect.Min.y) / rowHeight, 0.0f, 1.0f);
					const bool dropAbove = localY < 0.5f;
					const float lineY = dropAbove ? layerRowRect.Min.y : layerRowRect.Max.y;
					drawList->AddLine(ImVec2(layerRowRect.Min.x, lineY),
					                  ImVec2(layerRowRect.Max.x, lineY), color, 2.0f);

					const ImGuiPayload* payload =
						ImGui::AcceptDragDropPayload(EditorDragDrop::HIERARCHY_LAYER_PAYLOAD);
					if (payload)
					{
						CGameLayer* draggedLayer = *static_cast<CGameLayer* const*>(payload->Data);
						const int draggedIndex = activeScene->GetLayerIndex(draggedLayer);
						if (draggedLayer && draggedLayer != layer && draggedIndex >= 0)
						{
							// 목적지는 "드래그 레이어를 뺀 목록" 기준으로 계산한다 —
							// MoveLayer 가 erase 후 insert 하므로 그 기준이 곧 최종 위치다.
							std::size_t targetIndex = layerIndex;
							if (static_cast<std::size_t>(draggedIndex) < layerIndex)
							{
								--targetIndex;
							}
							// 표시가 뒤집혀 있으므로 "위에 놓기" = 모델 인덱스 +1.
							const std::size_t newIndex = dropAbove ? targetIndex + 1 : targetIndex;
							auto cmd = MakeOwnerPtr<CMoveLayerCommand>(activeScene, draggedLayer, newIndex);
							if (Editor::CommandManager.ExecuteCommand(std::move(cmd)))
							{
								Editor::SelectLayer(draggedLayer);
							}
						}
					}
				}
				else
				{
					drawList->AddRect(layerRowRect.Min, layerRowRect.Max, color);
					const ImGuiPayload* payload =
						ImGui::AcceptDragDropPayload(EditorDragDrop::HIERARCHY_ENTITY_PAYLOAD);
					if (payload)
					{
						CGameObject* draggedObj = *static_cast<CGameObject* const*>(payload->Data);
						// 레이어 행에 떨어뜨리면 부모를 떼고 그 레이어의 루트 맨 아래로 간다.
						executeHierarchyMove(draggedObj, nullptr, nullptr, true, layer);
					}
				}
				ImGui::EndDragDropTarget();
			}
		}

		// ── 우클릭 컨텍스트 메뉴 ──────────────────────────────────────────────
		if (ImGui::BeginPopupContextItem("HierarchyLayerContext"))
		{
			Editor::SelectLayer(layer);

			EditorGuiActions::DrawAddObjectMenu(*activeScene, nullptr, nullptr, layer);
			EditorGuiActions::DrawPasteObjectMenuItem(*activeScene, nullptr, layer);
			ImGui::Separator();
			drawAddLayerMenuItem();
			// 마지막 레이어는 삭제 불가 — 씬이 "레이어 0개"를 허용하지 않는다.
			const bool canDelete = layerCount > 1;
			if (ImGui::MenuItem(Loc::Text(EditorLocKeys::HierarchyDeleteLayer), nullptr, false, canDelete))
			{
				auto cmd = MakeOwnerPtr<CDeleteLayerCommand>(activeScene, layer);
				if (Editor::CommandManager.ExecuteCommand(std::move(cmd)))
				{
					Editor::ClearLayerSelection();
				}
			}
			ImGui::EndPopup();
		}

		// ── 가시성 토글(눈 아이콘, 우측 정렬) ─────────────────────────────────
		// 오브젝트의 EditorHidden 과 달리 레이어 Visible 은 저장되는 런타임 속성이므로
		// undo 스택에 올린다(인스펙터의 같은 체크박스와 동일 경로).
		{
			// ImTextButton 은 내부에서 라벨을 SetCursorPos 로 찍고 끝나 커서를 버튼 뒤가 아니라
			// 글자 위치에 남긴다. 한 줄 행에선 오차가 작아 묻히지만 썸네일 행은 두 배로 높아서
			// 다음 행이 이 행 중간에서 시작한다 → 행 높이는 헤더가 정한 값으로 되돌린다.
			const ImVec2 cursorAfterHeader = ImGui::GetCursorScreenPos();

			// 폭은 한 줄 버튼 폭, 높이는 행 전체(썸네일 때문에 행이 높다) — 글리프는 세로 중앙.
			ImGui::SameLine(ImGui::GetContentRegionMax().x - eyeButtonWidth);
			const char* icon = layer->Visible ? EditorIcons::ICON_EYE : EditorIcons::ICON_EYE_SLASH;
			ImText eyeText;
			eyeText.SetScale(0.7f).SetAlign(ImText::Align::Center);
			if (ImTextButton(eyeText, icon, ImVec2(eyeButtonWidth, nodeH)))
			{
				LayerPropertySnapshot properties = LayerPropertySnapshot::Capture(*layer);
				properties.Visible = !properties.Visible;
				EditorLayerActions::SetLayerProperty(
					*activeScene, *layer, CSetLayerPropertyCommand::EField::Visible, properties);
			}

			ImGui::SetCursorScreenPos(cursorAfterHeader);
		}

		if (isOpen)
		{
			const auto rootIt = rootsByLayer.find(layer);
			if (rootIt != rootsByLayer.end())
			{
				for (std::size_t rootIndex : rootIt->second)
				{
					drawObject(rootIndex);
				}
			}
			ImLayerHeaderEnd();
		}
	};

	// ── 캔버스 노드 ─────────────────────────────────────────────────────────────
	// 레이어보다 위, 트리 맨 위 한 줄. 레이어를 담는 부모 노드로 만들지 않는 이유: 그러면
	// 전 레이어가 한 단계 들여쓰기되는데, 캔버스는 어차피 런타임에 하나뿐이라 접을 일이 없다.
	// 클릭 = 캔버스 선택 → 인스펙터가 캔버스 설정(배경색·뷰포트)을 띄운다.
	{
		// 씬 이름은 SceneManager 키(= 프로젝트 상대 경로)라 그대로 쓰면 한 줄을 넘긴다 —
		// 파일 이름만 보여준다.
		const std::string canvasName = Editor::GetActiveScenePath().empty()
			? std::string(activeScene->GetName())
			: Editor::GetActiveScenePath().stem().string();

		char canvasLabel[256];
		std::snprintf(canvasLabel, sizeof(canvasLabel),
			Loc::Text(EditorLocKeys::HierarchyCanvasNodeFormat), canvasName.c_str());
		if (ImGui::Selectable(canvasLabel, Editor::IsCanvasSelected()))
		{
			Editor::SelectCanvas();
		}
		ImGui::Separator();
	}

	for (std::size_t i = layerCount; i > 0; --i)
	{
		drawLayer(i - 1);
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
				if (visibleObject && visibleObject->GetInstanceGuid() == guid)
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
			m_selectionAnchorGuid = pendingSelection.Object->GetInstanceGuid();
		}
		else
		{
			Editor::SelectEntity(pendingSelection.Object);
			m_selectionAnchorGuid = pendingSelection.Object->GetInstanceGuid();
		}
	}

	drawBackgroundPopup();
}

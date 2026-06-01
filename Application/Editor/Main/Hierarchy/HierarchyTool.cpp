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
			EditorGuiDrawHelpers::DrawAddObjectMenu(*activeScene, INVALID_ENTITY_ID);
			ImGui::EndPopup();
		}
	};

	SceneSnapshot snapshot;
	activeScene->BuildSnapshot(snapshot);
	if (snapshot.Objects.empty())
	{
		ImGui::TextDisabled(Loc::Text("hierarchy.scene_empty"));
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

	// ── 선택된 오브젝트의 컴포넌트/스크립트 리스트 (드래그 소스) ─────────────────
	// 각 항목을 드래그하면 "HIERARCHY_COMPONENT" 페이로드가 실린다 →
	// Ref<T> 프로퍼티(인스펙터)의 드롭 타깃이 받아 참조를 설정한다.
	auto drawComponentDragList = [&](EntityId entity)
	{
		if (false == Core::Reflection.IsValid())
		{
			return;
		}

		// 컴포넌트 리스트 항목 색상 — 진한 주황(활성) / 더 어두운 주황(비활성).
		const ImVec4 colorEnabled (0.85f, 0.50f, 0.15f, 1.0f);
		const ImVec4 colorDisabled(0.50f, 0.30f, 0.10f, 1.0f);

		// 컴포넌트의 IsEnabled bool 프로퍼티를 읽는다(없으면 항상 활성).
		auto isComponentEnabled = [](void* comp, const ComponentTypeInfo& ti) -> bool
		{
			if (nullptr == comp) return true;
			for (const ReflectPropertyInfo& prop : ti.Properties)
			{
				if (EReflectPropertyType::Bool == prop.Type && prop.Name && 0 == std::strcmp(prop.Name, "IsEnabled"))
				{
					if (void* field = CReflectionRegistry::GetPropertyAddress(comp, prop))
						return *static_cast<bool*>(field);
					break;
				}
			}
			return true;
		};

		// 한 항목을 ImTree 리프 노드로 렌더한다 — 일반 Selectable 대신 ImTree 를 써야
		// 커스텀 트리의 들여쓰기 보정과 자식 연결선(DrawLinesToNodes)에 동기화된다.
		auto drawItem = [&](const char* label, const ImVec4& color,
		                    EntityId ent, TypeId typeId, bool isScript, const char* focusName)
		{
			ImGui::Utillity::IDGroup itemId(static_cast<const void*>(label));
			const ImGuiTreeNodeFlags leafFlags =
				ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
				ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DrawLinesToNodes;

			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImTree(label, leafFlags);
			ImGui::PopStyleColor();

			// 클릭(Release) → 인스펙터에서 해당 컴포넌트 탭으로 포커스.
			if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
			{
				Editor::SelectEntity(ent);
				Editor::SetFocusComponent(focusName);
			}
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
			{
				EditorDragDrop::HierarchyComponentPayload ref{ ent, typeId, isScript };
				ImGui::SetDragDropPayload(EditorDragDrop::HIERARCHY_COMPONENT_PAYLOAD, &ref, sizeof(ref));
				ImGui::Text("%s", label);
				ImGui::EndDragDropSource();
			}
		};

		// 일반 컴포넌트
		for (std::size_t i = 0; i < Core::Reflection->GetComponentTypeCount(); ++i)
		{
			const ComponentTypeInfo* typeInfo = Core::Reflection->GetComponentType(i);
			if (nullptr == typeInfo || nullptr == typeInfo->Type.Name)
			{
				continue;
			}
			// 구조용/내부 컴포넌트와 ScriptComponent(컨테이너)는 노출하지 않는다.
			// (실제 스크립트는 아래에서 따로 나열한다.)
			if (0 == std::strcmp(typeInfo->Type.Name, "GameObject")) continue;
			if (0 == std::strcmp(typeInfo->Type.Name, "TransformHierarchy2D")) continue;
			if (0 == std::strcmp(typeInfo->Type.Name, "ScriptComponent")) continue;
			if (false == Core::Reflection->HasComponent(*activeScene, entity, typeInfo->Type.Id))
			{
				continue;
			}

			void* comp = Core::Reflection->GetComponentAddress(*activeScene, entity, typeInfo->Type.Id);
			const bool enabled = isComponentEnabled(comp, *typeInfo);
			const char* label = typeInfo->Type.DisplayName ? typeInfo->Type.DisplayName : typeInfo->Type.Name;
			drawItem(label, enabled ? colorEnabled : colorDisabled, entity, typeInfo->Type.Id, false, typeInfo->Type.Name);
		}

		// 스크립트 (ScriptComponent.Instance 의 타입)
		if (ScriptComponent* scriptComp = activeScene->GetComponent<ScriptComponent>(entity))
		{
			if (INVALID_TYPE_ID != scriptComp->ScriptTypeId)
			{
				const ScriptTypeInfo* scriptInfo = Core::Reflection->FindScript(scriptComp->ScriptTypeId);
				const char* label = (scriptInfo && scriptInfo->Type.Name) ? scriptInfo->Type.Name : "Script";
				drawItem(label, scriptComp->IsEnabled ? colorEnabled : colorDisabled,
				         entity, scriptComp->ScriptTypeId, true, "ScriptComponent");
			}
		}
	};

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
		const bool isOpen = ImTree(name, flags);

		// ── 클릭으로 선택 (Release 기준) ───────────────────────────────────
		// Press 가 아니라 Release 에서 선택한다. Press 로 선택하면 드래그를 시작하는
		// 순간 선택이 바뀌어 인스펙터가 갱신되고, 드래그-드랍 대상(Ref 프로퍼티)이
		// 사라져 드롭을 못 한다. Release 기준이면: 단순 클릭은 선택, 드래그는
		// 아이템 밖에서 떼지므로(IsItemHovered=false) 선택이 일어나지 않는다.
		if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
			&& !ImGui::IsItemToggledOpen())
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
			ImGui::SetDragDropPayload(EditorDragDrop::HIERARCHY_ENTITY_PAYLOAD, &object.Entity, sizeof(EntityId));
			ImGui::Text(Loc::Text("hierarchy.move_format"), name);
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
				if (ImGui::MenuItem(Loc::Text("hierarchy.unparent")))
				{
					auto cmd = MakeOwnerPtr<CSetParentCommand>(activeScene, object.Entity, INVALID_ENTITY_ID);
					Editor::CommandManager.ExecuteCommand(std::move(cmd));
				}
				ImGui::Separator();
			}

			EditorGuiDrawHelpers::DrawAddComponentMenu(*activeScene, object.Entity);
			ImGui::EndPopup();
		}

		// ── 선택된 오브젝트: 컴포넌트 리스트를 자식보다 "위"에 표시 ───────────
		// 트리를 펼치지 않아도(선택만 돼도) 펼쳐진 것처럼 아래에 컴포넌트 리스트를
		// 나열한다. 트리를 펼쳤으면 컴포넌트 리스트가 자식 목록보다 위에 온다.
		if (Editor::IsSelected(object.Entity))
		{
			// 컴포넌트 항목을 자식과 같은 깊이에 놓는다. ImTree 가 이미 한 단계 push
			// 했으면(펼친 노드) 그대로, 아니면(리프/접힘) 수동 TreePush 해서 커스텀
			// 트리의 들여쓰기·연결선 보정이 컴포넌트 리프에도 동일하게 적용되게 한다.
			const bool alreadyPushed = isOpen && hasChildren;
			if (false == alreadyPushed)
			{
				ImGui::TreePush(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(object.Entity)));
			}
			drawComponentDragList(object.Entity);
			if (false == alreadyPushed)
			{
				ImGui::TreePop();
			}
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

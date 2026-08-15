#include "pch.h"
#include "ImDockWindow.h"

#include "Editor/ImWindow/ImWindowFlag.h"   // IMDOCKWINDOW_FLAG_*

CImDockWindow::CImDockWindow(ImGuiID id, ImGuiID parentId)
	: CImWindow(id, parentId)
	, m_mainDockID(0)
	, m_mainSplitedID(0)
	// 도킹 노드가 스스로 그리는 버튼(우측 상단 닫기 · 좌측 창 메뉴)은 기본으로 끈다.
	// 노드 닫기는 그 노드 안 창을 **전부** 닫아 버리는데, 이 프로젝트의 dock 창은
	// 전부 "탭을 하나씩 닫고 창 메뉴로 되켠다"는 방식이라 쓸 자리가 없다.
	// 기본값이 아니라 창마다 적으면 새 dock 창을 만들 때마다 빠뜨린다 — 실제로 그랬다.
	// 노드 닫기 버튼이 필요한 dock 이 생기면 그 창에서 이 플래그를 빼면 된다.
	, m_imguiDockFlags(ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton)
	, m_bNeedRebuildDockLayout(true)
	, m_bUseDocking(true)
{
	m_mainDockID = ImHashStr("DockSpace", 0, GetID());
	m_imWndClass.ClassId = m_mainDockID;
	m_imWndClass.DockingAllowUnclassed = false;
	m_imWndClass.DockingAlwaysTabBar = false;
}

CImDockWindow::~CImDockWindow()
{
	for(auto& childWnd : m_childImWindowVector)
	{
		if (childWnd)
		{
			childWnd->Destroy();
		}
	}
}

namespace
{
	// ImGuiDir → 슬롯 이름 변환 (SetDockLayout 하위 호환용)
	const char* DirToSlotName(ImGuiDir dir)
	{
		switch (dir)
		{
		case ImGuiDir_Left:  return "Left";
		case ImGuiDir_Right: return "Right";
		case ImGuiDir_Up:    return "Up";
		case ImGuiDir_Down:  return "Down";
		default:             return "Main";
		}
	}
}

void CImDockWindow::AddDockSplit(const char* fromSlot, ImGuiDir dir, float ratio, const char* newSlot)
{
	DockSplitDef def;
	def.fromSlot  = fromSlot ? fromSlot : "";
	def.direction = dir;
	def.ratio     = ratio;
	def.newSlot   = newSlot ? newSlot : "";
	m_splitDefs.push_back(std::move(def));
	m_bNeedRebuildDockLayout = true;
}

void CImDockWindow::SetDockLayout(ImGuiDir dir, float splitRatio)
{
	// 하위 호환: 루트("")에서 분할, 방향명을 슬롯명으로 사용
	AddDockSplit("", dir, splitRatio, DirToSlotName(dir));
}

BitFlag& CImDockWindow::GetImGuiDockFlags()
{
	return m_imguiDockFlags;
}

BitFlag& CImDockWindow::GetCustomDockFlags()
{
	return m_customDockFlags;
}

void CImDockWindow::UseStoredDockLayout()
{
	m_bNeedRebuildDockLayout = false;
	// "저장된 배치를 쓴다" = **아무것도 명시적으로 배치하지 않는다.**
	// 시작 시 등록된 자식들은 배치 예약이 걸려 있는데, 그걸 남겨 두면 첫 프레임에
	// DockBuilderDockWindow 로 전부 다시 붙여 버려서 ini 로 복원한 배치가 날아간다.
	// (분할 ID 가 아직 0 이라 죄다 dockspace 루트에 탭으로 몰린다.)
	m_pendingDockChildIDs.clear();
}

bool CImDockWindow::AddChildImWindow(SafePtr<IImWindow> child)
{
	if (false == child.IsValid())
	{
		return false;
	}
	if (child->GetID() == GetID())
	{
		return false;
	}
	if(false == HasChildImWindow(child->GetID()))
	{
		SafePtr<CImWindow> castedChild = DynamicSafePtrCast<CImWindow>(child);
		if (false == castedChild.IsValid())
		{
			return false;
		}

		m_childImWindowVector.push_back(castedChild);
		castedChild->m_ownerID = GetID();
		castedChild->m_ownerWindow = SafeFromThis();
		// 재빌드가 아니라 **이 자식만** 배치 예약한다. 재빌드는 노드를 지우므로
		// 창 하나 추가할 때마다 사용자의 도킹 배치가 초기화된다.
		m_pendingDockChildIDs.push_back(castedChild->GetID());
		return true;
	}
	return false;
}

void CImDockWindow::RemoveChildImWindow(ImGuiID id)
{
	for (SafePtr<CImWindow>& childWnd : m_childImWindowVector)
	{
		if (childWnd && childWnd->GetID() == id)
		{
			childWnd->m_ownerID = 0;
			childWnd->m_ownerWindow.Reset();
			break;
		}
	}

	m_childImWindowVector.erase( std::remove_if(
		m_childImWindowVector.begin(),
		m_childImWindowVector.end(),
		[id] (const SafePtr<CImWindow>& a) {
			return false == a.IsValid() || a->GetID() == id;
		}),
		m_childImWindowVector.end()
	);
	// 자식이 빠졌다고 재빌드할 이유가 없다 — 남은 창들은 이미 제자리에 있고,
	// 사라진 창의 탭은 ImGui 가 제출이 끊기면 알아서 정리한다.
	// 여기서 재빌드하면 창 하나 닫을 때마다 배치가 초기화된다.
	m_pendingDockChildIDs.erase(
		std::remove(m_pendingDockChildIDs.begin(), m_pendingDockChildIDs.end(), id),
		m_pendingDockChildIDs.end());
}

SafePtr<CImWindow> CImDockWindow::FindChildImWindow(ImGuiID id)
{
	for (const auto& pWnd : m_childImWindowVector)
	{
		if(pWnd && pWnd->GetID() == id)
		{
			return pWnd;
		}
	}
	return nullptr;
}

bool CImDockWindow::HasChildImWindow(ImGuiID id) const
{
	for (const auto& pWnd : m_childImWindowVector)
	{
		if(pWnd && pWnd->GetID() == id)
		{
			return true;
		}
	}
	return false;
}

void CImDockWindow::OnPreBegin()
{
	m_bUseDocking = !m_customDockFlags[IMDOCKWINDOW_FLAG_NO_DOCKING];

	PushDockStyle();
}

void CImDockWindow::OnPostBegin()
{
	PopDockStyle();
	if (false == m_bUseDocking)
	{
		return;
	}
	bool isBeginDockBuild = false;
	isBeginDockBuild = BeginBuildDockLayout();

	SubmitDockSpace();

	for (std::size_t i = 0; i < m_childImWindowVector.size(); ++i)
	{
		if (SafePtr<CImWindow>& childWnd = m_childImWindowVector[i])
		{
			// 도킹 (재)지정 필요: 레이아웃 재빌드 중이거나, child 가 이번 프레임에
			// hidden→visible 로 전이(창 메뉴로 다시 켠 경우). 후자를 처리 안 하면
			// child 가 어느 dock node 에도 안 붙어 빈 떠다니는 윈도우로 떠서
			// "다시 안 켜지는" 것처럼 보인다.
			const bool justBecameVisible =
				childWnd->m_bIsVisible.first && (childWnd->m_bIsVisible.first != childWnd->m_bIsVisible.second);
			// 방금 추가돼 아직 어느 노드에도 안 붙은 자식(AddChildImWindow 예약분).
			const bool pendingDock =
				std::find(m_pendingDockChildIDs.begin(), m_pendingDockChildIDs.end(), childWnd->GetID())
					!= m_pendingDockChildIDs.end();
			const char* label = childWnd->GetImGuiLabel();

			// 자리를 잡아 줘야 하는가.
			// 재빌드와 신규 자식은 무조건 배치한다. 숨김→표시로 돌아오는 창은 다르다 —
			// ImGui 가 그 창의 도킹 위치를 이미 기억하고 있으므로, 거기에 대고
			// DockBuilderDockWindow 를 부르면 기억을 덮어써서 오히려 어긋난다.
			// (ini 로 복원된 dock 은 분할 ID 가 0 이라 아예 도킹이 풀린다 — 툴을 껐다 켜면
			//  떠다니는 창이 되던 증상이 이것이다.)
			// 그래서 **도킹 정보가 없는 창만** 배치한다.
			bool needsPlacement = isBeginDockBuild || pendingDock;
			if (false == needsPlacement && justBecameVisible)
			{
				const ImGuiWindow* existing = ImGui::FindWindowByName(label);
				needsPlacement = (nullptr == existing) || (0 == existing->DockId);
			}

			if (needsPlacement)
			{
				ImGuiID targetID = m_mainSplitedID;

				if (childWnd->m_initDockSlotIsSet)
				{
					// 슬롯 이름으로 탐색 (InitializeDockLayout(const char*) 사용 시)
					// 빈 문자열 ""은 루트/중앙 슬롯을 명시적으로 지정
					auto it = m_slotMap.find(childWnd->m_initDockSlot);
					if (it != m_slotMap.end() && it->second != 0)
					{
						targetID = it->second;
					}
				}
				else
				{
					// 방향 기반 탐색 (InitializeDockLayout(ImGuiDir) 하위 호환)
					const ImGuiDir dockDir = childWnd->m_initDockLayoutDirection;
					if (dockDir >= 0 && dockDir < ImGuiDir_COUNT)
					{
						const char* dirSlot = DirToSlotName(dockDir);
						auto it = m_slotMap.find(dirSlot);
						if (it != m_slotMap.end() && it->second != 0)
						{
							targetID = it->second;
						}
					}
				}

				// 재빌드를 거치지 않은 dock(ini 로 복원)은 분할 ID 가 비어 있다. 0 을 넘기면
				// DockBuilderDockWindow 가 창을 배치하는 게 아니라 도킹을 **해제**한다.
				// 위에서 "도킹 정보가 있는 창"은 이미 걸러 냈으므로 여기 오는 창은
				// 어디든 붙여 줘야 하는 창이다 → dockspace 루트로 떨어뜨린다.
				if (0 == targetID)
				{
					targetID = m_mainDockID;
				}

				ImGui::DockBuilderDockWindow(label, targetID);
			}
			childWnd->Update();
		}
	}
	m_pendingDockChildIDs.clear();

	if (isBeginDockBuild)
	{
		EndBuildDockLayout();
	}
}

void CImDockWindow::SubmitDockSpace()
{
	//////////////////////////////////////////
	// Sumit the DockSpace
	//////////////////////////////////////////
	ImGuiIO& io = ImGui::GetIO();
	if ( io.ConfigFlags & ImGuiConfigFlags_DockingEnable )
	{
		ImGui::DockSpace(m_mainDockID , ImVec2(0.0f , 0.0f) , m_imguiDockFlags.operator int(), &m_imWndClass);
	}
}

bool CImDockWindow::BeginBuildDockLayout()
{
	if (true == m_bNeedRebuildDockLayout)
    {
		m_bNeedRebuildDockLayout = false;
		m_slotMap.clear();

        ImGui::DockBuilderRemoveNode(m_mainDockID);
        ImGui::DockBuilderAddNode(m_mainDockID, ImGuiDockNodeFlags_DockSpace | m_imguiDockFlags.Get());
		if (m_imWindow)
		{
			ImGui::DockBuilderSetNodeSize(m_mainDockID, m_imWindow->Size);
		}

		// 루트 슬롯 ("") 을 mainDockID로 초기화
		m_slotMap[""] = m_mainDockID;

		// 등록 순서대로 분할 적용
		for (const DockSplitDef& def : m_splitDefs)
		{
			if (def.ratio <= 0.0f)
			{
				continue;
			}

			// 소스 슬롯 조회 (없으면 루트)
			auto srcIt = m_slotMap.find(def.fromSlot);
			if (srcIt == m_slotMap.end() || srcIt->second == 0)
			{
				continue;
			}

			ImGuiID& sourceID = srcIt->second;   // 나머지(remainder)가 여기에 덮어쓰임
			ImGuiID  newID    = 0;

			ImGui::DockBuilderSplitNode(
				sourceID,         // 분할할 노드 (out: 나머지로 갱신)
				def.direction,    // 방향
				def.ratio,        // 새 슬롯 비율
				&newID,           // [out] 새로 분리된 노드 ID
				&sourceID         // [in/out] 나머지 노드 ID
			);

			// 새 슬롯 등록
			if (!def.newSlot.empty())
			{
				m_slotMap[def.newSlot] = newID;
			}
		}

		// 루트 슬롯 ("")의 최종 remainder = mainSplitedID
		m_mainSplitedID = m_slotMap[""];
        return true;
    }
    return false;
}

void CImDockWindow::EndBuildDockLayout() const
{
	ImGui::DockBuilderFinish(m_mainDockID);
}

void CImDockWindow::PushDockStyle()
{
	if (m_customDockFlags.Has(IMDOCKWINDOW_FLAG_FULLSCREEN))
	{
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos( viewport->Pos );
		ImGui::SetNextWindowSize( viewport->Size );
		ImGui::SetNextWindowViewport( viewport->ID );
		m_dockStyleBuilder.PushStyleVar(ImGuiStyleVar_WindowRounding , 0.0f);
		m_dockStyleBuilder.PushStyleVar(ImGuiStyleVar_WindowBorderSize , 0.0f);
	}
	if (m_customDockFlags.Has(IMDOCKWINDOW_FLAG_PADDING))
	{
		m_dockStyleBuilder.PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f , 0.0f));
	}
}

void CImDockWindow::PopDockStyle()
{
	m_dockStyleBuilder.PopStyle();
}

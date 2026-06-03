#include "pch.h"
#include "ImDockWindow.h"

#include "Editor/ImWindow/ImWindowFlag.h"   // IMDOCKWINDOW_FLAG_*

CImDockWindow::CImDockWindow(ImGuiID id, ImGuiID parentId)
	: CImWindow(id, parentId)
	, m_mainDockID(0)
	, m_mainSplitedID(0)
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
		m_bNeedRebuildDockLayout = true;
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
	m_bNeedRebuildDockLayout = true;
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
			if (isBeginDockBuild || justBecameVisible)
			{
				const char* label = childWnd->GetImGuiLabel();

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

				ImGui::DockBuilderDockWindow(label, targetID);
			}
			childWnd->Update();
		}
	}

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

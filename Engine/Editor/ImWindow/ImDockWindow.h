#pragma once

class CImDockWindow : public CImWindow
{
public:
	// 슬롯 기반 분할 정의
	// fromSlot : 분할할 소스 슬롯 이름 ("" = 루트 노드)
	// direction: 분할 방향
	// ratio    : 새 슬롯이 차지할 비율 (0~1)
	// newSlot  : 분리된 새 노드에 부여할 이름
	// → 소스 슬롯은 나머지(remainder)가 되어 이름을 유지
	struct DockSplitDef
	{
		std::string fromSlot;
		ImGuiDir    direction;
		float       ratio;
		std::string newSlot;
	};

public:
	CImDockWindow(ImGuiID id, ImGuiID parentId = 0);
	virtual ~CImDockWindow();

public:
	// 슬롯 기반 분할 등록 (등록 순서대로 적용됨)
	void AddDockSplit(const char* fromSlot, ImGuiDir dir, float ratio, const char* newSlot);

	// 하위 호환: ImGuiDir_Left/Right/Up/Down 단일 레벨 분할
	// 방향 이름("Left","Right","Up","Down")을 슬롯명으로 사용하며
	// 항상 루트 노드의 나머지에서 분할됨
	void SetDockLayout(ImGuiDir dir, float splitRatio);

	BitFlag& GetImGuiDockFlags();
	BitFlag& GetCustomDockFlags();
	void UseStoredDockLayout();

	bool				AddChildImWindow(SafePtr<IImWindow> child);
	void				RemoveChildImWindow(ImGuiID id);
	SafePtr<CImWindow>  FindChildImWindow(ImGuiID id);
	bool 				HasChildImWindow(ImGuiID id) const;

private:
	void OnPreBegin() override;
	void OnPostBegin() override;

	void SubmitDockSpace();
	bool BeginBuildDockLayout();
	void EndBuildDockLayout() const;
	void PushDockStyle();
	void PopDockStyle();

protected:
	ImGuiID m_mainDockID;
	ImGuiID m_mainSplitedID;

	std::vector<DockSplitDef>					m_splitDefs;
	std::unordered_map<std::string, ImGuiID>	m_slotMap;

	BitFlag	m_imguiDockFlags;
	BitFlag	m_customDockFlags;
	bool	m_bNeedRebuildDockLayout;
	bool    m_bUseDocking;

	std::vector<SafePtr<CImWindow>> m_childImWindowVector;
	ImGui::Utillity::StyleBuilder m_dockStyleBuilder;
};

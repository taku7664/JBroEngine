#pragma once

class CImDockWindow : public CImWindow
{
public:
	CImDockWindow(ImGuiID id, ImGuiID parentId = 0);
	virtual ~CImDockWindow();

public:
	void SetDockLayout(ImGuiDir dir, float splitRatio);

	BitFlag& GetImGuiDockFlags();
	BitFlag& GetCustomDockFlags();

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
	ImGuiID m_splitedID[ImGuiDir_COUNT];
	float	m_splitRatio[ImGuiDir_COUNT];

	BitFlag	m_imguiDockFlags;
	BitFlag	m_customDockFlags;
	bool	m_bNeedRebuildDockLayout;
	bool    m_bUseDocking;

	std::vector<SafePtr<CImWindow>> m_childImWindowVector;
	ImGui::Utillity::StyleBuilder m_dockStyleBuilder;
};

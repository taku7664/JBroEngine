#pragma once
#include "IImWindow.h"

class CImDockWindow;

class CImWindow : public IImWindow, public EnableSafeFromThis<CImWindow>
{
	friend class CImDockWindow;
public:
    CImWindow(ImGuiID id, ImGuiID parentId = 0);
    virtual ~CImWindow();

public:
    void Initialize();
    void Finalize();
    void Update();

public:
    UINT GetID() const final override;
	ImGuiID GetOwnerID() const final override;

    void SetTitle(const char* title) override;
    const char* GetTitle() const override;

    void SetSize(ImVec2 size, bool delay = true) override;
	ImVec2 GetSize() const override;

	void SetPosition(ImVec2 pos, bool delay = true) override;
	ImVec2 GetPosition() const override;

    void SetVisible(bool b) override;
	bool GetVisible() const override;

	BitFlag& GetImGuiWindowFlags() override;
	BitFlag& GetCustomWindowFlags() override;

	void Destroy() override;
	void Focus() override;

	void InitializeDockLayout(ImGuiDir dir) override;
	ImGuiWindow* GetImGuiWindow() override;

public:
    bool IsAlive() const;
    ImGuiDir GetInitDockLayout() const;

private:
    void InitializeWindowRect();
	void UpdateWindowState();
	void UpdateWindowRect();

protected:
    virtual void OnCreate() override {}
    virtual void OnDestroy() override {}
    virtual void OnUpdate() override {}
    virtual void OnPreBegin() override {}
    virtual void OnPostBegin() override {}
    virtual void OnMenuBar() override {}
    virtual void OnFocusEnter() override {}
    virtual void OnFocusStay() override {}
    virtual void OnFocusExit() override {}
    virtual void OnRenderEnter() override {}
    virtual void OnRenderStay() override {}
    virtual void OnRenderExit() override {}
    virtual void OnClipEnter() override {}
    virtual void OnClipStay() override {}
    virtual void OnClipExit() override {}
    virtual void OnPreEnd() override {}
    virtual void OnPostEnd() override {}
	virtual void OnShow() override {}
	virtual void OnHide() override {}
	virtual void OnOpen() override {}
	virtual void OnClose() override {}

private:
    void HandleCreate() override final;
    void HandleDestroy() override final;
    void HandleUpdate() override final;
    void HandleBegin() override final;
	void HandleRender() override final;
	void HandleEnd() override final;
	void HandleFocus() override final;

	void HandleEvent();
	void HandleHidden();

protected:
    std::string         m_title;
    ImGuiID             m_hashedID;
	ImGuiID				m_ownerID;
	ImGuiID				m_dockID;

	SafePtr<CImWindow>	m_ownerWindow;
    ImGuiWindow*        m_imWindow;
    ImGuiWindowClass    m_imWndClass = {};

    BitFlag				m_imguiFlags;
    BitFlag				m_windowFlags;

	bool				m_bIsFirstTick;
	bool				m_bIsAlive;
	bool				m_bBeginResult;

	ImGuiDir			m_initDockLayoutDirection;
	float				m_smoothWindowTick;
	float				m_smoothWindowCount;
	ImVec2				m_startWindowSize;
	ImVec2				m_targetWindowSize;
    ImVec2				m_startWindowPos;
    ImVec2				m_targetWindowPos;

    std::pair<bool, bool> m_bIsVisible;
    std::pair<bool, bool> m_bIsLock;
    std::pair<bool, bool> m_bIsFocused;
    std::pair<bool, bool> m_bIsClipped;
    std::pair<bool, bool> m_bIsRendered;
};

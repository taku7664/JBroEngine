#pragma once

// UINT / ImGuiID / ImVec2 / ImGuiDir / ImGuiWindow / BitFlag — 인터페이스 시그니처에 사용.
#include <Windows.h>
#include "ThirdParty/imgui/imgui.h"
#include "Utillity/Base/BitFlag.h"

class IImWindow
{
public:
    virtual UINT GetID() const = 0;
	virtual ImGuiID GetOwnerID() const = 0;

    virtual void SetTitle(const char* title) = 0;
    // 매 프레임 Loc::Text(key)로 타이틀을 자동 갱신.
    // 키가 비어있으면 자동 갱신 해제. 도구는 OnCreate에서 한 번만 호출하면 됨.
    virtual void SetLocalizedTitleKey(const char* key) = 0;
    virtual const char* GetTitle() const = 0;

    virtual void SetSize(ImVec2 size, bool delay = true) = 0;
    virtual ImVec2 GetSize() const = 0;

    virtual void SetPosition(ImVec2 pos, bool delay = true) = 0;
    virtual ImVec2 GetPosition() const = 0;

	virtual void SetVisible(bool b) = 0;
	virtual bool GetVisible() const = 0;

	virtual BitFlag& GetImGuiWindowFlags() = 0;
	virtual BitFlag& GetCustomWindowFlags() = 0;

	virtual void Destroy() = 0;
	virtual void Focus() = 0;

	virtual void InitializeDockLayout(ImGuiDir dir) = 0;
	virtual void InitializeDockLayout(const char* slot) = 0;
	virtual ImGuiWindow* GetImGuiWindow() = 0;

protected:

    virtual void OnCreate()         = 0;

    virtual void OnDestroy()        = 0;

    virtual void OnUpdate()         = 0;

    virtual void OnPreBegin()       = 0;
    virtual void OnPostBegin()      = 0;
	// Called while CImWindow owns an active ImGui menu bar scope.
	// Implementations must not call BeginMenuBar() or EndMenuBar().
    virtual void OnMenuBar()        = 0;

    virtual void OnFocusEnter()     = 0;
    virtual void OnFocusStay()      = 0;
    virtual void OnFocusExit()      = 0;

    virtual void OnRenderEnter()    = 0;
    virtual void OnRenderStay()     = 0;
    virtual void OnRenderExit()     = 0;

    virtual void OnClipEnter()      = 0;
    virtual void OnClipStay()       = 0;
    virtual void OnClipExit()       = 0;

    virtual void OnPreEnd()         = 0;
    virtual void OnPostEnd()        = 0;

	virtual void OnShow()			= 0;
	virtual void OnHide()			= 0;
	virtual void OnOpen()			= 0;
	virtual void OnClose()			= 0;

private:
    virtual void HandleCreate() = 0;
    virtual void HandleDestroy() = 0;
    virtual void HandleUpdate() = 0;
    virtual void HandleBegin() = 0;
    virtual void HandleRender() = 0;
    virtual void HandleEnd() = 0;
    virtual void HandleFocus() = 0;

};


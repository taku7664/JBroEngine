#pragma once

#include "Core/Platform/IPlatform.h"
#include "Core/Input/InputTypes.h" // ETouchPhase

class CMobilePlatform final : public IPlatform
{
public:
	bool Initialize(const PlatformDesc& desc) override;
	void PollEvents(PlatformEvent& platformEvent) override;
	void Finalize() override;

	SafePtr<IRenderSurface> GetMainRenderSurface() const override;
	EPlatformType GetPlatformType() const override;
	const wchar_t* GetName() const override;

	void RequestExit();
	void SetFocus(bool isFocused);
	void SetNativeSurfaceHandle(void* handle);
	void ResizeSurface(int width, int height);
	void NotifyPause();
	void NotifyResume();

	// 네이티브 입력 주입 — surface/focus/pause/resume inject 와 동일 계약(NativeActivity/UIKit 브리지가 호출).
	// 터치 이벤트를 엔진 InputSystem 으로 전달한다. 메인 스레드 호출 가정.
	void InjectTouch(std::int32_t pointerId, int x, int y, ETouchPhase phase);

private:
	PlatformDesc m_desc;
	OwnerPtr<IRenderSurface> m_mainRenderSurface;
	bool m_wantsExit = false;
	bool m_isPaused = false;
	bool m_isInitialized = false;
};

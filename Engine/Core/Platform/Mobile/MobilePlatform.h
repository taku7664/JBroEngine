#pragma once

#include "Core/Platform/IPlatform.h"

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

private:
	PlatformDesc m_desc;
	OwnerPtr<IRenderSurface> m_mainRenderSurface;
	bool m_wantsExit = false;
	bool m_isPaused = false;
	bool m_isInitialized = false;
};

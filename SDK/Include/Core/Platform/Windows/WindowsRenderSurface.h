#pragma once

#include "Core/Platform/IRenderSurface.h"

#if JBRO_PLATFORM_WINDOWS
using HWND = struct HWND__*;
using UINT = unsigned int;
using WPARAM = unsigned __int64;
using LPARAM = __int64;
using LRESULT = __int64;
#endif

class CWindowsRenderSurface final : public IRenderSurface
{
public:
	bool Create(const RenderSurfaceCreateDesc& desc) override;
	void Destroy() override;
	void PollEvents(PlatformEvent& platformEvent) override;

	RenderSurfaceSize GetSize() const override;
	NativeSurfaceHandle GetNativeSurfaceHandle() const override;
	void SetNativeMessageHandler(NativeSurfaceMessageHandler handler) override;

private:
#if JBRO_PLATFORM_WINDOWS
	static LRESULT __stdcall WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
#endif

private:
	RenderSurfaceCreateDesc m_desc;
	NativeSurfaceMessageHandler m_nativeMessageHandler;
	void* m_nativeHandle = nullptr;
	bool m_isFocused = true;
	bool m_focusGained = false;
	bool m_focusLost = false;
	bool m_isCreated = false;
	bool m_resized = false;       // WM_SIZE 엣지(PollEvents 에서 1회 디스패치 후 클리어)
	int  m_resizeWidth = 0;
	int  m_resizeHeight = 0;
};

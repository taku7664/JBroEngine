#pragma once

#include "Core/Platform/IRenderSurface.h"

class CMobileRenderSurface final : public IRenderSurface
{
public:
	bool Create(const RenderSurfaceCreateDesc& desc) override;
	void Destroy() override;
	void PollEvents(PlatformEvent& platformEvent) override;

	RenderSurfaceSize GetSize() const override;
	NativeSurfaceHandle GetNativeSurfaceHandle() const override;
	void SetNativeMessageHandler(NativeSurfaceMessageHandler handler) override;

	void SetNativeSurfaceHandle(void* handle);
	void SetSize(int width, int height);
	void SetFocus(bool isFocused);

private:
	RenderSurfaceCreateDesc m_desc;
	NativeSurfaceMessageHandler m_nativeMessageHandler;
	void* m_nativeHandle = nullptr;
	bool m_isFocused = true;
	bool m_focusGained = false;
	bool m_focusLost = false;
	bool m_isCreated = false;
	bool m_resized = false;       // SetSize 엣지(PollEvents 에서 1회 디스패치 후 클리어)
	int  m_resizeWidth = 0;
	int  m_resizeHeight = 0;
};

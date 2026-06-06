#pragma once

#include "Core/Platform/IRenderSurface.h"

#if JBRO_PLATFORM_WEB
struct EmscriptenVisibilityChangeEvent;
struct EmscriptenUiEvent;
#endif

class CWebCanvasSurface final : public IRenderSurface
{
public:
	bool Create(const RenderSurfaceCreateDesc& desc) override;
	void Destroy() override;
	void PollEvents(PlatformEvent& platformEvent) override;

	RenderSurfaceSize GetSize() const override;
	NativeSurfaceHandle GetNativeSurfaceHandle() const override;
	void SetNativeMessageHandler(NativeSurfaceMessageHandler handler) override;

#if JBRO_PLATFORM_WEB
private:
	// Emscripten 이벤트 콜백(브라우저 → 엔진). userData 로 this 를 받는다.
	static int OnVisibilityChange(int eventType, const EmscriptenVisibilityChangeEvent* event, void* userData);
	static int OnCanvasResize(int eventType, const EmscriptenUiEvent* event, void* userData);
#endif

private:
	RenderSurfaceCreateDesc m_desc;
	void* m_canvasHandle = nullptr;
	bool m_isCreated = false;
	bool m_isFocused = true;
	bool m_focusGained = false;
	bool m_focusLost = false;
	bool m_resized = false;
	int  m_resizeWidth = 0;
	int  m_resizeHeight = 0;
};

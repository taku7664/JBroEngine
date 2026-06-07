#pragma once

#include "Core/Platform/IRenderSurface.h"

#if JBRO_PLATFORM_WEB
#include <emscripten/html5.h> // EM_BOOL / 이벤트 구조체 / 콜백 typedef (버전별 EM_BOOL=bool 정합)
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
	static EM_BOOL OnVisibilityChange(int eventType, const EmscriptenVisibilityChangeEvent* event, void* userData);
	static EM_BOOL OnCanvasResize(int eventType, const EmscriptenUiEvent* event, void* userData);
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

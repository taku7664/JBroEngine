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

	// 레터박스 때문에 캔버스의 표시(CSS) 크기와 백킹(렌더) 크기가 다르다. 마우스는 CSS 좌표로
	// 들어오므로 두 크기를 입력에 알려 줘야 스크립트가 게임 화면 픽셀을 본다.
	void UpdateGameSurfaceRect();
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
	// 입력에 아직 못 알린 크기 변경이 있는지. 부팅 직후에는 입력 시스템도 백킹 크기도 아직
	// 없을 수 있어, 실제로 알릴 수 있을 때까지 남겨 둔다.
	bool m_gameSurfaceRectPending = true;
};

#pragma once

#include "Core/Platform/IRenderSurface.h"

class CWebCanvasSurface final : public IRenderSurface
{
public:
	bool Create(const RenderSurfaceCreateDesc& desc) override;
	void Destroy() override;
	void PollEvents(PlatformEvent& platformEvent) override;

	RenderSurfaceSize GetSize() const override;
	NativeSurfaceHandle GetNativeSurfaceHandle() const override;
	void SetNativeMessageHandler(NativeSurfaceMessageHandler handler) override;

private:
	RenderSurfaceCreateDesc m_desc;
	void* m_canvasHandle = nullptr;
	bool m_isCreated = false;
};

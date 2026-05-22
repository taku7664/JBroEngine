#pragma once

#include "Core/RHI/RHIResource.h"
#include "Core/RHI/RHITypes.h"

class IRHISwapchain : public IRHIResource
{
public:
	virtual bool Initialize(const RenderSurfaceDesc& surfaceDesc) = 0;
	virtual void Resize(const RenderSurfaceSize& size) = 0;
	virtual void Present() = 0;

	virtual RenderSurfaceSize GetSize() const = 0;
};

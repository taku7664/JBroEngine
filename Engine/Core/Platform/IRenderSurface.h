#pragma once

#include "Utillity/SafePtr.h"
#include "Core/Platform/PlatformTypes.h"
#include "Core/Platform/RenderSurfaceTypes.h"

class IRenderSurface : public EnableSafeFromThis<IRenderSurface>
{
public:
	virtual ~IRenderSurface() = default;

public:
	virtual bool Create(const RenderSurfaceCreateDesc& desc) = 0;
	virtual void Destroy() = 0;
	virtual void PollEvents(PlatformEvent& platformEvent) = 0;

	virtual RenderSurfaceSize GetSize() const = 0;
	virtual NativeSurfaceHandle GetNativeSurfaceHandle() const = 0;
	virtual void SetNativeMessageHandler(NativeSurfaceMessageHandler handler) = 0;
};

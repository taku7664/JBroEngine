#pragma once

#include "Utillity/SafePtr.h"
#include "Core/Platform/PlatformTypes.h"
#include "Core/Platform/IRenderSurface.h"

class IPlatform : public EnableSafeFromThis<IPlatform>
{
public:
	virtual ~IPlatform() = default;

public:
	virtual bool Initialize(const PlatformDesc& desc) = 0;
	virtual void PollEvents(PlatformEvent& platformEvent) = 0;
	virtual void Finalize() = 0;

	virtual SafePtr<IRenderSurface> GetMainRenderSurface() const = 0;
	virtual EPlatformType GetPlatformType() const = 0;
	virtual const wchar_t* GetName() const = 0;
};

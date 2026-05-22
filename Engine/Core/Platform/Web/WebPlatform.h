#pragma once

#include "Core/Platform/IPlatform.h"

class CWebPlatform final : public IPlatform
{
public:
	bool Initialize(const PlatformDesc& desc) override;
	void PollEvents(PlatformEvent& platformEvent) override;
	void Finalize() override;

	SafePtr<IRenderSurface> GetMainRenderSurface() const override;
	EPlatformType GetPlatformType() const override;
	const wchar_t* GetName() const override;

private:
	PlatformDesc m_desc;
	OwnerPtr<IRenderSurface> m_mainRenderSurface;
	bool m_isInitialized = false;
};

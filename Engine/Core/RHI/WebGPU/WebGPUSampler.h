#pragma once

#include "Core/RHI/IRHISampler.h"
#include "Core/RHI/WebGPU/WebGPUCommon.h"

class CWebGPUSampler final : public IRHISampler
{
public:
	explicit CWebGPUSampler(const RHISamplerDesc& desc);
	~CWebGPUSampler() override;

	const RHISamplerDesc& GetDesc() const override;

#if JBRO_PLATFORM_WEB
	void BindNativeSampler(WGPUSampler sampler);
	WGPUSampler GetNativeSampler() const;
#endif

private:
	RHISamplerDesc m_desc;
#if JBRO_PLATFORM_WEB
	WGPUSampler m_sampler = nullptr;
#endif
};


#include "pch.h"
#include "WebGPUSampler.h"

CWebGPUSampler::CWebGPUSampler(const RHISamplerDesc& desc)
	: m_desc(desc)
{
}

CWebGPUSampler::~CWebGPUSampler()
{
#if JBRO_PLATFORM_WEB
	if (m_sampler)
	{
		wgpuSamplerRelease(m_sampler);
		m_sampler = nullptr;
	}
#endif
}

const RHISamplerDesc& CWebGPUSampler::GetDesc() const
{
	return m_desc;
}

#if JBRO_PLATFORM_WEB
void CWebGPUSampler::BindNativeSampler(WGPUSampler sampler)
{
	m_sampler = sampler;
}

WGPUSampler CWebGPUSampler::GetNativeSampler() const
{
	return m_sampler;
}
#endif


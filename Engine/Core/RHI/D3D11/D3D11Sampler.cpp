#include "pch.h"
#include "D3D11Sampler.h"

#if JBRO_PLATFORM_WINDOWS
#include <d3d11.h>
#endif

CD3D11Sampler::CD3D11Sampler(const RHISamplerDesc& desc)
	: m_desc(desc)
{
}

CD3D11Sampler::~CD3D11Sampler()
{
#if JBRO_PLATFORM_WINDOWS
	if (m_sampler)
	{
		m_sampler->Release();
		m_sampler = nullptr;
	}
#endif
}

const RHISamplerDesc& CD3D11Sampler::GetDesc() const
{
	return m_desc;
}

#if JBRO_PLATFORM_WINDOWS
void CD3D11Sampler::BindNativeSampler(ID3D11SamplerState* sampler)
{
	m_sampler = sampler;
}

ID3D11SamplerState* CD3D11Sampler::GetNativeSampler() const
{
	return m_sampler;
}
#endif

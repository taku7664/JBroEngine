#pragma once

#include "Core/Platform/PlatformDefines.h"
#include "Core/RHI/IRHISampler.h"

#if JBRO_PLATFORM_WINDOWS
struct ID3D11SamplerState;
#endif

class CD3D11Sampler final : public IRHISampler
{
public:
	CD3D11Sampler(const RHISamplerDesc& desc);
	~CD3D11Sampler() override;

	const RHISamplerDesc& GetDesc() const override;

#if JBRO_PLATFORM_WINDOWS
	void BindNativeSampler(ID3D11SamplerState* sampler);
	ID3D11SamplerState* GetNativeSampler() const;
#endif

private:
	RHISamplerDesc m_desc;
#if JBRO_PLATFORM_WINDOWS
	ID3D11SamplerState* m_sampler = nullptr;
#endif
};

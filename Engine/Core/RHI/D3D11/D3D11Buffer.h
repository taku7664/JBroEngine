#pragma once

#include "Core/Platform/PlatformDefines.h"
#include "Core/RHI/IRHIBuffer.h"

#if JBRO_PLATFORM_WINDOWS
struct ID3D11Buffer;
#endif

class CD3D11Buffer final : public IRHIBuffer
{
public:
	CD3D11Buffer(const RHIBufferDesc& desc);
	~CD3D11Buffer() override;

	const RHIBufferDesc& GetDesc() const override;

#if JBRO_PLATFORM_WINDOWS
	void BindNativeBuffer(ID3D11Buffer* buffer);
	ID3D11Buffer* GetNativeBuffer() const;
#endif

private:
	RHIBufferDesc m_desc;
#if JBRO_PLATFORM_WINDOWS
	ID3D11Buffer* m_buffer = nullptr;
#endif
};

#include "pch.h"
#include "D3D11Buffer.h"

#if JBRO_PLATFORM_WINDOWS
#include <d3d11.h>
#endif

CD3D11Buffer::CD3D11Buffer(const RHIBufferDesc& desc)
	: m_desc(desc)
{
}

CD3D11Buffer::~CD3D11Buffer()
{
#if JBRO_PLATFORM_WINDOWS
	if (m_buffer)
	{
		m_buffer->Release();
		m_buffer = nullptr;
	}
#endif
}

const RHIBufferDesc& CD3D11Buffer::GetDesc() const
{
	return m_desc;
}

#if JBRO_PLATFORM_WINDOWS
void CD3D11Buffer::BindNativeBuffer(ID3D11Buffer* buffer)
{
	m_buffer = buffer;
}

ID3D11Buffer* CD3D11Buffer::GetNativeBuffer() const
{
	return m_buffer;
}
#endif

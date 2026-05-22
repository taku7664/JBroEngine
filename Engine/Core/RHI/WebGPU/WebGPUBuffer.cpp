#include "pch.h"
#include "WebGPUBuffer.h"

CWebGPUBuffer::CWebGPUBuffer(const RHIBufferDesc& desc)
	: m_desc(desc)
{
}

CWebGPUBuffer::~CWebGPUBuffer()
{
#if JBRO_PLATFORM_WEB
	if (m_buffer)
	{
		wgpuBufferRelease(m_buffer);
		m_buffer = nullptr;
	}
#endif
}

const RHIBufferDesc& CWebGPUBuffer::GetDesc() const
{
	return m_desc;
}

#if JBRO_PLATFORM_WEB
void CWebGPUBuffer::BindNativeBuffer(WGPUBuffer buffer)
{
	m_buffer = buffer;
}

WGPUBuffer CWebGPUBuffer::GetNativeBuffer() const
{
	return m_buffer;
}
#endif


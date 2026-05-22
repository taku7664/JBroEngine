#pragma once

#include "Core/RHI/IRHIBuffer.h"
#include "Core/RHI/WebGPU/WebGPUCommon.h"

class CWebGPUBuffer final : public IRHIBuffer
{
public:
	explicit CWebGPUBuffer(const RHIBufferDesc& desc);
	~CWebGPUBuffer() override;

	const RHIBufferDesc& GetDesc() const override;

#if JBRO_PLATFORM_WEB
	void BindNativeBuffer(WGPUBuffer buffer);
	WGPUBuffer GetNativeBuffer() const;
#endif

private:
	RHIBufferDesc m_desc;
#if JBRO_PLATFORM_WEB
	WGPUBuffer m_buffer = nullptr;
#endif
};


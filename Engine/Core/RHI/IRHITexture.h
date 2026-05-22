#pragma once

#include "Core/RHI/RHIResource.h"
#include "Core/RHI/RHIGraphicsTypes.h"

struct RHITextureNativeHandle
{
	void* Texture = nullptr;
	void* ShaderResourceView = nullptr;
	void* RenderTargetView = nullptr;
};

class IRHITexture : public IRHIResource
{
public:
	virtual const RHITexture2DDesc& GetDesc() const = 0;
	virtual RHITextureNativeHandle GetNativeHandle() const = 0;
};

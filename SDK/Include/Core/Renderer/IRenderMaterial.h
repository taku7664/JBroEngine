#pragma once

#include "Core/Renderer/IRenderResource.h"
#include "Core/Renderer/RendererTypes.h"

class IRHIGraphicsPipeline;
class IRHITexture;
class IRHISampler;

class IRenderMaterial : public IRenderResource
{
public:
	virtual SafePtr<IRHIGraphicsPipeline> GetPipeline() const = 0;
	virtual SafePtr<IRHITexture> GetTexture() const = 0;
	virtual SafePtr<IRHISampler> GetSampler() const = 0;
	virtual ERenderQueue GetRenderQueue() const = 0;
};

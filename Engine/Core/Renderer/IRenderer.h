#pragma once

#include "Core/Renderer/RendererTypes.h"

class IRenderResource;
class IRenderScene;

class IRenderer
{
public:
	virtual ~IRenderer() = default;

public:
	virtual bool Initialize(const RendererDesc& desc) = 0;
	virtual void Render(IRenderScene& scene) = 0;
	virtual void Finalize() = 0;

	virtual bool CreateGpuResource(IRenderResource& resource) = 0;
	virtual void DestroyGpuResource(IRenderResource& resource) = 0;
};


#pragma once

#include "Core/Renderer/RendererTypes.h"
#include "Core/Platform/RenderSurfaceTypes.h"

class IRenderResource;
class IRenderScene;

class IRenderer
{
public:
	virtual ~IRenderer() = default;

public:
	virtual bool Initialize(const RendererDesc& desc) = 0;
	virtual void SetRenderTargetSize(const RenderSurfaceSize& size) {}
	// Set the view camera used for the next Render() call.
	// posX/Y: camera center in world units.
	// orthographicSize: half-height visible in world units (default 1.0 = NDC space).
	// Aspect ratio is derived automatically from the current render-target size.
	virtual void SetViewCamera(float posX, float posY, float orthographicSize) {}
	// Explicit half-extents + rotation mode — stretch rendering with camera rotation.
	// halfW: half-width  visible in world units (X axis).
	// halfH: half-height visible in world units (Y axis).
	// cosR/sinR: camera rotation (cos/sin of angle). Default = no rotation.
	// No aspect-ratio derivation; content stretches to fill the viewport pixel area.
	virtual void SetViewCameraEx(float posX, float posY, float halfW, float halfH, float cosR = 1.0f, float sinR = 0.0f) {}
	// Draw a full-viewport quad in NDC space with the given RGBA color.
	// Directly overwrites pixels (no blending) — use after BeginRenderPass+SetViewport
	// to clear a sub-viewport area per-camera.
	virtual void FillViewportColor(float r, float g, float b, float a) {}
	virtual void Render(IRenderScene& scene) = 0;
	virtual void Finalize() = 0;

	virtual bool CreateGpuResource(IRenderResource& resource) = 0;
	virtual void DestroyGpuResource(IRenderResource& resource) = 0;
};


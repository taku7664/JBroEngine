#pragma once

#include "Core/RHI/RHIResource.h"
#include "Core/RHI/RHITypes.h"

class IRHISwapchain : public IRHIResource
{
public:
	virtual bool Initialize(const RenderSurfaceDesc& surfaceDesc) = 0;
	virtual void Resize(const RenderSurfaceSize& size) = 0;
	virtual void Present() = 0;

	// 표시 방향 기준 크기(Android pre-rotation 시 surface 의 네이티브 방향과 다를 수 있음).
	virtual RenderSurfaceSize GetSize() const = 0;

	// surface pre-rotation(클립공간 회전). 렌더러가 투영에 곱해 표시 방향을 보정한다.
	// 회전이 없는 백엔드(D3D11/WebGPU 등)는 기본 항등(cos=1,sin=0).
	virtual float GetPreRotationCosR() const { return 1.0f; }
	virtual float GetPreRotationSinR() const { return 0.0f; }
};

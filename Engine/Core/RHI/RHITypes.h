#pragma once

#include "Core/Platform/RenderSurfaceTypes.h"

enum class ERHIApi
{
	None,
	D3D11,
	WebGPU,
	WebGL2,
	Vulkan
};

struct RenderSurfaceDesc
{
	NativeSurfaceHandle NativeHandle;
	RenderSurfaceSize Size;
};

struct RHINativeDeviceDesc
{
	void* Device = nullptr;
	void* DeviceContext = nullptr;
};

struct RHIDesc
{
	ERHIApi Api = ERHIApi::None;
	RenderSurfaceDesc Surface;
	bool EnableDebugLayer = false;
};

#pragma once

#include <cstdint>
#include <functional>

struct RenderSurfaceCreateDesc
{
	const wchar_t* Title = L"JBroEngine";
	int Width = 1280;
	int Height = 720;
	bool IsResizable = true;
};

struct RenderSurfaceSize
{
	int Width = 0;
	int Height = 0;
};

enum class ERenderSurfaceType
{
	None,
	Win32Hwnd,
	HtmlCanvas,
	MobileNativeSurface
};

struct NativeSurfaceHandle
{
	ERenderSurfaceType SurfaceType = ERenderSurfaceType::None;
	void* Handle = nullptr;
};

struct NativeSurfaceMessage
{
	void* SurfaceHandle = nullptr;
	std::uint32_t Message = 0;
	std::uintptr_t WParam = 0;
	std::intptr_t LParam = 0;
};

using NativeSurfaceMessageHandler = std::function<bool(const NativeSurfaceMessage& message, std::intptr_t& result)>;

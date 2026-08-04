#pragma once

#include "Utillity/Pointer/SafePtr.h"

enum class ERHILoadOp
{
	Load,
	Clear,
	DontCare
};

enum class ERHIStoreOp
{
	Store,
	DontCare
};

// RHI 는 색 타입을 모른다 — 백엔드가 API 에 넘길 4채널 float 이 필요할 뿐이다.
// 호출부(엔진 Color)는 SetClearColor(color.Data()) 로 넘긴다.
struct RenderPassColorAttachmentDesc
{
	SafePtr<class IRHITexture> Target;
	ERHILoadOp LoadOp = ERHILoadOp::Clear;
	ERHIStoreOp StoreOp = ERHIStoreOp::Store;
	float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

	void SetClearColor(float r, float g, float b, float a)
	{
		ClearColor[0] = r;
		ClearColor[1] = g;
		ClearColor[2] = b;
		ClearColor[3] = a;
	}

	// rgba 는 연속된 4채널. nullptr 이면 아무것도 하지 않는다(기본값 유지).
	void SetClearColor(const float* rgba)
	{
		if (nullptr == rgba)
		{
			return;
		}
		for (int channel = 0; channel < 4; ++channel)
		{
			ClearColor[channel] = rgba[channel];
		}
	}
};

struct RenderPassDesc
{
	RenderPassColorAttachmentDesc ColorAttachment;
};

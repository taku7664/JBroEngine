#pragma once

#include "Utillity/SafePtr.h"

struct Color
{
	float R = 0.0f;
	float G = 0.0f;
	float B = 0.0f;
	float A = 1.0f;
};

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

struct RenderPassColorAttachmentDesc
{
	SafePtr<class IRHITexture> Target;
	ERHILoadOp LoadOp = ERHILoadOp::Clear;
	ERHIStoreOp StoreOp = ERHIStoreOp::Store;
	Color ClearColor;
};

struct RenderPassDesc
{
	RenderPassColorAttachmentDesc ColorAttachment;
};

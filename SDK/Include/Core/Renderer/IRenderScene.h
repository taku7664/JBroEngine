#pragma once

#include "Core/Renderer/RendererTypes.h"

class IRenderScene
{
public:
	virtual ~IRenderScene() = default;

public:
	virtual void Clear() = 0;
	virtual void Submit(const RenderItem& item) = 0;
	virtual std::uint32_t GetRenderItemCount() const = 0;
	virtual const RenderItem* GetRenderItems() const = 0;
};


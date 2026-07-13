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
	// 제출 순서 정렬. 구현이 필요 없으면 no-op(렌더러는 그리기 전에 항상 호출).
	virtual void Sort() {}
};


#pragma once

#include "Core/Renderer/RendererTypes.h"

// 정렬된 아이템 배열 안의 연속 구간. 레이어 하나가 차지하는 범위를 가리킨다.
struct RenderItemRange
{
	std::uint32_t Begin = 0;
	std::uint32_t Count = 0;
};

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
	// layerIndex 아이템들의 연속 구간. Sort() 이후에만 유효하다(정렬 1순위가 LayerIndex 라
	// 같은 레이어 아이템이 반드시 붙어 있다). 레이어 분할을 안 하는 구현은 전체를 돌려준다 —
	// 이 경우 호출자는 모든 레이어에 대해 같은 전체 범위를 받는다.
	virtual RenderItemRange GetLayerRange(RenderLayerIndex layerIndex) const
	{
		(void)layerIndex;
		return RenderItemRange{ 0, GetRenderItemCount() };
	}
};


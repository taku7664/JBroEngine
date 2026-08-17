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
	// 아이템을 씬에 넘긴다. **이동 전용**이다 — 아이템은 SafePtr 5개를 들고 있어 복사마다
	// 참조 카운트가 5쌍 발생하고, 이 경로는 오브젝트 수 × 매 프레임 돈다. 호출자는 로컬
	// RenderItem 을 채운 뒤 std::move 로 넘긴다.
	//
	// 구현은 제출 시점에 item.BuildWorldBounds() 로 파생 경계를 채워야 한다 — 컬링이 뷰마다
	// 4코너를 다시 변환하지 않게 하는 계약이다(Transform/LocalHalfExtents 확정 시점이 여기다).
	virtual void Submit(RenderItem&& item) = 0;
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


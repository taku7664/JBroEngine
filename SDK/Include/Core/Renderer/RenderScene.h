#pragma once

#include "Core/Renderer/IRenderScene.h"

#include <vector>

class CRenderScene final : public IRenderScene
{
public:
	void Clear() override;
	void Submit(const RenderItem& item) override;
	std::uint32_t GetRenderItemCount() const override;
	const RenderItem* GetRenderItems() const override;
	void Sort() override;
	RenderItemRange GetLayerRange(RenderLayerIndex layerIndex) const override;

private:
	static bool ShouldSortBefore(const RenderItem& lhs, const RenderItem& rhs);
	// 정렬된 배열을 1회 훑어 레이어별 구간을 채운다(Sort 안에서만 호출).
	void RebuildLayerRanges();

private:
	std::vector<RenderItem> m_renderItems;
	// 인덱스 = RenderLayerIndex. 아이템이 없는 레이어는 Count=0.
	std::vector<RenderItemRange> m_layerRanges;
	// 제출 순서가 정렬 순서를 어겼는가(true 일 때만 stable_sort).
	bool m_needsSort = false;
	// 마지막 Sort 이후 아이템이 바뀌었는가. 구간 테이블 재구축 판단용 —
	// Sort 는 카메라·레이어마다 불리므로 변화 없으면 즉시 반환해야 한다.
	bool m_isDirty = false;
};

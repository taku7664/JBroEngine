#pragma once

#include "Core/Renderer/IRenderScene.h"

#include <vector>

class CRenderScene final : public IRenderScene
{
public:
	void Clear() override;
	void Submit(RenderItem&& item) override;
	std::uint32_t GetRenderItemCount() const override;
	const RenderItem* GetRenderItems() const override;
	void Sort() override;
	RenderItemRange GetLayerRange(RenderLayerIndex layerIndex) const override;

private:
	// 정렬 키 하나. 아이템 본체(200바이트급)를 비교·이동하는 대신 이 12바이트만 정렬한다.
	// Key 동률일 때 Index 오름차순으로 결정하므로 stable_sort 없이 제출 순서가 보존된다
	// (stable_sort 는 임시 버퍼를 힙에 잡는다 — 프레임 루프 할당 금지 규칙에 걸린다).
	struct SortEntry
	{
		std::uint64_t Key = 0;
		std::uint32_t Index = 0;
	};

	static bool ShouldSortBefore(const RenderItem& lhs, const RenderItem& rhs);
	// 아이템의 정렬 우선순위를 단조 증가 정수 키로 접는다(ShouldSortBefore 와 동일 순서).
	static std::uint64_t MakeSortKey(const RenderItem& item);
	// 정렬된 배열을 1회 훑어 레이어별 구간을 채운다(Sort 안에서만 호출).
	void RebuildLayerRanges();

private:
	std::vector<RenderItem> m_renderItems;
	// 정렬 작업 버퍼. 멤버로 들고 있어야 용량이 유지되어 프레임당 힙 할당이 사라진다.
	std::vector<SortEntry> m_sortEntries;
	std::vector<RenderItem> m_sortScratch;
	// 인덱스 = RenderLayerIndex. 아이템이 없는 레이어는 Count=0.
	std::vector<RenderItemRange> m_layerRanges;
	// 제출 순서가 정렬 순서를 어겼는가(true 일 때만 stable_sort).
	bool m_needsSort = false;
	// 마지막 Sort 이후 아이템이 바뀌었는가. 구간 테이블 재구축 판단용 —
	// Sort 는 카메라·레이어마다 불리므로 변화 없으면 즉시 반환해야 한다.
	bool m_isDirty = false;
};

#include "pch.h"
#include "RenderScene.h"

void CRenderScene::Clear()
{
	m_renderItems.clear();
	m_layerRanges.clear();
	m_needsSort = false;
	m_isDirty = false;
}

void CRenderScene::Submit(const RenderItem& item)
{
	if (false == m_renderItems.empty()
		&& ShouldSortBefore(item, m_renderItems.back()))
	{
		m_needsSort = true;
	}
	m_renderItems.push_back(item);
	m_isDirty = true;
}

std::uint32_t CRenderScene::GetRenderItemCount() const
{
	return static_cast<std::uint32_t>(m_renderItems.size());
}

const RenderItem* CRenderScene::GetRenderItems() const
{
	return m_renderItems.empty() ? nullptr : m_renderItems.data();
}

void CRenderScene::Sort()
{
	// 뷰포트·레이어마다 호출된다 — 제출 이후 첫 호출만 실제 작업을 한다.
	if (false == m_isDirty)
	{
		return;
	}

	if (m_needsSort)
	{
		// stable_sort — 큐/정렬키 동률 아이템은 제출 순서를 유지한다(std::sort 는 동률 순서가
		// 불안정해 겹친 스프라이트가 프레임마다 앞뒤로 뒤바뀌며 z-플리커를 유발).
		std::stable_sort(m_renderItems.begin(), m_renderItems.end(), ShouldSortBefore);
	}
	RebuildLayerRanges();
	m_needsSort = false;
	m_isDirty = false;
}

void CRenderScene::RebuildLayerRanges()
{
	m_layerRanges.clear();
	if (m_renderItems.empty())
	{
		return;
	}

	// LayerIndex 가 정렬 1순위라 같은 레이어 아이템은 연속이다. 최대 인덱스까지 테이블을
	// 만들고(빈 레이어는 Count=0) 한 번의 훑기로 구간을 채운다.
	const RenderLayerIndex maxLayerIndex = m_renderItems.back().LayerIndex;
	m_layerRanges.resize(static_cast<std::size_t>(maxLayerIndex) + 1);

	std::uint32_t rangeBegin = 0;
	for (std::uint32_t i = 1; i <= static_cast<std::uint32_t>(m_renderItems.size()); ++i)
	{
		const bool isEnd = (i == m_renderItems.size());
		if (isEnd || m_renderItems[i].LayerIndex != m_renderItems[rangeBegin].LayerIndex)
		{
			const RenderLayerIndex layerIndex = m_renderItems[rangeBegin].LayerIndex;
			m_layerRanges[layerIndex] = RenderItemRange{ rangeBegin, i - rangeBegin };
			rangeBegin = i;
		}
	}
}

RenderItemRange CRenderScene::GetLayerRange(RenderLayerIndex layerIndex) const
{
	if (static_cast<std::size_t>(layerIndex) >= m_layerRanges.size())
	{
		return RenderItemRange{ 0, 0 };
	}
	return m_layerRanges[layerIndex];
}

bool CRenderScene::ShouldSortBefore(const RenderItem& lhs, const RenderItem& rhs)
{
	// 레이어가 1순위 — 레이어 경계를 넘는 큐/정렬키 비교는 의미가 없다(레이어는 각자
	// 합성되므로 Z오더 스코프가 레이어 내부로 닫혀 있다).
	if (lhs.LayerIndex != rhs.LayerIndex)
	{
		return lhs.LayerIndex < rhs.LayerIndex;
	}
	if (lhs.Queue != rhs.Queue)
	{
		return static_cast<int>(lhs.Queue) < static_cast<int>(rhs.Queue);
	}
	return lhs.SortOrder < rhs.SortOrder;
}

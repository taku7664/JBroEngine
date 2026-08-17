#include "pch.h"
#include "RenderScene.h"

void CRenderScene::Clear()
{
	m_renderItems.clear();
	m_layerRanges.clear();
	m_needsSort = false;
	m_isDirty = false;
}

void CRenderScene::Submit(RenderItem&& item)
{
	if (false == m_renderItems.empty()
		&& ShouldSortBefore(item, m_renderItems.back()))
	{
		m_needsSort = true;
	}
	// 뷰와 무관한 월드 경계는 지금 한 번만 낸다 — 뷰포트 × 레이어 × 배치 look-ahead 마다
	// 4코너를 다시 변환하던 비용을 없앤다(IRenderScene::Submit 계약).
	item.BuildWorldBounds();
	m_renderItems.push_back(std::move(item));
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
		// 키 배열만 정렬하고 아이템은 1회 gather 로 재배치한다. 아이템 본체는 SafePtr 5개 +
		// float 40여 개(200바이트급)라 비교 정렬로 직접 섞으면 N log N 번의 대형 이동이 된다.
		//
		// 동률은 제출 인덱스로 깨므로 std::sort 로도 제출 순서가 보존된다 — stable_sort 가
		// 필요 없어지고(임시 버퍼 힙 할당도 사라진다), z-플리커 방지 계약은 그대로다.
		const std::uint32_t itemCount = static_cast<std::uint32_t>(m_renderItems.size());
		m_sortEntries.clear();
		m_sortEntries.reserve(itemCount);
		for (std::uint32_t i = 0; i < itemCount; ++i)
		{
			m_sortEntries.push_back(SortEntry{ MakeSortKey(m_renderItems[i]), i });
		}

		std::sort(m_sortEntries.begin(), m_sortEntries.end(),
			[](const SortEntry& lhs, const SortEntry& rhs)
			{
				if (lhs.Key != rhs.Key)
				{
					return lhs.Key < rhs.Key;
				}
				return lhs.Index < rhs.Index;
			});

		m_sortScratch.clear();
		m_sortScratch.reserve(itemCount);
		for (const SortEntry& entry : m_sortEntries)
		{
			m_sortScratch.push_back(std::move(m_renderItems[entry.Index]));
		}
		m_renderItems.swap(m_sortScratch);
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

std::uint64_t CRenderScene::MakeSortKey(const RenderItem& item)
{
	// [49:34] LayerIndex(16) | [33:32] Queue(2) | [31:0] SortOrder(32)
	// ShouldSortBefore 와 같은 우선순위·같은 순서다(레이어 → 큐 → 정렬순서, 모두 오름차순).
	//
	// SortOrder 는 int32 이므로 부호 비트를 뒤집어 무부호 오름차순으로 옮긴다
	// (INT_MIN → 0). 그냥 캐스팅하면 음수가 가장 큰 값이 되어 순서가 뒤집힌다.
	// 큐가 4개를 넘으면 2비트 칸을 넘쳐 레이어 순서를 오염시킨다 — 조용히 깨지지 않게 막는다.
	static_assert(static_cast<int>(ERenderQueue::Overlay) <= 3,
		"ERenderQueue 가 4개를 넘었다 — MakeSortKey 의 비트 배치를 넓혀야 한다.");

	const std::uint64_t layer = static_cast<std::uint64_t>(item.LayerIndex);
	const std::uint64_t queue = static_cast<std::uint64_t>(static_cast<std::uint32_t>(item.Queue) & 0x3u);
	const std::uint64_t order = static_cast<std::uint64_t>(
		static_cast<std::uint32_t>(item.SortOrder) ^ 0x8000'0000u);
	return (layer << 34) | (queue << 32) | order;
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

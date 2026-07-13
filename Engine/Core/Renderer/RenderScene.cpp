#include "pch.h"
#include "RenderScene.h"

void CRenderScene::Clear()
{
	m_renderItems.clear();
	m_needsSort = false;
}

void CRenderScene::Submit(const RenderItem& item)
{
	if (false == m_renderItems.empty()
		&& ShouldSortBefore(item, m_renderItems.back()))
	{
		m_needsSort = true;
	}
	m_renderItems.push_back(item);
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
	if (false == m_needsSort)
	{
		return;
	}

	// stable_sort — 큐/정렬키 동률 아이템은 제출 순서를 유지한다(std::sort 는 동률 순서가
	// 불안정해 겹친 스프라이트가 프레임마다 앞뒤로 뒤바뀌며 z-플리커를 유발).
	std::stable_sort(m_renderItems.begin(), m_renderItems.end(), ShouldSortBefore);
	m_needsSort = false;
}

bool CRenderScene::ShouldSortBefore(const RenderItem& lhs, const RenderItem& rhs)
{
	if (lhs.Queue != rhs.Queue)
	{
		return static_cast<int>(lhs.Queue) < static_cast<int>(rhs.Queue);
	}
	return lhs.SortOrder < rhs.SortOrder;
}

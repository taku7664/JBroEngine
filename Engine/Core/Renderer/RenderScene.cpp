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

	std::sort(m_renderItems.begin(), m_renderItems.end(), ShouldSortBefore);
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

#include "pch.h"
#include "RenderScene.h"

void CRenderScene::Clear()
{
	m_renderItems.clear();
}

void CRenderScene::Submit(const RenderItem& item)
{
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
	std::sort(m_renderItems.begin(), m_renderItems.end(), [](const RenderItem& lhs, const RenderItem& rhs)
		{
			if (lhs.Queue != rhs.Queue)
			{
				return static_cast<int>(lhs.Queue) < static_cast<int>(rhs.Queue);
			}
			return lhs.SortOrder < rhs.SortOrder;
		});
}

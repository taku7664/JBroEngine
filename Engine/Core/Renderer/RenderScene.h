#pragma once

#include "Core/Renderer/IRenderScene.h"

class CRenderScene final : public IRenderScene
{
public:
	void Clear() override;
	void Submit(const RenderItem& item) override;
	std::uint32_t GetRenderItemCount() const override;
	const RenderItem* GetRenderItems() const override;
	void Sort();

private:
	static bool ShouldSortBefore(const RenderItem& lhs, const RenderItem& rhs);

private:
	std::vector<RenderItem> m_renderItems;
	bool m_needsSort = false;
};

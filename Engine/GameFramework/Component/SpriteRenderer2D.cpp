#include "pch.h"
#include "SpriteRenderer2D.h"

#include "Core/Asset/SpriteAsset.h"

#include <cmath>

void SpriteRenderer2D::ComputeLocalBounds(Rect& outBounds) const
{
	if (false == SpriteAssetCache.IsValid() || EAssetType::Sprite != SpriteAssetCache->GetAssetType())
	{
		// 아직 렌더 시스템이 자산을 해석하지 않았다 → 경계 미상(빈 Rect).
		return;
	}

	if (CachedPixelsPerUnit <= 0.0f)
	{
		return;
	}

	const CSpriteAsset& asset = static_cast<const CSpriteAsset&>(*SpriteAssetCache.Get());
	float frameWidth = static_cast<float>(asset.GetWidth());
	float frameHeight = static_cast<float>(asset.GetHeight());

	// 프레임 선택 규칙은 SpriteRenderSystem 과 같아야 한다 — 범위를 넘으면 마지막 프레임.
	const std::vector<SpriteFrame>& frames = asset.GetFrames();
	if (false == frames.empty() && frameWidth > 0.0f && frameHeight > 0.0f)
	{
		const std::uint32_t count = static_cast<std::uint32_t>(frames.size());
		const SpriteFrame& frame = frames[m_frameIndex < count ? m_frameIndex : count - 1];
		frameWidth = static_cast<float>(frame.Width);
		frameHeight = static_cast<float>(frame.Height);
	}

	// 쿼드는 Offset 을 중심으로 그려진다(반전은 부호만 뒤집으므로 경계에 영향 없음).
	const float halfWidth = std::abs((frameWidth / CachedPixelsPerUnit) * m_size.x) * 0.5f;
	const float halfHeight = std::abs((frameHeight / CachedPixelsPerUnit) * m_size.y) * 0.5f;
	outBounds = Rect(m_offset.x - halfWidth, m_offset.y - halfHeight, m_offset.x + halfWidth, m_offset.y + halfHeight);
}

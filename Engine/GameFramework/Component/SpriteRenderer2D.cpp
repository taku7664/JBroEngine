#include "pch.h"
#include "SpriteRenderer2D.h"

#include "Core/Asset/SpriteAsset.h"

#include <cmath>

bool SpriteRenderer2D::TryGetLocalQuad(Vector2& outCenter, Vector2& outSize) const
{
	if (false == SpriteAssetCache.IsValid() || EAssetType::Sprite != SpriteAssetCache->GetAssetType())
	{
		// 아직 렌더 시스템이 자산을 해석하지 않았다 → 쿼드를 낼 수 없다.
		return false;
	}

	if (CachedPixelsPerUnit <= 0.0f)
	{
		return false;
	}

	const CSpriteAsset& asset = static_cast<const CSpriteAsset&>(*SpriteAssetCache.Get());
	float frameWidth = static_cast<float>(asset.GetWidth());
	float frameHeight = static_cast<float>(asset.GetHeight());
	// 슬라이스가 없으면 자산 전체가 한 프레임이고 피벗은 임포트 옵션 값이다.
	float pivotX = asset.GetImportOptions().PivotX;
	float pivotY = asset.GetImportOptions().PivotY;

	const std::vector<SpriteFrame>& frames = asset.GetFrames();
	if (false == frames.empty() && frameWidth > 0.0f && frameHeight > 0.0f)
	{
		const std::uint32_t count = static_cast<std::uint32_t>(frames.size());
		const SpriteFrame& frame = frames[m_frameIndex < count ? m_frameIndex : count - 1];
		frameWidth = static_cast<float>(frame.Width);
		frameHeight = static_cast<float>(frame.Height);
		pivotX = frame.PivotX;
		pivotY = frame.PivotY;
	}

	outSize.x = (frameWidth / CachedPixelsPerUnit) * m_size.x;
	outSize.y = (frameHeight / CachedPixelsPerUnit) * m_size.y;
	// 반전은 크기 부호를 뒤집는다(별도 텍스처 없이 미러링).
	if (m_flipX) outSize.x = -outSize.x;
	if (m_flipY) outSize.y = -outSize.y;

	// 쿼드 메시는 ±0.5 중심 고정이라(배칭 근거) 피벗은 **이동 성분**으로 넣는다.
	// 피벗 점이 오브젝트 원점 + Offset 에 오도록 쿼드 중심을 민다.
	// 부호가 뒤집힌 outSize 를 그대로 쓰므로 반전 시 피벗을 축으로 미러링된다 — 분기 불필요.
	// 피벗 규약은 Y-up (0=아래, 1=위) — Transform2D.Anchor 와 같다.
	outCenter.x = m_offset.x + (0.5f - pivotX) * outSize.x;
	outCenter.y = m_offset.y + (0.5f - pivotY) * outSize.y;
	return true;
}

void SpriteRenderer2D::ComputeLocalBounds(Rect& outBounds) const
{
	Vector2 center;
	Vector2 size;
	if (false == TryGetLocalQuad(center, size))
	{
		// 경계 미상(빈 Rect).
		return;
	}

	const float halfWidth = std::abs(size.x) * 0.5f;
	const float halfHeight = std::abs(size.y) * 0.5f;
	outBounds = Rect(center.x - halfWidth, center.y - halfHeight, center.x + halfWidth, center.y + halfHeight);
}

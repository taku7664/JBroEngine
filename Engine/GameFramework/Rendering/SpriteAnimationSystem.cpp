#include "pch.h"
#include "SpriteAnimationSystem.h"

#include "Core/Asset/AssetRef.h"
#include "Core/Asset/AssetRef.inl"   // AssetRef<IAsset> 소멸자 인스턴스화
#include "Core/Asset/IAssetManager.h"
#include "Core/Asset/SpriteAsset.h"
#include "Core/ScriptCore.h"
#include "Core/Time/Time.h"
#include "GameFramework/Component/SpriteAnimator2D.h"
#include "GameFramework/Component/SpriteRenderer2D.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Scene/Scene.h"

#include <cstdint>

CSpriteAnimationSystem::CSpriteAnimationSystem(SafePtr<IAssetManager> assetManager)
	: m_assetManager(assetManager)
{
}

void CSpriteAnimationSystem::SetAssetManager(SafePtr<IAssetManager> assetManager)
{
	m_assetManager = assetManager;
}

void CSpriteAnimationSystem::OnUpdate(CGameScene& scene)
{
	const float delta = Script.Time.IsValid() ? Script.Time->GetDeltaSeconds() : 0.0f;

	scene.ForEach<SpriteAnimator2D>(
		[&](SpriteAnimator2D& animator)
		{
			if (false == IsActiveComponent(animator))
			{
				return;
			}

			CGameObject* owner = animator.GetOwner().TryGet();
			if (nullptr == owner)
			{
				return;
			}

			SpriteRenderer2D* sprite = owner->GetComponent<SpriteRenderer2D>();
			if (nullptr == sprite)
			{
				return;
			}

			// 총 프레임 수 조회 — 시트 슬라이스. 자산이 이미 로드돼 있으면 캐시 히트.
			std::uint32_t totalFrames = 0;
			if (m_assetManager.IsValid())
			{
				AssetRef<IAsset> assetRef = m_assetManager->LoadAsset(sprite->SpriteGuid);
				if (assetRef.IsValid() && EAssetType::Sprite == assetRef->GetAssetType())
				{
					totalFrames = static_cast<std::uint32_t>(
						static_cast<CSpriteAsset*>(assetRef.Get())->GetFrames().size());
				}
			}
			if (0 == totalFrames)
			{
				return;
			}

			// 재생 범위 [start, start+range) 를 총 프레임 수로 클램프.
			const std::uint32_t start = animator.StartFrame < totalFrames ? animator.StartFrame : totalFrames - 1;
			std::uint32_t range = animator.FrameCount > 0 ? animator.FrameCount : (totalFrames - start);
			if (start + range > totalFrames)
			{
				range = totalFrames - start;
			}

			// 애니메이션할 프레임이 1 이하면 정적 표시.
			if (range <= 1)
			{
				sprite->FrameIndex          = start;
				animator.RuntimeLocalFrame  = 0;
				animator.RuntimeElapsedSeconds = 0.0f;
				return;
			}

			if (animator.Playing && animator.FramesPerSecond > 0.0f)
			{
				const float frameDuration = 1.0f / animator.FramesPerSecond;
				animator.RuntimeElapsedSeconds += delta;
				while (animator.RuntimeElapsedSeconds >= frameDuration)
				{
					animator.RuntimeElapsedSeconds -= frameDuration;
					++animator.RuntimeLocalFrame;
					if (animator.RuntimeLocalFrame >= range)
					{
						if (animator.Loop)
						{
							animator.RuntimeLocalFrame = 0;
						}
						else
						{
							animator.RuntimeLocalFrame = range - 1;
							animator.Playing           = false;
							break;
						}
					}
				}
			}

			if (animator.RuntimeLocalFrame >= range)
			{
				animator.RuntimeLocalFrame = range - 1;
			}
			sprite->FrameIndex = start + animator.RuntimeLocalFrame;
		});
}

void CSpriteAnimationSystem::OnSimulationStop(CGameScene& scene)
{
	// 재생 정지 — 런타임 진행 상태 초기화(재개 시 처음부터).
	scene.ForEach<SpriteAnimator2D>(
		[](SpriteAnimator2D& animator)
		{
			animator.RuntimeElapsedSeconds = 0.0f;
			animator.RuntimeLocalFrame     = 0;
		});
}

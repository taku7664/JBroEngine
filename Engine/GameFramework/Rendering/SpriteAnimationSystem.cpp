#include "pch.h"
#include "SpriteAnimationSystem.h"

#include "Core/Asset/AnimationClipAsset.h"
#include "Core/Asset/AssetRef.h"
#include "Core/Asset/AssetRef.inl"   // AssetRef<IAsset> 소멸자 인스턴스화
#include "Core/Asset/IAssetManager.h"
#include "Core/Asset/SpriteAsset.h"
#include "Core/ScriptCore.h"
#include "Core/Time/Time.h"
#include "GameFramework/Component/SpriteAnimator2D.h"
#include "GameFramework/Component/SpriteRenderer2D.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Canvas/Canvas.h"
#include "GameFramework/Scripting/GameScript.h"

#include <cstdint>
#include <cstring>

namespace
{
	// 재생 구간 [start, start+range) 를 시트 총 프레임 수로 클램프한다.
	// range 는 최소 1 을 보장한다(0 이면 순환 계산이 무너진다).
	void ResolvePlaybackRange(std::uint32_t totalFrames, std::uint32_t requestedStart,
	                          std::uint32_t requestedCount,
	                          std::uint32_t& outStart, std::uint32_t& outRange)
	{
		outStart = requestedStart < totalFrames ? requestedStart : totalFrames - 1;
		outRange = requestedCount > 0 ? requestedCount : (totalFrames - outStart);
		if (outStart + outRange > totalFrames)
		{
			outRange = totalFrames - outStart;
		}
		if (0 == outRange)
		{
			outRange = 1;
		}
	}
}

CSpriteAnimationSystem::CSpriteAnimationSystem(SafePtr<IAssetManager> assetManager)
	: m_assetManager(assetManager)
{
}

void CSpriteAnimationSystem::SetAssetManager(SafePtr<IAssetManager> assetManager)
{
	m_assetManager = assetManager;
}

std::uint32_t CSpriteAnimationSystem::GetSheetFrameCount(const AssetGuid& spriteGuid) const
{
	if (false == m_assetManager.IsValid())
	{
		return 0;
	}
	AssetRef<IAsset> assetRef = m_assetManager->LoadAsset(spriteGuid);
	if (false == assetRef.IsValid() || EAssetType::Sprite != assetRef->GetAssetType())
	{
		return 0;
	}
	return static_cast<std::uint32_t>(static_cast<CSpriteAsset*>(assetRef.Get())->GetFrames().size());
}

CAnimationClipAsset* CSpriteAnimationSystem::LoadClipAt(const SpriteAnimator2D& animator, std::int32_t index) const
{
	if (index < 0 || static_cast<std::size_t>(index) >= animator.ClipGuids.Size() || false == m_assetManager.IsValid())
	{
		return nullptr;
	}
	AssetRef<IAsset> assetRef = m_assetManager->LoadAsset(animator.ClipGuids[static_cast<std::size_t>(index)]);
	if (false == assetRef.IsValid() || EAssetType::AnimationClip != assetRef->GetAssetType())
	{
		return nullptr;
	}
	return static_cast<CAnimationClipAsset*>(assetRef.Get());
}

std::int32_t CSpriteAnimationSystem::FindClipIndex(const SpriteAnimator2D& animator, const char* clipName) const
{
	const bool wantsFirst = (nullptr == clipName || '\0' == clipName[0]);

	for (std::size_t i = 0; i < animator.ClipGuids.Size(); ++i)
	{
		CAnimationClipAsset* clip = LoadClipAt(animator, static_cast<std::int32_t>(i));
		if (nullptr == clip)
		{
			continue;
		}
		if (wantsFirst || 0 == std::strcmp(clip->GetName().c_str(), clipName))
		{
			return static_cast<std::int32_t>(i);
		}
	}
	return -1;
}

void CSpriteAnimationSystem::DispatchEvent(CGameObject* owner, const char* clipName, const char* eventName)
{
	if (nullptr == owner)
	{
		return;
	}
	CGameCanvas* canvas = owner->GetCanvas();
	if (nullptr == canvas)
	{
		return;
	}

	canvas->ForEachScriptOnObject(*owner, [&](CGameScript& script, CGameCanvas::ScriptRuntimeState&)
	{
		CGameObject* scriptOwner = script.GetOwner().TryGet();
		if (false == script.IsEnabled() || nullptr == scriptOwner || false == scriptOwner->IsActiveInHierarchy())
		{
			return;
		}
		script.AnimationEvent(clipName, eventName);
	});
}

void CSpriteAnimationSystem::DispatchEnd(CGameObject* owner, const char* clipName)
{
	if (nullptr == owner)
	{
		return;
	}
	CGameCanvas* canvas = owner->GetCanvas();
	if (nullptr == canvas)
	{
		return;
	}

	canvas->ForEachScriptOnObject(*owner, [&](CGameScript& script, CGameCanvas::ScriptRuntimeState&)
	{
		CGameObject* scriptOwner = script.GetOwner().TryGet();
		if (false == script.IsEnabled() || nullptr == scriptOwner || false == scriptOwner->IsActiveInHierarchy())
		{
			return;
		}
		script.AnimationEnd(clipName);
	});
}

void CSpriteAnimationSystem::StartClip(SpriteAnimator2D& animator, SpriteRenderer2D& sprite, CGameObject& owner,
                                       std::int32_t clipIndex, const CAnimationClipAsset& clip)
{
	const AnimationClipData& data = clip.GetData();

	// 클립이 자기 시트를 지정했으면 렌더러의 시트를 갈아 끼운다. 지정하지 않았으면
	// 렌더러가 이미 들고 있는 시트를 그대로 쓴다(한 시트를 구간으로 나눠 쓰는 흔한 경우).
	if (false == data.SpriteGuid.IsNull() && data.SpriteGuid != sprite.GetSpriteGuid())
	{
		sprite.SetSpriteGuid(data.SpriteGuid);
	}

	animator.RuntimeClipIndex      = clipIndex;
	animator.RuntimeLocalFrame     = 0;
	animator.RuntimeElapsedSeconds = 0.0f;
	animator.Playing               = true;

	const std::string& name = clip.GetName();
	std::size_t i = 0;
	for (; i + 1 < SpriteAnimator2D::CLIP_NAME_CAPACITY && i < name.size(); ++i)
	{
		animator.CurrentClip[i] = name[i];
	}
	animator.CurrentClip[i] = '\0';

	// 첫 프레임이 이미 화면에 나온 것이므로 0번 프레임 이벤트는 지금 발화한다.
	for (const AnimationClipEvent& clipEvent : data.Events)
	{
		if (0 == clipEvent.Frame)
		{
			DispatchEvent(&owner, animator.CurrentClip, clipEvent.Name.c_str());
		}
	}
}

void CSpriteAnimationSystem::UpdateClipMode(SpriteAnimator2D& animator, SpriteRenderer2D& sprite,
                                            CGameObject& owner, float deltaSeconds)
{
	// ── 요청 소비 ────────────────────────────────────────────────────────────
	if (animator.HasStopRequest)
	{
		animator.HasStopRequest = false;
		animator.Playing        = false;
	}

	if (animator.HasPlayRequest)
	{
		animator.HasPlayRequest = false;
		const std::int32_t requested = FindClipIndex(animator, animator.RequestedClip);
		if (requested >= 0)
		{
			if (CAnimationClipAsset* clip = LoadClipAt(animator, requested))
			{
				StartClip(animator, sprite, owner, requested, *clip);
			}
		}
		// 못 찾으면 무시한다 — 현재 재생을 끊지 않는다.
	}

	// ── 아직 아무 클립도 안 정해졌으면 기본 클립으로 시작 ────────────────────
	if (animator.RuntimeClipIndex < 0)
	{
		const std::int32_t initial = FindClipIndex(animator, animator.DefaultClip.c_str());
		if (initial < 0)
		{
			return;   // 클립 자산이 아직 로드되지 않았거나 전부 무효.
		}
		CAnimationClipAsset* initialClip = LoadClipAt(animator, initial);
		if (nullptr == initialClip)
		{
			return;
		}
		StartClip(animator, sprite, owner, initial, *initialClip);
	}

	CAnimationClipAsset* clip = LoadClipAt(animator, animator.RuntimeClipIndex);
	if (nullptr == clip)
	{
		return;
	}
	const AnimationClipData& data = clip->GetData();

	const AssetGuid& sheetGuid = data.SpriteGuid.IsNull() ? sprite.GetSpriteGuid() : data.SpriteGuid;
	const std::uint32_t totalFrames = GetSheetFrameCount(sheetGuid);
	if (0 == totalFrames)
	{
		return;
	}

	std::uint32_t start = 0;
	std::uint32_t range = 0;
	ResolvePlaybackRange(totalFrames, data.StartFrame, data.FrameCount, start, range);

	// 애니메이션할 프레임이 1 이하면 정적 표시.
	if (range <= 1)
	{
		sprite.SetFrameIndex(start);
		animator.RuntimeLocalFrame     = 0;
		animator.RuntimeElapsedSeconds = 0.0f;
		return;
	}

	if (animator.Playing && data.FramesPerSecond > 0.0f)
	{
		const float frameDuration = 1.0f / data.FramesPerSecond;
		animator.RuntimeElapsedSeconds += deltaSeconds;
		while (animator.RuntimeElapsedSeconds >= frameDuration)
		{
			animator.RuntimeElapsedSeconds -= frameDuration;
			++animator.RuntimeLocalFrame;

			if (animator.RuntimeLocalFrame >= range)
			{
				if (data.Loop)
				{
					animator.RuntimeLocalFrame = 0;
				}
				else
				{
					// 마지막 프레임에서 멈춘다. 그 프레임의 이벤트는 도달했을 때 이미 발화했다.
					animator.RuntimeLocalFrame = range - 1;
					animator.Playing           = false;
					DispatchEnd(&owner, animator.CurrentClip);
					break;
				}
			}

			// 프레임률이 낮아 여러 프레임을 한 틱에 넘겨도 지나친 이벤트를 모두 순서대로 발화한다.
			for (const AnimationClipEvent& clipEvent : data.Events)
			{
				if (clipEvent.Frame == animator.RuntimeLocalFrame)
				{
					DispatchEvent(&owner, animator.CurrentClip, clipEvent.Name.c_str());
				}
			}
		}
	}

	if (animator.RuntimeLocalFrame >= range)
	{
		animator.RuntimeLocalFrame = range - 1;
	}
	sprite.SetFrameIndex(start + animator.RuntimeLocalFrame);
}

void CSpriteAnimationSystem::UpdateSheetMode(SpriteAnimator2D& animator, SpriteRenderer2D& sprite, float deltaSeconds)
{
	const std::uint32_t totalFrames = GetSheetFrameCount(sprite.GetSpriteGuid());
	if (0 == totalFrames)
	{
		return;
	}

	std::uint32_t start = 0;
	std::uint32_t range = 0;
	ResolvePlaybackRange(totalFrames, animator.StartFrame, animator.FrameCount, start, range);

	// 애니메이션할 프레임이 1 이하면 정적 표시.
	if (range <= 1)
	{
		sprite.SetFrameIndex(start);
		animator.RuntimeLocalFrame     = 0;
		animator.RuntimeElapsedSeconds = 0.0f;
		return;
	}

	if (animator.Playing && animator.FramesPerSecond > 0.0f)
	{
		const float frameDuration = 1.0f / animator.FramesPerSecond;
		animator.RuntimeElapsedSeconds += deltaSeconds;
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
	sprite.SetFrameIndex(start + animator.RuntimeLocalFrame);
}

void CSpriteAnimationSystem::OnUpdate(CGameCanvas& canvas)
{
	const float delta = Script.Time.IsValid() ? Script.Time->GetDeltaSeconds() : 0.0f;

	canvas.ForEach<SpriteAnimator2D>(
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

			if (animator.ClipGuids.Size() > 0)
			{
				UpdateClipMode(animator, *sprite, *owner, delta);
			}
			else
			{
				UpdateSheetMode(animator, *sprite, delta);
			}
		});
}

void CSpriteAnimationSystem::OnSimulationStop(CGameCanvas& canvas)
{
	// 재생 정지 — 런타임 진행 상태 초기화(재개 시 처음부터).
	// 요청 슬롯도 비운다: 정지 직전에 남은 Play 요청이 재개하자마자 튀지 않게.
	canvas.ForEach<SpriteAnimator2D>(
		[](SpriteAnimator2D& animator)
		{
			animator.RuntimeElapsedSeconds = 0.0f;
			animator.RuntimeLocalFrame     = 0;
			animator.RuntimeClipIndex      = -1;
			animator.HasPlayRequest        = false;
			animator.HasStopRequest        = false;
			animator.RequestedClip[0]      = '\0';
			animator.CurrentClip[0]        = '\0';
		});
}

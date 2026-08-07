#pragma once

#include "Core/Asset/IAsset.h"
#include "Core/Asset/IAssetLoader.h"

#include <cstdint>
#include <string>
#include <vector>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  CAnimationClipAsset / CAnimationClipAssetLoader
//
//  스프라이트시트의 **한 구간에 이름을 붙인 것**이 클립이다. "idle", "run", "hit" 처럼
//  이름으로 지목해 재생한다(`SpriteAnimator2D::Play("run")`).
//
//  왜 컴포넌트 필드가 아니라 에셋인가:
//    클립은 오브젝트마다 다시 적는 값이 아니라 시트에 딸린 사실이다. 에셋이면 프리팹
//    여럿이 같은 클립을 참조하고, 프레임 범위를 한 곳에서 고쳐도 전부 반영된다.
//    리플렉션 `Array<T>` 가 스칼라 원소만 받는 제약도 함께 피한다 — 이벤트 목록처럼
//    중첩된 구조는 인스펙터 프로퍼티로는 못 담지만 에셋 본문 YAML 로는 자연스럽다.
//
//  디스크 표현은 `.janimclip` 소스 파일 안의 YAML (Material 의 `.jmat`, 효과의 `.jfx` 패턴).
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// 클립 재생 중 특정 프레임에 도달하면 스크립트로 알리는 지점.
// Frame 은 **클립 로컬** 인덱스다(0 = 클립의 첫 프레임). 시트 절대 인덱스가 아니다 —
// 클립의 StartFrame 을 옮겨도 이벤트가 따라 움직이게 하기 위함이다.
struct AnimationClipEvent
{
	std::uint32_t Frame = 0;
	std::string   Name;
};

struct AnimationClipData
{
	// 재생할 시트. 비워 두면 SpriteRenderer2D 가 이미 들고 있는 시트를 그대로 쓴다
	// (한 시트 안에서 구간만 나눠 쓰는 흔한 경우에 자산 참조를 두 번 적지 않게).
	AssetGuid     SpriteGuid = INVALID_ASSET_GUID;
	// Play(name) 로 지목할 이름. 비어 있으면 파일 이름(확장자 제외)을 쓴다.
	std::string   Name;
	std::uint32_t StartFrame      = 0;
	// 0 = StartFrame 부터 시트의 마지막 프레임까지.
	std::uint32_t FrameCount      = 0;
	float         FramesPerSecond = 12.0f;
	bool          Loop            = true;

	std::vector<AnimationClipEvent> Events;
};

class CAnimationClipAsset final : public IAsset
{
public:
	CAnimationClipAsset(const AssetMetaData& metaData, AnimationClipData data);

	// IAsset
	AssetGuid            GetGuid()      const override;
	// Ref<T> 가 RTTI 없이 타입을 확인하는 컴파일타임 짝 — 로더 등록이 지키는
	// "이 타입은 이 클래스" 계약을 코드로 적어 둔 것이다.
	static constexpr EAssetType StaticAssetType() { return EAssetType::AnimationClip; }

	EAssetType           GetAssetType() const override;
	EAssetLoadState      GetLoadState() const override;
	const AssetMetaData& GetMetaData()  const override;
	void                 ApplyImportOptions(const std::string& importOptionsYaml) override;

	// 데이터
	const AnimationClipData& GetData() const { return m_data; }
	// 재생 이름 — 데이터에 이름이 없으면 파일 이름(확장자 제외)으로 폴백한다.
	// 클립을 만들자마자 파일명만으로 Play 가 되도록.
	const std::string&       GetName() const { return m_effectiveName; }

	// 클립 데이터가 in-place 로 갱신될 때마다 증가한다(ApplyImportOptions).
	// SpriteAnimationSystem 이 캐시한 재생 상태를 무효화하는 데 쓴다
	// (스프라이트의 PixelGeneration, 효과의 Generation 패턴).
	std::uint32_t GetGeneration() const { return m_generation; }

private:
	void RefreshEffectiveName();

	AssetMetaData     m_metaData;
	AnimationClipData m_data;
	std::string       m_effectiveName;
	EAssetLoadState   m_loadState  = EAssetLoadState::Loaded;
	std::uint32_t     m_generation = 0;
};

// ── YAML 직렬화 ─────────────────────────────────────────────────────────────
// `.janimclip` 파일 본문 텍스트 ↔ AnimationClipData.
class CAnimationClipSerializer final
{
public:
	static AnimationClipData FromYaml(const std::string& yamlText);
	static std::string       ToYaml(const AnimationClipData& data);
};

class CAnimationClipAssetLoader final : public IAssetLoader
{
public:
	EAssetType       GetSupportedType() const override;
	bool             CanLoad(const AssetLoadDesc& desc) const override;
	OwnerPtr<IAsset> Load(const AssetLoadDesc& desc) override;
	void             Unload(IAsset& asset) override;
	bool             ReloadInto(IAsset& existing, const AssetMetaData& metaData) override;
};

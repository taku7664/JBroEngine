#pragma once

#include "Core/Asset/AssetRef.h"
#include "Core/Asset/AssetTypes.h"

class IAsset;
class IAssetManager;

// ── CTransientAssetLoad ──────────────────────────────────────────────────────
// "지금 이걸 보여주는 동안만" 자산을 들고 있는 일회성 로더.
//
// 왜 필요한가: `AssetRef` 는 **use-count** 만 관리한다. use-count 가 0 이 되어도 자산은
// `CAssetManager` 캐시에 그대로 남는다(자동 GC 없음 — 해제는 `UnloadAsset` 명시 호출뿐).
// 그래서 인스펙터 미리보기처럼 잠깐 보고 마는 코드가 `LoadAsset` 만 하면, 자산을 하나씩
// 눌러 볼 때마다 디코딩된 PCM·픽셀이 계단식으로 누적된다(오디오 미리듣기에서 실제로 터진 적 있다).
//
// 계약:
//  · `Acquire(manager, guid)` — 대상이 바뀌면 이전 자산을 내리고 새 자산을 올린다.
//    같은 guid 를 다시 주면 재로드하지 않고 들고 있던 것을 그대로 돌려준다(매 프레임 호출 안전).
//  · 소멸·`Release()` 시 자신이 캐시에 올린 자산만 내린다. **이미 캐시에 있던 자산은 건드리지
//    않는다** — 다른 시스템(캔버스·리소스 레지스트리)이 쓰려고 올려둔 것을 치우면 안 되기 때문.
//  · 내릴 때 `AssetRef` 를 먼저 놓아 use-count 를 0 으로 만든 뒤 `UnloadAsset` 을 부른다.
//    (use-count > 0 이면 `UnloadAsset` 이 거부하므로 순서가 뒤집히면 조용히 안 내려간다.)
//
// 주의: 자산 내부 버퍼를 **raw 포인터로 계속 참조하는** 표시기가 있다면, 그 포인터를 먼저
// 끊은 뒤 `Release()` 해야 한다(내려간 자산의 PCM 을 가리키는 dangling 방지).
class CTransientAssetLoad final
{
public:
	CTransientAssetLoad() = default;
	~CTransientAssetLoad();

	CTransientAssetLoad(const CTransientAssetLoad&) = delete;
	CTransientAssetLoad& operator=(const CTransientAssetLoad&) = delete;

	// 대상 자산을 지정하고 로드된 핸들을 돌려준다(실패 시 빈 핸들).
	const AssetRef<IAsset>& Acquire(IAssetManager& manager, const AssetGuid& guid);
	// 들고 있던 자산을 놓는다(자신이 올린 것이면 캐시에서도 내린다).
	void Release();

	const AssetRef<IAsset>& Get() const { return m_asset; }
	bool IsValid() const { return m_asset.IsValid(); }

private:
	IAssetManager*   m_manager = nullptr;
	AssetGuid        m_guid;
	AssetRef<IAsset> m_asset;
	// 이 자산을 캐시에 올린 게 우리인가. false 면 Release 에서 내리지 않는다.
	bool             m_ownsCacheEntry = false;
};

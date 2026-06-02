#pragma once

#include "Core/Asset/AssetTypes.h"
#include "Utillity/Pointer/SafePtr.h"

class IAsset;
class IAssetManager;

// ── AssetRef<T> ──────────────────────────────────────────────────────────────
// 자산을 strong 의미로 잡는 핸들. AssetRef 가 살아있는 동안은 그 자산이 unload 되지 않는다.
// SafePtr<T> 는 weak (객체 사망 감지만), AssetRef<T> 는 strong (객체 수명 보호) 으로 의미가 갈린다.
//
// 사용처:
//  - 씬 진입 시 sceneAssets 를 AssetRef 들로 보유 → 씬이 active 인 동안 자산 보호.
//  - 인스펙터 미리듣기/미리보기 → 인스펙터가 표시 중인 자산을 AssetRef 로 잡음.
//  - ResourceRegistry → 에디터 영구 리소스를 AssetRef 로 보유 (IsPersistent 와 양립).
//
// 정합성:
//  - 복사 시 AssetManager 의 use-count++.
//  - 소멸 시 use-count--.
//  - UnloadAsset / UnloadNonPersistentAssets 는 use-count > 0 자산을 스킵.
//  - ReloadAsset 은 in-place data swap 이라 use-count 와 무관 (객체 주소 보존).
template<typename T>
class AssetRef final
{
public:
	AssetRef() noexcept = default;
	AssetRef(std::nullptr_t) noexcept {}

	// Manager + GUID 로 직접 생성. Manager 가 사용처에 노출하는 LoadAsset 류가 호출.
	// asset 포인터는 ReloadAsset 의 in-place 모델 가정 하에서 캐시. nullptr 이면 빈 핸들.
	AssetRef(SafePtr<IAssetManager> manager, const AssetGuid& guid, T* asset);

	AssetRef(const AssetRef& rhs);
	AssetRef& operator=(const AssetRef& rhs);

	AssetRef(AssetRef&& rhs) noexcept;
	AssetRef& operator=(AssetRef&& rhs) noexcept;

	// 다형 변환 (Derived → Base).
	template<typename U>
	requires std::is_convertible_v<U*, T*>
	AssetRef(const AssetRef<U>& rhs);

	template<typename U>
	requires std::is_convertible_v<U*, T*>
	AssetRef& operator=(const AssetRef<U>& rhs);

	~AssetRef();

	void Reset() noexcept;

	bool      IsValid()    const noexcept { return nullptr != m_asset; }
	const AssetGuid& GetGuid() const noexcept { return m_guid; }
	T*        Get()        const noexcept { return m_asset; }

	T& operator*()  const noexcept { return *m_asset; }
	T* operator->() const noexcept { return  m_asset; }
	explicit operator bool() const noexcept { return IsValid(); }

private:
	template<typename U> friend class AssetRef;

	template<typename TTo>
	friend AssetRef<TTo> StaticAssetRefCast(const AssetRef<IAsset>& base);

	void AcquireUseCount();
	void ReleaseUseCount();

private:
	SafePtr<IAssetManager> m_manager;
	AssetGuid              m_guid;
	T*                     m_asset = nullptr;
};

// AssetRef<IAsset> 을 AssetRef<T> (derived) 로 static_cast.
// 호출자가 type 검증을 마친 뒤에 사용.
template<typename T>
AssetRef<T> StaticAssetRefCast(const AssetRef<IAsset>& base);

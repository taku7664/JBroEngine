#pragma once

#include "Core/Asset/IAsset.h"               // IAsset (is_base_of, dynamic_cast)
#include "GameFramework/Object/GameObject.h"  // CGameObject (is_base_of, Object 카테고리)
#include "GameFramework/Reflection/ReflectionTypes.h" // ERefCategory, RefBase (단일 정의)
#include "GameFramework/Scripting/GameScript.h" // CGameScript (is_base_of, dynamic_cast)
#include "Utillity/File/FilePath.h"           // File::Guid

#include <cstring>
#include <type_traits>
#include <typeinfo>

// ─────────────────────────────────────────────────────────────────────────────
//  Ref<T> — 오브젝트/컴포넌트/스크립트/에셋에 대한 "안정적 참조".
//
//  - 직접 포인터를 들고 있지 않고, 대상의 안정 식별자(guid 문자열)만 저장한다.
//      · GameObject / Component / Script : 대상 오브젝트의 InstanceGuid
//      · Asset                            : 에셋의 AssetGuid
//    덕분에 씬 저장/로드, 컴포넌트 재배치(sparse-set 재할당), DLL 핫리로드를
//    넘어서도 참조가 끊기지 않는다.
//
//  - 저장부는 RefBase 의 고정 길이 char 버퍼(POD)다. File::Guid(std::filesystem::path)
//    같은 힙 포인터 보유 객체를 게임 스크립트 인스턴스에 두면 호스트↔게임 DLL ABI
//    불일치로 값이 깨지므로, guid 는 POD 버퍼로만 저장하고 해석 시점에만 File::Guid 로 변환.
//
//  - Get() 은 매 호출 시 활성 씬/에셋 매니저에서 다시 해석한다(캐시 없음).
//
//  카테고리는 T 로부터 컴파일타임에 결정된다:
//      IAsset 파생      → Asset
//      CGameScript 파생 → Script
//      CGameScene       → Scene (guid 대신 씬 이름 저장 — SceneManager 의 유일 키)
//      그 외(POD 컴포넌트, GameObject 포함) → Component
// ─────────────────────────────────────────────────────────────────────────────

// ERefCategory / RefBase 는 GameFramework/Reflection/ReflectionTypes.h 에 단일 정의.

// Scene 카테고리는 is_same 으로만 판별하므로 전방 선언으로 충분하다.
// (단, Ref<CGameScene> 을 실제 인스턴스화하는 TU 는 Scene.h 를 포함해야 한다 —
//  나머지 트레이트 평가에 완전형이 필요.)
class CGameScene;

namespace RefDetail
{
	// Ref.cpp 에서 정의 — 무거운 헤더(Scene/Core/AssetManager)를 헤더로 끌어오지 않는다.
	// 경계엔 POD(const char*) 만 넘긴다 — File::Guid 는 Ref.cpp 내부에서만 구성.

	// (오브젝트 guid + 컴포넌트 guid) → 활성 씬의 그 오브젝트에서 컴포넌트 guid 로 특정
	// 컴포넌트를 찾고, 타입이 맞으면 주소(void*)를 반환. 같은 타입이 여럿이어도 1개 지목.
	// 컴포넌트 guid 가 비어 있으면(구 데이터) 타입 첫 매치로 폴백.
	void* ResolveComponent(const char* objectGuid, const char* componentGuid, TypeId componentTypeId);

	// InstanceGuid → 활성 씬의 오브젝트(CGameObject*) 자체.
	CGameObject* ResolveObject(const char* instanceGuid);

	// (오브젝트 guid + 컴포넌트 guid) → 그 오브젝트의 특정 CGameScript.
	// 컴포넌트 guid 가 비어 있으면 첫 CGameScript 로 폴백.
	CGameScript* ResolveScript(const char* objectGuid, const char* componentGuid, TypeId scriptTypeId);

	// AssetGuid → 활성 에셋 매니저에서 로드/조회한 IAsset*.
	IAsset* ResolveAsset(const char* assetGuid);

	// 씬 이름 → SceneManager 의 로드된 씬(CGameScene*). 파괴/미로드면 nullptr.
	CGameScene* ResolveScene(const char* sceneName);
}

template<typename T>
class Ref : public RefBase
{
public:
	// Ref<T> 는 5가지 카테고리 중 하나여야 한다. 그 외(예: Ref<Transform2D>, Ref<int>)는
	// 해석 경로가 없어 항상 null 로 조용히 실패하므로 컴파일 타임에 막는다.
	static_assert(
		std::is_same_v<CGameScene, T>      ||
		std::is_base_of_v<IAsset, T>       ||
		std::is_base_of_v<CGameScript, T>  ||
		std::is_base_of_v<CGameObject, T>  ||
		std::is_base_of_v<CComponent, T>,
		"Ref<T>: T must be CGameScene or derive from IAsset, CGameScript, CGameObject, or CComponent.");

	static constexpr ERefCategory Category =
		std::is_same_v<CGameScene, T>     ? ERefCategory::Scene  :
		std::is_base_of_v<IAsset, T>      ? ERefCategory::Asset  :
		std::is_base_of_v<CGameScript, T> ? ERefCategory::Script :
		std::is_base_of_v<CGameObject, T> ? ERefCategory::Object :
		                                    ERefCategory::Component;

	Ref() = default;
	explicit Ref(const File::Guid& guid) { SetGuid(guid); }

	// ── 식별자 접근 ────────────────────────────────────────────────────────────
	// IsNull()/Clear()/GuidText()/ComponentGuidText()/SetGuidText()/SetComponentGuidText() 는 RefBase 제공.
	File::Guid GetGuid() const { return File::Guid(Guid); }
	void       SetGuid(const File::Guid& guid) { SetGuidText(guid.generic_string().c_str()); }

	// 컴포넌트/스크립트 Ref 지정: 소유 오브젝트 guid + 대상 컴포넌트 guid 를 함께 설정한다.
	void SetComponentGuid(const File::Guid& objectGuid, const File::Guid& componentGuid)
	{
		SetGuidText(objectGuid.generic_string().c_str());
		SetComponentGuidText(componentGuid.generic_string().c_str());
	}

	// ── 해석 ───────────────────────────────────────────────────────────────────
	// 대상이 없거나(삭제/미로드) 타입이 맞지 않으면 nullptr.
	T* Get() const
	{
		if (IsNull())
		{
			return nullptr;
		}

		if constexpr (ERefCategory::Asset == Category)
		{
			return dynamic_cast<T*>(RefDetail::ResolveAsset(Guid));
		}
		else if constexpr (ERefCategory::Script == Category)
		{
			// 스크립트 — (오브젝트 guid + 컴포넌트 guid) 로 특정 CGameScript 해석.
			if constexpr (requires { T::StaticTypeName(); })
			{
				return static_cast<T*>(RefDetail::ResolveScript(
					Guid,
					ComponentGuid,
					MakeStableTypeId(T::StaticTypeName())));
			}
			else
			{
				return dynamic_cast<T*>(RefDetail::ResolveScript(Guid, ComponentGuid, INVALID_TYPE_ID));
			}
		}
		else if constexpr (ERefCategory::Object == Category)
		{
			// GameObject(CGameObject) 자체 — InstanceGuid 로 오브젝트 해석.
			return static_cast<T*>(RefDetail::ResolveObject(Guid));
		}
		else if constexpr (ERefCategory::Scene == Category)
		{
			// 씬 — guid 가 아니라 씬 이름으로 SceneManager 에서 해석.
			return static_cast<T*>(RefDetail::ResolveScene(Guid));
		}
		else
		{
			// 컴포넌트 — (오브젝트 guid + 컴포넌트 guid) 로 특정 1개. void* 는 실제 T*.
			if constexpr (requires { T::StaticTypeName(); })
			{
				return static_cast<T*>(RefDetail::ResolveComponent(
					Guid,
					ComponentGuid,
					MakeStableTypeId(T::StaticTypeName())));
			}
			else
			{
				return dynamic_cast<T*>(static_cast<CComponent*>(RefDetail::ResolveComponent(
					Guid,
					ComponentGuid,
					INVALID_TYPE_ID)));
			}
		}
	}

	// ── 편의 연산자 ────────────────────────────────────────────────────────────
	explicit operator bool() const { return nullptr != Get(); }
	T*        operator->() const { return Get(); }
	T&        operator*()  const { return *Get(); }

	bool operator==(const Ref& rhs) const
	{
		return 0 == std::strcmp(Guid, rhs.Guid) && 0 == std::strcmp(ComponentGuid, rhs.ComponentGuid);
	}
	bool operator!=(const Ref& rhs) const { return !(*this == rhs); }
};

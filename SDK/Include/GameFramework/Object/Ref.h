#pragma once

#include "Core/Asset/IAsset.h"               // IAsset (is_base_of, dynamic_cast)
#include "GameFramework/Reflection/ReflectionTypes.h" // ERefCategory, RefBase (단일 정의)
#include "GameFramework/Scripting/GameScript.h" // CGameScript (is_base_of, dynamic_cast)
#include "Utillity/File/FilePath.h"           // File::Guid

#include <cstring>
#include <type_traits>
#include <typeindex>
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
//      그 외(POD 컴포넌트, GameObject 포함) → Component
// ─────────────────────────────────────────────────────────────────────────────

// ERefCategory / RefBase 는 GameFramework/Reflection/ReflectionTypes.h 에 단일 정의.

namespace RefDetail
{
	// Ref.cpp 에서 정의 — 무거운 헤더(Scene/Core/AssetManager)를 헤더로 끌어오지 않는다.
	// 경계엔 POD(const char*) 만 넘긴다 — File::Guid 는 Ref.cpp 내부에서만 구성.

	// InstanceGuid → 활성 씬의 엔티티에서 type_index 로 컴포넌트 주소(void*) 해석.
	void* ResolveComponent(const char* instanceGuid, std::type_index componentType);

	// InstanceGuid → 활성 씬 엔티티의 ScriptComponent.Instance(CGameScript*).
	CGameScript* ResolveScript(const char* instanceGuid);

	// AssetGuid → 활성 에셋 매니저에서 로드/조회한 IAsset*.
	IAsset* ResolveAsset(const char* assetGuid);
}

template<typename T>
class Ref : public RefBase
{
public:
	static constexpr ERefCategory Category =
		std::is_base_of_v<IAsset, T>      ? ERefCategory::Asset  :
		std::is_base_of_v<CGameScript, T> ? ERefCategory::Script :
		                                    ERefCategory::Component;

	Ref() = default;
	explicit Ref(const File::Guid& guid) { SetGuid(guid); }

	// ── 식별자 접근 ────────────────────────────────────────────────────────────
	// IsNull()/Clear()/GuidText()/SetGuidText() 는 RefBase 제공.
	File::Guid GetGuid() const { return File::Guid(Guid); }
	void       SetGuid(const File::Guid& guid) { SetGuidText(guid.generic_string().c_str()); }

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
			return dynamic_cast<T*>(RefDetail::ResolveScript(Guid));
		}
		else
		{
			// 컴포넌트(및 GameObject) — 풀에서 받은 void* 는 실제로 T* 이다.
			return static_cast<T*>(RefDetail::ResolveComponent(Guid, std::type_index(typeid(T))));
		}
	}

	// ── 편의 연산자 ────────────────────────────────────────────────────────────
	explicit operator bool() const { return nullptr != Get(); }
	T*        operator->() const { return Get(); }
	T&        operator*()  const { return *Get(); }

	bool operator==(const Ref& rhs) const { return 0 == std::strcmp(Guid, rhs.Guid); }
	bool operator!=(const Ref& rhs) const { return 0 != std::strcmp(Guid, rhs.Guid); }
};

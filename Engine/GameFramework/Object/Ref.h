#pragma once

#include "Core/Asset/IAsset.h"               // IAsset (is_base_of, GetAssetType)
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
//    덕분에 캔버스 저장/로드, 컴포넌트 재배치(sparse-set 재할당), DLL 핫리로드를
//    넘어서도 참조가 끊기지 않는다.
//
//  - 저장부는 RefBase 의 고정 길이 char 버퍼(POD)다. File::Guid(std::filesystem::path)
//    같은 힙 포인터 보유 객체를 게임 스크립트 인스턴스에 두면 호스트↔게임 DLL ABI
//    불일치로 값이 깨지므로, guid 는 POD 버퍼로만 저장하고 해석 시점에만 File::Guid 로 변환.
//
//  - Get() 은 매 호출 시 활성 캔버스/에셋 매니저에서 다시 해석한다(캐시 없음).
//
//  카테고리는 T 로부터 컴파일타임에 결정된다:
//      IAsset 파생      → Asset
//      CGameScript 파생 → Script
//      CGameCanvas       → Canvas (guid 대신 캔버스 이름 저장 — CanvasManager 의 유일 키)
//      그 외(POD 컴포넌트, GameObject 포함) → Component
// ─────────────────────────────────────────────────────────────────────────────

// ERefCategory / RefBase 는 GameFramework/Reflection/ReflectionTypes.h 에 단일 정의.

// Canvas 카테고리는 is_same 으로만 판별하므로 전방 선언으로 충분하다.
// (단, Ref<CGameCanvas> 을 실제 인스턴스화하는 TU 는 Canvas.h 를 포함해야 한다 —
//  나머지 트레이트 평가에 완전형이 필요.)
class CGameCanvas;

namespace RefDetail
{
	// Ref.cpp 에서 정의 — 무거운 헤더(Canvas/Core/AssetManager)를 헤더로 끌어오지 않는다.
	// 경계엔 POD(const char*) 만 넘긴다 — File::Guid 는 Ref.cpp 내부에서만 구성.

	// (오브젝트 guid + 컴포넌트 guid) → 활성 캔버스의 그 오브젝트에서 컴포넌트 guid 로 특정
	// 컴포넌트를 찾고, 타입이 맞으면 주소(void*)를 반환. 같은 타입이 여럿이어도 1개 지목.
	// 컴포넌트 guid 가 비어 있으면(구 데이터) 타입 첫 매치로 폴백.
	void* ResolveComponent(const char* objectGuid, const char* componentGuid, TypeId componentTypeId);

	// InstanceGuid → 활성 캔버스의 오브젝트(CGameObject*) 자체.
	CGameObject* ResolveObject(const char* instanceGuid);

	// (오브젝트 guid + 컴포넌트 guid) → 그 오브젝트의 특정 CGameScript.
	// 컴포넌트 guid 가 비어 있으면 첫 CGameScript 로 폴백.
	CGameScript* ResolveScript(const char* objectGuid, const char* componentGuid, TypeId scriptTypeId);

	// AssetGuid → 활성 에셋 매니저에서 로드/조회한 IAsset*.
	IAsset* ResolveAsset(const char* assetGuid);

	// 캔버스 이름 → CanvasManager 의 로드된 캔버스(CGameCanvas*). 파괴/미로드면 nullptr.
	CGameCanvas* ResolveCanvas(const char* canvasName);
}

template<typename T>
class Ref : public RefBase
{
public:
	// Ref<T> 는 5가지 카테고리 중 하나여야 한다. 그 외(예: Ref<Transform2D>, Ref<int>)는
	// 해석 경로가 없어 항상 null 로 조용히 실패하므로 컴파일 타임에 막는다.
	static_assert(
		std::is_same_v<CGameCanvas, T>      ||
		std::is_base_of_v<IAsset, T>       ||
		std::is_base_of_v<CGameScript, T>  ||
		std::is_base_of_v<CGameObject, T>  ||
		std::is_base_of_v<CComponent, T>,
		"Ref<T>: T must be CGameCanvas or derive from IAsset, CGameScript, CGameObject, or CComponent.");

	// 에셋 참조는 T 가 자기 자산 타입을 컴파일타임에 알려줘야 한다 — Get() 이 RTTI 대신
	// 그 값으로 타입을 확인한다. 짝이 없는 클래스(여러 자산 타입을 겸하는 CFileAsset 등)를
	// static_cast 로 내려보내면 엉뚱한 자산을 그 타입으로 읽게 되므로 여기서 막는다.
	// 그런 참조가 필요하면 자산 타입 하나에 대응하는 전용 클래스를 만들 것(예: CCanvasAsset).
	static_assert(
		false == std::is_base_of_v<IAsset, T> || requires { T::StaticAssetType(); },
		"Ref<T>: asset T must expose `static constexpr EAssetType StaticAssetType()`.");

	static constexpr ERefCategory Category =
		std::is_same_v<CGameCanvas, T>     ? ERefCategory::Canvas  :
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
			// 타입 판정은 **자산 타입 enum** 으로 한다 — RTTI 를 쓰지 않는다.
			// Get() 은 스크립트가 매 프레임 부를 수 있는 자리라 dynamic_cast 는 곧 병목이고,
			// 애초에 물어볼 필요도 없다: "이 자산 타입은 이 클래스"는 로더 등록이 지키는
			// 계약이고 StaticAssetType() 이 그 짝을 컴파일타임에 들고 있다.
			// (오브젝트/스크립트/컴포넌트 분기가 TypeId 로 검증하고 static_cast 하는 것과 같은 결.)
			IAsset* asset = RefDetail::ResolveAsset(Guid);
			if (nullptr == asset || T::StaticAssetType() != asset->GetAssetType())
			{
				return nullptr;
			}
			return static_cast<T*>(asset);
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
		else if constexpr (ERefCategory::Canvas == Category)
		{
			// 캔버스 — guid 가 아니라 캔버스 이름으로 CanvasManager 에서 해석.
			return static_cast<T*>(RefDetail::ResolveCanvas(Guid));
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
	// 참조가 "설정되어 있는가"(guid 보유)만 본다 — 대상을 해석/로드하지 않는다.
	//  · 이전엔 Get() 을 불러 매 검사마다 File::Guid(=fs::path) 힙 생성 + 재해석을 했고,
	//    Asset 카테고리는 존재 확인만 해도 LoadAsset(디스크 I/O)을 트리거하는 부작용이 있었다.
	//  · 주의: 대상이 살아있음을 보장하지 않는다("설정됨" ≠ "해석 가능"). 안전한 역참조는
	//    `if (T* p = ref.Get()) p->...` 관용구를 쓴다(operator-> 는 대상이 없으면 nullptr).
	explicit operator bool() const { return false == IsNull(); }
	T*        operator->() const { return Get(); }
	T&        operator*()  const { return *Get(); }

	bool operator==(const Ref& rhs) const
	{
		return 0 == std::strcmp(Guid, rhs.Guid) && 0 == std::strcmp(ComponentGuid, rhs.ComponentGuid);
	}
	bool operator!=(const Ref& rhs) const { return !(*this == rhs); }
};

#pragma once

#include "Utillity/Pointer/SafePtr.h"

class CGameObject;

// ─────────────────────────────────────────────────────────────────────────────
//  CComponent — 다형성 컴포넌트 베이스.
//
//  · GameObject 에 부착되며, 타입별 TObjectPool 에 거주한다(메모리 소유=풀).
//    논리적 소유는 GameObject 가 가진다(파괴 시 GameObject 가 풀에서 해제).
//  · 외부(스크립트/에디터)는 SafeFromThis() 로 얻은 SafePtr<CComponent> 로 참조한다.
//  · GetTypeName() 은 직렬화/인스펙터의 타입 키다(리플렉션 레지스트리 조회). 런타임
//    타입 해석(Ref<T>/GetComponent<T>)은 RTTI(dynamic_cast)로 처리한다.
//  · 라이프사이클 훅은 기본 no-op. 시스템/스크립트가 구동한다.
//
//  ⚠ DLL 경계: 엔진 측 빌트인 컴포넌트만 이 베이스를 직접 상속한다. 게임 스크립트
//     인스턴스(CGameScript)는 이 베이스로 병합하지 않는다(별도 DLL allocator 소유).
// ─────────────────────────────────────────────────────────────────────────────
class CComponent : public EnableSafeFromThis<CComponent>
{
public:
	virtual ~CComponent() = default;

	bool                 IsEnabled = true;
	SafePtr<CGameObject> Owner;

	// 직렬화/인스펙터 타입 키 (리플렉션 레지스트리에 등록된 이름과 일치해야 함).
	virtual const char* GetTypeName() const = 0;

	// 라이프사이클 (기본 no-op).
	virtual void OnCreate() {}
	virtual void OnStart() {}
	virtual void OnUpdate(float /*deltaSeconds*/) {}
	virtual void OnFixedUpdate(float /*fixedDeltaSeconds*/) {}
	virtual void OnDestroy() {}

	CGameObject* GetOwner() const { return Owner.TryGet(); }
};

// 빌트인 컴포넌트 클래스 본문에 한 번 기재 — GetTypeName 을 자동 구현한다.
#define JBRO_COMPONENT(NAME)                                       \
public:                                                            \
	const char* GetTypeName() const override { return #NAME; }

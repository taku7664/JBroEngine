#pragma once

#include "GameFramework/Object/GameInstance.h"
#include "Utillity/File/FilePath.h"
#include "Utillity/Pointer/SafePtr.h"

class CGameObject;
class CGameScene;
class CReflectionRegistry;

class ComponentConstructionToken final
{
public:
	ComponentConstructionToken(const ComponentConstructionToken&) = default;
	ComponentConstructionToken& operator=(const ComponentConstructionToken&) = default;

private:
	ComponentConstructionToken() = default;

	friend class CGameScene;
	friend class CReflectionRegistry;
};

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
//  · 게임 스크립트도 이 베이스를 상속한다. 메모리는 씬의 타입 소거 스크립트 풀에
//    거주하지만, GameObject 에서는 다른 컴포넌트와 동일한 SafePtr 로 참조한다.
// ─────────────────────────────────────────────────────────────────────────────
class CComponent : public GameInstance, public EnableSafeFromThis<CComponent>
{
public:
	virtual ~CComponent() = default;

	bool IsEnabled() const { return m_isEnabled; }
	void SetEnabled(bool enabled) { m_isEnabled = enabled; }
	const SafePtr<CGameObject>& GetOwner() const { return m_owner; }

	// InstanceGuid 는 GameInstance 베이스가 보유한다. Ref<T> 가 (오브젝트 guid + 컴포넌트
	// guid) 쌍으로 특정 1개를 지목하므로, 같은 타입 컴포넌트가 한 오브젝트에 여러 개
	// 있어도 구분된다. CGameScene::AddComponent 가 부여하고 직렬화가 보존한다.

	// 직렬화/인스펙터 타입 키 (리플렉션 레지스트리에 등록된 이름과 일치해야 함).
	virtual const char* GetTypeName() const = 0;

	// 라이프사이클 (기본 no-op). 엔진 컴포넌트는 System 이 구동하므로 Update 류 훅이 없다.
	// 부착/탈착 시점에만 컴포넌트 자신이 처리할 동작(물리월드 등록/해제 등)을 위해 둘만 둔다.
	// CGameScene::AddComponent → OnCreate, CGameScene::DestroyComponent → OnDestroy.

protected:
	CComponent(ComponentConstructionToken, const SafePtr<CGameObject>& owner)
		: m_owner(owner)
	{
	}

	virtual void OnCreate() {}
	virtual void OnDestroy() {}

private:
	friend class CGameScene;

	bool m_isEnabled = true;
	SafePtr<CGameObject> m_owner;
};

// 빌트인 컴포넌트 클래스 본문에 한 번 기재 — GetTypeName 을 자동 구현한다.
#define JBRO_COMPONENT(NAME)                                       \
public:                                                            \
	NAME(ComponentConstructionToken token,                           \
		const SafePtr<CGameObject>& owner)                            \
		: CComponent(token, owner) {}                                 \
	const char* GetTypeName() const override { return #NAME; }

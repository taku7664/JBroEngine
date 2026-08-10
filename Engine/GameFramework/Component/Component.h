#pragma once

#include "GameFramework/Object/GameInstance.h"
#include "GameFramework/Reflection/ReflectionTypes.h"
#include "Utillity/File/FilePath.h"
#include "Utillity/Pointer/SafePtr.h"

class CGameObject;
class CGameCanvas;
class CReflectionRegistry;
class CRenderer2DComponent;

class ComponentConstructionToken final
{
public:
	ComponentConstructionToken(const ComponentConstructionToken&) = default;
	ComponentConstructionToken& operator=(const ComponentConstructionToken&) = default;

private:
	explicit ComponentConstructionToken(TypeId typeId)
		: m_typeId(typeId)
	{
	}

	TypeId m_typeId = INVALID_TYPE_ID;

	friend class CComponent;
	friend class CGameCanvas;
	friend class CReflectionRegistry;
};

// ─────────────────────────────────────────────────────────────────────────────
//  CComponent — 다형성 컴포넌트 베이스.
//
//  · GameObject 에 부착되며, 타입별 TObjectPool 에 거주한다(메모리 소유=풀).
//    논리적 소유는 GameObject 가 가진다(파괴 시 GameObject 가 풀에서 해제).
//  · 외부(스크립트/에디터)는 SafeFromThis() 로 얻은 SafePtr<CComponent> 로 참조한다.
//  · GetTypeName() 은 직렬화/인스펙터의 타입 키다(리플렉션 레지스트리 조회).
//    일반 런타임 타입 해석은 생성 시 고정한 TypeId로 처리하고 기반 타입 조회만 RTTI를 쓴다.
//  · 컴포넌트 자체 라이프사이클은 없다. 각 시스템이 필요한 시점에 컴포넌트를 처리한다.
//
//  · 게임 스크립트도 이 베이스를 상속한다. 메모리는 캔버스의 타입 소거 스크립트 풀에
//    거주하지만, GameObject 에서는 다른 컴포넌트와 동일한 SafePtr 로 참조한다.
// ─────────────────────────────────────────────────────────────────────────────
class CComponent : public GameInstance, public EnableSafeFromThis<CComponent>
{
public:
	virtual ~CComponent() = default;

	bool IsEnabled() const { return m_isEnabled; }
	void SetEnabled(bool enabled) { m_isEnabled = enabled; }
	const SafePtr<CGameObject>& GetOwner() const { return m_owner; }
	TypeId GetTypeId() const { return m_typeId; }

	// InstanceGuid 는 GameInstance 베이스가 보유한다. Ref<T> 가 (오브젝트 guid + 컴포넌트
	// guid) 쌍으로 특정 1개를 지목하므로, 같은 타입 컴포넌트가 한 오브젝트에 여러 개
	// 있어도 구분된다. CGameCanvas::AddComponent 가 부여하고 직렬화가 보존한다.

	// 직렬화/인스펙터 타입 키 (리플렉션 레지스트리에 등록된 이름과 일치해야 함).
	virtual const char* GetTypeName() const = 0;

	// 렌더러 식별 — RTTI(dynamic_cast) 없이 "이 컴포넌트가 2D 렌더러인가"를 묻는다.
	// CRenderer2DComponent 가 this 를 반환하도록 재정의한다. 가상 호출 1회로 끝나므로
	// 매 프레임 경로(버튼 히트테스트 등)에서 써도 된다.
	// 선례: IRenderer::AsForward2DRenderer().
	virtual CRenderer2DComponent* AsRenderer2DComponent() { return nullptr; }
	const CRenderer2DComponent* AsRenderer2DComponent() const
	{
		return const_cast<CComponent*>(this)->AsRenderer2DComponent();
	}

protected:
	CComponent(ComponentConstructionToken token, const SafePtr<CGameObject>& owner)
		: m_owner(owner)
		, m_typeId(token.m_typeId)
	{
	}

private:
	friend class CGameCanvas;
	friend class CCanvasRuntimeAccess;

	bool m_isEnabled = true;
	SafePtr<CGameObject> m_owner;
	TypeId m_typeId = INVALID_TYPE_ID;
};

// 빌트인 컴포넌트 클래스 본문에 한 번 기재 — GetTypeName 을 자동 구현한다.
// BASE 는 직접 상속하는 컴포넌트 베이스(생성자 위임 대상)다.
#define JBRO_COMPONENT_BASE(NAME, BASE)                            \
public:                                                            \
	static constexpr const char* StaticTypeName() { return #NAME; }   \
	NAME(ComponentConstructionToken token,                           \
		const SafePtr<CGameObject>& owner)                            \
		: BASE(token, owner) {}                                       \
	const char* GetTypeName() const override { return #NAME; }

// CComponent 를 직접 상속하는 컴포넌트용 단축형.
#define JBRO_COMPONENT(NAME) JBRO_COMPONENT_BASE(NAME, CComponent)

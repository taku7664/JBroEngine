#pragma once

#include "GameFramework/Component/Component.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Physics2D/Collision2D.h"
#include "Utillity/Math/SizeT.h"
#include "Utillity/Pointer/SafePtr.h"

class CGameScene;
class CScriptSystem;
class CPhysics2DSystem;

// 게임 스크립트는 GameObject 의 순서 있는 컴포넌트 목록에 직접 부착되는 CComponent 다.
// 라이프사이클은 ScriptSystem 이 오브젝트의 컴포넌트 표시 순서대로 호출한다.
class CGameScript : public CComponent
{
public:
	virtual ~CGameScript() = default;

	// 생명주기 상태를 갖는 인스턴스 — 복사/이동하면 유령 인스턴스가 생기므로 금지.
	CGameScript(const CGameScript&) = delete;
	CGameScript& operator=(const CGameScript&) = delete;
	CGameScript(CGameScript&&) = delete;
	CGameScript& operator=(CGameScript&&) = delete;

public:
	// ── 사용자 API ────────────────────────────────────────────────────────────
	// 전부 SafePtr 를 돌려준다 — 멤버로 저장해도 안전(대상 파괴 시 IsValid()==false).
	SafePtr<CGameScene> GetScene() const;
	const char* GetTypeName() const override { return m_typeName; }

	// 부착된 오브젝트의 T 컴포넌트(첫 매치) SafePtr. 없으면 빈 SafePtr.
	template<typename T>
	SafePtr<T> GetComponent() const
	{
		CGameObject* object = GetOwner().TryGet();
		if (nullptr == object)
		{
			return SafePtr<T>();
		}

		T* component = object->GetComponent<T>();
		if (nullptr == component)
		{
			return SafePtr<T>();
		}
		// 컴포넌트도 EnableSafeFromThis<CComponent> — SafeFromThis 로 안전참조를 얻어 T 로 다운캐스트.
		return StaticSafePtrCast<T>(component->SafeFromThis());
	}

public:
	CGameScript(
		ComponentConstructionToken token,
		const SafePtr<CGameObject>& owner)
		: CComponent(token, owner)
	{
	}

protected:
	virtual void OnCreate() {}
	virtual void OnStart() {}
	virtual void OnUpdate() {}
	virtual void OnFixedUpdate() {}
	virtual void OnDestroy() {}

	// 윈도우 이벤트 훅(스크립트가 override). 인자는 Utillity 타입만 사용.
	virtual void OnApplicationFocusGained() {}
	virtual void OnApplicationFocusLost() {}
	virtual void OnSurfaceResized(const Size<int>& /*clientSize*/) {}

	// 물리 충돌 훅(스크립트가 override). 오브젝트당 콜라이더가 접촉을 시작/유지/종료할 때
	// 호출된다. 같은 충돌은 양쪽 오브젝트의 스크립트에 각자 관점(Collision2D)으로 전달된다.
	virtual void OnCollisionEnter(const Collision2D& /*collision*/) {}
	virtual void OnCollisionStay(const Collision2D& /*collision*/) {}
	virtual void OnCollisionExit(const Collision2D& /*collision*/) {}

	// 트리거 훅(스크립트가 override). 한쪽이라도 IsTrigger 콜라이더인 접촉에서 호출된다.
	virtual void OnTriggerEnter(const Collision2D& /*collision*/) {}
	virtual void OnTriggerStay(const Collision2D& /*collision*/) {}
	virtual void OnTriggerExit(const Collision2D& /*collision*/) {}

private:
	// ── 호스트 전용 진입점 ────────────────────────────────────────────────────
	// 사용자 스크립트가 직접 호출하면 생명주기 플래그가 깨지므로 private + friend.
	friend class CScriptSystem;    // Bind / Start / Update / FixedUpdate
	friend class CGameScene;       // 윈도우 이벤트 디스패치
	friend class CPhysics2DSystem; // 충돌/트리거 디스패치
	friend class CReflectionRegistry;

	void Bind(CGameScene& scene, const char* typeName);
	void Create();
	void Start();
	void Update();
	void FixedUpdate();
	void Destroy();
	bool IsStarted() const;

	// 호스트가 윈도우 이벤트를 받아 호출하는 디스패치 진입점(시작된 인스턴스에만 전달).
	void ApplicationFocusGained();
	void ApplicationFocusLost();
	void SurfaceResized(const Size<int>& clientSize);

	// Physics2DSystem 이 접촉 판정 후 호출하는 디스패치 진입점(시작된 인스턴스에만 전달).
	// 트리거(한쪽이라도 IsTrigger)는 Trigger* 로, 그 외는 Collision* 로 전달된다.
	void CollisionEnter(const Collision2D& collision);
	void CollisionStay(const Collision2D& collision);
	void CollisionExit(const Collision2D& collision);
	void TriggerEnter(const Collision2D& collision);
	void TriggerStay(const Collision2D& collision);
	void TriggerExit(const Collision2D& collision);

private:
	SafePtr<CGameScene> m_scene;
	char m_typeName[128] = "CGameScript";
	bool m_isCreated = false;
	bool m_isStarted = false;
};

// 사용자 대면 별칭 — 스크립트는 접두사 없는 이름으로 쓴다(예: class Foo : public GameScript).
// 엔진 내부 코드는 CGameScript 를 계속 쓴다(반환형도 원본 타입으로 표기됨).
using GameScript = CGameScript;

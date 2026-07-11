#pragma once

#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Physics2D/Collision2D.h"
#include "Utillity/Math/SizeT.h"
#include "Utillity/Pointer/SafePtr.h"

class CGameScene;
class CScriptSystem;
class CPhysics2DSystem;
class ScriptComponent;

// 게임 스크립트 베이스. DLL 에서 파생되며, 호스트가 ScriptComponent 를 통해 인스턴스를
// 생성/구동한다. 부착 씬/오브젝트는 SafePtr 로 들고 있어(호스트 소유) 파괴 후에도 안전 —
// 사용자가 getter 반환값을 멤버로 저장해도 IsValid() 로 댕글링을 막는다. 매 접근이 guid
// 재조회가 아니라 O(1) 포인터 역참조다(Ref 는 씬 파일에 저장되는 디자이너 참조 전용).
class CGameScript
{
public:
	CGameScript() = default;
	virtual ~CGameScript() = default;

	// 생명주기 상태를 갖는 인스턴스 — 복사/이동하면 유령 인스턴스가 생기므로 금지.
	CGameScript(const CGameScript&) = delete;
	CGameScript& operator=(const CGameScript&) = delete;
	CGameScript(CGameScript&&) = delete;
	CGameScript& operator=(CGameScript&&) = delete;

public:
	// ── 사용자 API ────────────────────────────────────────────────────────────
	// 전부 SafePtr 를 돌려준다 — 멤버로 저장해도 안전(대상 파괴 시 IsValid()==false).
	SafePtr<CGameScene>  GetScene() const;
	SafePtr<CGameObject> GetGameObject() const;

	// 부착된 오브젝트의 T 컴포넌트(첫 매치) SafePtr. 없으면 빈 SafePtr.
	template<typename T>
	SafePtr<T> GetComponent() const
	{
		CGameObject* object = m_owner.TryGet();
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

	bool IsStarted() const;
	bool IsBound() const;

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
	friend class ScriptComponent;  // ResetInstance → Destroy(OnDestroy 발화)

	void Bind(CGameScene& scene, CGameObject& object);
	void Create();
	void Start();
	void Update();
	void FixedUpdate();
	void Destroy();

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
	SafePtr<CGameScene>  m_scene; // 부착 씬(호스트 소유) — Bind 에서 SafeFromThis 로 설정
	SafePtr<CGameObject> m_owner; // 부착 오브젝트(호스트 소유)
	bool m_isCreated = false;
	bool m_isStarted = false;
	bool m_isBound   = false;
};

// 사용자 대면 별칭 — 스크립트는 접두사 없는 이름으로 쓴다(예: class Foo : public GameScript).
// 엔진 내부 코드는 CGameScript 를 계속 쓴다(반환형도 원본 타입으로 표기됨).
using GameScript = CGameScript;

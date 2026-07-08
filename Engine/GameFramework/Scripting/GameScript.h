#pragma once

#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Physics2D/Collision2D.h"
#include "Utillity/Math/SizeT.h"
#include "Utillity/Pointer/SafePtr.h"

class CGameScene;

// 게임 스크립트 베이스. DLL 에서 파생되며, 호스트가 ScriptComponent 를 통해 인스턴스를
// 생성/구동한다. 부착된 오브젝트는 SafePtr 로 들고 있어(호스트 소유) 파괴 후에도 안전.
class CGameScript
{
public:
	virtual ~CGameScript() = default;

public:
	void Bind(CGameScene& scene, CGameObject& object);
	CGameScene*      GetScene() const;
	CGameObject* GetGameObject() const;

	// 부착된 오브젝트의 컴포넌트 접근 — 옛 GetScene()->GetComponent<T>(GetEntity()) 대체.
	template<typename T>
	T* GetComponent() const
	{
		CGameObject* object = GetGameObject();
		return object ? object->GetComponent<T>() : nullptr;
	}

	void Create();
	void Start();
	void Update();
	void FixedUpdate();
	void Destroy();
	bool IsStarted() const;
	bool IsBound() const;

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
	CGameScene*              m_scene = nullptr;
	SafePtr<CGameObject> m_owner;
	bool m_isCreated = false;
	bool m_isStarted = false;
	bool m_isBound   = false;
};

// 사용자 대면 별칭 — 스크립트는 접두사 없는 이름으로 쓴다(예: class Foo : public GameScript).
// 엔진 내부 코드는 CGameScript 를 계속 쓴다(반환형도 원본 타입으로 표기됨).
using GameScript = CGameScript;

#pragma once

#include "GameFramework/Object/GameObject.h"
#include "Utillity/Pointer/SafePtr.h"

class CScene;

// 게임 스크립트 베이스. DLL 에서 파생되며, 호스트가 ScriptComponent 를 통해 인스턴스를
// 생성/구동한다. 부착된 오브젝트는 SafePtr 로 들고 있어(호스트 소유) 파괴 후에도 안전.
class CGameScript
{
public:
	virtual ~CGameScript() = default;

public:
	void Bind(CScene& scene, CGameObject& object);
	CScene*      GetScene() const;
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

protected:
	virtual void OnCreate() {}
	virtual void OnStart() {}
	virtual void OnUpdate() {}
	virtual void OnFixedUpdate() {}
	virtual void OnDestroy() {}

private:
	CScene*              m_scene = nullptr;
	SafePtr<CGameObject> m_owner;
	bool m_isCreated = false;
	bool m_isStarted = false;
	bool m_isBound   = false;
};

#include "pch.h"
#include "GameScript.h"

void CGameScript::Bind(CGameScene& scene, CGameObject& object)
{
	m_scene = &scene;
	m_owner = object.SafeFromThis();
	m_isBound = true;
}

CGameScene* CGameScript::GetScene() const
{
	return m_scene;
}

CGameObject* CGameScript::GetGameObject() const
{
	return m_owner.TryGet();
}

void CGameScript::Create()
{
	if (m_isCreated)
	{
		return;
	}

	OnCreate();
	m_isCreated = true;
}

void CGameScript::Start()
{
	if (m_isStarted)
	{
		return;
	}

	if (false == m_isCreated)
	{
		Create();
	}

	OnStart();
	m_isStarted = true;
}

void CGameScript::Update()
{
	if (false == m_isStarted)
	{
		Start();
	}

	OnUpdate();
}

void CGameScript::FixedUpdate()
{
	if (false == m_isStarted)
	{
		return;
	}

	OnFixedUpdate();
}

void CGameScript::Destroy()
{
	if (m_isCreated)
	{
		OnDestroy();
	}

	m_isCreated = false;
	m_isStarted = false;
	m_isBound   = false;
	m_scene     = nullptr;
	m_owner.Reset();
}

void CGameScript::ApplicationFocusGained()
{
	if (m_isStarted)
	{
		OnApplicationFocusGained();
	}
}

void CGameScript::ApplicationFocusLost()
{
	if (m_isStarted)
	{
		OnApplicationFocusLost();
	}
}

void CGameScript::SurfaceResized(const Size<int>& clientSize)
{
	if (m_isStarted)
	{
		OnSurfaceResized(clientSize);
	}
}

void CGameScript::CollisionEnter(const Collision2D& collision)
{
	if (m_isStarted)
	{
		OnCollisionEnter(collision);
	}
}

void CGameScript::CollisionStay(const Collision2D& collision)
{
	if (m_isStarted)
	{
		OnCollisionStay(collision);
	}
}

void CGameScript::CollisionExit(const Collision2D& collision)
{
	if (m_isStarted)
	{
		OnCollisionExit(collision);
	}
}

void CGameScript::TriggerEnter(const Collision2D& collision)
{
	if (m_isStarted)
	{
		OnTriggerEnter(collision);
	}
}

void CGameScript::TriggerStay(const Collision2D& collision)
{
	if (m_isStarted)
	{
		OnTriggerStay(collision);
	}
}

void CGameScript::TriggerExit(const Collision2D& collision)
{
	if (m_isStarted)
	{
		OnTriggerExit(collision);
	}
}

bool CGameScript::IsStarted() const
{
	return m_isStarted;
}

bool CGameScript::IsBound() const
{
	return m_isBound;
}

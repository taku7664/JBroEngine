#include "pch.h"
#include "GameScript.h"

#include "GameFramework/Scene/Scene.h"

#include <algorithm>
#include <cstring>

void CGameScript::Bind(CGameScene& scene, const char* typeName)
{
	// 씬 참조와 스크립트 타입 메타만 바인딩한다. Owner는 CComponent 생성자에서 확정된다.
	m_scene = scene.SafeFromThis();
	const char* sourceName = typeName ? typeName : "CGameScript";
	const std::size_t length = std::min(std::strlen(sourceName), sizeof(m_typeName) - 1);
	std::memcpy(m_typeName, sourceName, length);
	m_typeName[length] = '\0';
}

SafePtr<CGameScene> CGameScript::GetScene() const
{
	return m_scene;
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
	m_scene.Reset();
}

void CGameScript::CanvasChanged(CGameScene& canvas)
{
	if (m_isStarted)
	{
		OnCanvasChanged(canvas);
	}
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

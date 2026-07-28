#include "pch.h"
#include "GameScript.h"

#include "GameFramework/Canvas/Canvas.h"

#include <algorithm>
#include <cstring>
#include <utility>

void CGameScript::Bind(CGameCanvas& canvas, const char* typeName)
{
	// 캔버스 참조와 스크립트 타입 메타만 바인딩한다. Owner는 CComponent 생성자에서 확정된다.
	m_canvas = canvas.SafeFromThis();
	const char* sourceName = typeName ? typeName : "CGameScript";
	const std::size_t length = std::min(std::strlen(sourceName), sizeof(m_typeName) - 1);
	std::memcpy(m_typeName, sourceName, length);
	m_typeName[length] = '\0';
}

SafePtr<CGameCanvas> CGameScript::GetCanvas() const
{
	return m_canvas;
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

	// 이 스크립트가 남긴 코루틴을 취소한다 — 이후 재개가 파괴된 인스턴스를 건드리지 못하게.
	// (일괄 파괴 경로는 DestroyScriptInstances 가 이미 CancelAll 하지만, 단건 파괴는 여기가 관문.)
	if (CGameCanvas* canvas = m_canvas.TryGet())
	{
		canvas->StopCoroutinesForOwner(this);
	}

	m_isCreated = false;
	m_isStarted = false;
	m_canvas.Reset();
}

CoroutineId CGameScript::StartCoroutine(Coroutine&& routine)
{
	CGameCanvas* canvas = m_canvas.TryGet();
	if (nullptr == canvas)
	{
		return CoroutineId{};
	}
	// owner = 이 스크립트의 안전참조. 스케줄러가 파괴/활성 판정에 쓴다.
	SafePtr<CGameScript> self = StaticSafePtrCast<CGameScript>(SafeFromThis());
	return canvas->StartCoroutine(std::move(routine), self);
}

void CGameScript::StopCoroutine(CoroutineId id)
{
	if (CGameCanvas* canvas = m_canvas.TryGet())
	{
		canvas->StopCoroutine(id);
	}
}

void CGameScript::StopAllCoroutines()
{
	if (CGameCanvas* canvas = m_canvas.TryGet())
	{
		canvas->StopCoroutinesForOwner(this);
	}
}

void CGameScript::CanvasChanged(CGameCanvas& canvas)
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

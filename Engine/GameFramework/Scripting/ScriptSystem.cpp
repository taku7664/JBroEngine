#include "pch.h"
#include "ScriptSystem.h"

#include "Core/EngineCore.h"
#include "Core/Input/InputSystem.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Canvas/Canvas.h"
#include "GameFramework/Scripting/GameScript.h"

#if defined(JBRO_EDITOR)
#include "Core/Debug/CpuProfiler.h"   // 스크립트 인스턴스별 Update 시간 계측(에디터 진단, 선택 시에만)
#include <chrono>
#endif

void CScriptSystem::OnUpdate(CGameCanvas& canvas)
{
	canvas.EnsureScriptExecutionOrder();
	// Start/Update 안에서 유저가 스폰·Ref 해석으로 캐시를 재빌드하면 이 순회가 무효화된다.
	// 가드가 재빌드를 미룬다(스폰된 스크립트는 다음 프레임 반영).
	CGameCanvas::ScriptIterationGuard iterationGuard(canvas);
	for (CGameCanvas::ScriptRuntimeState* runtime : canvas.m_scriptExecutionOrder)
	{
		if (nullptr == runtime || nullptr == runtime->Instance)
		{
			continue;
		}
		CGameScript& script = *runtime->Instance;
		CGameObject* owner = script.GetOwner().TryGet();
		const bool active = script.IsEnabled() && owner && owner->IsActiveInHierarchy();
		if (false == active)
		{
			if (runtime->InputRegistered && runtime->InputHandler && Engine.InputSystem.IsValid())
			{
				Engine.InputSystem->UnregisterHandler(runtime->InputHandler);
				runtime->InputRegistered = false;
			}
			continue;
		}

		if (false == script.IsStarted())
		{
			script.Start();
		}

		if (false == runtime->InputRegistered && runtime->InputHandler && Engine.InputSystem.IsValid())
		{
			Engine.InputSystem->RegisterHandler(runtime->InputHandler);
			runtime->InputRegistered = true;
		}
	}

#if defined(JBRO_EDITOR)
	// CPU 프로파일러가 Script 풀을 선택한 프레임에만 인스턴스별 Update 시간을 잰다(예외3 — 선택 시에만).
	// 나머지 프레임은 아래 else 경로로 계측 없이 그대로 돈다. 게임 빌드에선 이 블록이 통째로 사라진다.
	CCpuProfiler* cpuProfiler = Engine.CpuProfiler.TryGet();
	const bool captureScripts = (nullptr != cpuProfiler) && cpuProfiler->ShouldCaptureScripts();
#endif
	for (CGameCanvas::ScriptRuntimeState* runtime : canvas.m_scriptExecutionOrder)
	{
		if (nullptr == runtime || nullptr == runtime->Instance)
		{
			continue;
		}
		CGameScript& script = *runtime->Instance;
		CGameObject* owner = script.GetOwner().TryGet();
		if (script.IsEnabled() && owner && owner->IsActiveInHierarchy() && script.IsStarted())
		{
#if defined(JBRO_EDITOR)
			if (captureScripts)
			{
				const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
				script.Update();
				const double microseconds = std::chrono::duration<double, std::micro>(
					std::chrono::steady_clock::now() - begin).count();
				cpuProfiler->RecordScriptTiming(owner, microseconds);
			}
			else
#endif
			{
				script.Update();
			}
		}
	}
}

void CScriptSystem::OnFixedUpdate(CGameCanvas& canvas)
{
	canvas.EnsureScriptExecutionOrder();
	// FixedUpdate 안 스폰·Ref 해석이 이 순회를 재빌드하지 못하게 잠금.
	CGameCanvas::ScriptIterationGuard iterationGuard(canvas);
	for (CGameCanvas::ScriptRuntimeState* runtime : canvas.m_scriptExecutionOrder)
	{
		if (nullptr == runtime || nullptr == runtime->Instance)
		{
			continue;
		}
		CGameScript& script = *runtime->Instance;
		CGameObject* owner = script.GetOwner().TryGet();
		if (script.IsEnabled() && owner && owner->IsActiveInHierarchy() && script.IsStarted())
		{
			script.FixedUpdate();
		}
	}
}

#include "pch.h"
#include "ScriptSystem.h"

#include "Core/EngineCore.h"
#include "Core/Input/InputSystem.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scripting/GameScript.h"

void CScriptSystem::OnUpdate(CGameScene& scene)
{
	scene.EnsureScriptExecutionOrder();
	for (CGameScene::ScriptRuntimeState* runtime : scene.m_scriptExecutionOrder)
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

	for (CGameScene::ScriptRuntimeState* runtime : scene.m_scriptExecutionOrder)
	{
		if (nullptr == runtime || nullptr == runtime->Instance)
		{
			continue;
		}
		CGameScript& script = *runtime->Instance;
		CGameObject* owner = script.GetOwner().TryGet();
		if (script.IsEnabled() && owner && owner->IsActiveInHierarchy() && script.IsStarted())
		{
			script.Update();
		}
	}
}

void CScriptSystem::OnFixedUpdate(CGameScene& scene)
{
	scene.EnsureScriptExecutionOrder();
	for (CGameScene::ScriptRuntimeState* runtime : scene.m_scriptExecutionOrder)
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

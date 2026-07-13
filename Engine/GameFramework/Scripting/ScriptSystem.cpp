#include "pch.h"
#include "ScriptSystem.h"

#include "Core/EngineCore.h"
#include "Core/Input/InputSystem.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scripting/GameScript.h"

namespace
{
	template<typename Fn>
	void ForEachScriptInObjectOrder(CGameScene& scene, Fn&& fn)
	{
		scene.ForEachObjectInHierarchyOrder([&](CGameObject& object)
		{
			for (const SafePtr<CComponent>& component : object.GetComponents())
			{
				if (CGameScript* script = dynamic_cast<CGameScript*>(component.TryGet()))
				{
					fn(*script);
				}
			}
		});
	}
}

void CScriptSystem::OnUpdate(CGameScene& scene)
{
	ForEachScriptInObjectOrder(scene, [&scene](CGameScript& script)
	{
		CGameObject* owner = script.GetOwner().TryGet();
		CGameScene::ScriptRuntimeState* runtime = scene.FindScriptRuntime(&script);
		const bool active = script.IsEnabled() && owner && owner->IsActiveInHierarchy();
		if (false == active)
		{
			if (runtime && runtime->InputRegistered && runtime->InputHandler && Engine.InputSystem.IsValid())
			{
				Engine.InputSystem->UnregisterHandler(runtime->InputHandler);
				runtime->InputRegistered = false;
			}
			return;
		}

		if (false == script.IsStarted())
		{
			script.Start();
		}

		if (runtime && false == runtime->InputRegistered && runtime->InputHandler && Engine.InputSystem.IsValid())
		{
			Engine.InputSystem->RegisterHandler(runtime->InputHandler);
			runtime->InputRegistered = true;
		}
	});

	ForEachScriptInObjectOrder(scene, [](CGameScript& script)
	{
		CGameObject* owner = script.GetOwner().TryGet();
		if (script.IsEnabled() && owner && owner->IsActiveInHierarchy() && script.IsStarted())
		{
			script.Update();
		}
	});
}

void CScriptSystem::OnFixedUpdate(CGameScene& scene)
{
	ForEachScriptInObjectOrder(scene, [](CGameScript& script)
	{
		CGameObject* owner = script.GetOwner().TryGet();
		if (script.IsEnabled() && owner && owner->IsActiveInHierarchy() && script.IsStarted())
		{
			script.FixedUpdate();
		}
	});
}

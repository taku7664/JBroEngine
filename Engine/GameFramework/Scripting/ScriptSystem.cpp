#include "pch.h"
#include "ScriptSystem.h"

#include "GameFramework/Component/ScriptComponent.h"
#include "GameFramework/Scene/Scene.h"

void CScriptSystem::OnUpdate(CScene& scene)
{
	scene.ForEach<ScriptComponent>(
		[&scene](EntityId entity, ScriptComponent& script)
		{
			if (false == script.IsEnabled || !script.Instance)
			{
				return;
			}

			if (false == script.Instance->IsBound())
			{
				script.Instance->Bind(scene, entity);
			}
			script.Instance->Update();
		});
}

void CScriptSystem::OnFixedUpdate(CScene& scene)
{
	// Only runs on scripts that have already been started (via Update).
	// Scripts not yet started are skipped — they will catch up in Update.
	scene.ForEach<ScriptComponent>(
		[](EntityId entity, ScriptComponent& script)
		{
			if (false == script.IsEnabled || !script.Instance)
			{
				return;
			}

			if (script.Instance->IsStarted())
			{
				script.Instance->FixedUpdate();
			}
		});
}

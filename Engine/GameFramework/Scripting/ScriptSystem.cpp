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

			script.Instance->Bind(scene, entity);
			script.Instance->Update();
		});
}

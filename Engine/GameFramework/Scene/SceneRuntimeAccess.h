#pragma once

#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scripting/GameScript.h"

// 호스트 런타임과 에디터만 사용하는 Scene 내부 접근점입니다.
// 게임 스크립트 SDK에는 스테이징하지 않습니다.
class CSceneRuntimeAccess final
{
public:
	template<typename TSystem, typename... Args>
	static TSystem* AddSystem(CGameScene& scene, Args&&... args)
	{
		return scene.AddSystem<TSystem>(std::forward<Args>(args)...);
	}

	template<typename TSystem>
	static TSystem* FindSystem(CGameScene& scene)
	{
		return scene.FindSystem<TSystem>();
	}

	template<typename TSystem>
	static const TSystem* FindSystem(const CGameScene& scene)
	{
		return scene.FindSystem<TSystem>();
	}

	static CGameScript* AddScript(
		CGameScene& scene,
		CGameObject& object,
		TypeId scriptTypeId,
		const CReflectionRegistry& registry)
	{
		return scene.AddScript(object, scriptTypeId, registry);
	}

	static void DestroyComponent(CGameScene& scene, CComponent* component)
	{
		scene.DestroyComponent(component);
	}

	static CComponent* FindComponentByGuid(CGameObject& object, const File::Guid& componentGuid)
	{
		return object.FindComponentByGuid(componentGuid);
	}

	static void* FindComponentByGuidAndType(
		CGameObject& object,
		const File::Guid& componentGuid,
		TypeId typeId)
	{
		return object.FindComponentRawByGuid(componentGuid, typeId);
	}

	static bool MoveComponent(CGameObject& object, const File::Guid& componentGuid, std::size_t newIndex)
	{
		return object.MoveComponent(componentGuid, newIndex);
	}

	static void SetObjectInstanceGuid(CGameScene& scene, CGameObject& object, const File::Guid& guid)
	{
		scene.SetObjectInstanceGuid(object, guid);
	}

	static void SetComponentInstanceGuid(CComponent& component, const File::Guid& guid)
	{
		component.SetInstanceGuid(guid);
	}

	static void SetCreationOrder(CGameObject& object, std::uint64_t creationOrder)
	{
		object.m_creationOrder = creationOrder;
		if (CGameScene* scene = object.GetScene())
		{
			scene->MarkScriptExecutionOrderDirty();
		}
	}

	static std::vector<CGameScene::ScriptMemoryPoolStats> GetScriptMemoryPoolStats(const CGameScene& scene)
	{
		return scene.GetScriptMemoryPoolStats();
	}

	static void ReserveScriptMemory(
		CGameScene& scene,
		TypeId scriptTypeId,
		std::size_t size,
		std::size_t alignment,
		std::size_t capacity)
	{
		scene.ReserveScriptMemory(scriptTypeId, size, alignment, capacity);
	}

	static void DispatchSurfaceEvent(CGameScene& scene, const SurfaceEvent& surfaceEvent)
	{
		scene.DispatchSurfaceEventToScripts(surfaceEvent);
	}

	static CGameScript* AsScript(CGameScene& scene, CComponent* component)
	{
		if (nullptr == component)
		{
			return nullptr;
		}
		const auto it = scene.m_scriptRuntimeByComponent.find(component);
		return it == scene.m_scriptRuntimeByComponent.end() || nullptr == it->second
			? nullptr
			: it->second->Instance;
	}

	static const CGameScript* AsScript(const CGameScene& scene, const CComponent* component)
	{
		if (nullptr == component)
		{
			return nullptr;
		}
		const auto it = scene.m_scriptRuntimeByComponent.find(component);
		return it == scene.m_scriptRuntimeByComponent.end() || nullptr == it->second
			? nullptr
			: it->second->Instance;
	}

	static CGameScript* FindScript(
		CGameScene& scene,
		CGameObject& object,
		const File::Guid& componentGuid,
		TypeId expectedType = INVALID_TYPE_ID)
	{
		scene.EnsureScriptExecutionOrder();
		const auto rangeIt = scene.m_scriptRangesByObject.find(&object);
		if (rangeIt == scene.m_scriptRangesByObject.end())
		{
			return nullptr;
		}

		const CGameScene::ScriptObjectRange range = rangeIt->second;
		const std::size_t end = range.Begin + range.Count;
		for (std::size_t index = range.Begin; index < end; ++index)
		{
			CGameScene::ScriptRuntimeState* runtime = scene.m_scriptExecutionOrder[index];
			CGameScript* script = runtime ? runtime->Instance : nullptr;
			if (nullptr == script)
			{
				continue;
			}
			if (false == componentGuid.IsNull() && script->GetInstanceGuid() != componentGuid)
			{
				continue;
			}
			if (INVALID_TYPE_ID != expectedType && script->GetTypeId() != expectedType)
			{
				continue;
			}
			return script;
		}
		return nullptr;
	}

	template<typename Fn>
	static void ForEachScriptInExecutionOrder(CGameScene& scene, Fn&& fn)
	{
		scene.EnsureScriptExecutionOrder();
		for (CGameScene::ScriptRuntimeState* runtime : scene.m_scriptExecutionOrder)
		{
			if (runtime && runtime->Instance)
			{
				fn(*runtime->Instance);
			}
		}
	}
};

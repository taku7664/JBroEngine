#pragma once

#include "GameFramework/Canvas/Canvas.h"
#include "GameFramework/Scripting/GameScript.h"

// 호스트 런타임과 에디터만 사용하는 Canvas 내부 접근점입니다.
// 게임 스크립트 SDK에는 스테이징하지 않습니다.
class CCanvasRuntimeAccess final
{
public:
	template<typename TSystem, typename... Args>
	static TSystem* AddSystem(CGameCanvas& canvas, Args&&... args)
	{
		return canvas.AddSystem<TSystem>(std::forward<Args>(args)...);
	}

	template<typename TSystem>
	static TSystem* FindSystem(CGameCanvas& canvas)
	{
		return canvas.FindSystem<TSystem>();
	}

	template<typename TSystem>
	static const TSystem* FindSystem(const CGameCanvas& canvas)
	{
		return canvas.FindSystem<TSystem>();
	}

	static CGameScript* AddScript(
		CGameCanvas& canvas,
		CGameObject& object,
		TypeId scriptTypeId,
		const CReflectionRegistry& registry)
	{
		return canvas.AddScript(object, scriptTypeId, registry);
	}

	static void DestroyComponent(CGameCanvas& canvas, CComponent* component)
	{
		canvas.DestroyComponent(component);
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

	static void SetObjectInstanceGuid(CGameCanvas& canvas, CGameObject& object, const File::Guid& guid)
	{
		canvas.SetObjectInstanceGuid(object, guid);
	}

	static void SetComponentInstanceGuid(CComponent& component, const File::Guid& guid)
	{
		component.SetInstanceGuid(guid);
	}

	// 에디터 undo/redo 전용 — 레이어를 파괴했다가 되살릴 때 원래 guid 를 되돌린다.
	// guid 가 바뀌면 뷰포트 LayerFilter·파일 레이어 승계가 대상을 잃는다.
	static void SetLayerInstanceGuid(CGameCanvas& canvas, CGameLayer& layer, const File::Guid& guid)
	{
		canvas.SetLayerInstanceGuid(layer, guid);
	}

	static void SetCreationOrder(CGameObject& object, std::uint64_t creationOrder)
	{
		object.m_creationOrder = creationOrder;
		if (CGameCanvas* canvas = object.GetCanvas())
		{
			canvas->MarkScriptExecutionOrderDirty();
		}
	}

	static std::vector<CGameCanvas::ScriptMemoryPoolStats> GetScriptMemoryPoolStats(const CGameCanvas& canvas)
	{
		return canvas.GetScriptMemoryPoolStats();
	}

	// 시스템별 Update 소요 시간(에디터 빌드에서만 채워진다). 매 프레임 읽히므로 참조로 준다.
	static const std::vector<CGameCanvas::SystemUpdateTiming>& GetSystemUpdateTimings(const CGameCanvas& canvas)
	{
		return canvas.GetSystemUpdateTimings();
	}

	static void ReserveScriptMemory(
		CGameCanvas& canvas,
		TypeId scriptTypeId,
		std::size_t size,
		std::size_t alignment,
		std::size_t capacity)
	{
		canvas.ReserveScriptMemory(scriptTypeId, size, alignment, capacity);
	}

	static void DispatchSurfaceEvent(CGameCanvas& canvas, const SurfaceEvent& surfaceEvent)
	{
		canvas.DispatchSurfaceEventToScripts(surfaceEvent);
	}

	static CGameScript* AsScript(CGameCanvas& canvas, CComponent* component)
	{
		if (nullptr == component)
		{
			return nullptr;
		}
		const auto it = canvas.m_scriptRuntimeByComponent.find(component);
		return it == canvas.m_scriptRuntimeByComponent.end() || nullptr == it->second
			? nullptr
			: it->second->Instance;
	}

	static const CGameScript* AsScript(const CGameCanvas& canvas, const CComponent* component)
	{
		if (nullptr == component)
		{
			return nullptr;
		}
		const auto it = canvas.m_scriptRuntimeByComponent.find(component);
		return it == canvas.m_scriptRuntimeByComponent.end() || nullptr == it->second
			? nullptr
			: it->second->Instance;
	}

	static CGameScript* FindScript(
		CGameCanvas& canvas,
		CGameObject& object,
		const File::Guid& componentGuid,
		TypeId expectedType = INVALID_TYPE_ID)
	{
		canvas.EnsureScriptExecutionOrder();
		const auto rangeIt = canvas.m_scriptRangesByObject.find(&object);
		if (rangeIt == canvas.m_scriptRangesByObject.end())
		{
			return nullptr;
		}

		const CGameCanvas::ScriptObjectRange range = rangeIt->second;
		const std::size_t end = range.Begin + range.Count;
		for (std::size_t index = range.Begin; index < end; ++index)
		{
			CGameCanvas::ScriptRuntimeState* runtime = canvas.m_scriptExecutionOrder[index];
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
	static void ForEachScriptInExecutionOrder(CGameCanvas& canvas, Fn&& fn)
	{
		canvas.EnsureScriptExecutionOrder();
		// fn 이 스폰·Ref 해석으로 캐시를 재빌드하지 못하게 순회 잠금.
		CGameCanvas::ScriptIterationGuard iterationGuard(canvas);
		for (CGameCanvas::ScriptRuntimeState* runtime : canvas.m_scriptExecutionOrder)
		{
			if (runtime && runtime->Instance)
			{
				fn(*runtime->Instance);
			}
		}
	}
};

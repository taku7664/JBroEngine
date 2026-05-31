#pragma once

#include "GameFramework/System/GameSystem.h"
#include "GameFramework/ECS/EntityTypes.h"
#include "GameFramework/Reflection/ReflectionTypes.h"

#include <cstdint>
#include <unordered_set>

struct ScriptComponent;

class CScriptSystem final : public CGameSystem
{
public:
	// 에디트 모드에서 인스펙터가 스크립트 프로퍼티를 표시/편집할 수 있도록 인스턴스를
	// 미리 생성한다(이미 있으면 no-op). PendingFields 를 적용하되, Bind/Start
	// (OnCreate/OnStart) 는 호출하지 않는다 — 순수 데이터 홀더로만 둔다. 플레이가
	// 시작되면 ScriptSystem 이 이 인스턴스를 그대로 Bind/Start 한다.
	static void EnsureEditTimeInstance(ScriptComponent& script);

protected:
	void OnUpdate(CScene& scene) override;
	void OnFixedUpdate(CScene& scene) override;

private:
	// 경고 로그 폭주 방지용 캐시 — (entity, typeId) 조합당 1회만 경고.
	// ScriptTypeId 가 바뀌면 다른 키가 되므로 자동으로 재시도된다.
	struct EntityTypeKey
	{
		EntityId Entity;
		TypeId   TypeId;
		bool operator==(const EntityTypeKey& rhs) const { return Entity == rhs.Entity && TypeId == rhs.TypeId; }
	};
	struct EntityTypeKeyHash
	{
		std::size_t operator()(const EntityTypeKey& k) const noexcept
		{
			return std::hash<std::uint64_t>{}(static_cast<std::uint64_t>(k.Entity))
				 ^ (std::hash<std::uint64_t>{}(static_cast<std::uint64_t>(k.TypeId)) << 1);
		}
	};
	std::unordered_set<EntityTypeKey, EntityTypeKeyHash> m_warnedFailedCreate;
};

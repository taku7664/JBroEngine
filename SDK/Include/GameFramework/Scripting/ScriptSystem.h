#pragma once

#include "GameFramework/System/GameSystem.h"
#include "GameFramework/ECS/EntityTypes.h"
#include "GameFramework/Reflection/ReflectionTypes.h"

#include <cstdint>
#include <unordered_set>

class CScriptSystem final : public CGameSystem
{
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

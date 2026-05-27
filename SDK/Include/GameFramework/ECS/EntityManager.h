#pragma once

#include "GameFramework/ECS/EntityTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class CEntityManager final
{
public:
	EntityId CreateEntity();
	bool DestroyEntity(EntityId entity);
	bool IsAlive(EntityId entity) const;

	std::size_t GetAliveCount() const;
	void Clear();

private:
	static constexpr std::uint32_t INITIAL_GENERATION = 1;

private:
	std::vector<std::uint32_t> m_generations;
	std::vector<std::uint32_t> m_freeIndices;
	std::size_t m_aliveCount = 0;
};

#pragma once

#include <cstdint>

using EntityId = std::uint64_t;

constexpr EntityId INVALID_ENTITY_ID = 0;

inline constexpr std::uint32_t ENTITY_INDEX_BITS = 32;
inline constexpr std::uint64_t ENTITY_INDEX_MASK = 0xFFFFFFFFull;

inline EntityId MakeEntityId(std::uint32_t index, std::uint32_t generation)
{
	return (static_cast<EntityId>(generation) << ENTITY_INDEX_BITS) | (static_cast<EntityId>(index) + 1ull);
}

inline std::uint32_t GetEntityIndex(EntityId entity)
{
	const std::uint32_t storedIndex = static_cast<std::uint32_t>(entity & ENTITY_INDEX_MASK);
	return 0 == storedIndex ? 0 : storedIndex - 1;
}

inline std::uint32_t GetEntityGeneration(EntityId entity)
{
	return static_cast<std::uint32_t>(entity >> ENTITY_INDEX_BITS);
}

inline bool IsValidEntityId(EntityId entity)
{
	return INVALID_ENTITY_ID != entity;
}

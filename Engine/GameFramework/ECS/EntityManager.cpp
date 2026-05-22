#include "pch.h"
#include "EntityManager.h"

EntityId CEntityManager::CreateEntity()
{
	std::uint32_t index = 0;
	if (false == m_freeIndices.empty())
	{
		index = m_freeIndices.back();
		m_freeIndices.pop_back();
	}
	else
	{
		index = static_cast<std::uint32_t>(m_generations.size());
		m_generations.push_back(INITIAL_GENERATION);
	}

	++m_aliveCount;
	return MakeEntityId(index, m_generations[index]);
}

bool CEntityManager::DestroyEntity(EntityId entity)
{
	if (false == IsAlive(entity))
	{
		return false;
	}

	const std::uint32_t index = GetEntityIndex(entity);
	++m_generations[index];
	if (0 == m_generations[index])
	{
		m_generations[index] = INITIAL_GENERATION;
	}

	m_freeIndices.push_back(index);
	--m_aliveCount;
	return true;
}

bool CEntityManager::IsAlive(EntityId entity) const
{
	if (false == IsValidEntityId(entity))
	{
		return false;
	}

	const std::uint32_t index = GetEntityIndex(entity);
	if (index >= m_generations.size())
	{
		return false;
	}

	return m_generations[index] == GetEntityGeneration(entity);
}

std::size_t CEntityManager::GetAliveCount() const
{
	return m_aliveCount;
}

void CEntityManager::Clear()
{
	m_generations.clear();
	m_freeIndices.clear();
	m_aliveCount = 0;
}


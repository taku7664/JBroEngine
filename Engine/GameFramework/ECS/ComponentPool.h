#pragma once

#include "GameFramework/ECS/EntityTypes.h"
#include "Memory/BlockPoolAllocator.h"
#include "Utillity/Framework.h"

#include <cassert>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

class IComponentPool
{
public:
	virtual ~IComponentPool() = default;

public:
	virtual void RemoveEntity(EntityId entity) = 0;
	virtual bool HasEntity(EntityId entity) const = 0;
	virtual void Clear() = 0;
	virtual std::size_t GetComponentCount() const = 0;
};

template<typename T>
// Component pools are internal World storage. They may use STL/type metadata
// because they are not part of the live-compile DLL boundary contract.
class CComponentPool final : public IComponentPool
{
public:
	explicit CComponentPool(std::size_t blocksPerChunk = DEFAULT_BLOCKS_PER_CHUNK)
		: m_allocator(sizeof(T), alignof(T), blocksPerChunk)
	{
	}

	~CComponentPool() override
	{
		Clear();
	}

	CComponentPool(const CComponentPool&) = delete;
	CComponentPool& operator=(const CComponentPool&) = delete;

public:
	template<typename... Args>
	T* Add(EntityId entity, Args&&... args)
	{
		if (T* component = Get(entity))
		{
			return component;
		}

		EnsureSparseSize(entity);

		void* memory = m_allocator.Allocate(sizeof(T), alignof(T));
		if (nullptr == memory)
		{
			return nullptr;
		}

		T* component = new(memory) T(std::forward<Args>(args)...);
		m_sparse[GetEntityIndex(entity)] = m_components.size() + 1;
		m_entities.push_back(entity);
		m_components.push_back(component);
		return component;
	}

	void RemoveEntity(EntityId entity) override
	{
		const std::size_t denseIndex = GetDenseIndex(entity);
		if (INVALID_DENSE_INDEX == denseIndex)
		{
			return;
		}

		const std::size_t lastIndex = m_components.size() - 1;
		T* removed = m_components[denseIndex];
		removed->~T();
		m_allocator.Free(removed, sizeof(T));

		if (denseIndex != lastIndex)
		{
			m_components[denseIndex] = m_components[lastIndex];
			m_entities[denseIndex] = m_entities[lastIndex];
			m_sparse[GetEntityIndex(m_entities[denseIndex])] = denseIndex + 1;
		}

		m_components.pop_back();
		m_entities.pop_back();
		m_sparse[GetEntityIndex(entity)] = 0;
	}

	bool HasEntity(EntityId entity) const override
	{
		return nullptr != Get(entity);
	}

	T* Get(EntityId entity)
	{
		const std::size_t denseIndex = GetDenseIndex(entity);
		return INVALID_DENSE_INDEX == denseIndex ? nullptr : m_components[denseIndex];
	}

	const T* Get(EntityId entity) const
	{
		const std::size_t denseIndex = GetDenseIndex(entity);
		return INVALID_DENSE_INDEX == denseIndex ? nullptr : m_components[denseIndex];
	}

	void Clear() override
	{
		for (T* component : m_components)
		{
			if (component)
			{
				component->~T();
				m_allocator.Free(component, sizeof(T));
			}
		}

		m_components.clear();
		m_entities.clear();
		m_sparse.clear();
	}

	std::size_t GetComponentCount() const override
	{
		return m_components.size();
	}

	const std::vector<EntityId>& GetEntities() const
	{
		return m_entities;
	}

	const AllocatorStats& GetAllocatorStats() const
	{
		return m_allocator.GetStats();
	}

private:
	void EnsureSparseSize(EntityId entity)
	{
		const std::size_t index = static_cast<std::size_t>(GetEntityIndex(entity));
		if (index >= m_sparse.size())
		{
			m_sparse.resize(index + 1, 0);
		}
	}

	std::size_t GetDenseIndex(EntityId entity) const
	{
		if (false == IsValidEntityId(entity))
		{
			return INVALID_DENSE_INDEX;
		}

		const std::size_t index = static_cast<std::size_t>(GetEntityIndex(entity));
		if (index >= m_sparse.size())
		{
			return INVALID_DENSE_INDEX;
		}

		const std::size_t densePlusOne = m_sparse[index];
		if (0 == densePlusOne)
		{
			return INVALID_DENSE_INDEX;
		}

		const std::size_t denseIndex = densePlusOne - 1;
		if (denseIndex >= m_entities.size() || m_entities[denseIndex] != entity)
		{
			return INVALID_DENSE_INDEX;
		}

		return denseIndex;
	}

private:
	static constexpr std::size_t DEFAULT_BLOCKS_PER_CHUNK = 256;
	static constexpr std::size_t INVALID_DENSE_INDEX = static_cast<std::size_t>(-1);

private:
	CBlockPoolAllocator m_allocator;
	std::vector<EntityId> m_entities;
	std::vector<T*> m_components;
	std::vector<std::size_t> m_sparse;
};

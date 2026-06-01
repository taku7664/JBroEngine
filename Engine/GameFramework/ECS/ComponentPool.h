#pragma once

#include "GameFramework/ECS/EntityTypes.h"
#include "Utillity/Framework.h"

#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  IComponentPool
// ─────────────────────────────────────────────────────────────────────────────

class IComponentPool
{
public:
	virtual ~IComponentPool() = default;

	virtual void        RemoveEntity(EntityId entity) = 0;
	virtual bool        HasEntity(EntityId entity) const = 0;
	virtual void        FlushPendingRemoves() = 0;
	virtual void        Clear() = 0;
	virtual std::size_t GetComponentCount() const = 0;

	// 타입소거 접근자 — Ref<T> 해석 등 컴파일타임에 T 를 모르는 코드에서
	// type_index 로 풀을 찾은 뒤 컴포넌트 주소를 void* 로 얻기 위해 사용한다.
	// (호출자가 올바른 타입으로 static_cast 책임짐.)
	virtual void*       GetRawComponent(EntityId entity) = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
//  CComponentPool<T>
//
//  Storage: packed dense vector — contiguous memory, cache-friendly ForEach.
//
//  Pointer contract:
//    T* returned by Add / Get is valid until the next call that causes
//    m_dense to reallocate (i.e. Add beyond reserved capacity) or until
//    FlushPendingRemoves executes a swap-and-pop.
//    → Pre-reserve at scene load to eliminate reallocation during gameplay.
//    → FlushPendingRemoves is called by CScene at the end of every Update,
//      after all systems and scripts have finished running for the frame.
//    → Scripts and systems may safely cache T* within a frame.
//
//  Multi-component per entity: DISABLED.
//    AddNew behaves identically to Add (idempotent, returns existing).
//    GetAll fills at most one element. RemoveSpecific delegates to RemoveEntity.
//
//  Removal: deferred (enqueued, not applied immediately).
//    Components of entities queued for removal remain visible in ForEachActive
//    until FlushPendingRemoves is called.  This gives one frame of grace,
//    which is acceptable for all standard game-loop uses.
// ─────────────────────────────────────────────────────────────────────────────

template<typename T>
class CComponentPool final : public IComponentPool
{
public:
	CComponentPool() = default;
	~CComponentPool() override = default;

	CComponentPool(const CComponentPool&)            = delete;
	CComponentPool& operator=(const CComponentPool&) = delete;

	// ── Add / AddNew ──────────────────────────────────────────────────────────

	// Returns the existing instance if the entity already has one;
	// otherwise constructs a new one in-place.
	template<typename... Args>
	T* Add(EntityId entity, Args&&... args)
	{
		if (!IsValidEntityId(entity))
		{
			return nullptr;
		}

		if (T* existing = Get(entity))
		{
			return existing;
		}

		EnsureSparseSize(entity);

		const std::size_t denseIdx = m_dense.size();
		if (denseIdx >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
		{
			return nullptr;
		}
		m_dense.emplace_back(std::forward<Args>(args)...);
		m_entities.push_back(entity);
		m_sparse[GetEntityIndex(entity)] = static_cast<std::uint32_t>(denseIdx + 1u);

		return &m_dense.back();
	}

	// Multi-component disabled — identical to Add.
	template<typename... Args>
	T* AddNew(EntityId entity, Args&&... args)
	{
		return Add(entity, std::forward<Args>(args)...);
	}

	// ── Removal ───────────────────────────────────────────────────────────────

	// Enqueues the entity for removal at the next FlushPendingRemoves.
	// Safe to call during ForEachActive or mid-frame.
	void RemoveEntity(EntityId entity) override
	{
		const std::uint32_t EI = GetEntityIndex(entity);
		if (EI >= static_cast<std::uint32_t>(m_sparse.size()) || m_sparse[EI] == 0u)
		{
			return;
		}
		m_pendingRemove.push_back(entity);
	}

	// Multi-component disabled — delegates to RemoveEntity.
	bool RemoveSpecific(EntityId entity, T* /*comp*/)
	{
		const std::uint32_t EI = GetEntityIndex(entity);
		if (EI >= static_cast<std::uint32_t>(m_sparse.size()) || m_sparse[EI] == 0u)
		{
			return false;
		}
		m_pendingRemove.push_back(entity);
		return true;
	}

	// Applies all queued removals via swap-and-pop.
	// Call once per frame, after all systems and scripts have finished.
	void FlushPendingRemoves() override
	{
		for (const EntityId entity : m_pendingRemove)
		{
			RemoveNow(entity);
		}
		m_pendingRemove.clear();
	}

	// ── Query ─────────────────────────────────────────────────────────────────

	// O(1) sparse lookup.
	bool HasEntity(EntityId entity) const override
	{
		const std::uint32_t EI = GetEntityIndex(entity);
		return EI < static_cast<std::uint32_t>(m_sparse.size()) && m_sparse[EI] != 0u;
	}

	// Returns the component pointer, or nullptr.
	T* Get(EntityId entity)
	{
		const std::uint32_t EI = GetEntityIndex(entity);
		if (EI >= static_cast<std::uint32_t>(m_sparse.size()) || m_sparse[EI] == 0u)
		{
			return nullptr;
		}
		return &m_dense[m_sparse[EI] - 1u];
	}

	const T* Get(EntityId entity) const
	{
		const std::uint32_t EI = GetEntityIndex(entity);
		if (EI >= static_cast<std::uint32_t>(m_sparse.size()) || m_sparse[EI] == 0u)
		{
			return nullptr;
		}
		return &m_dense[m_sparse[EI] - 1u];
	}

	void* GetRawComponent(EntityId entity) override
	{
		return Get(entity);
	}

	// Multi-component disabled — fills at most one element.
	void GetAll(EntityId entity, std::vector<T*>& out)
	{
		if (T* comp = Get(entity))
		{
			out.push_back(comp);
		}
	}

	void GetAll(EntityId entity, std::vector<const T*>& out) const
	{
		if (const T* comp = Get(entity))
		{
			out.push_back(comp);
		}
	}

	// ── Iteration ─────────────────────────────────────────────────────────────

	// Iterates every live component in dense order.
	// Components pending removal are still included until FlushPendingRemoves.
	template<typename Func>
	void ForEachActive(Func&& func)
	{
		const std::size_t count = m_dense.size();
		for (std::size_t i = 0; i < count; ++i)
		{
			func(m_entities[i], m_dense[i]);
		}
	}

	template<typename Func>
	void ForEachActive(Func&& func) const
	{
		const std::size_t count = m_dense.size();
		for (std::size_t i = 0; i < count; ++i)
		{
			func(m_entities[i], m_dense[i]);
		}
	}

	// ── Misc ──────────────────────────────────────────────────────────────────

	void Clear() override
	{
		m_dense.clear();
		m_entities.clear();
		m_sparse.clear();
		m_pendingRemove.clear();
	}

	std::size_t GetComponentCount() const override
	{
		return m_dense.size();
	}

	// Pre-allocate to prevent reallocation (and T* invalidation) during gameplay.
	// Call at scene load with the expected maximum entity count.
	void Reserve(std::size_t capacity)
	{
		m_dense.reserve(capacity);
		m_entities.reserve(capacity);
	}

private:
	void EnsureSparseSize(EntityId entity)
	{
		const std::uint32_t idx = GetEntityIndex(entity);
		if (idx >= static_cast<std::uint32_t>(m_sparse.size()))
		{
			m_sparse.resize(idx + 1u, 0u);
		}
	}

	// Immediate swap-and-pop removal. Only called from FlushPendingRemoves.
	void RemoveNow(EntityId entity)
	{
		const std::uint32_t EI = GetEntityIndex(entity);
		if (EI >= static_cast<std::uint32_t>(m_sparse.size()) || m_sparse[EI] == 0u)
		{
			return; // already removed (duplicate in queue)
		}

		const std::uint32_t denseIdx = m_sparse[EI] - 1u;
		const std::uint32_t lastIdx  = static_cast<std::uint32_t>(m_dense.size()) - 1u;

		if (denseIdx != lastIdx)
		{
			// Move the last element into the vacated slot.
			m_dense[denseIdx]    = std::move(m_dense[lastIdx]);
			m_entities[denseIdx] = m_entities[lastIdx];

			// Update the sparse entry for the element that just moved.
			m_sparse[GetEntityIndex(m_entities[denseIdx])] = static_cast<std::uint32_t>(denseIdx + 1u);
		}

		m_dense.pop_back();
		m_entities.pop_back();
		m_sparse[EI] = 0;
	}

private:
	std::vector<T>             m_dense;         // contiguous component storage
	std::vector<EntityId>      m_entities;      // dense index → EntityId (for swap-and-pop)
	std::vector<std::uint32_t> m_sparse;        // entityIndex → dense index + 1  (0 = absent)
	                                            // uint32_t: dense index fits in 32 bits,
	                                            // halves sparse array memory vs size_t on x64
	std::vector<EntityId>      m_pendingRemove; // entities queued for end-of-frame removal
};

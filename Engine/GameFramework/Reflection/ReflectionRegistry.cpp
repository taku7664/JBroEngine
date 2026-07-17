#include "pch.h"
#include "ReflectionRegistry.h"
#include "Core/Logging/LoggerInternal.h"

#include <cstring>
#include <cstdlib>
#include <mutex>
#include <unordered_map>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace
{
	struct ScriptAllocationContext
	{
		CGameCanvas* Canvas = nullptr;
		TypeId ScriptTypeId = INVALID_TYPE_ID;
	};

	struct ScriptAllocationRecord
	{
		CGameCanvas* Canvas = nullptr;
		TypeId ScriptTypeId = INVALID_TYPE_ID;
		std::uint64_t CanvasGeneration = 0;
	};

	thread_local ScriptAllocationContext g_scriptAllocationContext;

	class ScriptAllocationRegistry final
	{
	public:
		void Register(
			void* ptr,
			CGameCanvas& canvas,
			TypeId scriptTypeId,
			std::uint64_t canvasGeneration)
		{
			if (nullptr == ptr || INVALID_TYPE_ID == scriptTypeId)
			{
				return;
			}

			std::lock_guard<std::mutex> lock(m_mutex);
			m_records[ptr] = { &canvas, scriptTypeId, canvasGeneration };
		}

		bool Consume(void* ptr, ScriptAllocationRecord& outRecord)
		{
			if (nullptr == ptr)
			{
				return false;
			}

			std::lock_guard<std::mutex> lock(m_mutex);
			const auto it = m_records.find(ptr);
			if (it == m_records.end())
			{
				return false;
			}

			outRecord = it->second;
			m_records.erase(it);
			return true;
		}

		void ForgetCanvas(const CGameCanvas& canvas)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			for (auto it = m_records.begin(); it != m_records.end();)
			{
				if (it->second.Canvas == &canvas)
				{
					it = m_records.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

	private:
		std::mutex m_mutex;
		std::unordered_map<void*, ScriptAllocationRecord> m_records;
	};

	ScriptAllocationRegistry& GetScriptAllocationRegistry()
	{
		static ScriptAllocationRegistry registry;
		return registry;
	}

	class ScriptAllocationScope final
	{
	public:
		ScriptAllocationScope(CGameCanvas* canvas, TypeId scriptTypeId)
			: m_previous(g_scriptAllocationContext)
		{
			g_scriptAllocationContext.Canvas = canvas;
			g_scriptAllocationContext.ScriptTypeId = scriptTypeId;
		}

		~ScriptAllocationScope()
		{
			g_scriptAllocationContext = m_previous;
		}

		ScriptAllocationScope(const ScriptAllocationScope&) = delete;
		ScriptAllocationScope& operator=(const ScriptAllocationScope&) = delete;

	private:
		ScriptAllocationContext m_previous;
	};
}

CComponentRegistration::CComponentRegistration(ComponentTypeInfo* typeInfo)
	: m_typeInfo(typeInfo)
{
}

CComponentRegistration& CComponentRegistration::AddProperty(const char* name, EReflectPropertyType propertyType, std::size_t offset, std::size_t size, std::size_t elementCount, bool isEditable)
{
	if (m_typeInfo && name)
	{
		ReflectPropertyInfo property;
		property.Name = name;
		property.DisplayName = name;
		property.Type = propertyType;
		property.Offset = offset;
		property.Size = size;
		property.ElementCount = elementCount;
		property.IsEditable = isEditable;
		m_typeInfo->Properties.push_back(property);
	}

	return *this;
}

CComponentRegistration& CComponentRegistration::AddAssetProperty(const char* name, std::size_t offset, EAssetType expectedType, bool isEditable)
{
	AddProperty(name, EReflectPropertyType::AssetGuid, offset, sizeof(AssetGuid), 1, isEditable);
	if (m_typeInfo && false == m_typeInfo->Properties.empty())
	{
		m_typeInfo->Properties.back().ExpectedAssetType = expectedType;
	}
	return *this;
}

const ComponentTypeInfo* CReflectionRegistry::FindComponent(TypeId typeId) const
{
	auto it = m_componentIndexById.find(typeId);
	return it != m_componentIndexById.end() ? &m_componentTypes[it->second] : nullptr;
}

const ComponentTypeInfo* CReflectionRegistry::FindComponentByName(const char* name) const
{
	if (nullptr == name)
	{
		return nullptr;
	}

	auto it = m_componentIdByName.find(name);
	return it != m_componentIdByName.end() ? FindComponent(it->second) : nullptr;
}

std::size_t CReflectionRegistry::GetComponentTypeCount() const
{
	return m_componentTypes.size();
}

const ComponentTypeInfo* CReflectionRegistry::GetComponentType(std::size_t index) const
{
	return index < m_componentTypes.size() ? &m_componentTypes[index] : nullptr;
}

const ScriptTypeInfo* CReflectionRegistry::FindScript(TypeId typeId) const
{
	auto it = m_scriptIndexById.find(typeId);
	return it != m_scriptIndexById.end() ? &m_scriptTypes[it->second] : nullptr;
}

const ScriptTypeInfo* CReflectionRegistry::FindScriptByName(const char* name) const
{
	if (nullptr == name)
	{
		return nullptr;
	}

	auto it = m_scriptIdByName.find(name);
	return it != m_scriptIdByName.end() ? FindScript(it->second) : nullptr;
}

std::size_t CReflectionRegistry::GetScriptTypeCount() const
{
	return m_scriptTypes.size();
}

const ScriptTypeInfo* CReflectionRegistry::GetScriptType(std::size_t index) const
{
	return index < m_scriptTypes.size() ? &m_scriptTypes[index] : nullptr;
}

bool CReflectionRegistry::AddComponent(CGameCanvas& canvas, CGameObject& object, TypeId typeId) const
{
	const ComponentTypeInfo* typeInfo = FindComponent(typeId);
	if (!typeInfo || false == CanAddComponent(object, typeId)) return false;
	return typeInfo->AddToObject && typeInfo->AddToObject(canvas, object);
}

bool CReflectionRegistry::ReserveComponentPool(CGameCanvas& canvas, TypeId typeId, std::size_t capacity) const
{
	const ComponentTypeInfo* typeInfo = FindComponent(typeId);
	if (nullptr == typeInfo || nullptr == typeInfo->ReserveInCanvas)
	{
		return false;
	}
	typeInfo->ReserveInCanvas(canvas, capacity);
	return true;
}

bool CReflectionRegistry::CanAddComponent(const CGameObject& object, TypeId typeId) const
{
	const ComponentTypeInfo* typeInfo = FindComponent(typeId);
	if (!typeInfo || false == typeInfo->CanAddToObject)
	{
		return false;
	}
	if (EComponentMultiplicity::Single == typeInfo->Multiplicity)
	{
		return false == HasComponent(object, typeId);
	}
	return true;
}

bool CReflectionRegistry::RemoveComponent(CGameCanvas& canvas, CGameObject& object, TypeId typeId) const
{
	const ComponentTypeInfo* typeInfo = FindComponent(typeId);
	return typeInfo && typeInfo->RemoveFromObject && typeInfo->RemoveFromObject(canvas, object);
}

bool CReflectionRegistry::RemoveComponentByGuid(CGameCanvas& canvas, CGameObject& object, TypeId typeId, const File::Guid& componentGuid) const
{
	if (componentGuid.IsNull())
	{
		return RemoveComponent(canvas, object, typeId);
	}

	CComponent* component = static_cast<CComponent*>(GetComponentAddressByGuid(object, typeId, componentGuid));
	if (nullptr == component)
	{
		return false;
	}
	canvas.RemoveComponent(component);
	return true;
}

bool CReflectionRegistry::HasComponent(const CGameObject& object, TypeId typeId) const
{
	const ComponentTypeInfo* typeInfo = FindComponent(typeId);
	return typeInfo && typeInfo->HasComponent && typeInfo->HasComponent(object);
}

void* CReflectionRegistry::GetComponentAddress(CGameObject& object, TypeId typeId) const
{
	const ComponentTypeInfo* typeInfo = FindComponent(typeId);
	return typeInfo && typeInfo->GetAddress ? typeInfo->GetAddress(object) : nullptr;
}

const void* CReflectionRegistry::GetComponentAddress(const CGameObject& object, TypeId typeId) const
{
	const ComponentTypeInfo* typeInfo = FindComponent(typeId);
	return typeInfo && typeInfo->GetConstAddress ? typeInfo->GetConstAddress(object) : nullptr;
}

void* CReflectionRegistry::GetComponentAddressByGuid(CGameObject& object, TypeId typeId, const File::Guid& componentGuid) const
{
	if (componentGuid.IsNull())
	{
		return GetComponentAddress(object, typeId);
	}

	for (void* address : GetComponentAddresses(object, typeId))
	{
		CComponent* component = static_cast<CComponent*>(address);
		if (component && component->GetInstanceGuid() == componentGuid)
		{
			return address;
		}
	}
	return nullptr;
}

const void* CReflectionRegistry::GetComponentAddressByGuid(const CGameObject& object, TypeId typeId, const File::Guid& componentGuid) const
{
	if (componentGuid.IsNull())
	{
		return GetComponentAddress(object, typeId);
	}

	const ComponentTypeInfo* typeInfo = FindComponent(typeId);
	if (nullptr == typeInfo || nullptr == typeInfo->GetAddresses)
	{
		return nullptr;
	}

	std::vector<void*> addresses = typeInfo->GetAddresses(const_cast<CGameObject&>(object));
	for (void* address : addresses)
	{
		const CComponent* component = static_cast<const CComponent*>(address);
		if (component && component->GetInstanceGuid() == componentGuid)
		{
			return address;
		}
	}
	return nullptr;
}

void* CReflectionRegistry::GetComponentAddressByIndex(CGameObject& object, TypeId typeId, std::size_t index) const
{
	std::vector<void*> addresses = GetComponentAddresses(object, typeId);
	return index < addresses.size() ? addresses[index] : nullptr;
}

std::vector<void*> CReflectionRegistry::GetComponentAddresses(CGameObject& object, TypeId typeId) const
{
	const ComponentTypeInfo* typeInfo = FindComponent(typeId);
	return typeInfo && typeInfo->GetAddresses ? typeInfo->GetAddresses(object) : std::vector<void*>();
}

void* CReflectionRegistry::GetPropertyAddress(void* component, const ReflectPropertyInfo& property)
{
	if (nullptr == component)
	{
		return nullptr;
	}

	// Script 프로퍼티는 GetFieldPtr 를 통해 접근한다 (불완전 타입 문제 회피).
	// Component 프로퍼티는 AddProperty() 에서 계산된 Offset 을 사용한다.
	if (nullptr != property.GetFieldPtr)
	{
		return property.GetFieldPtr(component);
	}

	return static_cast<void*>(static_cast<std::uint8_t*>(component) + property.Offset);
}

const void* CReflectionRegistry::GetPropertyAddress(const void* component, const ReflectPropertyInfo& property)
{
	if (nullptr == component)
	{
		return nullptr;
	}

	if (nullptr != property.GetFieldPtr)
	{
		// GetFieldPtr 는 void* 를 받으므로 const_cast 가 필요하다.
		// 반환값은 const void* 로 사용되므로 실제 쓰기는 발생하지 않는다.
		return property.GetFieldPtr(const_cast<void*>(component));
	}

	return static_cast<const void*>(static_cast<const std::uint8_t*>(component) + property.Offset);
}

TypeId CReflectionRegistry::MakeTypeId(const char* name)
{
	return MakeStableTypeId(name);
}

ComponentTypeInfo* CReflectionRegistry::RegisterComponentInternal(ComponentTypeInfo&& typeInfo)
{
	if (INVALID_TYPE_ID == typeInfo.Type.Id || nullptr == typeInfo.Type.Name)
	{
		return nullptr;
	}

	auto found = m_componentIndexById.find(typeInfo.Type.Id);
	if (found != m_componentIndexById.end())
	{
		ComponentTypeInfo& existing = m_componentTypes[found->second];
		if (0 == std::strcmp(existing.Type.Name, typeInfo.Type.Name))
		{
			return &existing;
		}
		CSystemLog::Error(std::format(
			"Stable TypeId collision between component types '{}' and '{}'.",
			existing.Type.Name,
			typeInfo.Type.Name));
		return nullptr;
	}

	const std::size_t index = m_componentTypes.size();
	m_componentIndexById.emplace(typeInfo.Type.Id, index);
	m_componentIdByName.emplace(typeInfo.Type.Name, typeInfo.Type.Id);
	m_componentTypes.push_back(std::move(typeInfo));
	return &m_componentTypes.back();
}

bool CReflectionRegistry::UnregisterScript(TypeId typeId)
{
	auto itIdx = m_scriptIndexById.find(typeId);
	if (itIdx == m_scriptIndexById.end())
	{
		return false;
	}

	const std::size_t removeIdx = itIdx->second;
	const std::string removeName = m_scriptTypes[removeIdx].Type.Name ? m_scriptTypes[removeIdx].Type.Name : "";

	// 이름 맵에서 제거
	m_scriptIdByName.erase(removeName);

	// 벡터에서 스왑-팝
	const std::size_t lastIdx = m_scriptTypes.size() - 1;
	if (removeIdx != lastIdx)
	{
		// 마지막 요소를 삭제할 자리로 이동
		m_scriptTypes[removeIdx] = std::move(m_scriptTypes[lastIdx]);
		// 인덱스 맵 업데이트
		const TypeId movedId = m_scriptTypes[removeIdx].Type.Id;
		m_scriptIndexById[movedId] = removeIdx;
	}

	m_scriptTypes.pop_back();
	m_scriptIndexById.erase(typeId);
	return true;
}

ScriptInstanceHandle CReflectionRegistry::CreateScriptInstance(
	TypeId typeId,
	CGameCanvas& canvas,
	CGameObject& owner) const
{
	ScriptInstanceHandle handle;
	const ScriptTypeInfo* info = FindScript(typeId);
	if (nullptr == info || nullptr == info->CreateInstance)
	{
		return handle;
	}

	ScriptAllocationScope allocationScope(&canvas, typeId);
	handle.Instance = info->CreateInstance(
		&m_scriptHostApi,
		ComponentConstructionToken{ typeId },
		owner.SafeFromThis());
	handle.DestroyInstance = info->DestroyInstance;
	handle.HostApi = &m_scriptHostApi;
	return handle;
}

void CReflectionRegistry::ForgetScriptAllocationsForCanvas(const CGameCanvas& canvas)
{
	GetScriptAllocationRegistry().ForgetCanvas(canvas);
}

bool CReflectionRegistry::RegisterScriptInternal(ScriptTypeInfo&& typeInfo)
{
	if (INVALID_TYPE_ID == typeInfo.Type.Id || nullptr == typeInfo.Type.Name)
	{
		return false;
	}
	if (const auto found = m_scriptIndexById.find(typeInfo.Type.Id); found != m_scriptIndexById.end())
	{
		const ScriptTypeInfo& existing = m_scriptTypes[found->second];
		if (0 != std::strcmp(existing.Type.Name, typeInfo.Type.Name))
		{
			CSystemLog::Error(std::format(
				"Stable TypeId collision between script types '{}' and '{}'.",
				existing.Type.Name,
				typeInfo.Type.Name));
		}
		return false;
	}

	const std::size_t index = m_scriptTypes.size();
	m_scriptIndexById.emplace(typeInfo.Type.Id, index);
	m_scriptIdByName.emplace(typeInfo.Type.Name, typeInfo.Type.Id);
	m_scriptTypes.push_back(std::move(typeInfo));
	return true;
}

void* CReflectionRegistry::AllocateScriptMemory(std::size_t size, std::size_t alignment)
{
	const std::size_t effectiveSize = std::max<std::size_t>(size, 1);
	const std::size_t effectiveAlignment = std::max<std::size_t>(alignment, alignof(void*));

	if (g_scriptAllocationContext.Canvas && INVALID_TYPE_ID != g_scriptAllocationContext.ScriptTypeId)
	{
		void* ptr = g_scriptAllocationContext.Canvas->AllocateScriptMemory(
			g_scriptAllocationContext.ScriptTypeId,
			effectiveSize,
			effectiveAlignment);
		if (ptr)
		{
			GetScriptAllocationRegistry().Register(
				ptr,
				*g_scriptAllocationContext.Canvas,
				g_scriptAllocationContext.ScriptTypeId,
				g_scriptAllocationContext.Canvas->GetScriptAllocationGeneration());
		}
		return ptr;
	}

#if defined(_MSC_VER)
	return _aligned_malloc(effectiveSize, effectiveAlignment);
#elif defined(__ANDROID__)
	// std::aligned_alloc 는 Android API 28+ 에서만 제공된다(min SDK 26).
	// 전 API 에서 가용한 posix_memalign 으로 정렬 할당한다(std::free 와 호환).
	void* alignedPtr = nullptr;
	if (0 != ::posix_memalign(&alignedPtr, effectiveAlignment, effectiveSize))
	{
		return nullptr;
	}
	return alignedPtr;
#else
	const std::size_t remainder = effectiveSize % effectiveAlignment;
	const std::size_t alignedSize = 0 == remainder ? effectiveSize : effectiveSize + (effectiveAlignment - remainder);
	return std::aligned_alloc(effectiveAlignment, alignedSize);
#endif
}

void CReflectionRegistry::FreeScriptMemory(void* ptr, std::size_t size, std::size_t alignment)
{
	if (nullptr == ptr)
	{
		return;
	}

	ScriptAllocationRecord record;
	if (GetScriptAllocationRegistry().Consume(ptr, record))
	{
		if (record.Canvas && record.Canvas->GetScriptAllocationGeneration() == record.CanvasGeneration)
		{
			record.Canvas->FreeScriptMemory(record.ScriptTypeId, ptr, size, alignment);
			return;
		}
		// The canvas already cleared the owning pool. Its storage has been released;
		// falling through to the direct allocator would double-free the pointer.
		return;
	}

#if defined(_MSC_VER)
	_aligned_free(ptr);
#else
	std::free(ptr);
#endif
}

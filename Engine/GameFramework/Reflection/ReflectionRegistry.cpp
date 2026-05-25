#include "pch.h"
#include "ReflectionRegistry.h"

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

bool CReflectionRegistry::AddComponent(CScene& scene, EntityId entity, TypeId typeId) const
{
	const ComponentTypeInfo* typeInfo = FindComponent(typeId);
	if (!typeInfo || !typeInfo->CanAddToEntity) return false;
	// For types that allow duplicates, AddComponent behaves like AddNewComponent
	// when called from the editor (duplicates are managed by the command layer).
	return typeInfo->AddToEntity && typeInfo->AddToEntity(scene, entity);
}

bool CReflectionRegistry::AddNewComponent(CScene& scene, EntityId entity, TypeId typeId) const
{
	const ComponentTypeInfo* typeInfo = FindComponent(typeId);
	return typeInfo && typeInfo->CanAddToEntity && typeInfo->AddNewToEntity && typeInfo->AddNewToEntity(scene, entity);
}

bool CReflectionRegistry::RemoveComponent(CScene& scene, EntityId entity, TypeId typeId) const
{
	const ComponentTypeInfo* typeInfo = FindComponent(typeId);
	return typeInfo && typeInfo->RemoveFromEntity && typeInfo->RemoveFromEntity(scene, entity);
}

bool CReflectionRegistry::RemoveSpecificComponent(CScene& scene, EntityId entity, TypeId typeId, void* component) const
{
	const ComponentTypeInfo* typeInfo = FindComponent(typeId);
	return typeInfo && typeInfo->RemoveSpecificFromEntity && typeInfo->RemoveSpecificFromEntity(scene, entity, component);
}

bool CReflectionRegistry::HasComponent(const CScene& scene, EntityId entity, TypeId typeId) const
{
	const ComponentTypeInfo* typeInfo = FindComponent(typeId);
	return typeInfo && typeInfo->HasComponent && typeInfo->HasComponent(scene, entity);
}

void* CReflectionRegistry::GetComponentAddress(CScene& scene, EntityId entity, TypeId typeId) const
{
	const ComponentTypeInfo* typeInfo = FindComponent(typeId);
	return typeInfo && typeInfo->GetAddress ? typeInfo->GetAddress(scene, entity) : nullptr;
}

const void* CReflectionRegistry::GetComponentAddress(const CScene& scene, EntityId entity, TypeId typeId) const
{
	const ComponentTypeInfo* typeInfo = FindComponent(typeId);
	return typeInfo && typeInfo->GetConstAddress ? typeInfo->GetConstAddress(scene, entity) : nullptr;
}

void CReflectionRegistry::GetAllComponentAddresses(CScene& scene, EntityId entity, TypeId typeId, std::vector<void*>& out) const
{
	const ComponentTypeInfo* typeInfo = FindComponent(typeId);
	if (typeInfo && typeInfo->GetAllAddresses)
	{
		typeInfo->GetAllAddresses(scene, entity, out);
	}
}

void* CReflectionRegistry::GetPropertyAddress(void* component, const ReflectPropertyInfo& property)
{
	if (nullptr == component)
	{
		return nullptr;
	}

	return static_cast<void*>(static_cast<std::uint8_t*>(component) + property.Offset);
}

const void* CReflectionRegistry::GetPropertyAddress(const void* component, const ReflectPropertyInfo& property)
{
	if (nullptr == component)
	{
		return nullptr;
	}

	return static_cast<const void*>(static_cast<const std::uint8_t*>(component) + property.Offset);
}

TypeId CReflectionRegistry::MakeTypeId(const char* name)
{
	if (nullptr == name || '\0' == name[0])
	{
		return INVALID_TYPE_ID;
	}

	TypeId hash = 14695981039346656037ull;
	while (*name)
	{
		hash ^= static_cast<unsigned char>(*name++);
		hash *= 1099511628211ull;
	}
	return hash ? hash : 1;
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
		return &m_componentTypes[found->second];
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

CGameScript* CReflectionRegistry::CreateScriptInstance(TypeId typeId) const
{
	const ScriptTypeInfo* info = FindScript(typeId);
	if (nullptr == info || nullptr == info->CreateInstance)
	{
		return nullptr;
	}
	return info->CreateInstance();
}

bool CReflectionRegistry::RegisterScriptInternal(ScriptTypeInfo&& typeInfo)
{
	if (INVALID_TYPE_ID == typeInfo.Type.Id || nullptr == typeInfo.Type.Name || m_scriptIndexById.contains(typeInfo.Type.Id))
	{
		return false;
	}

	const std::size_t index = m_scriptTypes.size();
	m_scriptIndexById.emplace(typeInfo.Type.Id, index);
	m_scriptIdByName.emplace(typeInfo.Type.Name, typeInfo.Type.Id);
	m_scriptTypes.push_back(std::move(typeInfo));
	return true;
}

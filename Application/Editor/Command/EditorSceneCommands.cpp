#include "pch.h"
#include "EditorSceneCommands.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Core/Core.h"
#include "Engine/Core/Logging/LoggerInternal.h"
#include "Engine/GameFramework/Object/GameObject.h"
#include "Engine/GameFramework/Reflection/ReflectionRegistry.h"
#include "Engine/GameFramework/Scene/Scene.h"

CAddComponentCommand::CAddComponentCommand(SafePtr<CScene> scene, EntityId entity, TypeId componentTypeId)
	: m_scene(scene)
	, m_entity(entity)
	, m_componentTypeId(componentTypeId)
{
}

const char* CAddComponentCommand::GetName() const
{
	return "Add Component";
}

bool CAddComponentCommand::Execute()
{
	if (false == m_scene.IsValid() || false == Core::Reflection.IsValid() || false == m_scene->IsAlive(m_entity))
	{
		return false;
	}

	CReflectionRegistry& reflection = *Core::Reflection;
	const ComponentTypeInfo* typeInfo = reflection.FindComponent(m_componentTypeId);
	if (nullptr == typeInfo)
	{
		return false;
	}

	if (typeInfo->AllowDuplicates)
	{
		// Always create a fresh instance; track the pointer for targeted undo.
		m_added = reflection.AddNewComponent(*m_scene, m_entity, m_componentTypeId);
		if (m_added)
		{
			std::vector<void*> all;
			reflection.GetAllComponentAddresses(*m_scene, m_entity, m_componentTypeId, all);
			m_addedComponent = all.empty() ? nullptr : all.back();
		}
	}
	else
	{
		if (reflection.HasComponent(*m_scene, m_entity, m_componentTypeId))
		{
			return false;
		}
		m_added = reflection.AddComponent(*m_scene, m_entity, m_componentTypeId);
		m_addedComponent = nullptr;
	}

	if (false == m_added)
	{
		CSystemLog::Warning("Failed to add reflected component.");
	}
	return m_added;
}

void CAddComponentCommand::Undo()
{
	if (false == m_added || false == m_scene.IsValid() || false == Core::Reflection.IsValid() || false == m_scene->IsAlive(m_entity))
	{
		return;
	}

	if (m_addedComponent)
	{
		Core::Reflection->RemoveSpecificComponent(*m_scene, m_entity, m_componentTypeId, m_addedComponent);
		m_addedComponent = nullptr;
	}
	else
	{
		Core::Reflection->RemoveComponent(*m_scene, m_entity, m_componentTypeId);
	}
	m_added = false;
}

void CAddComponentCommand::Redo()
{
	if (false == m_added && m_scene && Core::Reflection && m_scene->IsAlive(m_entity))
	{
		Execute();
	}
}

CCreateGameObjectCommand::CCreateGameObjectCommand(SafePtr<CScene> scene, const char* name)
	: m_scene(scene)
	, m_name(name ? name : "GameObject")
{
}

const char* CCreateGameObjectCommand::GetName() const
{
	return "Create GameObject";
}

bool CCreateGameObjectCommand::Execute()
{
	if (false == m_scene.IsValid())
	{
		return false;
	}

	CGameObject gameObject = m_scene->CreateGameObject(m_name.c_str());
	m_entity = gameObject.GetEntityId();
	m_created = gameObject.IsValid();
	return m_created;
}

void CCreateGameObjectCommand::Undo()
{
	if (m_created && m_scene && m_scene->IsAlive(m_entity))
	{
		m_scene->DestroyEntity(m_entity);
		m_created = false;
	}
}

void CCreateGameObjectCommand::Redo()
{
	if (false == m_created)
	{
		Execute();
	}
}

EntityId CCreateGameObjectCommand::GetEntity() const
{
	return m_entity;
}

CSetComponentPropertyCommand::CSetComponentPropertyCommand(
	SafePtr<CScene> scene,
	EntityId entity,
	TypeId componentTypeId,
	std::size_t propertyOffset,
	std::vector<std::uint8_t> oldValue,
	std::vector<std::uint8_t> newValue,
	std::size_t instanceIndex)
	: m_scene(scene)
	, m_entity(entity)
	, m_componentTypeId(componentTypeId)
	, m_propertyOffset(propertyOffset)
	, m_instanceIndex(instanceIndex)
	, m_oldValue(std::move(oldValue))
	, m_newValue(std::move(newValue))
{
}

const char* CSetComponentPropertyCommand::GetName() const
{
	return "Set Component Property";
}

bool CSetComponentPropertyCommand::Execute()
{
	return WriteValue(m_newValue);
}

void CSetComponentPropertyCommand::Undo()
{
	WriteValue(m_oldValue);
}

void CSetComponentPropertyCommand::Redo()
{
	WriteValue(m_newValue);
}

bool CSetComponentPropertyCommand::WriteValue(const std::vector<std::uint8_t>& value)
{
	if (value.empty() || false == m_scene.IsValid() || false == Core::Reflection.IsValid() || false == m_scene->IsAlive(m_entity))
	{
		return false;
	}

	void* component = nullptr;
	if (m_instanceIndex == 0)
	{
		component = Core::Reflection->GetComponentAddress(*m_scene, m_entity, m_componentTypeId);
	}
	else
	{
		std::vector<void*> all;
		Core::Reflection->GetAllComponentAddresses(*m_scene, m_entity, m_componentTypeId, all);
		component = m_instanceIndex < all.size() ? all[m_instanceIndex] : nullptr;
	}

	if (nullptr == component)
	{
		return false;
	}

	std::memcpy(static_cast<std::uint8_t*>(component) + m_propertyOffset, value.data(), value.size());
	return true;
}

#endif

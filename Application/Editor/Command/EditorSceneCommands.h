#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Command/EditorCommandManager.h"
#include "Engine/GameFramework/ECS/EntityTypes.h"
#include "Engine/GameFramework/Reflection/ReflectionTypes.h"

class CScene;

class CAddComponentCommand final : public IEditorCommand
{
public:
	CAddComponentCommand(SafePtr<CScene> scene, EntityId entity, TypeId componentTypeId);
	~CAddComponentCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	SafePtr<CScene> m_scene;
	EntityId m_entity = INVALID_ENTITY_ID;
	TypeId m_componentTypeId = INVALID_TYPE_ID;
	bool m_added = false;
	// Non-null when the component type allows duplicates; points to the specific
	// instance that was created so Undo can remove exactly that one.
	void* m_addedComponent = nullptr;
};

class CCreateGameObjectCommand final : public IEditorCommand
{
public:
	CCreateGameObjectCommand(SafePtr<CScene> scene, const char* name);
	~CCreateGameObjectCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;
	EntityId GetEntity() const;

private:
	SafePtr<CScene> m_scene;
	std::string m_name;
	EntityId m_entity = INVALID_ENTITY_ID;
	bool m_created = false;
};

class CSetComponentPropertyCommand final : public IEditorCommand
{
public:
	CSetComponentPropertyCommand(
		SafePtr<CScene> scene,
		EntityId entity,
		TypeId componentTypeId,
		std::size_t propertyOffset,
		std::vector<std::uint8_t> oldValue,
		std::vector<std::uint8_t> newValue,
		// For component types that allow duplicates, pass the 0-based instance index
		// so undo/redo modifies the correct instance rather than always the first one.
		std::size_t instanceIndex = 0);
	~CSetComponentPropertyCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	bool WriteValue(const std::vector<std::uint8_t>& value);

private:
	SafePtr<CScene> m_scene;
	EntityId m_entity = INVALID_ENTITY_ID;
	TypeId m_componentTypeId = INVALID_TYPE_ID;
	std::size_t m_propertyOffset = 0;
	std::size_t m_instanceIndex = 0;
	std::vector<std::uint8_t> m_oldValue;
	std::vector<std::uint8_t> m_newValue;
};

#endif

#pragma once

#include "GameFramework/Reflection/ReflectionTypes.h"
#include "GameFramework/Scene/Scene.h"
#include "Utillity/SafePtr.h"

#include <functional>
#include <type_traits>
#include <unordered_map>

using ComponentAddFunc = bool(*)(CScene& scene, EntityId entity);
using ComponentRemoveFunc = bool(*)(CScene& scene, EntityId entity);
using ComponentHasFunc = bool(*)(const CScene& scene, EntityId entity);
using ComponentAddressFunc = void* (*)(CScene& scene, EntityId entity);
using ConstComponentAddressFunc = const void* (*)(const CScene& scene, EntityId entity);

struct ComponentTypeInfo
{
	ReflectTypeInfo Type;
	std::vector<ReflectPropertyInfo> Properties;
	ComponentAddFunc AddToEntity = nullptr;
	ComponentRemoveFunc RemoveFromEntity = nullptr;
	ComponentHasFunc HasComponent = nullptr;
	ComponentAddressFunc GetAddress = nullptr;
	ConstComponentAddressFunc GetConstAddress = nullptr;
	bool CanAddToEntity = true;
};

struct ScriptTypeInfo
{
	ReflectTypeInfo Type;
};

class CComponentRegistration final
{
public:
	CComponentRegistration() = default;
	CComponentRegistration(ComponentTypeInfo* typeInfo);

	CComponentRegistration& AddProperty(const char* name, EReflectPropertyType propertyType, std::size_t offset, std::size_t size, std::size_t elementCount = 1, bool isEditable = true);

private:
	ComponentTypeInfo* m_typeInfo = nullptr;
};

class CReflectionRegistry final : public EnableSafeFromThis<CReflectionRegistry>
{
public:
	template<typename T>
	CComponentRegistration RegisterComponent(const ComponentRegisterDesc& desc);

	template<typename T>
	bool RegisterScript(const ScriptRegisterDesc& desc);

	const ComponentTypeInfo* FindComponent(TypeId typeId) const;
	const ComponentTypeInfo* FindComponentByName(const char* name) const;
	std::size_t GetComponentTypeCount() const;
	const ComponentTypeInfo* GetComponentType(std::size_t index) const;

	const ScriptTypeInfo* FindScript(TypeId typeId) const;
	const ScriptTypeInfo* FindScriptByName(const char* name) const;
	std::size_t GetScriptTypeCount() const;
	const ScriptTypeInfo* GetScriptType(std::size_t index) const;

	bool AddComponent(CScene& scene, EntityId entity, TypeId typeId) const;
	bool RemoveComponent(CScene& scene, EntityId entity, TypeId typeId) const;
	bool HasComponent(const CScene& scene, EntityId entity, TypeId typeId) const;
	void* GetComponentAddress(CScene& scene, EntityId entity, TypeId typeId) const;
	const void* GetComponentAddress(const CScene& scene, EntityId entity, TypeId typeId) const;

	static void* GetPropertyAddress(void* component, const ReflectPropertyInfo& property);
	static const void* GetPropertyAddress(const void* component, const ReflectPropertyInfo& property);
	static TypeId MakeTypeId(const char* name);

private:
	ComponentTypeInfo* RegisterComponentInternal(ComponentTypeInfo&& typeInfo);
	bool RegisterScriptInternal(ScriptTypeInfo&& typeInfo);

private:
	std::vector<ComponentTypeInfo> m_componentTypes;
	std::vector<ScriptTypeInfo> m_scriptTypes;
	std::unordered_map<TypeId, std::size_t> m_componentIndexById;
	std::unordered_map<std::string, TypeId> m_componentIdByName;
	std::unordered_map<TypeId, std::size_t> m_scriptIndexById;
	std::unordered_map<std::string, TypeId> m_scriptIdByName;
};

template<typename T>
CComponentRegistration CReflectionRegistry::RegisterComponent(const ComponentRegisterDesc& desc)
{
	static_assert(std::is_default_constructible_v<T>, "Reflected components must be default constructible.");

	ComponentTypeInfo typeInfo;
	typeInfo.Type.Id = MakeTypeId(desc.Name);
	typeInfo.Type.Name = desc.Name;
	typeInfo.Type.DisplayName = desc.DisplayName ? desc.DisplayName : desc.Name;
	typeInfo.Type.Category = desc.Category;
	typeInfo.Type.Kind = EReflectTypeKind::Component;
	typeInfo.Type.Size = sizeof(T);
	typeInfo.Type.Alignment = alignof(T);
	typeInfo.CanAddToEntity = desc.CanAddToEntity;
	typeInfo.AddToEntity = [](CScene& scene, EntityId entity) -> bool {
		return nullptr != scene.AddComponent<T>(entity);
	};
	typeInfo.RemoveFromEntity = [](CScene& scene, EntityId entity) -> bool {
		if (false == scene.HasComponent<T>(entity))
		{
			return false;
		}
		scene.RemoveComponent<T>(entity);
		return true;
	};
	typeInfo.HasComponent = [](const CScene& scene, EntityId entity) -> bool {
		return scene.HasComponent<T>(entity);
	};
	typeInfo.GetAddress = [](CScene& scene, EntityId entity) -> void* {
		return scene.GetComponent<T>(entity);
	};
	typeInfo.GetConstAddress = [](const CScene& scene, EntityId entity) -> const void* {
		return scene.GetComponent<T>(entity);
	};

	ComponentTypeInfo* registeredType = RegisterComponentInternal(std::move(typeInfo));
	return CComponentRegistration(registeredType);
}

template<typename T>
bool CReflectionRegistry::RegisterScript(const ScriptRegisterDesc& desc)
{
	ScriptTypeInfo typeInfo;
	typeInfo.Type.Id = MakeTypeId(desc.Name);
	typeInfo.Type.Name = desc.Name;
	typeInfo.Type.DisplayName = desc.DisplayName ? desc.DisplayName : desc.Name;
	typeInfo.Type.Category = desc.Category;
	typeInfo.Type.Kind = EReflectTypeKind::Script;
	typeInfo.Type.Size = sizeof(T);
	typeInfo.Type.Alignment = alignof(T);
	return RegisterScriptInternal(std::move(typeInfo));
}

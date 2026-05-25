#pragma once

#include "GameFramework/Reflection/ReflectionTypes.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scripting/GameScript.h"
#include "Utillity/SafePtr.h"

#include <functional>
#include <type_traits>
#include <unordered_map>

using ComponentAddFunc              = bool(*)(CScene& scene, EntityId entity);
using ComponentAddNewFunc           = bool(*)(CScene& scene, EntityId entity);
using ComponentRemoveFunc           = bool(*)(CScene& scene, EntityId entity);
using ComponentRemoveSpecificFunc   = bool(*)(CScene& scene, EntityId entity, void* component);
using ComponentHasFunc              = bool(*)(const CScene& scene, EntityId entity);
using ComponentAddressFunc          = void*       (*)(CScene& scene, EntityId entity);
using ConstComponentAddressFunc     = const void* (*)(const CScene& scene, EntityId entity);
using ComponentGetAllAddressesFunc  = void(*)(CScene& scene, EntityId entity, std::vector<void*>& out);

// 스크립트 인스턴스 팩토리 함수 타입
using CreateScriptFunc = CGameScript*(*)();

struct ComponentTypeInfo
{
	ReflectTypeInfo Type;
	std::vector<ReflectPropertyInfo> Properties;
	ComponentAddFunc             AddToEntity              = nullptr; // idempotent
	ComponentAddNewFunc          AddNewToEntity           = nullptr; // always creates
	ComponentRemoveFunc          RemoveFromEntity         = nullptr; // removes all instances
	ComponentRemoveSpecificFunc  RemoveSpecificFromEntity = nullptr; // removes one instance
	ComponentHasFunc             HasComponent             = nullptr;
	ComponentAddressFunc         GetAddress               = nullptr; // returns first instance
	ConstComponentAddressFunc    GetConstAddress          = nullptr;
	ComponentGetAllAddressesFunc GetAllAddresses          = nullptr;
	bool CanAddToEntity    = true;
	bool AllowDuplicates   = false;
};

struct ScriptTypeInfo
{
	ReflectTypeInfo  Type;
	CreateScriptFunc CreateInstance = nullptr; // 스크립트 인스턴스 생성 팩토리
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

	// DLL 언로드 시 해당 DLL이 등록한 스크립트 타입을 제거합니다.
	bool UnregisterScript(TypeId typeId);

	// 등록된 스크립트 타입의 인스턴스를 생성합니다. 호출자가 소유권을 가집니다.
	CGameScript* CreateScriptInstance(TypeId typeId) const;

	bool AddComponent(CScene& scene, EntityId entity, TypeId typeId) const;
	bool AddNewComponent(CScene& scene, EntityId entity, TypeId typeId) const;
	bool RemoveComponent(CScene& scene, EntityId entity, TypeId typeId) const;
	bool RemoveSpecificComponent(CScene& scene, EntityId entity, TypeId typeId, void* component) const;
	bool HasComponent(const CScene& scene, EntityId entity, TypeId typeId) const;
	void* GetComponentAddress(CScene& scene, EntityId entity, TypeId typeId) const;
	const void* GetComponentAddress(const CScene& scene, EntityId entity, TypeId typeId) const;
	void GetAllComponentAddresses(CScene& scene, EntityId entity, TypeId typeId, std::vector<void*>& out) const;

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
	typeInfo.CanAddToEntity  = desc.CanAddToEntity;
	typeInfo.AllowDuplicates = desc.AllowDuplicates;
	typeInfo.AddToEntity = [](CScene& scene, EntityId entity) -> bool {
		return nullptr != scene.AddComponent<T>(entity);
	};
	typeInfo.AddNewToEntity = [](CScene& scene, EntityId entity) -> bool {
		return nullptr != scene.AddNewComponent<T>(entity);
	};
	typeInfo.RemoveFromEntity = [](CScene& scene, EntityId entity) -> bool {
		if (false == scene.HasComponent<T>(entity))
		{
			return false;
		}
		scene.RemoveComponent<T>(entity);
		return true;
	};
	typeInfo.RemoveSpecificFromEntity = [](CScene& scene, EntityId entity, void* component) -> bool {
		return scene.RemoveSpecificComponent<T>(entity, static_cast<T*>(component));
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
	typeInfo.GetAllAddresses = [](CScene& scene, EntityId entity, std::vector<void*>& out) {
		for (T* ptr : scene.GetAllComponents<T>(entity))
		{
			out.push_back(ptr);
		}
	};

	ComponentTypeInfo* registeredType = RegisterComponentInternal(std::move(typeInfo));
	return CComponentRegistration(registeredType);
}

template<typename T>
bool CReflectionRegistry::RegisterScript(const ScriptRegisterDesc& desc)
{
	static_assert(std::is_base_of_v<CGameScript, T>, "Script types must derive from CGameScript.");
	static_assert(std::is_default_constructible_v<T>, "Script types must be default constructible.");

	ScriptTypeInfo typeInfo;
	typeInfo.Type.Id = MakeTypeId(desc.Name);
	typeInfo.Type.Name = desc.Name;
	typeInfo.Type.DisplayName = desc.DisplayName ? desc.DisplayName : desc.Name;
	typeInfo.Type.Category = desc.Category;
	typeInfo.Type.Kind = EReflectTypeKind::Script;
	typeInfo.Type.Size = sizeof(T);
	typeInfo.Type.Alignment = alignof(T);
	typeInfo.CreateInstance = []() -> CGameScript* { return new T(); };
	return RegisterScriptInternal(std::move(typeInfo));
}

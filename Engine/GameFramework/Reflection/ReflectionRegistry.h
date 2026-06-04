#pragma once

#include "GameFramework/Reflection/ReflectionTypes.h"
#include "GameFramework/Scripting/ScriptMacros.h"
#include "Core/Game/GameModuleTypes.h"
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Scripting/GameScript.h"
#include "Utillity/Pointer/SafePtr.h"

#include <functional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

class CGameObject;

using ComponentAddFunc              = bool(*)(CScene& scene, CGameObject& object);
using ComponentRemoveFunc           = bool(*)(CScene& scene, CGameObject& object);
using ComponentHasFunc              = bool(*)(const CGameObject& object);
using ComponentAddressFunc          = void*       (*)(CGameObject& object);
using ConstComponentAddressFunc     = const void* (*)(const CGameObject& object);
using ComponentAddressesFunc        = std::vector<void*>(*)(CGameObject& object); // 멀티 컴포넌트: 같은 타입 전부

// 스크립트 인스턴스 팩토리 함수 타입
using CreateScriptFunc = CGameScript*(*)(const GameModuleHostApi* hostApi);
using DestroyScriptFunc = void(*)(CGameScript* script, const GameModuleHostApi* hostApi);

struct ScriptInstanceHandle
{
	CGameScript* Instance = nullptr;
	DestroyScriptFunc DestroyInstance = nullptr;
	const GameModuleHostApi* HostApi = nullptr;
};

// 한 오브젝트는 같은 타입 컴포넌트를 여러 개 보유할 수 있다(멀티 컴포넌트).
// GetAddress 는 첫 인스턴스, GetAddresses 는 전부를 돌려준다.
struct ComponentTypeInfo
{
	ReflectTypeInfo Type;
	std::vector<ReflectPropertyInfo> Properties;
	ComponentAddFunc          AddToObject     = nullptr;
	ComponentRemoveFunc       RemoveFromObject = nullptr;
	ComponentHasFunc          HasComponent    = nullptr;
	ComponentAddressFunc      GetAddress      = nullptr;   // 첫 인스턴스(없으면 nullptr)
	ComponentAddressesFunc    GetAddresses    = nullptr;   // 같은 타입 전부
	ConstComponentAddressFunc GetConstAddress = nullptr;
	bool CanAddToObject = true;
};

struct ScriptTypeInfo
{
	ReflectTypeInfo                  Type;
	std::vector<ReflectPropertyInfo> Properties;       // REFLECT_FIELD 로 자동 채워짐
	CreateScriptFunc  CreateInstance  = nullptr;
	DestroyScriptFunc DestroyInstance = nullptr;
};

class CComponentRegistration final
{
public:
	CComponentRegistration() = default;
	CComponentRegistration(ComponentTypeInfo* typeInfo);

	CComponentRegistration& AddProperty(const char* name, EReflectPropertyType propertyType, std::size_t offset, std::size_t size, std::size_t elementCount = 1, bool isEditable = true);

	// Enum 프로퍼티 등록 — magic_enum 으로 이름/변환 메타를 자동 생성한다(보일러플레이트 없음).
	// 호출부는 .AddEnumProperty<EMyEnum>("Field", offsetof(C, Field)) 한 줄.
	//
	// ⚠ 정의는 호스트 전용 헤더 "ReflectionEnumRegister.h" 에 있다(magic_enum 의존). 이 헤더
	//    (ReflectionRegistry.h)는 SDK 로 미러되어 게임 스크립트 DLL 도 포함하는데, DLL 은
	//    magic_enum 을 갖지 않으므로 magic_enum 의존을 공개 헤더에 두면 안 된다. 컴포넌트 등록
	//    지점(BuiltinComponentRegistry.cpp, 호스트)만 ReflectionEnumRegister.h 를 include 한다.
	template<typename TEnum>
	CComponentRegistration& AddEnumProperty(const char* name, std::size_t offset, bool isEditable = true);

	// 정의 분리용 — ReflectionEnumRegister.h 가 m_typeInfo 에 접근해 마지막 프로퍼티에 메타를 단다.
	ComponentTypeInfo* GetTypeInfo() const { return m_typeInfo; }

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

	// JPROP codegen 용 — 명시적 프로퍼티 목록(offset 기반)으로 등록한다.
	// REFLECT_FIELD 의 GetReflectEntries 정적초기화 매직을 쓰지 않는다.
	template<typename T>
	bool RegisterScript(const ScriptRegisterDesc& desc, const std::vector<ScriptPropertyDesc>& properties);

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

	// 등록된 스크립트 타입의 인스턴스를 생성합니다. 호출자는 DestroyScriptInstance로 파괴해야 합니다.
	ScriptInstanceHandle CreateScriptInstance(TypeId typeId) const;
	void DestroyScriptInstance(ScriptInstanceHandle& instance) const;
	const GameModuleHostApi* GetScriptHostApi() const;

	bool AddComponent(CScene& scene, CGameObject& object, TypeId typeId) const;
	bool RemoveComponent(CScene& scene, CGameObject& object, TypeId typeId) const;
	bool HasComponent(const CGameObject& object, TypeId typeId) const;
	void* GetComponentAddress(CGameObject& object, TypeId typeId) const;
	const void* GetComponentAddress(const CGameObject& object, TypeId typeId) const;
	// 같은 타입 컴포넌트 전부(멀티 컴포넌트). 없으면 빈 벡터.
	std::vector<void*> GetComponentAddresses(CGameObject& object, TypeId typeId) const;

	static void* GetPropertyAddress(void* component, const ReflectPropertyInfo& property);
	static const void* GetPropertyAddress(const void* component, const ReflectPropertyInfo& property);
	static TypeId MakeTypeId(const char* name);

private:
	ComponentTypeInfo* RegisterComponentInternal(ComponentTypeInfo&& typeInfo);
	bool RegisterScriptInternal(ScriptTypeInfo&& typeInfo);
	static void* AllocateScriptMemory(std::size_t size, std::size_t alignment);
	static void FreeScriptMemory(void* ptr, std::size_t size, std::size_t alignment);

private:
	std::vector<ComponentTypeInfo> m_componentTypes;
	std::vector<ScriptTypeInfo> m_scriptTypes;
	std::unordered_map<TypeId, std::size_t> m_componentIndexById;
	std::unordered_map<std::string, TypeId> m_componentIdByName;
	std::unordered_map<TypeId, std::size_t> m_scriptIndexById;
	std::unordered_map<std::string, TypeId> m_scriptIdByName;
	GameModuleHostApi m_scriptHostApi = { &CReflectionRegistry::AllocateScriptMemory, &CReflectionRegistry::FreeScriptMemory };
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
	typeInfo.CanAddToObject = desc.CanAddToEntity;
	typeInfo.AddToObject = [](CScene& scene, CGameObject& object) -> bool {
		return nullptr != scene.AddComponent<T>(object);
	};
	typeInfo.RemoveFromObject = [](CScene& scene, CGameObject& object) -> bool {
		if (false == object.HasComponent<T>())
		{
			return false;
		}
		scene.RemoveComponent<T>(object);
		return true;
	};
	typeInfo.HasComponent = [](const CGameObject& object) -> bool {
		return object.HasComponent<T>();
	};
	typeInfo.GetAddress = [](CGameObject& object) -> void* {
		return object.GetComponent<T>();
	};
	typeInfo.GetAddresses = [](CGameObject& object) -> std::vector<void*> {
		std::vector<void*> result;
		for (T* comp : object.GetComponents<T>())
		{
			result.push_back(comp);
		}
		return result;
	};
	typeInfo.GetConstAddress = [](const CGameObject& object) -> const void* {
		return object.GetComponent<T>();
	};

	ComponentTypeInfo* registeredType = RegisterComponentInternal(std::move(typeInfo));
	return CComponentRegistration(registeredType);
}

namespace detail
{
	template<typename T, typename = void>
	struct HasReflectEntries : std::false_type {};

	template<typename T>
	struct HasReflectEntries<T, std::void_t<decltype(T::GetReflectEntries())>> : std::true_type {};
}

template<typename T>
bool CReflectionRegistry::RegisterScript(const ScriptRegisterDesc& desc)
{
	static_assert(std::is_base_of_v<CGameScript, T>, "Script types must derive from CGameScript.");
	static_assert(std::is_default_constructible_v<T>, "Script types must be default constructible.");

	ScriptTypeInfo typeInfo;
	typeInfo.Type.Id          = MakeTypeId(desc.Name);
	typeInfo.Type.Name        = desc.Name;
	typeInfo.Type.DisplayName = desc.DisplayName ? desc.DisplayName : desc.Name;
	typeInfo.Type.Category    = desc.Category;
	typeInfo.Type.Kind        = EReflectTypeKind::Script;
	typeInfo.Type.Size        = sizeof(T);
	typeInfo.Type.Alignment   = alignof(T);

	typeInfo.CreateInstance = [](const GameModuleHostApi* hostApi) -> CGameScript* {
		if (nullptr == hostApi || nullptr == hostApi->Allocate)
		{
			return nullptr;
		}
		void* memory = hostApi->Allocate(sizeof(T), alignof(T));
		if (nullptr == memory)
		{
			return nullptr;
		}
		return new (memory) T();
	};

	typeInfo.DestroyInstance = [](CGameScript* script, const GameModuleHostApi* hostApi) {
		if (nullptr == script)
		{
			return;
		}
		static_cast<T*>(script)->~T();
		if (hostApi && hostApi->Free)
		{
			hostApi->Free(script, sizeof(T), alignof(T));
		}
	};

	// ── REFLECT_FIELD 자동 등록 ─────────────────────────────────────────────
	// SCRIPT_CLASS(T) 를 사용한 스크립트는 GetReflectEntries() 가 있다.
	// 없는 클래스(레거시)는 Properties 가 비어 있는 채로 등록된다.
	if constexpr (detail::HasReflectEntries<T>::value)
	{
		for (const ScriptReflectEntry& entry : T::GetReflectEntries())
		{
			ReflectPropertyInfo prop;
			prop.Name         = entry.Name;
			prop.DisplayName  = entry.Name;
			prop.Type         = entry.Type;
			prop.GetFieldPtr  = entry.GetFieldPtr;   // Offset 대신 함수 포인터 방식 사용
			prop.Size         = entry.Size;
			prop.ElementCount = entry.ElementCount;
			prop.IsEditable   = true;
			typeInfo.Properties.push_back(prop);
		}
	}

	return RegisterScriptInternal(std::move(typeInfo));
}

template<typename T>
bool CReflectionRegistry::RegisterScript(const ScriptRegisterDesc& desc, const std::vector<ScriptPropertyDesc>& properties)
{
	static_assert(std::is_base_of_v<CGameScript, T>, "Script types must derive from CGameScript.");
	static_assert(std::is_default_constructible_v<T>, "Script types must be default constructible.");

	ScriptTypeInfo typeInfo;
	typeInfo.Type.Id          = MakeTypeId(desc.Name);
	typeInfo.Type.Name        = desc.Name;
	typeInfo.Type.DisplayName = desc.DisplayName ? desc.DisplayName : desc.Name;
	typeInfo.Type.Category    = desc.Category;
	typeInfo.Type.Kind        = EReflectTypeKind::Script;
	typeInfo.Type.Size        = sizeof(T);
	typeInfo.Type.Alignment   = alignof(T);

	typeInfo.CreateInstance = [](const GameModuleHostApi* hostApi) -> CGameScript* {
		if (nullptr == hostApi || nullptr == hostApi->Allocate) { return nullptr; }
		void* memory = hostApi->Allocate(sizeof(T), alignof(T));
		if (nullptr == memory) { return nullptr; }
		return new (memory) T();
	};
	typeInfo.DestroyInstance = [](CGameScript* script, const GameModuleHostApi* hostApi) {
		if (nullptr == script) { return; }
		static_cast<T*>(script)->~T();
		if (hostApi && hostApi->Free) { hostApi->Free(script, sizeof(T), alignof(T)); }
	};

	for (const ScriptPropertyDesc& d : properties)
	{
		ReflectPropertyInfo prop;
		prop.Name         = d.Name;
		prop.DisplayName  = d.DisplayName ? d.DisplayName : d.Name;
		prop.Type         = d.Type;
		prop.Offset       = d.Offset;            // 생성 파일이 offsetof 로 계산 (GetFieldPtr 미사용)
		prop.Size         = d.Size;
		prop.ElementCount = d.ElementCount ? d.ElementCount : 1;
		prop.IsEditable   = true;
		prop.Tooltip      = d.Tooltip;
		prop.Category     = d.Category;
		prop.HasRange     = d.HasRange;
		prop.RangeMin     = d.RangeMin;
		prop.RangeMax     = d.RangeMax;
		prop.Serialize    = d.Serialize;
		prop.RefCategory  = d.RefCategory;
		prop.RefTypeName  = d.RefTypeName;
		typeInfo.Properties.push_back(prop);
	}

	return RegisterScriptInternal(std::move(typeInfo));
}

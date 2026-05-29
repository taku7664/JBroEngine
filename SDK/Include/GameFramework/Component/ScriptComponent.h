#pragma once

#include "GameFramework/Reflection/ReflectionTypes.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameFramework/Scripting/GameScript.h"

#include <cstdint>
#include <string>
#include <vector>

// ── ScriptPendingField ────────────────────────────────────────────────────────
// 씬 로드 또는 DLL 핫리로드 직후, 스크립트 인스턴스가 아직 생성되기 전
// 복원해야 할 필드 값을 임시 보관한다.
//
// ScriptSystem::OnUpdate 에서 인스턴스를 처음 생성할 때 적용되며,
// 적용이 완료되면 ScriptComponent::PendingFields 가 비워진다.
struct ScriptPendingField
{
	std::string           Name; // 프로퍼티 이름 (타입 변경 사이에서도 이름으로 매칭)
	EReflectPropertyType  Type = EReflectPropertyType::Float;
	std::string           Text; // non-trivial reflected values such as AssetGuid
	std::vector<uint8_t>  Data; // 원본 필드의 raw bytes (크기 = ReflectPropertyInfo::Size)
};

// ── ScriptComponent ───────────────────────────────────────────────────────────
// 엔티티에 부착되는 스크립트 컴포넌트.
// ScriptTypeId 로 등록된 타입을 지정하며, 인스턴스는 ScriptSystem 이 지연 생성한다.
struct ScriptComponent
{
	ScriptComponent() = default;
	~ScriptComponent()
	{
		ResetInstance();
	}

	ScriptComponent(const ScriptComponent&) = delete;
	ScriptComponent& operator=(const ScriptComponent&) = delete;

	ScriptComponent(ScriptComponent&& rhs) noexcept
	{
		MoveFrom(rhs);
	}

	ScriptComponent& operator=(ScriptComponent&& rhs) noexcept
	{
		if (this != &rhs)
		{
			ResetInstance();
			MoveFrom(rhs);
		}
		return *this;
	}

	void SetInstance(ScriptInstanceHandle&& handle)
	{
		ResetInstance();
		Instance        = handle.Instance;
		DestroyInstance = handle.DestroyInstance;
		HostApi         = handle.HostApi;
		handle.Instance        = nullptr;
		handle.DestroyInstance = nullptr;
		handle.HostApi         = nullptr;
	}

	void ResetInstance()
	{
		if (Instance && DestroyInstance)
		{
			DestroyInstance(Instance, HostApi);
		}
		Instance        = nullptr;
		DestroyInstance = nullptr;
		HostApi         = nullptr;
	}

	// ── 기본 필드 ──────────────────────────────────────────────────────────
	TypeId            ScriptTypeId    = INVALID_TYPE_ID;
	CGameScript*      Instance        = nullptr;
	DestroyScriptFunc DestroyInstance = nullptr;
	const GameModuleHostApi* HostApi  = nullptr;
	bool              IsEnabled       = true;

	// ── 지연 복원 필드 ─────────────────────────────────────────────────────
	// 씬 로드 또는 핫리로드 후 인스턴스 생성 시 ScriptSystem 이 적용하고 비운다.
	std::vector<ScriptPendingField> PendingFields;

private:
	void MoveFrom(ScriptComponent& rhs)
	{
		ScriptTypeId    = rhs.ScriptTypeId;
		Instance        = rhs.Instance;
		DestroyInstance = rhs.DestroyInstance;
		HostApi         = rhs.HostApi;
		IsEnabled       = rhs.IsEnabled;
		PendingFields   = std::move(rhs.PendingFields);

		rhs.Instance        = nullptr;
		rhs.DestroyInstance = nullptr;
		rhs.HostApi         = nullptr;
	}
};

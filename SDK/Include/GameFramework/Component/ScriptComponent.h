#pragma once

#include "GameFramework/Component/Component.h"
#include "GameFramework/Reflection/ReflectionTypes.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameFramework/Scripting/GameScript.h"
#include "Core/EngineCore.h"
#include "Core/Input/InputSystem.h"
#include "Core/Input/IInputHandler.h"

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
class ScriptComponent final : public CComponent
{
	JBRO_COMPONENT(ScriptComponent)
public:
	ScriptComponent() = default;
	~ScriptComponent() override
	{
		ResetInstance();
	}

	ScriptComponent(const ScriptComponent&) = delete;
	ScriptComponent& operator=(const ScriptComponent&) = delete;
	ScriptComponent(ScriptComponent&&) = delete;
	ScriptComponent& operator=(ScriptComponent&&) = delete;

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
		// 입력 핸들러는 인스턴스 파괴 *전에* 해제(핸들러 포인터가 인스턴스 내부를 가리킴).
		// 핫리로드/씬 언로드 시 컴포넌트 파괴 경로가 항상 여기를 거쳐 죽은 포인터를 막는다.
		// 등록 주체는 InputSystem — 호스트 전용 경로. (ScriptComponent 는 호스트 소유 객체라
		// ResetInstance 는 호스트에서만 emit/실행된다 → 전역 Engine 사용 안전. Ref.cpp 가
		// ScriptComponent.h 를 DLL 에서 컴파일하지만 ResetInstance 를 odr-use 하지 않는다.)
		if (InputRegistered && InputHandler && Engine.InputSystem.IsValid())
		{
			Engine.InputSystem->UnregisterHandler(InputHandler);
		}
		InputHandler    = nullptr;
		InputRegistered = false;

		if (Instance && DestroyInstance)
		{
			DestroyInstance(Instance, HostApi);
		}
		Instance        = nullptr;
		DestroyInstance = nullptr;
		HostApi         = nullptr;
	}

	// ── 기본 필드 ──────────────────────────────────────────────────────────
	// IsEnabled 는 CComponent 베이스가 보유.
	TypeId            ScriptTypeId    = INVALID_TYPE_ID;
	CGameScript*      Instance        = nullptr;
	DestroyScriptFunc DestroyInstance = nullptr;
	const GameModuleHostApi* HostApi  = nullptr;

	// ── 입력 핸들러 (스크립트가 InputHandler<...> 상속 시) ────────────────────
	// 인스턴스 생성 시 ScriptSystem 이 ToInputHandler 썽크로 캐싱하고, Start 후 등록한다.
	// 해제는 ResetInstance(파괴/언로드) + 비활성 전환 시. null 이면 입력 비핸들러.
	IInputHandler* InputHandler    = nullptr;
	bool           InputRegistered = false;

	// ── 지연 복원 필드 ─────────────────────────────────────────────────────
	// 씬 로드 또는 핫리로드 후 인스턴스 생성 시 ScriptSystem 이 적용하고 비운다.
	std::vector<ScriptPendingField> PendingFields;
};

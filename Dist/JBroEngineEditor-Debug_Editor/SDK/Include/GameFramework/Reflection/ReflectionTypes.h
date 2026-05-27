#pragma once

#include "GameFramework/ECS/EntityTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using TypeId = std::uint64_t;
inline constexpr TypeId INVALID_TYPE_ID = 0;

enum class EReflectTypeKind
{
	Unknown,
	Component,
	Script
};

enum class EReflectPropertyType
{
	Bool,
	Int32,
	UInt32,
	Float,
	AngleDegrees,  // float (radians) stored in memory, displayed/edited as degrees in inspector
	String,
	Vector2Float,
	ColorFloat4,
	AssetGuid,
	EntityId,
	Enum,
	Layout2D  // Normalized(x,y) + Pixel(x,y) — maps to struct Layout2D
};

struct ReflectTypeInfo
{
	TypeId Id = INVALID_TYPE_ID;
	const char* Name = nullptr;
	const char* DisplayName = nullptr;
	const char* Category = nullptr;
	EReflectTypeKind Kind = EReflectTypeKind::Unknown;
	std::size_t Size = 0;
	std::size_t Alignment = 0;
};

struct ReflectPropertyInfo
{
	const char*          Name         = nullptr;
	const char*          DisplayName  = nullptr;
	EReflectPropertyType Type         = EReflectPropertyType::Float;

	// ── 필드 접근 방식 (둘 중 하나만 사용) ───────────────────────────────────
	// Component 프로퍼티: AddProperty() 경유, offsetof 로 컴파일 타임에 계산됨.
	std::size_t          Offset       = 0;
	// Script 프로퍼티: REFLECT_FIELD 매크로 경유, 런타임 함수 포인터로 접근.
	// 클래스 선언부(불완전 타입)에서 offsetof 를 쓸 수 없으므로 이 방식을 사용한다.
	void*              (*GetFieldPtr)(void*) = nullptr;

	std::size_t          Size         = 0;
	std::size_t          ElementCount = 1;
	bool                 IsEditable   = true;
};

struct ComponentRegisterDesc
{
	const char* Name = nullptr;
	const char* DisplayName = nullptr;
	const char* Category = nullptr;
	bool CanAddToEntity  = true;
};

struct ScriptRegisterDesc
{
	const char* Name = nullptr;
	const char* DisplayName = nullptr;
	const char* Category = nullptr;
};

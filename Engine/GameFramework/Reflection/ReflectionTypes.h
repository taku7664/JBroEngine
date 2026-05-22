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
	String,
	Vector2Float,
	ColorFloat4,
	AssetGuid,
	EntityId,
	Enum
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
	const char* Name = nullptr;
	const char* DisplayName = nullptr;
	EReflectPropertyType Type = EReflectPropertyType::Float;
	std::size_t Offset = 0;
	std::size_t Size = 0;
	std::size_t ElementCount = 1;
	bool IsEditable = true;
};

struct ComponentRegisterDesc
{
	const char* Name = nullptr;
	const char* DisplayName = nullptr;
	const char* Category = nullptr;
	bool CanAddToEntity = true;
};

struct ScriptRegisterDesc
{
	const char* Name = nullptr;
	const char* DisplayName = nullptr;
	const char* Category = nullptr;
};

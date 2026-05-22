#pragma once

#include "GameFramework/ECS/EntityTypes.h"
#include "GameFramework/Reflection/ReflectionTypes.h"

#include <string>

enum class EPrefabSerializeResult
{
	Success,
	InvalidArgument,
	IoError,
	ParseError
};

struct PrefabPropertyOverride
{
	EntityId TargetEntity = INVALID_ENTITY_ID;
	TypeId ComponentType = INVALID_TYPE_ID;
	std::string PropertyName;
	std::string SerializedValue;
};

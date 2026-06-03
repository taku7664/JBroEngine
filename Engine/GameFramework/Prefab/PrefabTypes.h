#pragma once

#include "GameFramework/Scene/SceneTypes.h"
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
	ObjectId TargetObject = INVALID_OBJECT_ID;
	TypeId ComponentType = INVALID_TYPE_ID;
	std::string PropertyName;
	std::string SerializedValue;
};

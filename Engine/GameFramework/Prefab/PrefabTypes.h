#pragma once

#include "GameFramework/Reflection/ReflectionTypes.h"
#include "Utillity/File/FilePath.h" // File::Guid

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
	File::Guid TargetObject;        // 프리팹 내 대상 오브젝트의 안정 식별자(InstanceGuid)
	TypeId ComponentType = INVALID_TYPE_ID;
	std::string PropertyName;
	std::string SerializedValue;
};

#pragma once

#include "GameFramework/Reflection/ReflectionTypes.h"
#include "GameFramework/Scripting/GameScript.h"
#include "Utillity/SafePtr.h"

struct ScriptComponent
{
	TypeId ScriptTypeId = INVALID_TYPE_ID;
	OwnerPtr<CGameScript> Instance;
	bool IsEnabled = true;
};

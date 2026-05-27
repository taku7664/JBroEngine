#pragma once

#include "GameFramework/ECS/EntityTypes.h"

struct TransformHierarchy2D
{
	EntityId Parent = INVALID_ENTITY_ID;
	EntityId FirstChild = INVALID_ENTITY_ID;
	EntityId PrevSibling = INVALID_ENTITY_ID;
	EntityId NextSibling = INVALID_ENTITY_ID;
};

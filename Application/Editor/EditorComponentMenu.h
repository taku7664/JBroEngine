#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/GameFramework/ECS/EntityTypes.h"

class CScene;

namespace EditorComponentMenu
{
	bool DrawAddComponentMenu(CScene& scene, EntityId entity);
	bool DrawAddComponentButton(CScene& scene, EntityId entity);
}

#endif

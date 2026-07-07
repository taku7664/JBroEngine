#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include <string>

namespace EditorSessionPersistence
{
	// Saves the active scene and the project-owned editor state as one operation.
	// A missing active scene is valid; an actual scene or project write failure is not.
	bool Save(std::string& outError);
}

#endif

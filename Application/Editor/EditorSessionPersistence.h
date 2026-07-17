#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include <string>

namespace EditorSessionPersistence
{
	// Saves the active canvas and the project-owned editor state as one operation.
	// A missing active canvas is valid; an actual canvas or project write failure is not.
	bool Save(std::string& outError);
}

#endif

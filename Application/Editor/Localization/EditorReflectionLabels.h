#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include <string>

struct ComponentTypeInfo;
struct ReflectPropertyInfo;
struct ScriptTypeInfo;

namespace EditorReflectionLabels
{
	const char* GetScriptDisplayName(const ScriptTypeInfo* scriptType);
	std::string GetComponentLabel(const ComponentTypeInfo& componentType);
	std::string GetPropertyLabel(const ReflectPropertyInfo& property);
	std::string GetCategoryLabel(const char* category);
}

#endif

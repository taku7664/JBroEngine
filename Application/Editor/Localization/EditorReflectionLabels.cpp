#include "pch.h"
#include "EditorReflectionLabels.h"

#include "Engine/GameFramework/Reflection/ReflectionRegistry.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

const char* EditorReflectionLabels::GetScriptDisplayName(const ScriptTypeInfo* scriptType)
{
	if (nullptr == scriptType)
	{
		return Loc::Text(EditorLocKeys::InspectorUnknownScript);
	}
	if (scriptType->Type.DisplayName && scriptType->Type.DisplayName[0] != '\0')
	{
		return scriptType->Type.DisplayName;
	}
	return scriptType->Type.Name ? scriptType->Type.Name : Loc::Text(EditorLocKeys::InspectorUnknownScript);
}

std::string EditorReflectionLabels::GetComponentLabel(const ComponentTypeInfo& componentType)
{
	const char* fallback = componentType.Type.DisplayName ? componentType.Type.DisplayName : componentType.Type.Name;
	const std::string key = std::string("editor.component.") + (componentType.Type.Name ? componentType.Type.Name : "");
	return Loc::TextOr(key.c_str(), fallback ? fallback : "");
}

std::string EditorReflectionLabels::GetPropertyLabel(const ReflectPropertyInfo& property)
{
	const char* fallback = property.DisplayName ? property.DisplayName : property.Name;
	const std::string key = std::string("editor.property.") + (property.Name ? property.Name : "");
	return Loc::TextOr(key.c_str(), fallback ? fallback : "");
}

std::string EditorReflectionLabels::GetCategoryLabel(const char* category)
{
	const char* safeCategory = category ? category : "Components";
	const std::string key = std::string("editor.category.") + safeCategory;
	return Loc::TextOr(key.c_str(), safeCategory);
}

#endif

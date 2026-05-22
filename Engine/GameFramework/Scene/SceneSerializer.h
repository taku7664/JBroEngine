#pragma once

#include "GameFramework/Scene/SceneTypes.h"

#include <string>

class CScene;

class CSceneSerializer final
{
public:
	ESceneSerializeResult SerializeToText(const CScene& scene, std::string& outText) const;
	ESceneSerializeResult DeserializeFromText(CScene& scene, const char* text) const;
	ESceneSerializeResult SaveToFile(const CScene& scene, const char* path) const;
	ESceneSerializeResult LoadFromFile(CScene& scene, const char* path) const;

private:
	static std::string EscapeField(const char* value);
	static std::string UnescapeField(const std::string& value);
};

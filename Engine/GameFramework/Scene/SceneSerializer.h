#pragma once

#include "File/FilePath.h"
#include "GameFramework/Scene/SceneTypes.h"

#include <string>

class CScene;

class CSceneSerializer final
{
public:
	ESceneSerializeResult SerializeToText(const CScene& scene, std::string& outText) const;
	ESceneSerializeResult DeserializeFromText(CScene& scene, const char* text) const;
	ESceneSerializeResult SaveToFile(const CScene& scene, const File::Path& path) const;
	ESceneSerializeResult LoadFromFile(CScene& scene, const File::Path& path) const;
};

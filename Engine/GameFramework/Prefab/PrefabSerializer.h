#pragma once

#include "File/FilePath.h"
#include "GameFramework/ECS/EntityTypes.h"
#include "GameFramework/Prefab/PrefabTypes.h"

#include <string>

class CGameObject;
class CScene;

class CPrefabSerializer final
{
public:
	EPrefabSerializeResult SerializePrefabToText(const CScene& scene, EntityId root, std::string& outText) const;
	EPrefabSerializeResult DeserializePrefabFromText(CScene& scene, const char* text, CGameObject* outRoot = nullptr) const;
	EPrefabSerializeResult SavePrefabToFile(const CScene& scene, EntityId root, const File::Path& path) const;
	EPrefabSerializeResult LoadPrefabFromFile(CScene& scene, const File::Path& path, CGameObject* outRoot = nullptr) const;

private:
	static CGameObject CloneHierarchy(const CScene& sourceScene, CScene& targetScene, EntityId sourceEntity);
	static void CopyComponents(const CScene& sourceScene, EntityId sourceEntity, CGameObject& targetObject);
	static EPrefabSerializeResult ConvertSceneResult(int sceneResult);
};

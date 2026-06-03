#pragma once

#include "GameFramework/Prefab/PrefabTypes.h"

#include <cstdint>
#include <string>

class CGameObject;
class CScene;

class CPrefabSerializer final
{
public:
	// root = 직렬화할 루트 오브젝트.
	EPrefabSerializeResult SerializePrefabToText(const CScene& scene, const CGameObject* root, std::string& outText) const;
	// outRoot != nullptr 이면 복원된 첫 루트 오브젝트 포인터를 기록한다(풀 소유, SafePtr 로 보유 권장).
	EPrefabSerializeResult DeserializePrefabFromText(CScene& scene, const char* text, CGameObject** outRoot = nullptr) const;
	EPrefabSerializeResult SavePrefabToFile(const CScene& scene, const CGameObject* root, const File::Path& path) const;
	EPrefabSerializeResult LoadPrefabFromFile(CScene& scene, const File::Path& path, CGameObject** outRoot = nullptr) const;

private:
	static CGameObject* CloneHierarchy(const CScene& sourceScene, CScene& targetScene, const CGameObject& sourceObject);
	static void CopyComponents(const CGameObject& sourceObject, CGameObject& targetObject);
	static EPrefabSerializeResult ConvertSceneResult(int sceneResult);
};

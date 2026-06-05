#pragma once

#include "Core/Asset/AssetTypes.h"
#include "GameFramework/Scene/SceneTypes.h"

#include <string>
#include <vector>

class CScene;

class CSceneSerializer final
{
public:
	ESceneSerializeResult SerializeToText(CScene& scene, std::string& outText) const;
	ESceneSerializeResult DeserializeFromText(CScene& scene, const char* text) const;
	ESceneSerializeResult SaveToFile(CScene& scene, const File::Path& path) const;
	ESceneSerializeResult LoadFromFile(CScene& scene, const File::Path& path) const;

	// 씬/프리팹 파일에서 ReferencedAssets 목록만 가볍게 읽는다 — 게임오브젝트나 에셋
	// 데이터를 만들지 않고 YAML 의 ReferencedAssets 시퀀스만 파싱한다. 프로젝트 로드 시
	// "이 씬을 띄우는 데 필요한 에셋"을 미리 알아내 선택적으로 로드하는 용도.
	std::vector<AssetGuid> ReadReferencedAssetsFromFile(const File::Path& path) const;
};

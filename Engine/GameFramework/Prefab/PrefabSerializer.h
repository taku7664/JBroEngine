#pragma once

#include "GameFramework/Prefab/PrefabTypes.h"

#include <cstdint>
#include <string>

class CGameObject;
class CGameCanvas;

// 서브트리(루트 1개 + 자손) ↔ 텍스트. 실제 일은 Serialization 계층이 한다 — 이 클래스는
// 프리팹 결과코드로 감싸는 얇은 껍데기다(오브젝트/컴포넌트 guid 보존, 스크립트 포함).
class CPrefabSerializer final
{
public:
	// root = 직렬화할 루트 오브젝트. scene 은 쓰지 않는다(호출부 호환용).
	EPrefabSerializeResult SerializePrefabToText(const CGameCanvas& scene, const CGameObject* root, std::string& outText) const;
	// outRoot != nullptr 이면 복원된 첫 루트 오브젝트 포인터를 기록한다(풀 소유, SafePtr 로 보유 권장).
	// 복원 오브젝트는 캔버스의 기본 레이어에 붙는다 — 다른 레이어로 보내는 건 호출자 몫이다.
	EPrefabSerializeResult DeserializePrefabFromText(CGameCanvas& scene, const char* text, CGameObject** outRoot = nullptr) const;
	EPrefabSerializeResult SavePrefabToFile(const CGameCanvas& scene, const CGameObject* root, const File::Path& path) const;
	EPrefabSerializeResult LoadPrefabFromFile(CGameCanvas& scene, const File::Path& path, CGameObject** outRoot = nullptr) const;
};

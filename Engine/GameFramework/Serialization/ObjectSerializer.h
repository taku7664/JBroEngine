#pragma once

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  ObjectSerializer ─ 오브젝트 1개 ↔ YAML/문자열 (직렬화 3계층의 중간)
//
//  · 오브젝트 노드 = { Name, Active, Layer, InstanceGuid, Transform2D, Components[] }.
//    계층(부모)은 Scene 레벨 관심사이므로 여기서 다루지 않는다(SceneSerializer 담당).
//  · 컴포넌트는 ComponentSerializer 에 위임한다.
//  · 문자열 API는 오브젝트 단위 복사용.
//
//  ⚠ 호스트 전용(yaml-cpp 의존) — 게임 DLL 에 노출하지 않는다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

#include "Core/Asset/AssetTypes.h"

#include <string>
#include <vector>

namespace YAML { class Node; }

class CScene;
class CGameObject;

namespace Serialization
{
	// 오브젝트의 속성/Transform/컴포넌트를 YAML 맵으로 직렬화한다.
	// referencedAssets 가 있으면 컴포넌트가 참조하는 에셋을 누적한다.
	// includeChildren=true 면 자식 서브트리를 Children 키에 재귀 중첩한다(복사용).
	//   씬 직렬화(SceneSerializer)는 계층을 평탄 목록+ParentIndex 로 다루므로 false 를 쓴다.
	YAML::Node WriteObject(const CGameObject& object, std::vector<AssetGuid>* referencedAssets,
	                       bool includeChildren = false);

	// YAML 맵에서 새 오브젝트를 만들어 scene 에 추가하고 속성/Transform/컴포넌트를 복원한다.
	// Children 키가 있으면 자식 서브트리도 만들어 부모로 연결한다(복사 붙여넣기).
	// 최상위 오브젝트의 부모 연결은 호출자가 처리한다(SceneSerializer 는 ParentIndex). 실패 시 nullptr.
	CGameObject* ReadObjectInto(CScene& scene, const YAML::Node& node,
	                            std::vector<AssetGuid>* referencedAssets);

	// ── 오브젝트 복사 API (문자열) ────────────────────────────────────────────
	// 클립보드 포맷은 { Objects: [ objectNode... ] } 맵(다중/단일 공용). 각 오브젝트는
	// 자식 서브트리를 포함한다. 레거시(단일 맵, Components 키)도 역직렬화는 허용한다.
	std::string               SerializeObjects(const std::vector<const CGameObject*>& objects);
	std::vector<CGameObject*> DeserializeObjects(CScene& scene, const char* text);

	// 단일 편의 래퍼(SerializeObjects/DeserializeObjects 위임).
	std::string  SerializeObject(const CGameObject& object);
	CGameObject* DeserializeObject(CScene& scene, const char* text);

	// 텍스트가 복사 오브젝트 직렬화인지 검사한다(붙여넣기 가능 판별). 다중/단일 모두 인식.
	bool         LooksLikeObject(const char* text);
}

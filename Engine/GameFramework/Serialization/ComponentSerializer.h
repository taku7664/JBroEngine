#pragma once

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  ComponentSerializer ─ 컴포넌트 1개 ↔ YAML/문자열 변환 (직렬화 3계층의 최하단)
//
//  · 모든 per-type write/read 헬퍼와 리플렉션/Ref 직렬화 로직을 한곳에 둔다.
//    ObjectSerializer / SceneSerializer 는 이 위에 얇게 쌓인다.
//  · YAML::Node 레벨 API(WriteComponent/ReadComponentInto)는 ObjectSerializer 가
//    오브젝트 노드에 컴포넌트를 박을 때 쓴다. 문자열 API는 단일 컴포넌트 복사용.
//
//  ⚠ 호스트 전용. yaml-cpp 에 의존하므로 게임 스크립트 DLL 에 노출하지 않는다
//    (StageSDK 스테이징 목록에서 Serialization 폴더는 제외).
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

#include "Core/Asset/AssetTypes.h"

#include <string>
#include <vector>

namespace YAML { class Node; }

class CComponent;
class CGameObject;

namespace Serialization
{
	// 컴포넌트 1개를 YAML 맵으로 직렬화한다. 공통 메타({ Type, InstanceGuid, IsEnabled })를
	// 부착하므로 ObjectSerializer 는 결과를 그대로 컴포넌트 시퀀스에 push 하면 된다.
	// referencedAssets 가 있으면 에셋 Ref/AssetGuid 필드를 거기에 누적한다.
	YAML::Node WriteComponent(const CComponent& component, std::vector<AssetGuid>* referencedAssets);

	// YAML 맵의 "Type" 으로 디스패치해 object 에 컴포넌트를 추가하고 필드를 복원한다.
	// InstanceGuid/IsEnabled 도 복원한다. 성공 시 추가된 컴포넌트, 실패 시 nullptr.
	CComponent* ReadComponentInto(CGameObject& object, const YAML::Node& node,
	                              std::vector<AssetGuid>* referencedAssets);

	// ── 단일 컴포넌트 복사 API (문자열) ───────────────────────────────────────
	// 컴포넌트 1개를 독립 문자열로 직렬화/역직렬화한다. 클립보드 복사·붙여넣기용.
	std::string SerializeComponent(const CComponent& component);
	bool        DeserializeComponentInto(CGameObject& object, const char* text);

	// 텍스트가 SerializeComponent 가 만든 단일 컴포넌트 직렬화인지 검사한다(붙여넣기 가능 판별).
	bool        LooksLikeComponent(const char* text);
}

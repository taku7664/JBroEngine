#pragma once

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  LayerSerializer ─ 레이어 ↔ YAML/문자열 (직렬화 계층의 Scene 과 Object 사이)
//
//  · 레이어 노드 = { Name, Guid, Blend, Opacity, Visible, Static, ForceOwnTexture,
//    Parallax } — 컴포짓 속성만. 소속 오브젝트 목록은 담지 않는다(캔버스 파일은 오브젝트를
//    평탄 목록 + LayerIndex 로, 레이어 파일은 Objects 시퀀스로 각자 담는다).
//  · 오브젝트는 ObjectSerializer 에 위임한다.
//  · 캔버스 파일(SceneSerializer)의 Layers 섹션과 레이어 파일(.jlayer)이 이 노드를 공유한다.
//    포맷이 갈리면 같은 레이어가 저장 경로마다 다르게 읽힌다.
//
//  ⚠ 호스트 전용(yaml-cpp 의존) — 게임 DLL 에 노출하지 않는다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

#include "Core/Asset/AssetTypes.h"
#include "GameFramework/Scene/SceneTypes.h"

#include <string>
#include <vector>

namespace YAML { class Node; }

class CGameScene;
class CGameLayer;

namespace Serialization
{
	// ── 노드 API (캔버스 파일의 Layers 섹션 / 레이어 파일이 공유) ────────────────
	YAML::Node WriteLayerNode(const CGameLayer& layer);
	// 노드에서 새 레이어를 만들어 scene 에 추가하고 속성 + InstanceGuid 를 복원한다.
	// 실패 시 nullptr.
	CGameLayer* ReadLayerNodeInto(CGameScene& scene, const YAML::Node& node);

	// ── 레이어 에셋(.jlayer) ──────────────────────────────────────────────────
	// 포맷: { Version, ReferencedAssets, Layer: {레이어 노드}, Objects: [오브젝트 노드...] }
	//   · ReferencedAssets 를 최상위에 두는 건 캔버스/프리팹과 같은 규약이다 — 빌드 수집기가
	//     오브젝트를 만들지 않고 이 키만 읽어 전이 의존을 훑는다. 없으면 그 레이어가 쓰는
	//     에셋이 패키지에서 통째로 빠진다.
	//   · Objects 의 각 항목은 루트 1개 + 자식 서브트리(Children 중첩)다.
	std::string SerializeLayer(const CGameScene& scene, const CGameLayer& layer);

	// 레이어 파일을 scene 에 새 레이어로 추가한다(목록 맨 위 = 컴포짓 최전면).
	// 오브젝트/레이어의 InstanceGuid 는 파일 값 그대로 복원된다 — 재발급하지 않는다.
	//   outLayer != nullptr 이면 만들어진 레이어를 기록한다.
	// 이미 같은 guid 의 오브젝트가 씬에 살아 있으면 DuplicateInstance 로 거부한다(아무것도
	// 만들지 않는다). 같은 레이어 파일을 한 캔버스에 두 번 로드하면 guid 가 겹치는데,
	// 그대로 두면 ObjectSerializer 가 조용히 새 guid 를 발급해 "guid 보존" 계약이 깨진다.
	ELayerSerializeResult DeserializeLayer(CGameScene& scene, const char* text, CGameLayer** outLayer = nullptr);

	bool LooksLikeLayer(const char* text);
}

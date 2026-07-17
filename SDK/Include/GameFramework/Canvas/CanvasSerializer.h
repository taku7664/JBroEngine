#pragma once

#include "Core/Asset/AssetTypes.h"
#include "GameFramework/Canvas/CanvasTypes.h"
#include "Utillity/Pointer/SafePtr.h"

#include <string>
#include <vector>

namespace YAML { class Node; }

class CGameCanvas;
class CGameLayer;

class CCanvasSerializer final
{
public:
	ECanvasSerializeResult SerializeToText(CGameCanvas& scene, std::string& outText) const;
	// 캔버스 내용을 파일 그대로 갈아끼운다 — 살아있는 것은 전부 파괴된다.
	ECanvasSerializeResult DeserializeFromText(CGameCanvas& scene, const char* text) const;

	// 캔버스 전환 — "파괴→생성"이 아니라 이 캔버스에 **diff 를 적용**한다:
	// 승계 레이어 판정 → 비승계 레이어 파괴 → 신규 로드 → 캔버스 속성 교체.
	// 승계 = 수신 캔버스의 레이어 노드가 SourceAsset(= `.jlayer`)을 참조하면서 KeepOnCanvasChange
	// 를 켰고, 같은 에셋에서 온 레이어가 지금 살아 있는 경우(초대장 모델). 그 레이어의
	// 오브젝트·스크립트·SafePtr·Ref 는 무손상으로 넘어오고, 컴포짓 속성과 스택 위치만 수신
	// 캔버스 저작으로 덮인다.
	// 승계 대상이 하나도 없으면 DeserializeFromText 와 결과가 같다(그대로 위임한다).
	//
	// outInheritedLayers = 실제로 승계된 레이어들(중복·nullptr 없음). 호출자가 전환을 끝낸 뒤
	// OnCanvasChanged 를 보낼 대상이다 — 훅은 이름·참조 에셋까지 갱신된 다음에 나가야 해서
	// 여기서 직접 보내지 않고 목록만 돌려준다. 승계 0개면 비어 있다.
	ECanvasSerializeResult TransitionFromText(CGameCanvas& canvas,
	                                         const char* text,
	                                         std::vector<SafePtr<CGameLayer>>& outInheritedLayers) const;

	ECanvasSerializeResult SaveToFile(CGameCanvas& scene, const File::Path& path) const;
	ECanvasSerializeResult LoadFromFile(CGameCanvas& scene, const File::Path& path) const;

	// 캔버스/프리팹 파일에서 ReferencedAssets 목록만 가볍게 읽는다 — 게임오브젝트나 에셋
	// 데이터를 만들지 않고 YAML 의 ReferencedAssets 시퀀스만 파싱한다. 프로젝트 로드 시
	// "이 캔버스를 띄우는 데 필요한 에셋"을 미리 알아내 선택적으로 로드하는 용도.
	std::vector<AssetGuid> ReadReferencedAssetsFromFile(const File::Path& path) const;

private:
	// 전체 교체와 전환이 공유하는 부분들 — 캔버스 포맷을 읽는 코드가 두 벌이 되면 한쪽만
	// 새 필드를 배워 같은 파일이 경로마다 다르게 읽힌다.
	ECanvasSerializeResult ParseCanvasRoot(const char* text, YAML::Node& outRoot) const;
	// 파일의 레이어 노드마다 "승계할 살아있는 인스턴스"를 찾아 대응시킨다(없으면 nullptr).
	// 인덱스 = 노드 순서 = 오브젝트의 LayerIndex 공간. 파괴 전에 부를 것.
	std::vector<CGameLayer*> ResolveInheritedLayers(CGameCanvas& canvas, const YAML::Node& layersNode) const;
	// Layers/Viewports/BackgroundColor/Objects 를 캔버스에 싣는다. 호출 전에 캔버스는 이미 받을
	// 준비가 돼 있어야 한다(전체 교체 = ClearObjects, 전환 = 비승계 파괴 + flush).
	ECanvasSerializeResult ReadCanvasBody(CGameCanvas& scene,
	                                     const YAML::Node& root,
	                                     const std::vector<CGameLayer*>& inherited,
	                                     std::vector<AssetGuid>& outReferencedAssets) const;
};

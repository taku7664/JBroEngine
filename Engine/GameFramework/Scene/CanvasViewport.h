#pragma once

#include "Utillity/File/FilePath.h"
#include "Utillity/Math/Layout2D.h"
#include "Utillity/Pointer/SafePtr.h"

#include <string>
#include <vector>

class CGameObject;

// ─────────────────────────────────────────────────────────────────────────────
//  CanvasViewport — 화면의 창. 캔버스가 목록으로 소유하는 저작 데이터.
//
//  = (카메라 참조 × 출력 렉트 × 레이어 필터). 카메라는 "월드 속의 눈"일 뿐이고
//  "화면 어디에 / 어떤 레이어를" 그릴지는 여기서 정한다.
//  · 보통 게임: 기본 뷰포트 1개(풀스크린, 카메라 미지정=폴백) — 유저는 볼 일이 없다.
//  · 스플릿스크린: 좌/우 렉트 뷰포트 2개 + UI 레이어만 그리는 풀스크린 뷰포트 1개.
//  · 미니맵: 우상단 소형 렉트 + 월드 레이어만.
//
//  카메라 참조가 guid + SafePtr 캐시 둘 다인 이유: guid 는 영속 키(직렬화·레이어 승계
//  후에도 같은 카메라를 지목), SafePtr 는 매 프레임 guid 파싱(File::Guid → 문자열 →
//  Guid128)을 피하는 캐시다. 캐시가 무효면(카메라 파괴/레이어 재로드) 그때만 재해석한다.
// ─────────────────────────────────────────────────────────────────────────────
struct CanvasViewport
{
	std::string Name;

	// 이 뷰포트가 보는 눈. 비어 있으면 폴백(캔버스의 첫 활성 카메라).
	File::Guid CameraObjectGuid;

	// 출력 렉트 — Camera2D 가 갖고 있던 Layout2D(정규화 × 해상도 + 픽셀)를 그대로 승계.
	//   Position 기본 (0,0)     → 좌상단 원점
	//   Size     기본 (1,1)     → 전체 해상도
	Layout2D Position = { Vector2(0.0f, 0.0f), Vector2(0.0f, 0.0f) };
	Layout2D Size     = { Vector2(1.0f, 1.0f), Vector2(0.0f, 0.0f) };

	// 그릴 레이어(캔버스 레이어 InstanceGuid). 비면 전체 레이어 — 대부분의 게임이 이 경우다.
	// 인덱스가 아니라 guid 인 이유: 에디터에서 레이어 순서를 바꿔도 필터가 어긋나지 않는다.
	std::vector<File::Guid> LayerFilter;

	bool Active = true;

	// ── 런타임 캐시(직렬화 안 함) ──────────────────────────────────────────────
	SafePtr<CGameObject> ResolvedCamera;
};

#pragma once

#include "GameFramework/Component/Component.h"

enum class ECameraProjectionMode2D
{
	Orthographic,
	PerspectiveReady
};

// ─────────────────────────────────────────────────────────────────────────────
//  Camera2D — 월드 속의 눈. 그것만.
//
//  "월드 어디를 보는가"만 답한다: 위치·회전은 오너 오브젝트의 트랜스폼, 시야 범위는
//  OrthographicSize. 화면 어디에 그릴지(뷰포트 렉트), 무엇을 그릴지(레이어 필터),
//  어떤 순서로 그릴지는 캔버스의 뷰포트 목록(CanvasViewport)이 정하고, 바탕색은
//  캔버스 속성이다 — 월드의 오브젝트가 화면 배치까지 아는 반쪽 신이 되지 않게.
//
//  카메라가 여러 개 있어도 되고(플레이어 추적·컷신·미니맵 탑다운), 뷰포트가 그중
//  어느 눈을 쓸지 고른다. 소속 레이어는 수명만 정할 뿐 보는 대상과 무관하다.
// ─────────────────────────────────────────────────────────────────────────────
class Camera2D final : public CComponent
{
	JBRO_COMPONENT(Camera2D)
public:
	ECameraProjectionMode2D ProjectionMode = ECameraProjectionMode2D::Orthographic;
	float OrthographicSize = 10.0f;
	float PerspectiveFovDegrees = 60.0f;
	float NearClip = -1.0f;
	float FarClip = 1.0f;
};

#pragma once

#include "Core/Renderer/RendererTypes.h"

#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  RenderCulling — 렌더 아이템 하나가 한 뷰에 보이는가.
//
//  렌더러(드로우 경로)와 GPU 프로파일러(드로우순서 캡처)가 **같은 함수**를 쓴다. 예전에는
//  Forward2DRenderer 와 GameCamera 가 각자 4코너 변환 루프를 들고 있어, 컬링 규칙을 바꾸면
//  프로파일러 표시만 조용히 어긋났다.
// ─────────────────────────────────────────────────────────────────────────────

// 월드 OBB(item.WorldCenter/WorldAxisX/WorldAxisY) 를 뷰 축에 투영해 뷰 사각형과 겹치는지 본다.
// halfW/halfH: 뷰 반폭·반높이(월드 유닛). cosR/sinR: 카메라 회전.
//
// 기존 4코너 판정과 **동일한 결과**다. 코너는 c ± ax ± ay 이고 월드→뷰 변환이 선형이므로
//   maxX = viewC.x + |projX(ax)| + |projX(ay)|,  minX = viewC.x - (같은 값)
// 이 되어, 겹침 조건 !(maxX < -halfW || minX > halfW) 는 |viewC.x| <= halfW + projX 와 같다.
inline bool IsRenderItemVisible(
	const RenderItem& item,
	float camX,
	float camY,
	float halfW,
	float halfH,
	float cosR,
	float sinR)
{
	// 뷰 크기가 아직 정해지지 않은 프레임(초기화 중 등)에는 컬링하지 않는다 — 기존 계약.
	if (halfW <= 0.0f || halfH <= 0.0f)
	{
		return true;
	}

	const float dx = item.WorldCenter[0] - camX;
	const float dy = item.WorldCenter[1] - camY;
	const float viewCx =  cosR * dx + sinR * dy;
	const float viewCy = -sinR * dx + cosR * dy;

	// 반축 2개를 뷰 축에 투영한 절대값의 합 = 뷰 축 기준 반경.
	const float projX = std::abs(cosR * item.WorldAxisX[0] + sinR * item.WorldAxisX[1])
	                  + std::abs(cosR * item.WorldAxisY[0] + sinR * item.WorldAxisY[1]);
	const float projY = std::abs(-sinR * item.WorldAxisX[0] + cosR * item.WorldAxisX[1])
	                  + std::abs(-sinR * item.WorldAxisY[0] + cosR * item.WorldAxisY[1]);

	return std::abs(viewCx) <= halfW + projX
		&& std::abs(viewCy) <= halfH + projY;
}

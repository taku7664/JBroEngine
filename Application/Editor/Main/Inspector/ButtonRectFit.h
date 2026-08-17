#pragma once

#include "Utillity/Math/RectT.h"

class CGameObject;

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  버튼 판정 렉트를 렌더러에 맞추는 **저작 시 1회** 계산.
//
//  런타임 추종이 아니다 — 렉트가 상태별 스프라이트를 따라가면 판정이 발진한다(Button2D.h).
//  여기서 낸 값은 인스펙터 숫자로 남아 눈에 보이고 손으로 고칠 수 있다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
namespace ButtonRectFit
{
	// buttonObject 와 그 자식들의 렌더러 경계 합집합을 **buttonObject 로컬** 로 돌려준다.
	//
	// 건너뛰는 것:
	//   · 자기 Button2D 를 가진 자식 — 그 영역은 그 버튼 소관이다
	//   · 비활성 오브젝트 / 꺼진 렌더러 — 보이는 것이 눌리는 것이다
	//
	// 렌더러가 하나도 없으면 false. 이때 0 렉트를 써 넣으면 **조용히 안 눌리는 버튼**이 된다.
	bool ComputeRendererUnion(const CGameObject& buttonObject, Rect& outLocalBounds);
}

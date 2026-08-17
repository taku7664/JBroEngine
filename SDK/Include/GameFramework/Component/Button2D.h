#pragma once

#include "GameFramework/Component/Component.h"
#include "Utillity/Math/Vector2T.h"

class CReflectionRegistry;

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Button2D ─ 포인터 판정과 상태 훅.
//
//  **판정 렉트는 렌더러를 보지 않는다.** 유니티의 RectTransform 자리다.
//  렌더러 합집합을 런타임에 따라가게 만들면 상태별 스프라이트 교체와 양립하지 않는다 —
//  호버 그림이 조금만 작아도 [호버 ON → 렉트 축소 → 포인터가 밖 → 호버 OFF → 렉트 확대]
//  가 매 프레임 반복한다. 에러가 안 나서 원인을 찾기 어려운 종류다.
//  합집합 자체는 유용하므로 **에디터 액션(1회 복사)** 으로 남긴다.
//
//  판정은 오브젝트 월드 행렬의 역변환 한 번이다 —
//      local = inverse(world) · pointerInLayerSpace
//      hit   = |local - Offset| <= Size/2
//  그래서 회전·스케일·부모 계층이 공짜로 따라온다(회전 버튼은 OBB 가 된다).
//
//  UI 전용이 아니다(월드 레이어 버튼도 지원) — 그래서 이름이 UIButton2D 가 아니다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
class Button2D final : public CComponent
{
	JBRO_COMPONENT(Button2D)
public:
	// 오브젝트 로컬 판정 렉트(유닛). 인스펙터의 "렌더러에 맞추기"가 여기에 값을 써 넣는다.
	Vector2 Size   = Vector2(2.0f, 0.6f);
	Vector2 Offset = Vector2(0.0f, 0.0f);

	// 끄면 포인터가 **통과한다** — 뒤에 있는 버튼이 대신 먹는다(가리는 판이 아니다).
	bool Interactable = true;

	// 런타임 상태. 시스템이 쓰고 스크립트/에디터가 읽는다 — 직렬화 대상이 아니다.
	bool IsHovered() const { return m_hovered; }
	bool IsPressed() const { return m_pressed; }

private:
	friend class CButton2DSystem;

	bool m_hovered = false;
	bool m_pressed = false;
};

#pragma once

#include "Core/Asset/AssetTypes.h"          // AssetGuid — normal 값 래치
#include "Core/Asset/SpriteAsset.h"         // Ref<CSpriteAsset>
#include "GameFramework/Component/Component.h"
#include "GameFramework/Component/SpriteRenderer2D.h"
#include "GameFramework/Object/Ref.h"
#include "Utillity/Math/Vector2T.h"
#include "Utillity/Types/Color.h"

#include <cstdint>

class CReflectionRegistry;

// 시각 전이 방식. None 이 기본인 이유는 소유권 계약(아래 TargetGraphic 주석) 때문이다.
enum class EButtonTransition : std::uint8_t
{
	None,
	ColorTint,
	SpriteSwap,
};

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

	// ── 시각 전이 ─────────────────────────────────────────────────────────────
	// 전이 대상. 유니티의 targetGraphic 자리다 — 판정 렉트와 **무관**하므로 여기서 무엇을
	// 바꾸든 눌리는 영역은 안 흔들린다.
	//
	// Transition != None 이면 버튼이 이 렌더러의 해당 속성을 **소유한다**:
	//   ColorTint  → Color
	//   SpriteSwap → SpriteGuid
	// 소유가 시작될 때 normal 값을 래치하고, 시뮬 정지/전이 해제 시 되돌린다.
	// 기본이 None 인 이유: 매 프레임 남의 필드를 덮어쓰는 컴포넌트는 인스펙터 편집을
	// 조용히 삼킨다. 소유는 명시적으로 켜야 한다.
	Ref<SpriteRenderer2D> TargetGraphic;
	EButtonTransition     Transition = EButtonTransition::None;

	// Normal 칸을 두지 않는다 — **렌더러에 이미 설정된 값이 normal 이다.**
	// 따로 두면 같은 값이 인스펙터 두 곳에 생기고 반드시 어긋난다.
	Color HoverTint    = Color(0.95f, 0.95f, 0.95f, 1.0f);
	Color PressedTint  = Color(0.78f, 0.78f, 0.78f, 1.0f);
	Color DisabledTint = Color(0.50f, 0.50f, 0.50f, 0.5f);

	// 비운 칸은 normal 스프라이트를 그대로 쓴다.
	Ref<CSpriteAsset> HoverSprite;
	Ref<CSpriteAsset> PressedSprite;
	Ref<CSpriteAsset> DisabledSprite;

	// 런타임 상태. 시스템이 쓰고 스크립트/에디터가 읽는다 — 직렬화 대상이 아니다.
	bool IsHovered() const { return m_hovered; }
	bool IsPressed() const { return m_pressed; }

private:
	friend class CButton2DSystem;

	bool m_hovered = false;
	bool m_pressed = false;

	// 소유 시작 시점의 normal 값. 되돌릴 때 쓴다.
	// m_latched 가 곧 "지금 이 버튼이 대상 속성을 쥐고 있다"는 뜻이다.
	bool       m_latched = false;
	Color      m_normalColor;
	AssetGuid  m_normalSpriteGuid;
};

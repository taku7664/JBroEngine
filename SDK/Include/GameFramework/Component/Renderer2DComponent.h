#pragma once

#include "GameFramework/Component/Component.h"
#include "Utillity/Math/RectT.h"
#include "Utillity/Math/Vector2T.h"

#include <cstdint>

class CReflectionRegistry;

// ─────────────────────────────────────────────────────────────────────────────
//  CRenderer2DComponent — 화면에 무언가를 그리는 컴포넌트의 공통 베이스.
//
//  존재 이유는 **타입을 모른 채 경계를 물어볼 수 있게 하는 것**이다.
//  버튼 히트테스트(나 + 자식 렌더러의 합집합), 에디터 프레임 셀렉트, 선택 외곽선,
//  컴포넌트 기준 컬링, 자동 카메라 맞춤이 전부 이 한 가지 질의 위에 얹힌다.
//
//  경계 규약
//   · 단위는 **유닛**(픽셀 아님). 오브젝트 로컬 공간이며 컴포넌트 Offset 이 이미 반영돼 있다.
//   · Rect 의 Top 은 **최소 y**, Bottom 은 **최대 y** 다. 월드는 y-up 이라 이름과 뒤집혀
//     보이지만 CSpriteAsset::LocalBounds 가 이미 쓰고 있는 엔진 공통 규약이며, 이 규약에서
//     Rect 의 Union/Contains/Inflate/GetWidth/GetHeight 가 전부 그대로 맞는다.
//   · **외곽선(OutlineWidth)은 경계에 포함하지 않는다.** 경계는 도형 기하다.
//   · 경계를 낼 수 없으면(자산 미로딩, 텍스트 셰이핑 전) **빈 Rect** 를 반환한다.
//     호출부는 IsEmpty() 로 걸러야 한다 — 0 크기 경계를 "원점의 점"으로 오해하면 안 된다.
//
//  더티 규약
//   · 경계에 영향을 주는 필드는 **세터로만** 바꾼다(세터가 캐시를 무효화한다).
//   · 리플렉션이 필드를 raw 로 덮어쓰는 경로(인스펙터 편집 · undo/redo · 역직렬화)는
//     세터를 타지 않으므로 CRendererComponentAccess::NotifyReflectedWrite() 로 알린다.
//   · 자산/폰트가 있어야 정해지는 경계(스프라이트 · 텍스트)는 해당 렌더 시스템이
//     인스턴스 캐시에 써 준다. 컴포넌트가 자산 매니저나 PPU 전역을 직접 보지 않는다
//     (Runtime 은 호스트 전용이라 게임 DLL 에서는 채워지지 않는다).
// ─────────────────────────────────────────────────────────────────────────────
class CRenderer2DComponent : public CComponent
{
public:
	// 로컬 경계(유닛). 더티일 때만 재계산하고 캐시를 돌려준다.
	const Rect& GetLocalBounds() const;

	// 오너 월드 행렬을 먹인 **축정렬** 경계. 회전이 걸리면 감싸는 상자가 커진다.
	Rect GetWorldBounds() const;

	// 로컬 네 모서리를 월드로 옮긴 값 — 회전을 보존한다(OBB 판정/외곽선용).
	// 순서는 좌하 → 우하 → 우상 → 좌상 (반시계).
	void GetWorldCorners(Vector2 outCorners[4]) const;

	// ── 렌더러블 공통 필드 ───────────────────────────────────────────────────
	// 5종이 전부 같은 의미로 따로 들고 있던 값이라 베이스로 올렸다.

	// 그리는 지점의 로컬 오프셋(유닛, 오브젝트 원점 기준).
	// 경계를 바꾸므로 세터로만 바꾼다.
	const Vector2& GetOffset() const { return m_offset; }
	void SetOffset(const Vector2& offset) { m_offset = offset; MarkBoundsDirty(); }

	// 같은 레이어 안에서의 드로우 순서. 경계와 무관하므로 열어 둔다.
	std::int32_t SortOrder = 0;

	CRenderer2DComponent* AsRenderer2DComponent() override { return this; }

protected:
	CRenderer2DComponent(ComponentConstructionToken token, const SafePtr<CGameObject>& owner)
		: CComponent(token, owner)
	{
	}

	// 파생 타입이 자기 필드로부터 로컬 경계를 만든다. 낼 수 없으면 outBounds 를 건드리지 않는다
	// (호출부가 빈 Rect 로 초기화해서 넘긴다).
	virtual void ComputeLocalBounds(Rect& outBounds) const = 0;

	// 경계에 영향을 주는 필드를 바꾼 세터가 호출한다.
	// protected 인 이유: 파생 렌더러의 세터는 불러야 하지만, 스크립트 사용자가 알 필요는 없다.
	void MarkBoundsDirty() const { m_boundsDirty = true; }

	Vector2 m_offset = Vector2(0.0f, 0.0f);

private:
	// 리플렉션 raw 쓰기 경로가 더티를 알리는 유일한 통로. 접근자 헤더는 SDK 로 나가지 않는다.
	friend class CRendererComponentAccess;
	// 리플렉션 등록만 raw offset 을 본다 — 파생 타입에서 offsetof(X, m_offset) 로 접근한다.
	friend void RegisterBuiltinComponents(CReflectionRegistry&);

	mutable Rect m_localBounds;
	mutable bool m_boundsDirty = true;
};

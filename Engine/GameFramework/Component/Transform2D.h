#pragma once

#include "Utillity/Math/Matrix3x2.h"
#include "Utillity/Math/Vector2T.h"

struct Transform2D
{
	// World 레이어  : 부모 기준 위치(지금까지와 동일).
	// Screen 레이어 : **앵커점으로부터의 오프셋**(유닛). Anchor 가 중앙(기본)이면 화면 좌표 그 자체다.
	Vector2 Position = Vector2(0.0f, 0.0f);
	Radian  RotationRadians = 0.0f;
	Vector2 Scale = Vector2(1.0f, 1.0f);

	// 화면 어디에 붙을 것인가. (0,0)=좌하 (1,1)=우상, 기본 (0.5,0.5)=중앙.
	//
	// **Screen 레이어의 루트 오브젝트에서만 산다.** 자식은 부모 기준 순수 로컬이고
	// (부모 렉트 개념이 없어 자식 앵커는 정의되지 않는다), World 레이어에서는 통째로 무시된다.
	//
	// 이 값은 Position 을 덮어쓰지 않는다 — 앵커점은 CTransformSystem 이 루트의 **부모 행렬**로
	// 넣는다. 그래서 인스펙터·기즈모·스크립트가 Position 을 쓰는 게 그대로 유효하다
	// (별도 컴포넌트가 매 프레임 Position 을 덮어쓰는 설계를 버린 이유가 이것이다).
	Vector2 Anchor = Vector2(0.5f, 0.5f);

	Matrix3x2 ToMatrix3x2() const
	{
		return Matrix3x2::Transform(Position, RotationRadians, Scale);
	}
};


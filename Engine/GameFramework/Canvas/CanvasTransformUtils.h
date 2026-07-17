#pragma once

// World 변환은 CGameObject 의 World 멤버(CTransformSystem 이 매 프레임 캐싱)에서 읽는다.
// 물리(FixedUpdate)는 캐시가 stale 일 수 있어 자체 on-demand 계산을 쓴다 — 이 헬퍼는
// Update 단계 소비자(렌더/카메라/에디터)용이다.

#include "GameFramework/Object/GameObject.h"
#include "Utillity/Math/Matrix3x2.h"

inline Matrix3x2 GetWorldTransform(const CGameObject& object)
{
	return object.GetWorld().Matrix;
}

#pragma once

// 월드 변환을 얻는 **두 가지** 방법. 이름이 곧 신선도 계약이다.
//
//   GetWorldTransform(o)        — 이번 프레임 확정값. CTransformSystem 이 전파해 둔 캐시를 읽는다.
//   ComputeWorldTransformNow(o) — 지금 이 순간 값. 로컬 트랜스폼에서 즉석 계산한다.
//
// 언제 무엇을 쓰나:
//   · 렌더 수집·카메라·에디터처럼 **시스템 단계**에서 도는 코드 → GetWorldTransform.
//     이 값은 프레임 내내 변하지 않으므로, 읽는 순서에 따라 결과가 달라지지 않는다.
//   · 스크립트 Update 나 물리 FixedUpdate 처럼 **전파 이전/사이**에 도는 코드가 방금 바꾼
//     위치를 즉시 반영해야 할 때 → ComputeWorldTransformNow.
//
// ⚠ 예전에는 이 헤더에 GetWorldTransform 하나뿐이었고, Canvas::Update 주석이 "신선한 값이
//   필요하면 이걸 쓰라"고 안내했다. 그런데 이 함수는 같은 캐시를 그대로 돌려줄 뿐이라 그
//   안내는 지켜지지 않았다. 실제로 즉석 계산을 하는 코드는 Physics2DSystem.cpp 안에만 있어
//   스크립트가 쓸 수 없었다. 그 구현을 여기로 올려 둘을 한 곳에서 고르게 만든 것이다.

#include "Utillity/Math/Matrix3x2.h"

class CGameObject;

// 이번 프레임 확정값(CTransformSystem 산출물). 정의는 .cpp — CGameObject 완전형이 필요하다.
Matrix3x2 GetWorldTransform(const CGameObject& object);

// 지금 이 순간 값. 부모 사슬을 걸어 올라가며 로컬 트랜스폼을 곱하고, 루트에서는
// CTransformSystem 과 **같은** 부모 행렬(화면 공간 레이어의 앵커 평행이동)을 함께 태운다.
// 그래서 두 함수는 전파가 끝난 시점에 같은 답을 낸다 — 앵커를 빠뜨리면 화면 공간 레이어에서
// 물리와 렌더가 서로 다른 위치를 믿게 된다.
Matrix3x2 ComputeWorldTransformNow(const CGameObject& object);

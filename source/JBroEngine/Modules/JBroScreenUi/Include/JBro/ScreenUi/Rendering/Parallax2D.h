#pragma once

#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Math2D.h>

namespace JBro
{
    // **패럴랙스 레이어가 월드에서 옮겨지는 양이다**(D-286, 기존 `ApplyLayerSpace`). 기존 엔진은 그 레이어의 뷰에서 카메라 위치만 계수배 했는데,
    // 그것은 그 레이어의 것을 월드에서 `카메라 위치 x (1 - 계수)` 만큼 옮겨 그린 것과 같다 - 회전·줌은 그대로다. 그리기·버튼의 역투영·에디터의
    // 레이어 공간 바꾸기가 이 하나를 쓴다. `view` 는 월드 → 뷰이고, 뒤집을 수 없으면 거짓이다. 화면 UI 모듈(D-362)에 두어 2D·3D 가 같이 쓴다.
    Bool ComputeParallaxOffset2D(const Matrix3x2& view, Float factor, Float& dx, Float& dy);
}

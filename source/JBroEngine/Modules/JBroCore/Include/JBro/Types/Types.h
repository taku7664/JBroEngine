#pragma once

// 프로젝트에서 공통으로 사용하는 JBro 값타입들을 한 번에 가져오는 프렐류드.
// 스칼라·각·수학·크기·컨테이너·문자열을 여기서 묶는다.
//
// **여기에 있는 것이 곧 "공용 값 타입" 이다.** 수학 타입을 Core 로 올리고도(D-241) 이 목록을
// 고치지 않아서, 한동안 이 헤더만 넣어서는 `Vector2` 도 `Degree` 도 쓸 수 없었다. Core 에 값 타입을
// 더하면 이 목록에도 더한다(D-249).
//
// 여기에 넣지 않는 것: `Simd128.h` 는 플랫폼 내장 함수를 끌고 오므로 쓰는 쪽이 직접 넣는다.
// `TextOptions.h` 는 리플렉션 계층에 기대는 텍스트 전용 설정이라 값 타입이 아니다.

#include <JBro/Types/Allocator.h>
#include <JBro/Types/Angle.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/BitFlag.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Delegate.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/FrameLiveness.h>
#include <JBro/Types/Hash.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/IntegerType.h>
#include <JBro/Types/LinearAllocator.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/Math3D.h>
#include <JBro/Types/Matrix4x4.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Types/SafeArea.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/Size.h>
#include <JBro/Types/String.h>
#include <JBro/Types/Table.h>
#include <JBro/Types/TypeTraits.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/Uuid.h>

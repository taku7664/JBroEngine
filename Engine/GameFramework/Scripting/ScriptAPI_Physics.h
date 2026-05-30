#pragma once

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  ScriptAPI_Physics.h ─ Physics2D 카테고리 umbrella
//
//  스크립트에서 Rigidbody2D / 콜라이더 / 충돌 콜백을 만질 때 추가 include.
//
//    #include "GameFramework/Scripting/ScriptAPI.h"
//    #include "GameFramework/Scripting/ScriptAPI_Physics.h"
//
//  ScriptAPI.h 만으로 게임을 만들 수도 있지만, 물리 컴포넌트를 자주 만지는
//  스크립트라면 이 헤더 한 줄을 더하는 게 편리하다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Physics2D/Physics2DTypes.h"

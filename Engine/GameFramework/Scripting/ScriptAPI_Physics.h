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
// 질의 API + RaycastHit2D.
//   레이  : Raycast(최근접) / RaycastAll(경로상 전부, 거리순)
//   오버랩: OverlapPoint / OverlapCircle / OverlapBox
//   스윕  : CircleCast / BoxCast — 폭을 가진 이동 판정(캐릭터 지면·벽 체크)
// 스크립트에서 GetCanvas()->GetPhysics2DSystem()->Raycast(...) 로 사용.
#include "GameFramework/Physics2D/Physics2DSystem.h"

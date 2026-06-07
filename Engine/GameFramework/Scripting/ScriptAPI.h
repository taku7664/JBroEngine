#pragma once

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  ScriptAPI.h  ─  스크립트 작성자를 위한 단일 진입점 헤더
//
//  스크립트 파일에서 아래 한 줄만 include 하면 모든 엔진 API 가 사용 가능합니다.
//
//    #include "GameFramework/Scripting/ScriptAPI.h"
//
//  제공 내용:
//    - CGameScript 기반 클래스 (상속 대상)
//    - SCRIPT_CLASS / REFLECT_FIELD 매크로
//    - Vector2 REFLECT_FIELD 지원
//    - 씬/오브젝트 접근 (GetScene(), GetGameObject(), GetComponent<T>())
//    - 자주 사용하는 컴포넌트 타입
//    - 입력 시스템
//    - 수학 유틸리티
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// ── 기반 클래스 + 매크로 ──────────────────────────────────────────────────────
#include "GameFramework/Scripting/GameScript.h"
#include "GameFramework/Scripting/ScriptMacros.h"

// ── 수학 ────────────────────────────────────────────────────────────────────
#include "Utillity/Math/Vector2T.h"
#include "Utillity/Math/RectT.h"
#include "Utillity/Types/EngineTypes.h"

// Vector2 → REFLECT_FIELD 지원 (ScriptMacros.h 가 먼저 정의된 이후)
template<> inline EReflectPropertyType ScriptFieldTypeOf<Vector2>()
{
	return EReflectPropertyType::Vector2Float;
}
template<> inline EReflectPropertyType ScriptFieldTypeOf<Rect>()
{
	return EReflectPropertyType::RectFloat;
}
template<> inline EReflectPropertyType ScriptFieldTypeOf<Bool>()
{
	return EReflectPropertyType::Bool;
}
template<> inline EReflectPropertyType ScriptFieldTypeOf<Int>()
{
	return EReflectPropertyType::Int64;
}
template<> inline EReflectPropertyType ScriptFieldTypeOf<UInt>()
{
	return EReflectPropertyType::UInt64;
}
template<> inline EReflectPropertyType ScriptFieldTypeOf<Float>()
{
	return EReflectPropertyType::Float;
}
template<> inline EReflectPropertyType ScriptFieldTypeOf<Degree>()
{
	return EReflectPropertyType::Degree;
}
template<> inline EReflectPropertyType ScriptFieldTypeOf<Radian>()
{
	return EReflectPropertyType::Radian;
}
template<> inline EReflectPropertyType ScriptFieldTypeOf<String>()
{
	return EReflectPropertyType::String;
}
template<> inline EReflectPropertyType ScriptFieldTypeOf<Asset>()
{
	return EReflectPropertyType::AssetGuid;
}
template<> inline EReflectPropertyType ScriptFieldTypeOf<File::Guid>()
{
	return EReflectPropertyType::AssetGuid;
}

// ── 씬 / 오브젝트 ────────────────────────────────────────────────────────────
#include "GameFramework/Scene/Scene.h"
#include "GameFramework/Object/GameObject.h"

// 스크립트 작성자용 별칭: 실체 타입은 CGameObject 지만 스크립트에선 GameObject 로 쓴다.
//   JPROP() Ref<GameObject> Target;  /  GetGameObject() 반환형 등.
using GameObject = CGameObject;

// ── 자주 사용하는 컴포넌트 ────────────────────────────────────────────────────
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Component/SpriteRenderer2D.h"
#include "GameFramework/Component/Camera2D.h"
// Physics2D 관련 컴포넌트(Rigidbody2D / 콜라이더)는 ScriptAPI_Physics.h 로 분리.

// ── 참조: Ref<T> (오브젝트/컴포넌트/스크립트/에셋) ───────────────────────────
//   JPROP() Ref<GameObject> Target;  처럼 선언하면 인스펙터에서 드래그-드랍으로 지정.
#include "GameFramework/Object/Ref.h"

// ── 자산 ────────────────────────────────────────────────────────────────────
#include "Core/Asset/AssetTypes.h"

// ── 입력 ────────────────────────────────────────────────────────────────────
#include "Core/Input/Input.h"          // Script.Input facade (전역설정/진동/연결조회)
#include "Core/Input/IInputHandler.h"  // IInputHandler + InputHandler<Layer,Order> (핸들러 상속용)
// InputDeviceContext / Keyboard / Mouse / Gamepad / EKeyCode 등은 Input.h 가 끌어오는
// InputDevices.h 에 정의됨.

// ── 로깅 (legacy, Log::Info 등 직접 호출용) ────────────────────────────────
#include "Core/Logging/Logger.h"

// ── Engine core service bundle — Engine.Debug / Engine.SceneManager 등 ──────
// GameScript DLL 은 Initialize 시점에 호스트 ScriptCore 를 복사받아 사용합니다.
#include "Core/ScriptCore.h"
#include "Core/Debug/Debug.h"
#include "Core/Time/Time.h"

// ── 표준 라이브러리 (스크립트에서 매우 자주 쓰임) ─────────────────────────
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

#pragma once

#include "Core/Input/InputTypes.h"
#include "Utillity/Types/Bool.h"
#include "Utillity/Math/Vector2T.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  InputAction — 이름 기반 액션 매핑.
//
//  · 핸들러는 디바이스 대신 액션을 읽는다: ctx.GetAction().GetValue<Vector2>("Move").
//  · 평가된 값(ActionState)은 POD 고정배열 → 호스트↔게임 DLL 경계 안전(GetValue 는 헤더 인라인).
//  · 저작(InputActionDef/InputBinding)은 호스트 전용(STL). 프로젝트세팅 .Jproject 에 직렬화.
//  · 평가는 CInputSystem 이 매 프레임 수행(바인딩 + 디바이스 상태 → 값).
// ─────────────────────────────────────────────────────────────────────────────

// 액션 값 타입. (엔진 Bool/Vector2 POD 사용. Float 는 트리거/1D.)
enum class EInputActionValueType : std::uint8_t
{
	Bool,
	Float,
	Vector2,
};

// ── 평가된 액션 값 (POD, ctx 스토어 한 칸) ──────────────────────────────────────
// 모든 값은 Vector2 에 담는다: Bool→(x!=0), Float→x, Vector2→(x,y). 타입태그로 의미 구분.
struct InputActionValue
{
	char                  Name[64] = {};
	EInputActionValueType Type     = EInputActionValueType::Bool;
	Vector2               Value    = {};
};

// ── ActionState — 핸들러가 읽는 액션 스토어 (POD 고정배열) ───────────────────────
// GetValue<T> 는 헤더 인라인이라 DLL 에서도 안전(경계 넘는 함수 호출 없음, POD 읽기뿐).
class ActionState
{
public:
	static constexpr std::size_t MaxActions = 64;

	// 지원 쿼리 타입: Vector2 / Bool / float. (그 외는 링크 에러로 차단.)
	template<typename T>
	T GetValue(const char* name) const;

private:
	const InputActionValue* Find(const char* name) const
	{
		if (nullptr == name)
		{
			return nullptr;
		}
		for (int i = 0; i < m_count; ++i)
		{
			if (0 == std::strcmp(m_values[i].Name, name))
			{
				return &m_values[i];
			}
		}
		return nullptr;
	}

private:
	InputActionValue m_values[MaxActions] = {};
	int              m_count = 0;

	friend class CInputSystem;
};

// ── GetValue 특수화 (강제변환 규칙 포함) ────────────────────────────────────────
// Vector2 쿼리: 저장이 Vector2 일 때만 유효(Bool→Vector2 금지 → Zero).
template<>
inline Vector2 ActionState::GetValue<Vector2>(const char* name) const
{
	const InputActionValue* value = Find(name);
	if (nullptr == value || EInputActionValueType::Vector2 != value->Type)
	{
		return Vector2{};
	}
	return value->Value;
}

// Bool 쿼리: 어떤 저장 타입이든 nonzero → true (Vector2 는 Zero 아니면 true).
template<>
inline Bool ActionState::GetValue<Bool>(const char* name) const
{
	const InputActionValue* value = Find(name);
	if (nullptr == value)
	{
		return Bool(false);
	}
	return Bool(value->Value.x != 0.0f || value->Value.y != 0.0f);
}

// float 쿼리: Float/Bool 은 x, Vector2 도 x(첫 축). 없으면 0.
template<>
inline float ActionState::GetValue<float>(const char* name) const
{
	const InputActionValue* value = Find(name);
	return (nullptr != value) ? value->Value.x : 0.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
//  저작 타입 (호스트 전용 — STL). 프로젝트세팅 직렬화 + CInputSystem::SetInputMap 입력.
// ─────────────────────────────────────────────────────────────────────────────

// 바인딩 소스 종류.
enum class EInputBindingSource : std::uint8_t
{
	Key,            // 키보드 키(EKeyCode)
	MouseButton,    // 마우스 버튼(EMouseButton)
	GamepadButton,  // 게임패드 버튼(EGamepadButton)
	GamepadAxis,    // 게임패드 단일 축(EGamepadAxis) — Float 용
	GamepadStick,   // 게임패드 스틱(0=Left,1=Right) — Vector2 직결
};

// Vector2 합성 역할(키 컴포지트). 스틱/단일바인딩은 None.
enum class EInputComposite : std::uint8_t
{
	None,
	Up,
	Down,
	Left,
	Right,
};

// 한 액션의 입력 소스 하나.
struct InputBinding
{
	EInputBindingSource Source       = EInputBindingSource::Key;
	int                 Code         = 0;   // 소스별 enum 값(EKeyCode/EMouseButton/EGamepadButton/EGamepadAxis/스틱index)
	int                 GamepadIndex = -1;  // -1 = 아무 패드나(연결된 첫 패드)
	EInputComposite     Composite    = EInputComposite::None; // Vector2 키 합성 역할
};

// 액션 정의(이름 + 값타입 + 바인딩 목록).
struct InputActionDef
{
	std::string               Name;
	EInputActionValueType     Type = EInputActionValueType::Bool;
	std::vector<InputBinding> Bindings;
};

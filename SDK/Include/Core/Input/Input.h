#pragma once

#include "Core/Input/InputDevices.h" // EInputDevice
#include "Utillity/Pointer/SafePtr.h"

#include <cstddef>

class CInputSystem;
class IInputHandler;

// ─────────────────────────────────────────────────────────────────────────────
//  CInput — 스크립트 공개 입력 facade.
//
//  · 디바이스 직접 접근 불가. 입력은 핸들러(InputHandler<Layer,Order> 상속)로만 받는다.
//  · 스크립트 공개 표면 = 전역 설정(SetDeviceEnabled) + 게임패드 출력/조회 뿐.
//  · **핸들러 등록/해제는 facade 가 하지 않는다.** 등록은 InputSystem 의 역할이며, 엔진이
//    ScriptComponent 수명에서 Engine.InputSystem 으로 직접 호출한다(사용자/Input 미개입).
//  · 실제 갱신/디스패치는 엔진 내부 CInputSystem 이 수행한다. 본 facade 는 그쪽으로 위임만 한다.
//    (CInput 메서드는 Engine.lib 에 있어 호스트/게임 DLL 양쪽에서 링크된다 → Script.Input 으로
//     호출해도 안전. CInputSystem 자체는 ScriptCore 에 노출하지 않는다.)
// ─────────────────────────────────────────────────────────────────────────────
class CInput final : public EnableSafeFromThis<CInput>
{
public:
	CInput() = default;
	~CInput() = default;
	CInput(const CInput&) = delete;
	CInput& operator=(const CInput&) = delete;

	// 호스트가 1회 연결(엔진 내부 시스템). 스크립트는 호출하지 않는다.
	void BindSystem(CInputSystem* system) { m_system = system; }

	// 전역 설정 — 스크립트 공개. 특정 디바이스 입력을 무시한다.
	void SetDeviceEnabled(EInputDevice device, bool enabled);
	bool IsDeviceEnabled(EInputDevice device) const;

	// ── 게임패드 (스크립트 공개) ──────────────────────────────────────────────
	// 입력 상태는 핸들러 ctx.GetGamepad(i) 로 읽고, 출력/설정/연결조회는 여기로.
	int  GetConnectedGamepadCount() const;
	bool IsGamepadConnected(std::size_t index) const;

	// 진동(rumble) — left/right 모터 0..1.
	// durationSeconds > 0 이면 워커 스레드 타이머가 시간 후 자동 정지(메인 행 걸려도 동작 — 무한진동 방지).
	// 0 이면 수동 정지/포커스 상실까지 지속.
	void SetGamepadVibration(std::size_t index, float leftMotor, float rightMotor, float durationSeconds = 0.0f);
	void StopGamepadVibration(std::size_t index);

	// 감도 — 전 패드 공통(정규화 0..1).
	void SetStickDeadzone(float deadzone);
	void SetTriggerThreshold(float threshold);

private:
	CInputSystem* m_system = nullptr;
};

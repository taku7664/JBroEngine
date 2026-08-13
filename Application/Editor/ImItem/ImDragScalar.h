#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "ThirdParty/imgui/imgui.h"

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  ImDragInt / ImDragFloat ─ 드래그로 굵게 잡고 -/+ 로 한 칸씩 맞추는 숫자 입력
//
//  왜 필요한가: ImGui::InputInt 는 값을 바꾸려면 타이핑하거나 -/+ 를 여러 번 눌러야 한다.
//  셀 개수·여백·간격처럼 "적당한 값을 찾아 가는" 수치에는 드래그가 훨씬 빠르다.
//  반대로 드래그만 있으면 정확히 1 을 더하기가 어렵다 — 그래서 둘을 붙였다.
//
//  범위가 **열린** 값(개수·픽셀 크기·거리)에만 쓴다. 0~1 정규화 값은 프로젝트 규칙대로
//  SliderFloat 을 쓴다 — 그쪽은 범위 자체가 작업 영역이라 현재 위치가 보여야 한다.
//
//  Min/Max 는 "말도 안 되는 값 방지"용 클램프다. 드래그는 AlwaysClamp 로 그 안에 갇힌다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class ImDragInt
{
public:
	explicit ImDragInt(const char* id);

	ImDragInt& Range(int minValue, int maxValue);
	ImDragInt& Speed(float unitsPerPixel);      // 드래그 1픽셀당 증가량
	ImDragInt& Step(int step);                  // -/+ 한 번당 증가량
	ImDragInt& StepButtons(bool show);          // 끄면 드래그만
	ImDragInt& Format(const char* format);
	ImDragInt& Width(float width);              // 0 = 남은 폭

	bool Draw(int& value) const;
	bool operator()(int& value) const { return Draw(value); }

private:
	const char* m_id = nullptr;
	const char* m_format = "%d";
	int   m_min = 0;
	int   m_max = 0;
	int   m_step = 1;
	float m_speed = 0.25f;
	float m_width = 0.0f;
	bool  m_stepButtons = true;
};

class ImDragFloat
{
public:
	explicit ImDragFloat(const char* id);

	ImDragFloat& Range(float minValue, float maxValue);
	ImDragFloat& Speed(float unitsPerPixel);
	ImDragFloat& Step(float step);
	ImDragFloat& StepButtons(bool show);
	ImDragFloat& Format(const char* format);
	ImDragFloat& Width(float width);

	bool Draw(float& value) const;
	bool operator()(float& value) const { return Draw(value); }

private:
	const char* m_id = nullptr;
	const char* m_format = "%.2f";
	float m_min = 0.0f;
	float m_max = 0.0f;
	float m_step = 1.0f;
	float m_speed = 0.5f;
	float m_width = 0.0f;
	bool  m_stepButtons = true;
};

#endif

#include "pch.h"
#include "ImDragScalar.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include <algorithm>

namespace
{
	// 드래그 칸과 -/+ 두 개를 한 줄에 넣는다. 버튼은 InputInt 처럼 정사각형이다.
	// 반환값은 드래그 칸에 쓸 폭.
	float SplitWidthForStepButtons(float totalWidth, bool stepButtons, float& outButtonSize, float& outSpacing)
	{
		outSpacing = ImGui::GetStyle().ItemInnerSpacing.x;
		outButtonSize = stepButtons ? ImGui::GetFrameHeight() : 0.0f;

		// 폭을 안 주면 **줄의 남은 폭**을 다 쓴다.
		// CalcItemWidth() 를 쓰면 안 된다 — FormLayout 은 SetNextItemWidth(-FLT_MIN) 로 칸을
		// 잡는데, 우리가 SetNextItemWidth 를 다시 부르는 순간 그 지시가 덮여서 CalcItemWidth 는
		// 기본 폭을 돌려준다. 그러면 드래그 칸이 좁아지고 -/+ 가 칸 밖으로 밀린다.
		const float total = totalWidth > 0.0f ? totalWidth : ImGui::GetContentRegionAvail().x;
		const float consumed = stepButtons ? (outButtonSize + outSpacing) * 2.0f : 0.0f;
		return std::max(total - consumed, 1.0f);
	}

	// 누르고 있으면 계속 증가하도록 — InputInt 의 -/+ 와 같은 감각.
	bool StepButton(const char* label, float size)
	{
		ImGui::PushButtonRepeat(true);
		const bool pressed = ImGui::Button(label, ImVec2(size, size));
		ImGui::PopButtonRepeat();
		return pressed;
	}
}

// ── ImDragInt ────────────────────────────────────────────────────────────────

ImDragInt::ImDragInt(const char* id)
	: m_id(id)
{
}

ImDragInt& ImDragInt::Range(int minValue, int maxValue) { m_min = minValue; m_max = maxValue; return *this; }
ImDragInt& ImDragInt::Speed(float unitsPerPixel)       { m_speed = unitsPerPixel; return *this; }
ImDragInt& ImDragInt::Step(int step)                   { m_step = step; return *this; }
ImDragInt& ImDragInt::StepButtons(bool show)           { m_stepButtons = show; return *this; }
ImDragInt& ImDragInt::Format(const char* format)       { m_format = format; return *this; }
ImDragInt& ImDragInt::Width(float width)               { m_width = width; return *this; }

bool ImDragInt::Draw(int& value) const
{
	ImGui::PushID(m_id);

	float buttonSize = 0.0f;
	float spacing = 0.0f;
	const float dragWidth = SplitWidthForStepButtons(m_width, m_stepButtons, buttonSize, spacing);

	ImGui::SetNextItemWidth(dragWidth);
	bool changed = ImGui::DragInt("##drag", &value, m_speed, m_min, m_max, m_format, ImGuiSliderFlags_AlwaysClamp);

	if (m_stepButtons)
	{
		ImGui::SameLine(0.0f, spacing);
		if (StepButton("-", buttonSize))
		{
			value -= m_step;
			changed = true;
		}
		ImGui::SameLine(0.0f, spacing);
		if (StepButton("+", buttonSize))
		{
			value += m_step;
			changed = true;
		}
		// 버튼은 DragInt 의 AlwaysClamp 를 안 타므로 여기서 직접 가둔다.
		if (m_min < m_max)
		{
			value = std::clamp(value, m_min, m_max);
		}
	}

	ImGui::PopID();
	return changed;
}

// ── ImDragFloat ──────────────────────────────────────────────────────────────

ImDragFloat::ImDragFloat(const char* id)
	: m_id(id)
{
}

ImDragFloat& ImDragFloat::Range(float minValue, float maxValue) { m_min = minValue; m_max = maxValue; return *this; }
ImDragFloat& ImDragFloat::Speed(float unitsPerPixel)           { m_speed = unitsPerPixel; return *this; }
ImDragFloat& ImDragFloat::Step(float step)                     { m_step = step; return *this; }
ImDragFloat& ImDragFloat::StepButtons(bool show)               { m_stepButtons = show; return *this; }
ImDragFloat& ImDragFloat::Format(const char* format)           { m_format = format; return *this; }
ImDragFloat& ImDragFloat::Width(float width)                   { m_width = width; return *this; }

bool ImDragFloat::Draw(float& value) const
{
	ImGui::PushID(m_id);

	float buttonSize = 0.0f;
	float spacing = 0.0f;
	const float dragWidth = SplitWidthForStepButtons(m_width, m_stepButtons, buttonSize, spacing);

	ImGui::SetNextItemWidth(dragWidth);
	bool changed = ImGui::DragFloat("##drag", &value, m_speed, m_min, m_max, m_format, ImGuiSliderFlags_AlwaysClamp);

	if (m_stepButtons)
	{
		ImGui::SameLine(0.0f, spacing);
		if (StepButton("-", buttonSize))
		{
			value -= m_step;
			changed = true;
		}
		ImGui::SameLine(0.0f, spacing);
		if (StepButton("+", buttonSize))
		{
			value += m_step;
			changed = true;
		}
		if (m_min < m_max)
		{
			value = std::clamp(value, m_min, m_max);
		}
	}

	ImGui::PopID();
	return changed;
}

#endif

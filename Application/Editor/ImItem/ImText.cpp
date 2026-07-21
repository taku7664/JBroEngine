#include "pch.h"
#include "ImText.h"
#include "Editor/ImGuiUtillity.h"
#include <algorithm>

ImText::ImText()
{
}

ImText::~ImText()
{
}

ImText& ImText::SetAlign(Align align)
{
	m_align = align;
	return *this;
}

ImText& ImText::SetScale(float scale)
{
	m_scale = scale;
	return *this;
}

ImText& ImText::SetHoveredTooltip(bool use, ImGuiHoveredFlags flags)
{
	m_hovered = { use, flags };
	m_hoveredCustomTooltip.clear();
	return *this;
}

ImText& ImText::SetHoveredTooltip(const char* tooltipText, ImGuiHoveredFlags flags)
{
	if (nullptr == tooltipText || '\0' == tooltipText[0])
	{
		m_hovered = { false, flags };
		m_hoveredCustomTooltip.clear();
	}
	else
	{
		m_hovered = { true, flags };
		m_hoveredCustomTooltip = tooltipText;
	}
	return *this;
}

ImText& ImText::UseSeparator(bool use)
{
	m_bUseSeparator = use;
	return *this;
}

void ImText::operator()(const char* text)
{
    // Always render text. If scale <= 0, fallback to 1.0f
    const float scale = (m_scale > 0.0f) ? m_scale : 1.0f;

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    float old = window->FontWindowScale;
    ImGui::SetWindowFontScale(scale);

    // Use content region width so alignment respects padding/scrolling
    float regionWidth = ImGui::GetContentRegionAvail().x;
    float textWidth = ImGui::CalcTextSize(text).x * scale;
    float weight = 0.0f;
    ImVec2 spacing = ImVec2(0, 0);

    switch (m_align)
    {
    case Align::Left:
        weight = 0.0f;
        spacing = ImGui::GetStyle().ItemSpacing;
        break;
    case Align::Right:
        weight = 1.0f;
        spacing = -ImGui::GetStyle().ItemSpacing;
        break;
    case Align::Center:
        weight = 0.5f;
        break;
    default:
        break;
    }

    // Compute cursor X based on available region
    const float availX = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(availX + spacing.x + (regionWidth - textWidth) * weight);

    if (m_bUseSeparator)
    {
        ImGui::SeparatorText(text);
    }
    else
    {
        ImGui::TextUnformatted(text);
    }

    // Tooltip when hovered — 커스텀 설명이 있으면 그것을, 없으면 라벨 텍스트 그대로.
    if (m_hovered.first && ImGui::IsItemHovered(m_hovered.second))
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(m_hoveredCustomTooltip.empty() ? text : m_hoveredCustomTooltip.c_str());
        ImGui::EndTooltip();
    }

    ImGui::SetWindowFontScale(old);
}

void ImInputText::SetLabel(const char* label)
{
	m_label = label;
}

ImInputText& ImInputText::SetMaxLength(size_t maxLength)
{
    m_maxLength = maxLength;
    if (m_buffer.size() > m_maxLength)
    {
        m_buffer.resize(m_maxLength);
    }
    m_buffer.reserve(std::max(m_buffer.size(), m_maxLength));
    return *this;
}

ImInputText& ImInputText::SetHintText(const char* hintStr)
{
    m_hint = hintStr;
    return *this;
}

const char* ImInputText::GetBuffer() const
{
    return m_buffer.c_str();
}

bool ImInputText::operator()(ImGuiInputTextFlags flags, bool invalid)
{
    ImGui::Utillity::InvalidFieldScope invalidScope(invalid);
    ImGui::PushID(this);
    // 원본 값 동기화 — 편집 중이 아닐 때만 버퍼를 갱신한다. InputText 가 쓸 ID 를 미리 구해
    // 활성 여부를 본다(PushID 이후라 같은 ID 스택 → 동일 ID).
    if (m_hasSource && ImGui::GetActiveID() != ImGui::GetID(m_label.c_str()))
    {
        m_buffer = m_source;
    }
    const bool changed = (m_hint.empty())
        ? ImGui::InputText(m_label.c_str(), &m_buffer, flags)
        : ImGui::InputTextWithHint(m_label.c_str(), m_hint.c_str(), &m_buffer, flags);

    // enforce max length after editing
    if (m_maxLength != ULLONG_MAX && m_buffer.size() > m_maxLength)
    {
        m_buffer.resize(m_maxLength);
    }
    ImGui::PopID();
    return changed;
}

ImInputText::ImInputText(const char* label)
    : m_label(label)
{
}

ImInputText::operator const char* ()
{
    return GetBuffer();
}

ImInputText& ImInputText::SetText(const std::string& text)
{
    m_buffer = text;
    if (m_buffer.size() > m_maxLength)
        m_buffer.resize(m_maxLength);
    return *this;
}

ImInputText& ImInputText::SetSourceText(const std::string& text)
{
    m_source = text;
    if (m_source.size() > m_maxLength)
        m_source.resize(m_maxLength);
    m_hasSource = true;
    // 편집 중이면 operator() 가 무시한다 — 여기서는 원본만 기록한다.
    m_buffer = m_source;
    return *this;
}

void ImInputText::Clear()
{
    m_buffer.clear();
}

const std::string& ImInputText::GetString() const
{
    return m_buffer;
}

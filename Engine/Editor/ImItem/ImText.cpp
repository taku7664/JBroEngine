#include "pch.h"
#include "ImText.h"
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
    if (invalid)
    {
        const ImVec4 red(0.85f, 0.20f, 0.20f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, red);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
    }
    ImGui::PushID(this);
    const bool changed = (m_hint.empty())
        ? ImGui::InputText("##iminputtext", &m_buffer, flags)
        : ImGui::InputTextWithHint("##iminputtext", m_hint.c_str(), &m_buffer, flags);

    // enforce max length after editing
    if (m_maxLength != ULLONG_MAX && m_buffer.size() > m_maxLength)
    {
        m_buffer.resize(m_maxLength);
    }
    if (invalid)
    {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    ImGui::PopID();
    return changed;
}

ImInputText::ImInputText(size_t maxLength)
    : m_maxLength(maxLength)
{
    if (m_maxLength != ULLONG_MAX)
        m_buffer.reserve(m_maxLength);
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

void ImInputText::Clear()
{
    m_buffer.clear();
}

const std::string& ImInputText::GetString() const
{
    return m_buffer;
}

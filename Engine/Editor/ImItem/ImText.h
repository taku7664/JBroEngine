#pragma once

#include <climits>     // ULLONG_MAX
#include <string>
#include <utility>

#include "ThirdParty/imgui/imgui.h"   // ImGuiHoveredFlags, ImGuiInputTextFlags

class ImText
{
public:
	enum class Align
	{
		Left,
		Center,
		Right,
	};
public:
	ImText();
	~ImText();

	ImText& SetAlign(Align align);
	ImText& SetScale(float scale);
	ImText& SetHoveredTooltip(bool use, ImGuiHoveredFlags flags = ImGuiHoveredFlags_None);
	// 라벨과 다른 설명을 툴팁으로 보여주고 싶을 때.
	// nullptr 또는 빈 문자열 → 툴팁 비활성.
	ImText& SetHoveredTooltip(const char* tooltipText, ImGuiHoveredFlags flags = ImGuiHoveredFlags_None);
	ImText& UseSeparator(bool use);

	void operator()( const char* text );

private:
	float	m_scale			= 1.0f;
	Align	m_align			= Align::Left;
	bool	m_bUseSeparator = false;
	std::pair<bool, ImGuiHoveredFlags> m_hovered = { false, ImGuiHoveredFlags_None };
	std::string m_hoveredCustomTooltip;
};

class ImInputText
{
public:
    ImInputText(size_t maxLength = ULLONG_MAX);

public:
	// 문자 최대 입력 길이를 제한합니다.
	ImInputText& SetMaxLength(size_t maxLength);

	// 힌트 문자를 설정합니다. nullptr시 힌트는 없습니다.
	ImInputText& SetHintText(const char* hintStr);

	// InputText Buffer를 반환합니다.
	const char* GetBuffer() const;

	// std::string 형태 버퍼를 반환합니다.
	const std::string& GetString() const;

	// Input Text를 엽니다.
	bool operator()(ImGuiInputTextFlags flags = ImGuiInputTextFlags_None, bool invalid = false);
	operator const char*();

    // 직접 버퍼를 설정합니다.
    ImInputText& SetText(const std::string& text);

    // 버퍼를 비웁니다.
    void Clear();

private:
	size_t m_maxLength = SIZE_MAX;
	std::string m_buffer;
	std::string m_hint;
};

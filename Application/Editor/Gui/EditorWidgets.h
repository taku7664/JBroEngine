#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Core/Asset/AssetTypes.h"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/magic_enum/magic_enum.hpp"

#include <functional>
#include <string>
#include <vector>

enum class EImValidationSeverity
{
	Info,
	Success,
	Warning,
	Error
};

class ImFieldLabel
{
public:
	explicit ImFieldLabel(const char* text);

	ImFieldLabel& Tooltip(const char* text);
	ImFieldLabel& Required(bool required = true);
	ImFieldLabel& Invalid(bool invalid = true);
	ImFieldLabel& Disabled(bool disabled = true);

	void Draw() const;
	void operator()() const { Draw(); }

private:
	const char* m_text = nullptr;
	const char* m_tooltip = nullptr;
	bool m_required = false;
	bool m_invalid = false;
	bool m_disabled = false;
};

class ImIconButton
{
public:
	ImIconButton(const char* id, const char* icon);

	ImIconButton& Tooltip(const char* text);
	ImIconButton& Size(ImVec2 size);
	ImIconButton& Selected(bool selected = true);
	ImIconButton& Disabled(bool disabled = true);

	bool Draw() const;
	bool operator()() const { return Draw(); }

private:
	const char* m_id = nullptr;
	const char* m_icon = nullptr;
	const char* m_tooltip = nullptr;
	ImVec2 m_size = ImVec2(0.0f, 0.0f);
	bool m_selected = false;
	bool m_disabled = false;
};

class ImValidationMessage
{
public:
	ImValidationMessage(const char* message, EImValidationSeverity severity = EImValidationSeverity::Info);

	ImValidationMessage& Wrap(bool wrap = true);

	void Draw() const;
	void operator()() const { Draw(); }

private:
	const char* m_message = nullptr;
	EImValidationSeverity m_severity = EImValidationSeverity::Info;
	bool m_wrap = true;
};

class ImFilterCombo
{
public:
	explicit ImFilterCombo(const char* id);

	ImFilterCombo& Items(const std::vector<std::string>& items);
	ImFilterCombo& CurrentIndex(int* index);
	ImFilterCombo& FilterHint(const char* text);
	ImFilterCombo& EmptyText(const char* text);
	ImFilterCombo& Width(float width);
	ImFilterCombo& MaxVisibleItems(int count);

	bool Draw() const;
	bool operator()() const { return Draw(); }

private:
	const char* m_id = nullptr;
	const std::vector<std::string>* m_items = nullptr;
	int* m_currentIndex = nullptr;
	const char* m_filterHint = nullptr;
	const char* m_emptyText = nullptr;
	float m_width = 0.0f;
	int m_maxVisibleItems = 12;
};

template <typename E>
class ImEnumCombo
{
public:
	ImEnumCombo(const char* id, E& value)
		: m_id(id)
		, m_value(value)
	{
	}

	ImEnumCombo& Width(float width)
	{
		m_width = width;
		return *this;
	}

	bool Draw() const
	{
		if (0.0f != m_width)
		{
			ImGui::SetNextItemWidth(m_width);
		}

		const std::string current(magic_enum::enum_name(m_value));
		bool changed = false;
		if (ImGui::BeginCombo(nullptr != m_id ? m_id : "##enum_combo", current.c_str()))
		{
			for (const E candidate : magic_enum::enum_values<E>())
			{
				const bool selected = (candidate == m_value);
				const std::string name(magic_enum::enum_name(candidate));
				if (ImGui::Selectable(name.c_str(), selected))
				{
					m_value = candidate;
					changed = true;
				}
				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		return changed;
	}

	bool operator()() const { return Draw(); }

private:
	const char* m_id = nullptr;
	E& m_value;
	float m_width = 0.0f;
};

class ImSearchBox
{
public:
	ImSearchBox(const char* id, std::string& text);

	ImSearchBox& Hint(const char* text);
	ImSearchBox& Width(float width);
	ImSearchBox& ShowClear(bool show = true);
	ImSearchBox& ClearTooltip(const char* text);
	ImSearchBox& Flags(ImGuiInputTextFlags flags);

	bool Draw() const;
	bool operator()() const { return Draw(); }

private:
	const char* m_id = nullptr;
	std::string& m_text;
	const char* m_hint = nullptr;
	float m_width = 0.0f;
	bool m_showClear = true;
	const char* m_clearTooltip = nullptr;
	ImGuiInputTextFlags m_flags = ImGuiInputTextFlags_None;
};

class ImPathField
{
public:
	ImPathField(const char* id, std::string& path);

	ImPathField& Hint(const char* text);
	ImPathField& Width(float width);
	ImPathField& ReserveTrailingWidth(float width);
	ImPathField& ReadOnly(bool readOnly = true);
	ImPathField& Invalid(bool invalid = true);
	ImPathField& Flags(ImGuiInputTextFlags flags);

	bool Draw() const;
	bool operator()() const { return Draw(); }

private:
	const char* m_id = nullptr;
	std::string& m_path;
	const char* m_hint = nullptr;
	float m_width = 0.0f;
	float m_reserveTrailingWidth = 0.0f;
	bool m_readOnly = true;
	bool m_invalid = false;
	ImGuiInputTextFlags m_flags = ImGuiInputTextFlags_None;
};

class ImSectionHeader
{
public:
	explicit ImSectionHeader(const char* title);

	ImSectionHeader& Description(const char* text);
	ImSectionHeader& SpacingBefore(bool spacing = true);
	ImSectionHeader& SpacingAfter(bool spacing = true);

	void Draw() const;
	void operator()() const { Draw(); }

private:
	const char* m_title = nullptr;
	const char* m_description = nullptr;
	bool m_spacingBefore = false;
	bool m_spacingAfter = true;
};

class ImStatusBadge
{
public:
	explicit ImStatusBadge(const char* text);

	ImStatusBadge& Severity(EImValidationSeverity severity);
	ImStatusBadge& Tooltip(const char* text);
	ImStatusBadge& MinWidth(float width);

	void Draw() const;
	void operator()() const { Draw(); }

private:
	const char* m_text = nullptr;
	const char* m_tooltip = nullptr;
	EImValidationSeverity m_severity = EImValidationSeverity::Info;
	float m_minWidth = 0.0f;
};

class ImActionButton
{
public:
	explicit ImActionButton(const char* label);

	ImActionButton& Severity(EImValidationSeverity severity);
	ImActionButton& Tooltip(const char* text);
	ImActionButton& Size(ImVec2 size);
	ImActionButton& Disabled(bool disabled = true);

	bool Draw() const;
	bool operator()() const { return Draw(); }

private:
	const char* m_label = nullptr;
	const char* m_tooltip = nullptr;
	EImValidationSeverity m_severity = EImValidationSeverity::Info;
	ImVec2 m_size = ImVec2(0.0f, 0.0f);
	bool m_disabled = false;
};

class ImReferenceField
{
public:
	using Callback = std::function<bool()>;
	using VoidCallback = std::function<void()>;

	ImReferenceField(const char* id, std::string label, bool isNull);

	ImReferenceField& Width(float width);
	ImReferenceField& Tooltip(std::string text);
	ImReferenceField& ClearTooltip(const char* text);
	ImReferenceField& AllowClear(bool allowClear = true);
	ImReferenceField& OnAcceptDrop(Callback callback);
	ImReferenceField& OnClear(VoidCallback callback);

	bool Draw() const;
	bool operator()() const { return Draw(); }

private:
	const char* m_id = nullptr;
	std::string m_label;
	bool m_isNull = true;
	float m_width = 0.0f;
	std::string m_tooltip;
	const char* m_clearTooltip = nullptr;
	bool m_allowClear = true;
	Callback m_acceptDrop;
	VoidCallback m_clear;
};

class ImAssetField
{
public:
	ImAssetField(const char* id, AssetGuid& guid);

	ImAssetField& Type(EAssetType type);
	ImAssetField& Width(float width);
	ImAssetField& AllowClear(bool allowClear = true);
	ImAssetField& AllowDirectories(bool allowDirectories = true);
	ImAssetField& NoneText(const char* text);
	ImAssetField& MissingText(const char* text);
	ImAssetField& Tooltip(const char* text);

	bool Draw() const;
	bool operator()() const { return Draw(); }

	static std::string BuildDisplayLabel(const AssetGuid& guid, const char* noneText = nullptr, const char* missingText = nullptr);

private:
	bool AcceptDrop() const;
	bool IsExpectedType(EAssetType type) const;
	std::string BuildTooltip() const;

	const char* m_id = nullptr;
	AssetGuid& m_guid;
	EAssetType m_type = EAssetType::Unknown;
	float m_width = 0.0f;
	bool m_allowClear = true;
	bool m_allowDirectories = false;
	const char* m_noneText = nullptr;
	const char* m_missingText = nullptr;
	const char* m_tooltip = nullptr;
};

#endif

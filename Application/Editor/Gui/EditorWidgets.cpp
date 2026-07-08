#include "pch.h"
#include "EditorWidgets.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/EditorDragDrop.h"
#include "Editor/Icons/FontAwesomeIcons.h"
#include "Editor/Localization/EditorLocalizationKeys.h"
#include "Engine/Editor/ImGuiUtillity.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <sstream>
#include <string_view>

namespace
{
	const ImVec4 LABEL_DISABLED_COLOR(0.55f, 0.55f, 0.55f, 1.0f);
	const ImVec4 LABEL_INVALID_COLOR(0.95f, 0.35f, 0.30f, 1.0f);
	const ImVec4 REQUIRED_MARK_COLOR(0.95f, 0.35f, 0.30f, 1.0f);

	ImVec4 ValidationColor(EImValidationSeverity severity)
	{
		switch (severity)
		{
		case EImValidationSeverity::Success:
			return ImVec4(0.45f, 0.85f, 0.50f, 1.0f);
		case EImValidationSeverity::Warning:
			return ImVec4(0.95f, 0.75f, 0.35f, 1.0f);
		case EImValidationSeverity::Error:
			return ImVec4(0.95f, 0.35f, 0.30f, 1.0f);
		case EImValidationSeverity::Info:
		default:
			return ImVec4(0.65f, 0.75f, 0.95f, 1.0f);
		}
	}

	const char* ValidationPrefix(EImValidationSeverity severity)
	{
		switch (severity)
		{
		case EImValidationSeverity::Success:
			return "[OK] ";
		case EImValidationSeverity::Warning:
			return "[!] ";
		case EImValidationSeverity::Error:
			return "[X] ";
		case EImValidationSeverity::Info:
		default:
			return "[i] ";
		}
	}

	ImVec4 WithAlpha(ImVec4 color, float alpha)
	{
		color.w = alpha;
		return color;
	}

	ImVec4 ScaleColor(ImVec4 color, float scale)
	{
		color.x = std::clamp(color.x * scale, 0.0f, 1.0f);
		color.y = std::clamp(color.y * scale, 0.0f, 1.0f);
		color.z = std::clamp(color.z * scale, 0.0f, 1.0f);
		return color;
	}

	bool IsEmptyText(const char* text)
	{
		return nullptr == text || '\0' == text[0];
	}

	bool ContainsCaseInsensitive(std::string_view text, std::string_view filter)
	{
		if (filter.empty())
		{
			return true;
		}

		auto toLower = [](unsigned char c) -> char
		{
			return static_cast<char>(std::tolower(c));
		};

		return std::search(text.begin(), text.end(), filter.begin(), filter.end(),
			[&](char lhs, char rhs)
			{
				return toLower(static_cast<unsigned char>(lhs)) == toLower(static_cast<unsigned char>(rhs));
			}) != text.end();
	}

	std::string AssetPathLabel(const File::Path& path)
	{
		return path.filename().generic_string();
	}

	const char* DefaultNoneText()
	{
		return Loc::Text(EditorLocKeys::InspectorRefNone);
	}

	const char* DefaultMissingText()
	{
		return Loc::Text(EditorLocKeys::InspectorRefMissing);
	}

	const char* DefaultClearText()
	{
		return Loc::Text(EditorLocKeys::CommonRemove);
	}

	void PushInvalidFrameStyle(bool invalid)
	{
		if (false == invalid)
		{
			return;
		}

		ImGui::PushStyleColor(ImGuiCol_Border, LABEL_INVALID_COLOR);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
	}

	void PopInvalidFrameStyle(bool invalid)
	{
		if (false == invalid)
		{
			return;
		}

		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
	}

	bool TryReadAssetPayload(const ImGuiPayload* payload, const EditorDragDrop::AssetPayload*& outPayload)
	{
		outPayload = nullptr;
		if (nullptr == payload
			|| false == payload->IsDataType(EditorDragDrop::ASSET_PAYLOAD_TYPE)
			|| payload->DataSize != sizeof(EditorDragDrop::AssetPayload))
		{
			return false;
		}

		outPayload = static_cast<const EditorDragDrop::AssetPayload*>(payload->Data);
		return nullptr != outPayload;
	}
}

ImFieldLabel::ImFieldLabel(const char* text)
	: m_text(text)
{
}

ImFieldLabel& ImFieldLabel::Tooltip(const char* text)
{
	m_tooltip = text;
	return *this;
}

ImFieldLabel& ImFieldLabel::Required(bool required)
{
	m_required = required;
	return *this;
}

ImFieldLabel& ImFieldLabel::Invalid(bool invalid)
{
	m_invalid = invalid;
	return *this;
}

ImFieldLabel& ImFieldLabel::Disabled(bool disabled)
{
	m_disabled = disabled;
	return *this;
}

void ImFieldLabel::Draw() const
{
	if (m_disabled)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, LABEL_DISABLED_COLOR);
	}
	else if (m_invalid)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, LABEL_INVALID_COLOR);
	}

	ImGui::TextUnformatted(nullptr != m_text ? m_text : "");

	if (m_disabled || m_invalid)
	{
		ImGui::PopStyleColor();
	}

	if (false == IsEmptyText(m_tooltip))
	{
		ImGui::Utillity::HoveredToolTip(m_tooltip);
	}

	if (m_required)
	{
		ImGui::SameLine(0.0f, 2.0f);
		ImGui::TextColored(REQUIRED_MARK_COLOR, "*");
	}
}

ImIconButton::ImIconButton(const char* id, const char* icon)
	: m_id(id)
	, m_icon(icon)
{
}

ImIconButton& ImIconButton::Tooltip(const char* text)
{
	m_tooltip = text;
	return *this;
}

ImIconButton& ImIconButton::Size(ImVec2 size)
{
	m_size = size;
	return *this;
}

ImIconButton& ImIconButton::Selected(bool selected)
{
	m_selected = selected;
	return *this;
}

ImIconButton& ImIconButton::Disabled(bool disabled)
{
	m_disabled = disabled;
	return *this;
}

bool ImIconButton::Draw() const
{
	bool clicked = false;
	ImGui::PushID(nullptr != m_id ? m_id : "");
	if (m_selected)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
	}
	if (m_disabled)
	{
		ImGui::BeginDisabled();
	}

	clicked = ImGui::Button(nullptr != m_icon ? m_icon : "", m_size);

	if (m_disabled)
	{
		ImGui::EndDisabled();
	}
	if (m_selected)
	{
		ImGui::PopStyleColor(3);
	}
	if (false == IsEmptyText(m_tooltip))
	{
		ImGui::Utillity::HoveredToolTip(m_tooltip);
	}
	ImGui::PopID();
	return clicked && false == m_disabled;
}

ImValidationMessage::ImValidationMessage(const char* message, EImValidationSeverity severity)
	: m_message(message)
	, m_severity(severity)
{
}

ImValidationMessage& ImValidationMessage::Wrap(bool wrap)
{
	m_wrap = wrap;
	return *this;
}

void ImValidationMessage::Draw() const
{
	if (IsEmptyText(m_message))
	{
		return;
	}

	const std::string text = std::string(ValidationPrefix(m_severity)) + m_message;
	ImGui::PushStyleColor(ImGuiCol_Text, ValidationColor(m_severity));
	if (m_wrap)
	{
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(text.c_str());
		ImGui::PopTextWrapPos();
	}
	else
	{
		ImGui::TextUnformatted(text.c_str());
	}
	ImGui::PopStyleColor();
}

ImFilterCombo::ImFilterCombo(const char* id)
	: m_id(id)
{
}

ImFilterCombo& ImFilterCombo::Items(const std::vector<std::string>& items)
{
	m_items = &items;
	return *this;
}

ImFilterCombo& ImFilterCombo::CurrentIndex(int* index)
{
	m_currentIndex = index;
	return *this;
}

ImFilterCombo& ImFilterCombo::FilterHint(const char* text)
{
	m_filterHint = text;
	return *this;
}

ImFilterCombo& ImFilterCombo::EmptyText(const char* text)
{
	m_emptyText = text;
	return *this;
}

ImFilterCombo& ImFilterCombo::Width(float width)
{
	m_width = width;
	return *this;
}

ImFilterCombo& ImFilterCombo::MaxVisibleItems(int count)
{
	m_maxVisibleItems = count;
	return *this;
}

bool ImFilterCombo::Draw() const
{
	if (nullptr == m_items || nullptr == m_currentIndex)
	{
		return false;
	}

	if (0.0f != m_width)
	{
		ImGui::SetNextItemWidth(m_width);
	}

	const int itemCount = static_cast<int>(m_items->size());
	const char* preview = (*m_currentIndex >= 0 && *m_currentIndex < itemCount)
		? (*m_items)[static_cast<std::size_t>(*m_currentIndex)].c_str()
		: (nullptr != m_emptyText ? m_emptyText : "");

	bool changed = false;
	ImGui::Utillity::StyleBuilder comboStyle;
	comboStyle.PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
	if (ImGui::BeginCombo(nullptr != m_id ? m_id : "##filter_combo", preview))
	{
		static char filter[128] = "";
		if (ImGui::IsWindowAppearing())
		{
			filter[0] = '\0';
			ImGui::SetKeyboardFocusHere();
		}

		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##filter", nullptr != m_filterHint ? m_filterHint : Loc::Text(EditorLocKeys::CommonFilter), filter, sizeof(filter));
		ImGui::Separator();

		const int maxVisible = std::max(1, m_maxVisibleItems);
		const float childHeight = ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(maxVisible);
		if (ImGui::BeginChild("##filter_combo_items", ImVec2(0.0f, std::min(childHeight, ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(std::max(1, itemCount)))), false))
		{
			for (int i = 0; i < itemCount; ++i)
			{
				const std::string& item = (*m_items)[static_cast<std::size_t>(i)];
				if (false == ContainsCaseInsensitive(item, filter))
				{
					continue;
				}

				const bool selected = (i == *m_currentIndex);
				if (ImGui::Selectable(item.c_str(), selected))
				{
					*m_currentIndex = i;
					changed = true;
				}
				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
		}
		ImGui::EndChild();
		ImGui::EndCombo();
	}

	return changed;
}

ImSearchBox::ImSearchBox(const char* id, std::string& text)
	: m_id(id)
	, m_text(text)
{
}

ImSearchBox& ImSearchBox::Hint(const char* text)
{
	m_hint = text;
	return *this;
}

ImSearchBox& ImSearchBox::Width(float width)
{
	m_width = width;
	return *this;
}

ImSearchBox& ImSearchBox::ShowClear(bool show)
{
	m_showClear = show;
	return *this;
}

ImSearchBox& ImSearchBox::ClearTooltip(const char* text)
{
	m_clearTooltip = text;
	return *this;
}

ImSearchBox& ImSearchBox::Flags(ImGuiInputTextFlags flags)
{
	m_flags = flags;
	return *this;
}

bool ImSearchBox::Draw() const
{
	bool changed = false;
	ImGui::PushID(nullptr != m_id ? m_id : "##search_box");

	const bool drawClear = m_showClear && false == m_text.empty();
	const float clearW = drawClear ? ImGui::GetFrameHeight() : 0.0f;
	const float fullW = 0.0f != m_width ? m_width : ImGui::GetContentRegionAvail().x;
	const float fieldW = drawClear
		? std::max(1.0f, fullW - clearW - ImGui::GetStyle().ItemSpacing.x)
		: std::max(1.0f, fullW);

	ImGui::SetNextItemWidth(fieldW);
	if (ImGui::InputTextWithHint("##input", nullptr != m_hint ? m_hint : Loc::Text(EditorLocKeys::CommonSearch), &m_text, m_flags))
	{
		changed = true;
	}

	if (drawClear)
	{
		ImGui::SameLine();
		if (ImIconButton("clear", EditorIcons::ICON_X_MARK)
			.Size(ImVec2(clearW, 0.0f))
			.Tooltip(nullptr != m_clearTooltip ? m_clearTooltip : DefaultClearText())
			.Draw())
		{
			m_text.clear();
			changed = true;
		}
	}

	ImGui::PopID();
	return changed;
}

ImPathField::ImPathField(const char* id, std::string& path)
	: m_id(id)
	, m_path(path)
{
}

ImPathField& ImPathField::Hint(const char* text)
{
	m_hint = text;
	return *this;
}

ImPathField& ImPathField::Width(float width)
{
	m_width = width;
	return *this;
}

ImPathField& ImPathField::ReserveTrailingWidth(float width)
{
	m_reserveTrailingWidth = width;
	return *this;
}

ImPathField& ImPathField::ReadOnly(bool readOnly)
{
	m_readOnly = readOnly;
	return *this;
}

ImPathField& ImPathField::Invalid(bool invalid)
{
	m_invalid = invalid;
	return *this;
}

ImPathField& ImPathField::Flags(ImGuiInputTextFlags flags)
{
	m_flags = flags;
	return *this;
}

bool ImPathField::Draw() const
{
	ImGui::PushID(nullptr != m_id ? m_id : "##path_field");

	const float fullW = 0.0f != m_width ? m_width : ImGui::GetContentRegionAvail().x;
	const float reservedW = std::max(0.0f, m_reserveTrailingWidth);
	const float fieldW = reservedW > 0.0f
		? std::max(1.0f, fullW - reservedW - ImGui::GetStyle().ItemInnerSpacing.x)
		: std::max(1.0f, fullW);
	ImGui::SetNextItemWidth(fieldW);

	const ImGuiInputTextFlags flags = m_flags | (m_readOnly ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None);
	PushInvalidFrameStyle(m_invalid);
	const bool changed = IsEmptyText(m_hint)
		? ImGui::InputText("##path", &m_path, flags)
		: ImGui::InputTextWithHint("##path", m_hint, &m_path, flags);
	PopInvalidFrameStyle(m_invalid);

	ImGui::PopID();
	return changed;
}

ImSectionHeader::ImSectionHeader(const char* title)
	: m_title(title)
{
}

ImSectionHeader& ImSectionHeader::Description(const char* text)
{
	m_description = text;
	return *this;
}

ImSectionHeader& ImSectionHeader::SpacingBefore(bool spacing)
{
	m_spacingBefore = spacing;
	return *this;
}

ImSectionHeader& ImSectionHeader::SpacingAfter(bool spacing)
{
	m_spacingAfter = spacing;
	return *this;
}

void ImSectionHeader::Draw() const
{
	if (m_spacingBefore)
	{
		ImGui::Spacing();
	}

	if (false == IsEmptyText(m_title))
	{
		ImGui::SeparatorText(m_title);
	}
	else
	{
		ImGui::Separator();
	}

	if (false == IsEmptyText(m_description))
	{
		ImGui::TextWrapped("%s", m_description);
	}

	if (m_spacingAfter)
	{
		ImGui::Spacing();
	}
}

ImStatusBadge::ImStatusBadge(const char* text)
	: m_text(text)
{
}

ImStatusBadge& ImStatusBadge::Severity(EImValidationSeverity severity)
{
	m_severity = severity;
	return *this;
}

ImStatusBadge& ImStatusBadge::Tooltip(const char* text)
{
	m_tooltip = text;
	return *this;
}

ImStatusBadge& ImStatusBadge::MinWidth(float width)
{
	m_minWidth = width;
	return *this;
}

void ImStatusBadge::Draw() const
{
	const char* text = nullptr != m_text ? m_text : "";
	const ImGuiStyle& style = ImGui::GetStyle();
	const ImVec2 textSize = ImGui::CalcTextSize(text);
	const ImVec2 size(
		std::max(m_minWidth, textSize.x + style.FramePadding.x * 2.0f),
		textSize.y + style.FramePadding.y * 2.0f);
	const ImVec2 pos = ImGui::GetCursorScreenPos();

	ImGui::PushID(this);
	ImGui::InvisibleButton("##status_badge", size);

	const ImVec4 base = ValidationColor(m_severity);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(
		pos,
		ImVec2(pos.x + size.x, pos.y + size.y),
		ImGui::GetColorU32(WithAlpha(base, 0.18f)),
		style.FrameRounding);
	drawList->AddRect(
		pos,
		ImVec2(pos.x + size.x, pos.y + size.y),
		ImGui::GetColorU32(WithAlpha(base, 0.65f)),
		style.FrameRounding);
	drawList->AddText(
		ImVec2(pos.x + style.FramePadding.x, pos.y + style.FramePadding.y),
		ImGui::GetColorU32(base),
		text);

	if (false == IsEmptyText(m_tooltip) && ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("%s", m_tooltip);
	}
	ImGui::PopID();
}

ImActionButton::ImActionButton(const char* label)
	: m_label(label)
{
}

ImActionButton& ImActionButton::Severity(EImValidationSeverity severity)
{
	m_severity = severity;
	return *this;
}

ImActionButton& ImActionButton::Tooltip(const char* text)
{
	m_tooltip = text;
	return *this;
}

ImActionButton& ImActionButton::Size(ImVec2 size)
{
	m_size = size;
	return *this;
}

ImActionButton& ImActionButton::Disabled(bool disabled)
{
	m_disabled = disabled;
	return *this;
}

bool ImActionButton::Draw() const
{
	const bool styled = EImValidationSeverity::Info != m_severity;
	if (styled)
	{
		const ImVec4 base = ValidationColor(m_severity);
		ImGui::PushStyleColor(ImGuiCol_Button, WithAlpha(base, 0.55f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WithAlpha(ScaleColor(base, 1.12f), 0.72f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, WithAlpha(ScaleColor(base, 0.92f), 0.85f));
	}
	if (m_disabled)
	{
		ImGui::BeginDisabled();
	}

	const bool clicked = ImGui::Button(nullptr != m_label ? m_label : "", m_size);

	if (m_disabled)
	{
		ImGui::EndDisabled();
	}
	if (styled)
	{
		ImGui::PopStyleColor(3);
	}
	if (false == IsEmptyText(m_tooltip))
	{
		ImGui::Utillity::HoveredToolTip(m_tooltip);
	}
	return clicked && false == m_disabled;
}

ImReferenceField::ImReferenceField(const char* id, std::string label, bool isNull)
	: m_id(id)
	, m_label(std::move(label))
	, m_isNull(isNull)
{
}

ImReferenceField& ImReferenceField::Width(float width)
{
	m_width = width;
	return *this;
}

ImReferenceField& ImReferenceField::Tooltip(std::string text)
{
	m_tooltip = std::move(text);
	return *this;
}

ImReferenceField& ImReferenceField::ClearTooltip(const char* text)
{
	m_clearTooltip = text;
	return *this;
}

ImReferenceField& ImReferenceField::AllowClear(bool allowClear)
{
	m_allowClear = allowClear;
	return *this;
}

ImReferenceField& ImReferenceField::OnAcceptDrop(Callback callback)
{
	m_acceptDrop = std::move(callback);
	return *this;
}

ImReferenceField& ImReferenceField::OnClear(VoidCallback callback)
{
	m_clear = std::move(callback);
	return *this;
}

bool ImReferenceField::Draw() const
{
	bool changed = false;
	ImGui::PushID(nullptr != m_id ? m_id : "");

	const float clearW = ImGui::GetFrameHeight();
	const float fullW = 0.0f != m_width ? m_width : ImGui::GetContentRegionAvail().x;
	const float fieldW = m_allowClear
		? std::max(1.0f, fullW - clearW - ImGui::GetStyle().ItemSpacing.x)
		: std::max(1.0f, fullW);

	ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
	const std::string buttonLabel = m_label.empty()
		? std::string("##reference_button")
		: m_label + "##reference_button";
	ImGui::Button(buttonLabel.c_str(), ImVec2(fieldW, 0.0f));
	ImGui::PopStyleColor(3);

	if (m_acceptDrop && m_acceptDrop())
	{
		changed = true;
	}

	if (false == m_tooltip.empty())
	{
		ImGui::Utillity::HoveredToolTip(m_tooltip.c_str());
	}

	if (m_allowClear)
	{
		ImGui::SameLine();
		if (ImIconButton("clear", EditorIcons::ICON_X_MARK)
			.Size(ImVec2(clearW, 0.0f))
			.Tooltip(nullptr != m_clearTooltip ? m_clearTooltip : DefaultClearText())
			.Disabled(m_isNull)
			.Draw())
		{
			if (m_clear)
			{
				m_clear();
			}
			changed = true;
		}
	}

	ImGui::PopID();
	return changed;
}

ImAssetField::ImAssetField(const char* id, AssetGuid& guid)
	: m_id(id)
	, m_guid(guid)
{
}

ImAssetField& ImAssetField::Type(EAssetType type)
{
	m_type = type;
	return *this;
}

ImAssetField& ImAssetField::Width(float width)
{
	m_width = width;
	return *this;
}

ImAssetField& ImAssetField::AllowClear(bool allowClear)
{
	m_allowClear = allowClear;
	return *this;
}

ImAssetField& ImAssetField::AllowDirectories(bool allowDirectories)
{
	m_allowDirectories = allowDirectories;
	return *this;
}

ImAssetField& ImAssetField::NoneText(const char* text)
{
	m_noneText = text;
	return *this;
}

ImAssetField& ImAssetField::MissingText(const char* text)
{
	m_missingText = text;
	return *this;
}

ImAssetField& ImAssetField::Tooltip(const char* text)
{
	m_tooltip = text;
	return *this;
}

bool ImAssetField::Draw() const
{
	return ImReferenceField(
		nullptr != m_id ? m_id : "##asset_field",
		BuildDisplayLabel(m_guid, m_noneText, m_missingText),
		m_guid.IsNull())
		.Width(m_width)
		.AllowClear(m_allowClear)
		.Tooltip(BuildTooltip())
		.ClearTooltip(DefaultClearText())
		.OnAcceptDrop([this]() { return AcceptDrop(); })
		.OnClear([this]() { m_guid = INVALID_ASSET_GUID; })
		.Draw();
}

std::string ImAssetField::BuildDisplayLabel(const AssetGuid& guid, const char* noneText, const char* missingText)
{
	if (guid.IsNull())
	{
		return nullptr != noneText ? noneText : DefaultNoneText();
	}

	const File::Path path = File::ResolvePath(guid);
	if (false == path.IsNull())
	{
		return AssetPathLabel(path);
	}

	std::ostringstream label;
	label << (nullptr != missingText ? missingText : DefaultMissingText()) << " (" << guid.generic_string() << ")";
	return label.str();
}

bool ImAssetField::AcceptDrop() const
{
	if (false == ImGui::BeginDragDropTarget())
	{
		return false;
	}

	bool changed = false;
	const EditorDragDrop::AssetPayload* hoveringPayload = nullptr;
	const bool compatible =
		TryReadAssetPayload(ImGui::GetDragDropPayload(), hoveringPayload)
		&& (m_allowDirectories || false == hoveringPayload->IsDirectory)
		&& IsExpectedType(hoveringPayload->Type);

	if (compatible)
	{
		if (const ImGuiPayload* accepted = ImGui::AcceptDragDropPayload(EditorDragDrop::ASSET_PAYLOAD_TYPE))
		{
			const EditorDragDrop::AssetPayload* payload = nullptr;
			if (TryReadAssetPayload(accepted, payload))
			{
				m_guid = EditorDragDrop::GetGuid(*payload);
				changed = true;
			}
		}
	}

	ImGui::EndDragDropTarget();
	return changed;
}

bool ImAssetField::IsExpectedType(EAssetType type) const
{
	return EAssetType::Unknown == m_type || type == m_type;
}

std::string ImAssetField::BuildTooltip() const
{
	if (false == IsEmptyText(m_tooltip))
	{
		return m_tooltip;
	}

	if (m_guid.IsNull())
	{
		return {};
	}

	const File::Path path = File::ResolvePath(m_guid);
	return false == path.IsNull()
		? path.generic_string()
		: m_guid.generic_string();
}

#endif

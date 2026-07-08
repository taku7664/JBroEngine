#include "pch.h"
#include "ImSectionHeader.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/ImItem/ImItemTypes.h"

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

	if (false == ImItem::IsEmptyText(m_title))
	{
		ImGui::SeparatorText(m_title);
	}
	else
	{
		ImGui::Separator();
	}

	if (false == ImItem::IsEmptyText(m_description))
	{
		ImGui::TextWrapped("%s", m_description);
	}

	if (m_spacingAfter)
	{
		ImGui::Spacing();
	}
}

#endif

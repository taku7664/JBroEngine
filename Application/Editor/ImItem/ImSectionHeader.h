#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

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

#endif

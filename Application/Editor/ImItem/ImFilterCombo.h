#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include <string>
#include <vector>

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

#endif

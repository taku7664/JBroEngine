#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

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

#endif

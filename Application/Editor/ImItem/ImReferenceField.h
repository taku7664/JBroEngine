#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include <functional>
#include <string>

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

#endif

#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "ThirdParty/imgui/imgui.h"

#include <array>
#include <string>

enum class EEditorShortcut
{
	SaveProject,
	Undo,
	Redo,
	CopyObjects,
	PasteObjects,
	DeleteObjects,
	TogglePlay,
	TogglePause,
};

struct EditorShortcutBinding
{
	ImGuiKey Key = ImGuiKey_None;
	bool Ctrl = false;
	bool Shift = false;
	bool Alt = false;
};

struct EditorShortcutDescriptor
{
	EEditorShortcut Id;
	const char* NameKey;
	const char* CategoryKey;
	EditorShortcutBinding Primary;
	EditorShortcutBinding Alternate;
};

class CEditorShortcutManager final
{
public:
	void ProcessInput();
	bool CanExecute(EEditorShortcut shortcut) const;
	bool Execute(EEditorShortcut shortcut) const;

	const std::array<EditorShortcutDescriptor, 8>& GetDescriptors() const;
	std::string GetShortcutText(EEditorShortcut shortcut) const;

private:
	const EditorShortcutDescriptor* Find(EEditorShortcut shortcut) const;
	bool IsPressed(const EditorShortcutBinding& binding) const;
	std::string FormatBinding(const EditorShortcutBinding& binding) const;
};

#endif

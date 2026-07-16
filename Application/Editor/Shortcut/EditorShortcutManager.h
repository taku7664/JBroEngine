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
	// 유니티 하이어라키와 같은 뜻/같은 바인딩(Ctrl+Shift+V) — 고른 오브젝트의 자식으로
	// 붙여넣는다. 레이어를 골랐으면 부모 없이 그 레이어의 루트로 간다.
	PasteObjectsAsChild,
	// 오브젝트든 레이어든 "고른 것"을 지운다(선택 종류로 분기).
	DeleteSelection,
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

	const std::array<EditorShortcutDescriptor, 9>& GetDescriptors() const;
	std::string GetShortcutText(EEditorShortcut shortcut) const;

private:
	const EditorShortcutDescriptor* Find(EEditorShortcut shortcut) const;
	bool IsPressed(const EditorShortcutBinding& binding) const;
	std::string FormatBinding(const EditorShortcutBinding& binding) const;
};

#endif

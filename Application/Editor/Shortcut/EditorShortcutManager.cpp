#include "pch.h"
#include "EditorShortcutManager.h"

#include "Editor/Editor.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorSessionPersistence.h"
#include "Editor/Gui/EditorGuiActions.h"
#include "Editor/Main/GameView/GameViewTool.h"
#include "Editor/Main/SceneView/SceneViewTool.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Core/Logging/LoggerInternal.h"
#include "Engine/GameFramework/Scene/Scene.h"
#include "Engine/GameFramework/Scene/SceneManager.h"
#include "Engine/Editor/Project/ProjectManager.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

namespace
{
	constexpr EditorShortcutBinding Bind(ImGuiKey key, bool ctrl = false, bool shift = false, bool alt = false)
	{
		return { key, ctrl, shift, alt };
	}

	const std::array<EditorShortcutDescriptor, 8> SHORTCUTS = {{
		{ EEditorShortcut::SaveProject,  EditorLocKeys::MenuFileSaveProject,     EditorLocKeys::MenuFile,       Bind(ImGuiKey_S, true), {} },
		{ EEditorShortcut::Undo,         EditorLocKeys::MenuEditUndo,             EditorLocKeys::MenuEdit,       Bind(ImGuiKey_Z, true), {} },
		{ EEditorShortcut::Redo,         EditorLocKeys::MenuEditRedo,             EditorLocKeys::MenuEdit,       Bind(ImGuiKey_Y, true), Bind(ImGuiKey_Z, true, true) },
		{ EEditorShortcut::CopyObjects,  EditorLocKeys::EditorMenuCopyObject,    EditorLocKeys::MenuEdit,       Bind(ImGuiKey_C, true), {} },
		{ EEditorShortcut::PasteObjects, EditorLocKeys::EditorMenuPasteObject,   EditorLocKeys::MenuEdit,       Bind(ImGuiKey_V, true), {} },
		{ EEditorShortcut::DeleteObjects,EditorLocKeys::HierarchyDeleteObject,    EditorLocKeys::MenuEdit,       Bind(ImGuiKey_Delete), {} },
		{ EEditorShortcut::TogglePlay,   EditorLocKeys::MenuSimulationPlayToggle, EditorLocKeys::MenuSimulation, Bind(ImGuiKey_F5), {} },
		{ EEditorShortcut::TogglePause,  EditorLocKeys::MenuSimulationPauseToggle,EditorLocKeys::MenuSimulation, Bind(ImGuiKey_F6), {} },
	}};
}

void CEditorShortcutManager::ProcessInput()
{
	const ImGuiIO& io = ImGui::GetIO();
	if (io.WantTextInput)
	{
		return;
	}

	for (const EditorShortcutDescriptor& descriptor : SHORTCUTS)
	{
		if ((IsPressed(descriptor.Primary) || IsPressed(descriptor.Alternate)) && CanExecute(descriptor.Id))
		{
			Execute(descriptor.Id);
			return;
		}
	}
}

bool CEditorShortcutManager::CanExecute(EEditorShortcut shortcut) const
{
	switch (shortcut)
	{
	case EEditorShortcut::SaveProject:
	{
		SafePtr<CProjectManager> projectManager = EditorContext::GetProjectManager();
		return projectManager.IsValid()
			&& projectManager->IsProjectLoaded()
			&& EditorSimulationGuard::CanSaveProject();
	}
	case EEditorShortcut::Undo:
		return Editor::CommandManager.CanUndo();
	case EEditorShortcut::Redo:
		return Editor::CommandManager.CanRedo();
	case EEditorShortcut::CopyObjects:
		return false == Editor::GetSelectedEntities().empty();
	case EEditorShortcut::PasteObjects:
		return EditorContext::GetActiveScene().IsValid() && EditorGuiActions::HasObjectClipboardData();
	case EEditorShortcut::DeleteObjects:
		return EditorContext::GetActiveScene().IsValid() && false == Editor::GetSelectedEntities().empty();
	case EEditorShortcut::TogglePlay:
		return EditorContext::GetActiveScene().IsValid();
	case EEditorShortcut::TogglePause:
		return Engine.SceneManager.IsValid()
			&& (Engine.SceneManager->IsSimulationPlaying() || Engine.SceneManager->IsSimulationPaused());
	default:
		return false;
	}
}

bool CEditorShortcutManager::Execute(EEditorShortcut shortcut) const
{
	if (false == CanExecute(shortcut))
	{
		return false;
	}

	switch (shortcut)
	{
	case EEditorShortcut::SaveProject:
	{
		std::string error;
		if (false == EditorSessionPersistence::Save(error))
		{
			CSystemLog::Error(error.empty() ? "Editor session save failed." : error);
			return false;
		}
		CSystemLog::Info("Editor session saved.");
		return true;
	}
	case EEditorShortcut::Undo:
		return Editor::CommandManager.Undo();
	case EEditorShortcut::Redo:
		return Editor::CommandManager.Redo();
	case EEditorShortcut::CopyObjects:
		return EditorGuiActions::CopySelectedObjectsToClipboard();
	case EEditorShortcut::PasteObjects:
	{
		SafePtr<CGameScene> scene = EditorContext::GetActiveScene();
		if (false == scene.IsValid())
		{
			return false;
		}
		if (Editor::SceneView)
		{
			const Vector2 pastePosition = Editor::SceneView->GetPreferredPasteWorldPosition();
			return EditorGuiActions::PasteObjectsFromClipboard(
				*scene,
				&pastePosition,
				Editor::SceneView->GetFocusedEditContext());
		}
		return EditorGuiActions::PasteObjectsFromClipboard(*scene);
	}
	case EEditorShortcut::DeleteObjects:
	{
		SafePtr<CGameScene> scene = EditorContext::GetActiveScene();
		return scene.IsValid() && EditorGuiActions::DeleteSelectedObjects(*scene);
	}
	case EEditorShortcut::TogglePlay:
		if (Engine.SceneManager->IsSimulationPlaying() || Engine.SceneManager->IsSimulationPaused())
		{
			Engine.SceneManager->StopSimulation();
		}
		else
		{
			Engine.SceneManager->PlaySimulation();
			if (Editor::GameView)
			{
				Editor::GameView->Focus();
			}
		}
		return true;
	case EEditorShortcut::TogglePause:
		if (Engine.SceneManager->IsSimulationPaused())
		{
			Engine.SceneManager->PlaySimulation();
		}
		else
		{
			Engine.SceneManager->PauseSimulation();
		}
		return true;
	default:
		return false;
	}
}

const std::array<EditorShortcutDescriptor, 8>& CEditorShortcutManager::GetDescriptors() const
{
	return SHORTCUTS;
}

std::string CEditorShortcutManager::GetShortcutText(EEditorShortcut shortcut) const
{
	const EditorShortcutDescriptor* descriptor = Find(shortcut);
	if (nullptr == descriptor)
	{
		return {};
	}

	std::string text = FormatBinding(descriptor->Primary);
	const std::string alternate = FormatBinding(descriptor->Alternate);
	if (false == alternate.empty())
	{
		text += " / ";
		text += alternate;
	}
	return text;
}

const EditorShortcutDescriptor* CEditorShortcutManager::Find(EEditorShortcut shortcut) const
{
	const auto it = std::find_if(SHORTCUTS.begin(), SHORTCUTS.end(), [shortcut](const EditorShortcutDescriptor& descriptor) {
		return descriptor.Id == shortcut;
	});
	return it != SHORTCUTS.end() ? &*it : nullptr;
}

bool CEditorShortcutManager::IsPressed(const EditorShortcutBinding& binding) const
{
	if (ImGuiKey_None == binding.Key)
	{
		return false;
	}

	const ImGuiIO& io = ImGui::GetIO();
	return io.KeyCtrl == binding.Ctrl
		&& io.KeyShift == binding.Shift
		&& io.KeyAlt == binding.Alt
		&& false == io.KeySuper
		&& ImGui::IsKeyPressed(binding.Key, false);
}

std::string CEditorShortcutManager::FormatBinding(const EditorShortcutBinding& binding) const
{
	if (ImGuiKey_None == binding.Key)
	{
		return {};
	}

	std::string text;
	if (binding.Ctrl) text += "Ctrl+";
	if (binding.Shift) text += "Shift+";
	if (binding.Alt) text += "Alt+";
	text += ImGui::GetKeyName(binding.Key);
	return text;
}

#endif

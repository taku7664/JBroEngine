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
#include "Engine/GameFramework/Canvas/Canvas.h"
#include "Engine/GameFramework/Canvas/CanvasManager.h"
#include "Engine/Editor/Project/ProjectManager.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

namespace
{
	constexpr EditorShortcutBinding Bind(ImGuiKey key, bool ctrl = false, bool shift = false, bool alt = false)
	{
		return { key, ctrl, shift, alt };
	}

	const std::array<EditorShortcutDescriptor, 9> SHORTCUTS = {{
		{ EEditorShortcut::SaveProject,  EditorLocKeys::MenuFileSaveProject,     EditorLocKeys::MenuFile,       Bind(ImGuiKey_S, true), {} },
		{ EEditorShortcut::Undo,         EditorLocKeys::MenuEditUndo,             EditorLocKeys::MenuEdit,       Bind(ImGuiKey_Z, true), {} },
		{ EEditorShortcut::Redo,         EditorLocKeys::MenuEditRedo,             EditorLocKeys::MenuEdit,       Bind(ImGuiKey_Y, true), Bind(ImGuiKey_Z, true, true) },
		{ EEditorShortcut::CopyObjects,  EditorLocKeys::EditorMenuCopyObject,    EditorLocKeys::MenuEdit,       Bind(ImGuiKey_C, true), {} },
		{ EEditorShortcut::PasteObjects, EditorLocKeys::EditorMenuPasteObject,   EditorLocKeys::MenuEdit,       Bind(ImGuiKey_V, true), {} },
		// IsPressed 가 Shift 를 정확히 비교하므로 Ctrl+Shift+V 가 위의 Ctrl+V 를 오발동시키지 않는다.
		{ EEditorShortcut::PasteObjectsAsChild, EditorLocKeys::EditorMenuPasteObjectAsChild, EditorLocKeys::MenuEdit, Bind(ImGuiKey_V, true, true), {} },
		{ EEditorShortcut::DeleteSelection, EditorLocKeys::EditorMenuDeleteSelection, EditorLocKeys::MenuEdit,   Bind(ImGuiKey_Delete), {} },
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
		return EditorContext::GetActiveCanvas().IsValid() && EditorGuiActions::HasObjectClipboardData();
	case EEditorShortcut::PasteObjectsAsChild:
		// 레이어만 골랐어도 허용 — 부모 없이 그 레이어 루트로 붙여넣는다.
		return EditorContext::GetActiveCanvas().IsValid()
			&& EditorGuiActions::HasObjectClipboardData()
			&& (nullptr != Editor::GetSelectedEntity() || nullptr != Editor::GetSelectedLayer());
	case EEditorShortcut::DeleteSelection:
	{
		if (false == EditorContext::GetActiveCanvas().IsValid())
		{
			return false;
		}
		// 오브젝트 우선 — 선택은 상호 배타지만 판정 순서를 Execute 와 맞춰 둔다.
		return false == Editor::GetSelectedEntities().empty()
			|| nullptr != Editor::GetSelectedLayer();
	}
	case EEditorShortcut::TogglePlay:
		return EditorContext::GetActiveCanvas().IsValid();
	case EEditorShortcut::TogglePause:
		return Engine.CanvasManager.IsValid()
			&& (Engine.CanvasManager->IsSimulationPlaying() || Engine.CanvasManager->IsSimulationPaused());
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
		SafePtr<CGameCanvas> scene = EditorContext::GetActiveCanvas();
		if (false == scene.IsValid())
		{
			return false;
		}
		if (Editor::CanvasView)
		{
			const Vector2 pastePosition = Editor::CanvasView->GetPreferredPasteWorldPosition();
			return EditorGuiActions::PasteObjectsFromClipboard(
				*scene,
				&pastePosition,
				Editor::CanvasView->GetFocusedEditContext());
		}
		return EditorGuiActions::PasteObjectsFromClipboard(*scene);
	}
	case EEditorShortcut::PasteObjectsAsChild:
	{
		SafePtr<CGameCanvas> scene = EditorContext::GetActiveCanvas();
		if (false == scene.IsValid())
		{
			return false;
		}
		// 오브젝트를 골랐으면 그 자식으로, 레이어만 골랐으면 부모 없이 그 레이어의 루트로.
		// 원본 위치를 유지한다 — 자식으로 넣는 의도는 배치가 아니라 계층이다.
		CGameObject* parent = Editor::GetSelectedEntity();
		return EditorGuiActions::PasteObjectsFromClipboard(*scene, nullptr, parent);
	}
	case EEditorShortcut::DeleteSelection:
	{
		SafePtr<CGameCanvas> scene = EditorContext::GetActiveCanvas();
		if (false == scene.IsValid())
		{
			return false;
		}
		if (false == Editor::GetSelectedEntities().empty())
		{
			return EditorGuiActions::DeleteSelectedObjects(*scene);
		}
		return EditorGuiActions::DeleteSelectedLayer(*scene);
	}
	case EEditorShortcut::TogglePlay:
		if (Engine.CanvasManager->IsSimulationPlaying() || Engine.CanvasManager->IsSimulationPaused())
		{
			Engine.CanvasManager->StopSimulation();
		}
		else
		{
			Engine.CanvasManager->PlaySimulation();
			if (Editor::GameView)
			{
				Editor::GameView->Focus();
			}
		}
		return true;
	case EEditorShortcut::TogglePause:
		if (Engine.CanvasManager->IsSimulationPaused())
		{
			Engine.CanvasManager->PlaySimulation();
		}
		else
		{
			Engine.CanvasManager->PauseSimulation();
		}
		return true;
	default:
		return false;
	}
}

const std::array<EditorShortcutDescriptor, 9>& CEditorShortcutManager::GetDescriptors() const
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

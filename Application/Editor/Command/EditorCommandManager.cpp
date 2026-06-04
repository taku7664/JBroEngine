#include "pch.h"
#include "EditorCommandManager.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "ThirdParty/imgui/imgui.h"

namespace
{
	// 드래그 진행 중인가 = 좌버튼 유지. ImGui 컨텍스트/프레임 밖이면 false.
	bool IsDragInProgress()
	{
		return nullptr != ImGui::GetCurrentContext() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
	}
}

bool CEditorCommandManager::ExecuteCommand(OwnerPtr<IEditorCommand> command, const char* documentKey)
{
	if (false == static_cast<bool>(command) || false == command->Execute())
	{
		return false;
	}

	// ── 편집 단위 묶기 ────────────────────────────────────────────────────────
	// 드래그 중(좌버튼 유지) 같은 대상 연속 편집은 최상단 커맨드에 병합해 undo 1개로 유지.
	const bool dragging = IsDragInProgress();
	if (dragging && m_dragMergeActive && false == m_undoStack.empty()
		&& m_undoStack.back()->TryMerge(*command))
	{
		// 병합됨 — command(중복 엔트리)는 버린다. 결과값은 이미 Execute 로 적용됨.
		m_redoStack.clear();
		MarkDirty(documentKey);
		return true;
	}

	m_undoStack.push_back(std::move(command));
	m_redoStack.clear();
	// 드래그 중 첫 커맨드면 이후 프레임부터 이 엔트리에 병합. 비드래그면 병합 비활성.
	m_dragMergeActive = dragging;
	MarkDirty(documentKey);
	return true;
}

bool CEditorCommandManager::Undo()
{
	if (m_undoStack.empty())
	{
		return false;
	}

	OwnerPtr<IEditorCommand> command = std::move(m_undoStack.back());
	m_undoStack.pop_back();
	command->Undo();
	m_redoStack.push_back(std::move(command));
	MarkDirty();
	return true;
}

bool CEditorCommandManager::Redo()
{
	if (m_redoStack.empty())
	{
		return false;
	}

	OwnerPtr<IEditorCommand> command = std::move(m_redoStack.back());
	m_redoStack.pop_back();
	command->Redo();
	m_undoStack.push_back(std::move(command));
	MarkDirty();
	return true;
}

void CEditorCommandManager::Clear()
{
	m_undoStack.clear();
	m_redoStack.clear();
	m_dragMergeActive = false;
	m_documents.clear();
	m_activeDocumentKey.clear();
	m_globalRevision = 0;
}

void CEditorCommandManager::SetActiveDocument(const char* documentKey)
{
	if (nullptr == documentKey || '\0' == documentKey[0])
	{
		m_activeDocumentKey.clear();
		return;
	}

	m_activeDocumentKey = documentKey;
	GetOrCreateDocumentState(m_activeDocumentKey.c_str());
}

const std::string& CEditorCommandManager::GetActiveDocument() const
{
	return m_activeDocumentKey;
}

void CEditorCommandManager::MarkDirty(const char* documentKey)
{
	EditorDocumentState& state = GetOrCreateDocumentState(ResolveDocumentKey(documentKey));
	state.CurrentRevision = ++m_globalRevision;
}

void CEditorCommandManager::MarkSaved(const char* documentKey)
{
	EditorDocumentState& state = GetOrCreateDocumentState(ResolveDocumentKey(documentKey));
	if (0 == state.CurrentRevision)
	{
		state.CurrentRevision = ++m_globalRevision;
	}
	state.SavedRevision = state.CurrentRevision;
}

bool CEditorCommandManager::IsDirty(const char* documentKey) const
{
	const EditorDocumentState* state = FindDocumentState(ResolveDocumentKey(documentKey));
	return nullptr != state && state->CurrentRevision != state->SavedRevision;
}

bool CEditorCommandManager::CanUndo() const
{
	return false == m_undoStack.empty();
}

bool CEditorCommandManager::CanRedo() const
{
	return false == m_redoStack.empty();
}

std::uint64_t CEditorCommandManager::GetGlobalRevision() const
{
	return m_globalRevision;
}

EditorDocumentState& CEditorCommandManager::GetOrCreateDocumentState(const char* documentKey)
{
	const char* key = ResolveDocumentKey(documentKey);
	EditorDocumentState& state = m_documents[key];
	if (state.Key.empty())
	{
		state.Key = key;
	}
	return state;
}

const EditorDocumentState* CEditorCommandManager::FindDocumentState(const char* documentKey) const
{
	const char* key = ResolveDocumentKey(documentKey);
	auto it = m_documents.find(key);
	return it == m_documents.end() ? nullptr : &it->second;
}

const char* CEditorCommandManager::ResolveDocumentKey(const char* documentKey) const
{
	if (nullptr != documentKey && '\0' != documentKey[0])
	{
		return documentKey;
	}

	return m_activeDocumentKey.empty() ? "__EditorGlobal" : m_activeDocumentKey.c_str();
}

#endif

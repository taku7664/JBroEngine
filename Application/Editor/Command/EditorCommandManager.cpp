#include "pch.h"
#include "EditorCommandManager.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "ThirdParty/imgui/imgui.h"

bool CEditorCommandManager::ExecuteCommand(OwnerPtr<IEditorCommand> command, const char* documentKey)
{
	if (false == static_cast<bool>(command) || false == command->Execute())
	{
		return false;
	}

	// ── 편집 단위 묶기(트랜잭션) ────────────────────────────────────────────────
	// 한 번의 드래그(좌버튼 누름~놓음) = undo 1개. 같은 대상 연속 편집은 최상단 커맨드에 병합.
	// 핵심: 드래그 경계 감지. ExecuteCommand 는 편집 프레임에만 호출되어 "놓는 순간"을 못 본다.
	// 좌버튼 down-duration 은 누름 동안 단조 증가하고 새 누름에서 0 으로 리셋되므로, 직전보다
	// 줄었거나(=새 누름) 버튼이 안 눌렸으면 새 그룹으로 본다 → 드래그마다 첫 커맨드가 push 된다.
	ImGuiContext* context = ImGui::GetCurrentContext();
	const bool  mouseDown = (nullptr != context) && ImGui::IsMouseDown(ImGuiMouseButton_Left);
	const float downDuration = (nullptr != context)
		? ImGui::GetIO().MouseDownDuration[ImGuiMouseButton_Left] : -1.0f;

	if (false == mouseDown || downDuration < m_lastMouseDownDuration)
	{
		m_dragMergeActive = false; // 새 누름/비드래그 → 새 병합 그룹.
	}
	m_lastMouseDownDuration = downDuration;

	if (mouseDown && m_dragMergeActive && false == m_undoStack.empty()
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
	m_dragMergeActive = mouseDown;
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
	m_lastMouseDownDuration = -1.0f;
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

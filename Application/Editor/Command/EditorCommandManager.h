#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Utillity/SafePtr.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class IEditorCommand
{
public:
	virtual ~IEditorCommand() = default;

public:
	virtual const char* GetName() const = 0;
	virtual bool Execute() = 0;
	virtual void Undo() = 0;
	virtual void Redo() = 0;
};

struct EditorDocumentState
{
	std::string Key;
	std::uint64_t CurrentRevision = 0;
	std::uint64_t SavedRevision = 0;
};

class CEditorCommandManager final
{
public:
	bool ExecuteCommand(OwnerPtr<IEditorCommand> command, const char* documentKey = nullptr);
	bool Undo();
	bool Redo();

	void Clear();
	void SetActiveDocument(const char* documentKey);
	const std::string& GetActiveDocument() const;

	void MarkDirty(const char* documentKey = nullptr);
	void MarkSaved(const char* documentKey = nullptr);
	bool IsDirty(const char* documentKey = nullptr) const;

	bool CanUndo() const;
	bool CanRedo() const;
	std::uint64_t GetGlobalRevision() const;

private:
	EditorDocumentState& GetOrCreateDocumentState(const char* documentKey);
	const EditorDocumentState* FindDocumentState(const char* documentKey) const;
	const char* ResolveDocumentKey(const char* documentKey) const;

private:
	std::vector<OwnerPtr<IEditorCommand>> m_undoStack;
	std::vector<OwnerPtr<IEditorCommand>> m_redoStack;
	std::unordered_map<std::string, EditorDocumentState> m_documents;
	std::string m_activeDocumentKey;
	std::uint64_t m_globalRevision = 0;
};

#endif

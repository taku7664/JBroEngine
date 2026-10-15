#pragma once
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/Bool.h>

namespace JBro::Code
{
    enum class SourceChangeKind { Edit, Undo, Redo };
    // Compact native edits. Lines are zero based; only column-zero affinity affects line anchors.
    struct SourceChange
    {
        UInt32 startLine = 0, endLine = 0, insertedLines = 0;
        Bool startsAtLineStart = true, endsAtLineStart = true;
        Bool removesText = false, insertsText = false;
    };
    struct SourceTransaction
    {
        UInt64 history = 0;
        SourceChangeKind kind = SourceChangeKind::Edit;
        Bool empty = false;
        Array<SourceChange> edits;
    };
    struct SourceChanges
    {
        Array<SourceTransaction> transactions;
        Bool reset = false;
    };
    // Session-owned source anchors. Text undo restores positions, not manual toggles.
    class Breakpoints final
    {
    public:
        void Open(const String& path, const String& text, UInt64 history);
        void Change(const String& path, const String& text, UInt64 history);
        void Change(const String& path, const String& text, UInt64 history, const SourceChanges& changes);
        void Toggle(const String& path, UInt32 line);
        Array<UInt32> Lines(const String& path) const;
        void Clear();
        UInt64 GetRevision() const;
    private:
        struct Anchor { UInt64 id; UInt32 line; };
        struct Snapshot { UInt64 history, fingerprint; Array<Anchor> anchors; };
        struct Document
        {
            String path, text;
            UInt64 history = 0;
            Array<Anchor> anchors;
            Array<UInt64> disabled;
            Array<Snapshot> snapshots;
        };
        Document* Find(const String& path);
        static Array<Anchor> Move(const String& before, const String& after, const Array<Anchor>& anchors);
        static Array<Anchor> Move(const SourceChange& change, const Array<Anchor>& anchors);
        void Apply(Document& document, const SourceTransaction& transaction);
        Array<Document> m_documents;
        UInt64 m_next = 1, m_revision = 0;
    };
}

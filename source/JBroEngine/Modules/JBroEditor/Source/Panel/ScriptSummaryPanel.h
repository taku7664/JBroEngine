#pragma once
#include <JBro/Editor/EditorPanel.h>
#include <JBro/Editor/Code/Summary.h>
#include <JBro/Editor/Widget/ScriptOutline.h>

namespace JBro
{
    class CodeEditorPanel;
    class ScriptSummaryPanel final : public UniquePanel
    {
    public:
        static constexpr const char* TypeName = "ScriptSummary";
        static void RegisterActions();
        const char* GetTitle() const override { return TypeName; }
        const char* GetDisplayTitle() const override;
        EditorDock GetPreferredDock() const override { return EditorDock::Right; }
        Bool OnCreate(EditorApplication& editor) override;
        void OnUpdate(Float elapsed) override;
        void OnDraw() override;
    private:
        struct AttributeDraft
        {
            String name;
            Bool enabled = false;
            Array<String> arguments;
        };
        struct FieldDraft
        {
            String key;
            Code::Position position;
            Bool initializerPresent = false;
            String initializer;
            Array<AttributeDraft> attributes;
        };
        struct Entry
        {
            String key, qualifiers;
            Code::SummaryEntry source;
            Bool fieldReadOnly = false;
        };
        void CollectEntries(const Array<Code::SummaryEntry>& entries, const String& parent, UInt32 depth, Bool fieldsReadOnly = false);
        void DrawDetails(CodeEditorPanel& document, Bool available);
        void DrawField(CodeEditorPanel& document, const Entry& field, Bool available);
        void CommitSelectedDraft(CodeEditorPanel& document);
        void Submit(CodeEditorPanel& document, FieldDraft& field, Bool initializer);
        EditorApplication* m_editor = nullptr;
        String m_path, m_filename, m_selected, m_error, m_errorPath;
        UInt64 m_summaryRevision = 0, m_pending = 0;
        Uuid m_pendingDocument;
        String m_pendingPath;
        Code::Position m_errorPosition;
        Array<FieldDraft> m_fields;
        Array<Entry> m_entries;
        Array<Widget::ScriptOutlineItem> m_outline;
        UInt32 m_fieldCount = 0, m_functionCount = 0;
        Int32 m_sourceVersion = 0;
        Bool m_navigationPending = false;
    };
}

#pragma once
#include <JBro/Editor/EditorPanel.h>
#include <JBro/Editor/Widget/CodeEditor.h>
#include <JBro/Editor/Code/LanguageClient.h>
#include "../Code/DocumentAssistance.h"

namespace JBro
{
    class EditorApplication;
    class CodeEditorPanel final : public InstancePanel
    {
    public:
        static constexpr const char* TypeName = "CodeEditor";
        static constexpr const char* DockAreaName = "CodeEditor";
        static void RegisterActions();
        const char* GetTitle() const override { return TypeName; }
        const char* GetDisplayTitle() const override { return m_title.c_str(); }
        Bool OnCreate(EditorApplication& editor) override;
        void OnDestroy() override;
        void OnUpdate(Float elapsed) override;
        void OnDraw() override;
        void OnMenuBar() override;
        Bool HasMenuBar() const override { return true; }
        Bool OnCloseRequested() override;
        Bool HasUnsavedChanges() const override;
        Bool SaveChanges() override;
        Bool Open(const String& path, UInt32 line = 1, UInt32 column = 0);
        const String& GetPath() const { return m_path; }
        const String& GetText() const { return m_text; }
        const Array<Code::Diagnostic>& GetDiagnostics() const { return m_diagnostics; }
        Bool Save();
        Bool Reload();
        void RequestClose();
        void RequestReload();
        void FocusAt(UInt32 line, UInt32 column = 0);
        void SelectDefinition(const Code::Range& range);
        void Undo();
        void Redo();
        void Cut();
        void Paste();
        Widget::CodeEditor& GetBuffer() { return m_buffer; }
        String GetBufferText() const { return m_buffer.GetText(); }
        Bool IsJBroScript() const { return m_jscript; }
        Bool IsReadOnly() const;
        Code::LanguageClient* GetLanguageClient() { return m_language.TryGet(); }
        Bool ApplySourceEdits(Int32 version, const Array<Code::TextEdit>& edits);
        Bool ApplyAgentText(const String& expected, const String& text);
        static CodeEditorPanel* FindActive(EditorApplication& editor);
    private:
        void UpdateTitle();
        void Changed();
        void CancelSiblingFocusRequests();
        EditorApplication* m_editor = nullptr;
        SafePtr<Code::LanguageClient> m_language;
        Widget::CodeEditor m_buffer;
        Code::DocumentAssistance m_assistance{m_buffer};
        String m_path, m_title, m_text, m_saved, m_diskBytes, m_message;
        Array<Code::Diagnostic> m_diagnostics;
        Array<Widget::CodeEditor::Marker> m_debugMarkers;
        UInt64 m_markerAnchors = UInt64(-1), m_markerClient = UInt64(-1), m_markerLocale = UInt64(-1);
        UInt32 m_executionLine = 0;
        Bool m_markerMatches = false;
        UInt64 m_revision = 0, m_analysisRevision = 0;
        UInt64 m_focusSerial = 0;
        Bool m_jscript = false, m_readOnly = false, m_bom = false, m_crlf = false;
        Bool m_focus = false, m_selectFocus = false;
        Code::Range m_focusRange;
        UInt32 m_focusLine = 0, m_focusColumn = 0;
    };
}

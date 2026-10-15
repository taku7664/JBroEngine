#pragma once
#include <JBro/Editor/EditorPanel.h>
#include <JBro/Editor/Widget/CodeEditor.h>
#include <JBro/Editor/Code/LanguageClient.h>

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
        void Undo();
        void Redo();
        Widget::CodeEditor& GetBuffer() { return m_buffer; }
    private:
        void UpdateTitle();
        void Changed();
        EditorApplication* m_editor = nullptr;
        Code::LanguageClient* m_language = nullptr;
        Widget::CodeEditor m_buffer;
        String m_path, m_title, m_text, m_saved, m_diskBytes, m_message;
        Array<Code::Diagnostic> m_diagnostics;
        UInt64 m_revision = 0, m_analysisRevision = 0;
        Bool m_jscript = false, m_readOnly = false, m_bom = false, m_crlf = false;
        Bool m_focus = false;
        UInt32 m_focusLine = 0, m_focusColumn = 0;
    };
}

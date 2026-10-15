#pragma once
#include <JBro/Editor/Code/Analysis.h>
#include <JBro/Editor/Code/Summary.h>
#include <JBro/Editor/Code/Breakpoints.h>
#include <JBro/Types/SafePtr.h>

namespace JBro::Widget
{
    // One document owns its edit history, cursor, selection and scroll state.
    class CodeEditor final
    {
    public:
        CodeEditor();
        ~CodeEditor();
        CodeEditor(const CodeEditor&) = delete;
        CodeEditor& operator=(const CodeEditor&) = delete;
        void SetText(const String& text);
        String GetText() const;
        void SetJBroScript(Bool enabled);
        void SetReadOnly(Bool enabled);
        Bool Render(const char* id, Bool reserveStatusLine = false);
        void Focus(UInt32 line = 0, UInt32 utf16Column = 0);
        void SetAnalysis(const Code::Analysis& analysis);
        void ClearAnalysis();
        void Undo();
        void Redo();
        Bool CanUndo() const;
        Bool CanRedo() const;
        UInt64 GetRevision() const;
        Code::Position GetCursor() const;
        Bool ApplyEdits(const Array<Code::TextEdit>& edits);
        UInt64 GetHistoryIndex() const;
        Code::SourceChanges TakeChanges();
        struct Marker { UInt32 line; Bool verified = false; String message; };
        void SetDebugMarkers(const Array<Marker>& markers, UInt32 executionLine = 0);
        UInt32 TakeBreakpointToggle();
        static UInt32 Utf16ToGlyph(std::string_view line, UInt32 character);
    private:
        struct State;
        OwnerPtr<State> m_state;
    };
}

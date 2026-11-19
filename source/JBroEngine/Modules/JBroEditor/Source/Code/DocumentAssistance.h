#pragma once
#include <JBro/Editor/Code/LanguageClient.h>
#include <JBro/Types/Float.h>

namespace JBro::Widget { class CodeEditor; }
namespace JBro::Code
{
    class DocumentAssistance final
    {
    public:
        explicit DocumentAssistance(Widget::CodeEditor& buffer);
        void Connect(SafePtr<LanguageClient> language, const String& path);
        void Invalidate();
        void Update(Float elapsed);
        void AfterDraw();
        void HighlightReferences(Position definition);
    private:
        Widget::CodeEditor& m_buffer;
        SafePtr<LanguageClient> m_language;
        String m_path;
        Position m_hoverPosition;
        Range m_hoverRange;
        Float m_hoverAge = 0.0f;
        UInt64 m_hoverRequest = 0, m_referencesRequest = 0;
        Bool m_hoverActive = false, m_hoverResolved = false;
    };
}

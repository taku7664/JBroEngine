#pragma once
#include <JBro/Editor/Document/Markdown.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/Float.h>
#include <JBro/RHI/RHI.h>

namespace JBro::Widget
{
    struct MarkdownImage
    {
        TextureHandle texture;
        UInt32 width = 0, height = 0;
        String message;
    };
    struct MarkdownCallbacks
    {
        void* user = nullptr;
        void (*openLink)(void*, const String&) = nullptr;
        MarkdownImage (*image)(void*, const String&) = nullptr;
    };
    class MarkdownView final
    {
    public:
        MarkdownView();
        ~MarkdownView();
        MarkdownView(const MarkdownView&) = delete;
        MarkdownView& operator=(const MarkdownView&) = delete;
        void SetDocument(Markdown::Document document);
        const Markdown::Document& GetDocument() const;
        void SetCallbacks(const MarkdownCallbacks& callbacks);
        void InvalidateLayout();
        void Render(const char* id);
        void Select(UInt32 begin, UInt32 end);
        void SelectAll();
        String SelectedText() const;
        Bool HasSelection() const;
        void Copy() const;
        void ShowFind();
        void SetSearch(const String& query);
        void NextMatch(Bool backwards = false);
        UInt32 MatchCount() const;
        Bool GoToAnchor(const String& anchor);
        UInt64 LayoutRevision() const;
        UInt32 VisibleGlyphCount() const;
    private:
        struct State;
        OwnerPtr<State> m_state;
    };
}

#pragma once
#include <JBro/Editor/EditorPanel.h>
#include <JBro/Editor/Widget/MarkdownView.h>
#include <JBro/Asset/ImageDecoder.h>
#include <JBro/Task/TaskGroup.h>

namespace JBro
{
    class MarkdownPanel final : public InstancePanel
    {
    public:
        static constexpr const char* TypeName = "Markdown";
        static constexpr const char* DockAreaName = "Documents";
        static void RegisterActions();
        const char* GetTitle() const override { return TypeName; }
        const char* GetDisplayTitle() const override { return m_title.c_str(); }
        Bool OnCreate(EditorApplication& editor) override;
        void OnDestroy() override;
        void OnUpdate(Float elapsed) override;
        void OnDraw() override;
        void OnMenuBar() override;
        Bool HasMenuBar() const override { return true; }
        Bool Open(const String& path, const String& anchor);
        void Focus(const String& anchor);
        void Reload();
        const String& GetPath() const { return m_path; }
        Widget::MarkdownView& GetView() { return m_view; }
        Bool IsLoading() const { return m_loading; }
        const String& GetError() const { return m_error; }
    private:
        class LoadTask;
        struct ImageEntry
        {
            String target, path;
            Widget::MarkdownImage image;
        };
        void Adopt(LoadTask& task, const TaskResult& result);
        void LoadImages();
        void ClearImages();
        void OpenLink(const String& target);
        void CancelTasks();
        EditorApplication* m_editor = nullptr;
        Widget::MarkdownView m_view;
        String m_path, m_title, m_error, m_anchor;
        UInt64 m_version = 0, m_writeTime = 0;
        Array<TaskGroupId> m_tasks;
        Array<ImageEntry> m_images;
        Bool m_loading = false, m_wasFocused = false, m_wasVisible = false;
    };
}

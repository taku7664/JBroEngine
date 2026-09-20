#pragma once

#include <JBro/Core/Log.h>
#include <JBro/Editor/EditorPanel.h>
#include <JBro/Types/String.h>

namespace JBro
{
    // 엔진이 한 말을 보여 준다(D-133). 기존 엔진 `CLogTool` 자리다.
    //
    // 창으로 띄운 에디터에는 콘솔이 없다. 그래서 에셋을 못 읽었다거나 감시가 서지 않았다는
    // 말이 그대로 사라졌고, 사용자는 "왜 안 되는지" 를 화면 어디에서도 볼 수 없었다.
    class LogPanel final : public EditorPanel
    {
    public:
        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        bool OnCreate(EditorApplication& editor) override;
        void OnDraw() override;
        EditorDock GetPreferredDock() const override { return EditorDock::Bottom; }

    private:
        void DrawToolBar();
        void DrawEntries();
        bool Passes(const LogEntry& entry) const;

        EditorApplication* m_editor = nullptr;
        String m_filter;
        // 등급마다 켜고 끈다. 경고만 보고 싶은 때가 있다.
        bool m_levels[5] = {false, true, true, true, true};
        // 새 줄이 오면 바닥으로 따라간다. 사람이 위로 올려 읽는 중이면 끈다.
        bool m_autoScroll = true;
        // 마지막으로 본 판번호. 같으면 스크롤을 건드리지 않는다.
        std::uint64_t m_seenRevision = 0;
        bool m_scrollToBottom = false;
    };
}

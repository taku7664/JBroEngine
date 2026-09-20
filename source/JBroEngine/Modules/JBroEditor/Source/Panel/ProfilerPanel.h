#pragma once

#include <JBro/Core/Profiler.h>
#include <JBro/Editor/EditorPanel.h>

namespace JBro
{
    // 프레임 안에서 어디에 시간이 갔는지 보여 준다(D-138). 기존 엔진 `CCpuProfilerWindow` 자리다.
    //
    // 통계 창은 프레임 시간 하나만 말한다. 느려졌을 때 **어디가** 느린지는 그 숫자로 알 수 없다.
    //
    // **켜야 잰다.** 재는 것 자체가 프레임에 얹히는 일이라, 창을 닫아 두면 끈다.
    class ProfilerPanel final : public EditorPanel
    {
    public:
        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        bool OnCreate(EditorApplication& editor) override;
        void OnDestroy() override;
        void OnUpdate(float deltaTime) override;
        void OnDraw() override;
        EditorDock GetPreferredDock() const override { return EditorDock::Bottom; }

    private:
        EditorApplication* m_editor = nullptr;
        // 한 프레임의 숫자는 널뛴다. 사람이 읽으려면 눌러 줘야 한다.
        struct Smoothed
        {
            const char* name = nullptr;
            std::uint32_t depth = 0;
            std::uint32_t callCount = 0;
            double milliseconds = 0.0;
        };
        Smoothed m_rows[Profiler::MaxSamples];
        std::size_t m_rowCount = 0;
        double m_frameMilliseconds = 0.0;
    };
}

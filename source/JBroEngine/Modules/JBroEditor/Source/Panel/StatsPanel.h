#pragma once

#include <JBro/Editor/EditorPanel.h>

#include <cstdint>

namespace JBro::System
{
    class AudioSystem;
}

namespace JBro
{
    // 프레임이 얼마나 걸리는지, 렌더러가 무엇을 몇 개 넘겼는지 보여 준다.
    //
    // **패널이 둘 이상이라는 것을 실제로 시험하는 자리이기도 하다.** 하나뿐이면
    // 레지스트리도 도킹도 도는지 알 수 없다.
    class StatsPanel final : public EditorPanel
    {
    public:
        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        bool OnCreate(EditorApplication& editor) override;
        void OnUpdate(float deltaTime) override;
        void OnDraw() override;
        EditorDock GetPreferredDock() const override { return EditorDock::Bottom; }

    private:
        void DrawAudioMeters(System::AudioSystem& audio, float masterPeak);

        // 프레임 시간은 한 프레임만 보면 튄다. 최근 것들을 굴려 평균을 낸다.
        static constexpr int SampleCount = 60;

        EditorApplication* m_editor = nullptr;
        float m_samples[SampleCount] = {};
        int m_nextSample = 0;
        int m_filledSamples = 0;
        std::uint64_t m_frames = 0;
        // 미터는 봉우리를 곧 떨어뜨리지 않고 천천히 내린다 - 한 블록만 보면 읽을 새가 없다.
        static constexpr std::uint32_t MaxMeteredBuses = 16;
        float m_busLevels[MaxMeteredBuses] = {};
        float m_masterLevel = 0.0f;
        static constexpr std::uint32_t SpectrumBands = 48;
        float m_spectrum[SpectrumBands] = {};
    };
}

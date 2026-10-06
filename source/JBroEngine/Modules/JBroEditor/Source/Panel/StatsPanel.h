#pragma once

#include <JBro/Editor/EditorPanel.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

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
    class StatsPanel final : public UniquePanel
    {
    public:
        // 패널 종류 이름이다(D-284). 종류 표와 `GetTitle` 이 같은 글자를 쓴다.
        static constexpr const char* TypeName = "Stats";

        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        Bool OnCreate(EditorApplication& editor) override;
        void OnUpdate(Float deltaTime) override;
        void OnDraw() override;
        EditorDock GetPreferredDock() const override { return EditorDock::Bottom; }

    private:
        void DrawAudioMeters(System::AudioSystem& audio, Float masterPeak);

        // 프레임 시간은 한 프레임만 보면 튄다. 최근 것들을 굴려 평균을 낸다.
        static constexpr Int32 SampleCount = 60;

        EditorApplication* m_editor = nullptr;
        Float m_samples[SampleCount] = {};
        Int32 m_nextSample = 0;
        Int32 m_filledSamples = 0;
        UInt64 m_frames = 0;
        // 미터는 봉우리를 곧 떨어뜨리지 않고 천천히 내린다 - 한 블록만 보면 읽을 새가 없다.
        static constexpr UInt32 MaxMeteredBuses = 16;
        Float m_busLevels[MaxMeteredBuses] = {};
        Float m_masterLevel = 0.0f;
        static constexpr UInt32 SpectrumBands = 48;
        Float m_spectrum[SpectrumBands] = {};
    };
}

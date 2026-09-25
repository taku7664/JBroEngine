#pragma once

#include <JBro/Canvas/GameSystem.h>

namespace JBro::System
{
    class AudioSystem;

    // 3D 의 소리 시스템이다(D-197). `Audio2DSystem` 과 같은 모양이고, 리스너 방향을 `Transform3D` 의 회전에서 읽는다.
    // 상태 기계·버스·클립은 차원과 무관한 `AudioSystem` 의 몫이다. **플레이 중에만 돈다.**
    class Audio3DSystem final : public GameSystem
    {
    public:
        static constexpr int ExecutionOrder = 450;

        explicit Audio3DSystem(AudioSystem& audio);
        int GetExecutionOrder() const override;
        void ReleaseAllSources(Canvas& canvas);

    protected:
        void OnUpdate(Canvas& canvas, float deltaTime) override;

    private:
        AudioSystem& m_audio;
        bool m_warnedListeners = false;
    };
}

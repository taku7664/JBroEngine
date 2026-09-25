#pragma once

#include <JBro/Canvas/GameSystem.h>

namespace JBro::System
{
    class AudioSystem;

    // 2D 의 소리 시스템이다(D-197). 리스너와 소스의 월드 위치를 `Transform2D` 에서 읽어 차원과 무관한 `AudioSystem` 에
    // 넘긴다 - 상태 기계·버스·클립은 그쪽 몫이다. **플레이 중에만 돈다**(프레임워크가 켜고 끈다). 끄면 소스의 보이스를
    // 멈추고 무장을 되돌린다 - 다시 켜면 `playOnStart` 가 한 번 다시 울린다.
    class Audio2DSystem final : public GameSystem
    {
    public:
        // 변환(100)·물리(200)·카메라(300)·추출(400) 뒤, 네트워크 송신(500) 앞이다. 그 프레임의 최종 위치로 소리를 옮긴다.
        static constexpr int ExecutionOrder = 450;

        explicit Audio2DSystem(AudioSystem& audio);
        int GetExecutionOrder() const override;
        // 게임이 멈출 때 부른다. 소스의 보이스를 멈추고 상태를 처음으로 되돌린다.
        void ReleaseAllSources(Canvas& canvas);

    protected:
        void OnUpdate(Canvas& canvas, float deltaTime) override;

    private:
        AudioSystem& m_audio;
        bool m_warnedListeners = false;
    };
}

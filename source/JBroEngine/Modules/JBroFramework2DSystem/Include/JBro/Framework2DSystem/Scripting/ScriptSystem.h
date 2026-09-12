#pragma once

#include <JBro/Canvas/GameSystem.h>
#include <JBro/Runtime/GameScriptBase.h>
#include <JBro/Types/Array.h>

#include <cstdint>

namespace JBro::System
{
    // 스크립트의 실행 순서와 수명 훅을 돌린다(D-45).
    //
    // ⚠ 미완이다. 여기서 도는 것은 `Canvas::AttachComponent<T>` 로 **정적으로** 붙인
    // 스크립트뿐이다. 사용자 스크립트는 DLL 안에서 이름으로 생성되어야 하고 그 경로는
    // 리플렉션(H5) 이 붙어야 열린다. Open Decision 3 이 못 박은 대로 완료로 치지 않는다.
    class ScriptSystem final : public GameSystem
    {
    public:
        int GetExecutionOrder() const override;

        // 한 프레임에서 실제로 돈 스크립트 수다. 순서 계약을 테스트가 붙잡는 손잡이다.
        std::size_t GetLastUpdateCount() const;
        std::size_t GetStartedCount() const;

    protected:
        void OnInitialize (Canvas& canvas) override;
        void OnFixedUpdate(Canvas& canvas, float fixedDeltaTime) override;
        void OnUpdate     (Canvas& canvas, float deltaTime) override;
        void OnShutdown   (Canvas& canvas) override;

    private:
        // 실행 순서 키다. 레이어 합성 순서가 가장 바깥이고, 그 안에서 계층 깊이(부모 먼저),
        // 그 안에서 부착 순서다. 같은 키가 겹치면 InstanceId 로 안정화한다.
        struct ScriptEntry
        {
            GameScriptBase* script = nullptr;
            std::uint32_t   layerOrder = 0;
            std::uint32_t   depth = 0;
            InstanceId      instanceId = InvalidInstanceId;
            bool            started = false;
        };

        void Rebuild(Canvas& canvas);
        static std::uint32_t MeasureDepth(const GameObject& object);

        Array<GameScriptBase*> m_collected;
        Array<ScriptEntry>     m_ordered;
        // 이미 OnCreate/OnStart 를 받은 스크립트다. 재생성된 슬롯과 헷갈리지 않도록
        // 주소가 아니라 InstanceId 로 기억한다.
        Array<InstanceId>      m_started;
        std::size_t            m_lastUpdateCount = 0;
    };
}

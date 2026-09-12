#pragma once

namespace JBro
{
    class Canvas;

    // 엔진 레이어 시스템 베이스. 사용자에게 노출되지 않는다.
    // 자기 Canvas 의 컴포넌트 풀을 ForEach 로 순회한다.
    class GameSystem
    {
    public:
        virtual ~GameSystem() = default;

        void Initialize (Canvas& canvas);
        void FixedUpdate(Canvas& canvas, float fixedDeltaTime);
        void Update     (Canvas& canvas, float deltaTime);
        void Shutdown   (Canvas& canvas);

        bool IsInitialized() const;
        bool IsEnabled()     const;
        void SetEnabled(bool enabled);
        virtual int GetExecutionOrder() const;

    protected:
        virtual void OnInitialize (Canvas& canvas);
        virtual void OnFixedUpdate(Canvas& canvas, float fixedDeltaTime);
        virtual void OnUpdate     (Canvas& canvas, float deltaTime);
        virtual void OnShutdown   (Canvas& canvas);

    private:
        bool m_initialized = false;
        bool m_enabled     = true;
    };
}

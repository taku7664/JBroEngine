#pragma once

#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

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
        void FixedUpdate(Canvas& canvas, Float fixedDeltaTime);
        void Update     (Canvas& canvas, Float deltaTime);
        void Shutdown   (Canvas& canvas);

        Bool IsInitialized() const;
        Bool IsEnabled()     const;
        void SetEnabled(Bool enabled);
        virtual Int32 GetExecutionOrder() const;

    protected:
        virtual void OnInitialize (Canvas& canvas);
        virtual void OnFixedUpdate(Canvas& canvas, Float fixedDeltaTime);
        virtual void OnUpdate     (Canvas& canvas, Float deltaTime);
        virtual void OnShutdown   (Canvas& canvas);

    private:
        Bool m_initialized = false;
        Bool m_enabled     = true;
    };
}

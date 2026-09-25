#pragma once

#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/System/IPhysics2DSystem.h>
#include <JBro/Canvas/GameSystem.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>

namespace JBro
{
    class GameObject;
}

namespace JBro::System
{
    // 2D 물리 커널(`JBroPhysics2D`)과 캔버스를 잇는 어댑터다(D-199, physics-plan §3.5).
    //
    // 고정 스텝마다 (1) 활성 `Rigidbody2D`·`Collider2D` 를 커널의 바디·도형에 맞추고, (2) 커널을 한 스텝 돌리고,
    // (3) 움직이는 몸의 자세와 속도를 컴포넌트에 되쓰고, (4) 닿기 시작한·떨어진 쌍을 두 오브젝트의 `GameScript2D` 에
    // 알린다(D-206). 커널 타입은 이 헤더에 나오지 않는다 - 이 헤더를 보는 에디터·호스트가 커널의 include 경로를
    // 갖지 않아도 되게, 상태는 cpp 안의 `State` 가 들고 있다.
    //
    // 질의(Raycast·OverlapBox)는 스텝과 무관하게 **지금의** 컴포넌트를 본다. 콜라이더를 끄거나 옮긴 직후에도 바로 맞다.
    class Physics2DSystem final : public GameSystem, public IPhysics2DSystem
    {
    public:
        Physics2DSystem();
        ~Physics2DSystem() override;

        int  GetExecutionOrder() const override;
        void SetGravity(Vec2 gravity);
        Vec2 GetGravity() const;

        bool Raycast(
            Vec2 origin,
            Vec2 direction,
            float distance,
            Collision2D& hit) const override;
        void OverlapBox(
            const Rect& area,
            Array<GameObjectHandle>& results) const override;

        // 커널에 올라간 바디와 도형의 수. 동기화가 만들고 지우는 것을 테스트가 붙잡는 손잡이다.
        std::size_t GetBodyCount() const;
        std::size_t GetShapeCount() const;

    protected:
        void OnInitialize (Canvas& canvas) override;
        void OnFixedUpdate(Canvas& canvas, float fixedDeltaTime) override;
        void OnShutdown   (Canvas& canvas) override;

    private:
        struct State;

        // 커널의 시작·끝 이벤트를 두 오브젝트의 GameScript2D 훅으로 보낸다(D-206).
        void DispatchEvents(Canvas& canvas);

        Canvas*         m_canvas = nullptr;
        Vec2            m_gravity{ 0.0f, -9.81f };
        OwnerPtr<State> m_state;
    };
}

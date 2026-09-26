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
    // 알린다(D-207). 커널 타입은 이 헤더에 나오지 않는다 - 이 헤더를 보는 에디터·호스트가 커널의 include 경로를
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
        // 좁은 판정을 나눌 물리 전용 워커 수(D-223). 다음 고정 스텝에서 커널에 먹인다. 0 이면 메인 한 스레드다.
        void          SetWorkerCount(std::uint32_t count);
        std::uint32_t GetWorkerCount() const;
        // 레이어 충돌 표(D-232). 비트 j 가 선 행 i 는 레이어 i 와 j 가 서로 지나간다. 다음 고정 스텝부터 먹는다.
        void SetIgnoredLayers(const std::uint32_t (&rows)[PhysicsLayerCount]);

        bool Raycast(Vec2 origin, Vec2 direction, float distance, RaycastHit2D& hit,
            std::uint32_t layerMask) const override;
        void RaycastAll(Vec2 origin, Vec2 direction, float distance, Array<RaycastHit2D>& hits,
            std::uint32_t layerMask) const override;
        void OverlapBox(const Rect& area, Array<GameObjectHandle>& results,
            std::uint32_t layerMask) const override;
        GameObjectHandle OverlapPoint(Vec2 point, std::uint32_t layerMask) const override;
        void OverlapCircle(Vec2 center, float radius, Array<GameObjectHandle>& results,
            std::uint32_t layerMask) const override;
        bool CircleCast(Vec2 origin, float radius, Vec2 direction, float distance, RaycastHit2D& hit,
            std::uint32_t layerMask) const override;
        bool BoxCast(Vec2 center, Vec2 halfExtents, float angle, Vec2 direction, float distance,
            RaycastHit2D& hit, std::uint32_t layerMask) const override;

        // 커널에 올라간 바디와 도형의 수. 동기화가 만들고 지우는 것을 테스트가 붙잡는 손잡이다.
        std::size_t GetBodyCount() const;
        std::size_t GetShapeCount() const;
        std::size_t GetJointCount() const;
        // 마지막 질의가 경계를 지나 조각을 들여다본 콜라이더 수(D-233). 경계 거르기를 테스트가 붙잡는 손잡이다.
        std::size_t GetLastQueryColliderCount() const;

    protected:
        void OnInitialize (Canvas& canvas) override;
        void OnFixedUpdate(Canvas& canvas, float fixedDeltaTime) override;
        void OnShutdown   (Canvas& canvas) override;

    private:
        struct State;

        // 커널의 시작·끝 이벤트를 두 오브젝트의 GameScript2D 훅으로 보낸다(D-207).
        void DispatchEvents(Canvas& canvas);
        // 켜진 콜라이더의 도형마다(폴리곤은 볼록 조각마다) 부른다. 모든 질의가 이 한 길로 도형을 본다 - 충돌과 같은 조각이다.
        template<typename Fn>
        // 질의 영역(area, 월드 축 정렬 상자)과 겹칠 수 있는 콜라이더의 조각만 부른다(D-233). 경계는 도형을 굽기 전에 원으로 어림한다.
        void ForEachQueryShape(std::uint32_t layerMask, const Rect& area, Fn&& visit) const;

        Canvas*         m_canvas = nullptr;
        Vec2            m_gravity{ 0.0f, -9.81f };
        std::uint32_t   m_workerCount = 0;
        mutable std::size_t m_lastQueryColliders = 0;
        std::uint32_t   m_ignoredLayers[PhysicsLayerCount] = {};
        OwnerPtr<State> m_state;
    };
}

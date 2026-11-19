#pragma once

#include <JBro/Canvas/GameSystem.h>
#include <JBro/Framework3D/System/IPhysics3DSystem.h>
#include <JBro/Types/Math3D.h>
#include <JBro/Types/SafePtr.h>

#include <cstddef>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/PhysicsLayerMask.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    class AssetSystem;
}

namespace JBro::System
{
    // 3D 물리 커널(`JBroPhysics3D`, 안은 Jolt)과 캔버스를 잇는 어댑터다(D-416, physics3d-plan §2.1). 2D 의 `Physics2DSystem` 자리다.
    //
    // 고정 스텝마다 (1) 켜진 `Collider3D` 를 오브젝트마다 모아 그 오브젝트의 바디 하나에 맞추고(고체는 복합 도형, 트리거는 저마다 센서),
    // `Rigidbody3D` 가 있으면 그 종류·성질을, 없으면 정적으로 둔다. (2) 커널을 한 스텝 돌리고, (3) 움직이는 몸의 자세와 속도를
    // `Transform3D`·`Rigidbody3D` 에 되쓴다. 커널 타입은 이 헤더에 나오지 않는다 - 에디터·호스트가 커널의 include 경로를 갖지 않아도 된다.
    // (4) 스텝이 끝나면 커널의 접촉 사건을 두 오브젝트의 `GameScript3D` 충돌·트리거 훅으로 보낸다(D-417). 질의(`IPhysics3DSystem`)는
    // 마지막 스텝에서 맞춘 바디를 본다.
    class Physics3DSystem final : public GameSystem, public IPhysics3DSystem
    {
    public:
        Physics3DSystem();
        ~Physics3DSystem() override;

        Int32 GetExecutionOrder() const override;
        void SetGravity(const Vector3& gravity);
        // 스텝을 나눌 물리 전용 워커 수(D-419). 다음 고정 스텝에서 커널에 먹인다. 0 이면 메인 한 스레드다.
        void SetWorkerCount(UInt32 count);
        UInt32 GetWorkerCount() const;
        // 프로젝트의 레이어 충돌 표(D-421, 2D 의 `Physics2DSystem::SetIgnoredLayers` 와 같은 뜻). 다음 고정 스텝부터 먹는다.
        void SetIgnoredLayers(const UInt32 (&rows)[PhysicsLayerCount]);
        // 메시 콜라이더가 모델의 메시 자료를 읽는 곳이다(D-418). 없으면 빌트인 도형만 메시 콜라이더가 된다.
        void SetAssets(const AssetSystem* assets);
        Vector3 GetGravity() const;

        // 커널에 올라간 바디와 센서의 수. 동기화가 만들고 지우는 것을 시험이 붙잡는 손잡이다.
        std::size_t GetBodyCount() const;
        std::size_t GetSensorCount() const;

        Bool Raycast(Vector3 origin, Vector3 direction, Float distance, RaycastHit3D& hit, UInt32 layerMask) const override;
        void RaycastAll(Vector3 origin, Vector3 direction, Float distance, Array<RaycastHit3D>& hits, UInt32 layerMask) const override;
        void OverlapSphere(Vector3 center, Float radius, Array<Handle::GameObject>& results, UInt32 layerMask) const override;
        void OverlapBox(Vector3 center, Vector3 halfExtents, Quaternion rotation, Array<Handle::GameObject>& results,
            UInt32 layerMask) const override;
        Bool SphereCast(Vector3 origin, Float radius, Vector3 direction, Float distance, RaycastHit3D& hit,
            UInt32 layerMask) const override;
        Bool BoxCast(Vector3 center, Vector3 halfExtents, Quaternion rotation, Vector3 direction, Float distance, RaycastHit3D& hit,
            UInt32 layerMask) const override;

    protected:
        void OnInitialize(Canvas& canvas) override;
        void OnFixedUpdate(Canvas& canvas, Float fixedDeltaTime) override;
        void OnShutdown(Canvas& canvas) override;

    private:
        struct State;

        // 커널의 시작·유지·끝 사건을 두 오브젝트의 GameScript3D 훅으로 보낸다(D-417).
        void DispatchEvents(Canvas& canvas);

        Canvas* m_canvas = nullptr;
        const AssetSystem* m_assets = nullptr;
        UInt32 m_workerCount = 0;
        UInt32 m_ignoredLayers[PhysicsLayerCount] = {};
        Vector3 m_gravity{0.0f, -9.81f, 0.0f};
        OwnerPtr<State> m_state;
    };
}

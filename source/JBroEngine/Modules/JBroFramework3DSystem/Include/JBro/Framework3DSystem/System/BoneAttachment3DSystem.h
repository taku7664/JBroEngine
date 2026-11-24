#pragma once

#include <JBro/Canvas/GameSystem.h>
#include <JBro/Framework3DSystem/System/Animation3DSystem.h>
#include <JBro/Runtime/TextStore.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Table.h>
#include <JBro/Types/UInt.h>

namespace JBro::System
{
    // **뼈에 붙이기**(D-460). `BoneAttachment3D` 마다 가장 가까운 조상의 `Animator3D` 를 찾아 소켓의 월드 자리를 그 오브젝트의 월드 캐시에 쓰고
    // 자식들에게 내린다. 자세 셈(350) 뒤, 렌더 추출(400) 앞이다. 물리(200)는 그 앞이라 붙은 콜라이더는 한 프레임 늦게 따른다.
    //
    // 이름은 소켓 이름의 칸 판번호나 소켓 출처의 판(`Animation3DSystem::GetSocketSourceRevision`)이 바뀔 때만 다시 푼다 - 프레임마다 견주지 않는다.
    class BoneAttachment3DSystem final : public GameSystem
    {
    public:
        static constexpr Int32 ExecutionOrder = 360;

        struct State
        {
            InstanceId instance = InvalidInstanceId;
            UInt32 stamp = 0;
            InstanceId animator = InvalidInstanceId;
            TextId name;
            UInt32 nameRevision = 0;
            UInt32 sourceRevision = 0;
            Bool found = false;
            Animation3DSystem::ResolvedSocket socket;
        };

        Int32 GetExecutionOrder() const override;
        void SetAnimation(Animation3DSystem* animation);
        // 그 붙이기의 상태다(시험이 본다). 아직 만나지 않았으면 null.
        const State* FindState(InstanceId attachment) const;

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;
        void OnShutdown(Canvas& canvas) override;

    private:
        Animation3DSystem* m_animation = nullptr;
        Array<State> m_states;
        Table<InstanceId, UInt32> m_stateOf;
        UInt32 m_stamp = 0;
    };
}

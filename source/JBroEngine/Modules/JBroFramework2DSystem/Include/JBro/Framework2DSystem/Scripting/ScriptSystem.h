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

        // 목록을 몇 번 다시 세웠는가. 더티 플래그가 실제로 일하는지를 테스트가 이 값으로
        // 본다 - 아무것도 바뀌지 않은 프레임에서 이 값이 오르면 지연 재구축이 아니다(A1).
        std::size_t GetRebuildCount() const;

    protected:
        void OnInitialize (Canvas& canvas) override;
        void OnFixedUpdate(Canvas& canvas, float fixedDeltaTime) override;
        void OnUpdate     (Canvas& canvas, float deltaTime) override;
        void OnShutdown   (Canvas& canvas) override;

    private:
        struct ScriptEntry
        {
            GameScriptBase* script = nullptr;
            InstanceId      instanceId = InvalidInstanceId;
            bool            started = false;
        };

        // **구 엔진과 같은 깊이 우선 순회다**(D-45, A3).
        //
        // 레이어 합성 순서로 루트를 줄 세우고, 루트마다 서브트리를 통째로 내려간다.
        // 한 오브젝트 안에서는 컴포넌트 배열 자리를 그대로 따른다 - `InstanceId` 로
        // 정렬하면 에디터에서 컴포넌트를 떼었다 되돌렸을 때(D-85 가 원래 자리로 보낸다)
        // 배열에서는 첫째인 것이 실행은 꼴찌가 된다.
        //
        // **매 프레임 돌지 않는다.** `Canvas::GetScriptOrderRevision()` 이 달라졌을 때만
        // 다시 세운다(D-45 의 지연 재구축).
        void EnsureOrder(Canvas& canvas);
        void Rebuild(Canvas& canvas);
        void AppendScripts(GameObject& object);
        bool IsScript(const ComponentBase* component) const;

        Array<GameScriptBase*>       m_collected;
        // `m_collected` 를 주소로 정렬한 것. 컴포넌트 슬롯이 스크립트인지 이분 탐색으로
        // 가른다 - 재구축은 cold path 이므로 여기서 dynamic_cast 를 쓰지 않는다(§9).
        Array<const ComponentBase*>  m_scriptKeys;
        Array<GameObject*>           m_roots;
        // 깊이 우선 순회를 재귀 대신 이 배열로 돈다. **재구축이 힙을 건드리면 안 된다** -
        // 정상 프레임에 스폰과 파괴가 들어 있고(D-54) 그것이 목록을 헌 것으로 만들므로,
        // 재구축은 드물어도 "정상 프레임" 안에서 일어난다. 멤버로 두면 `Clear` 가 용량을
        // 남기므로 두 번째 재구축부터 할당이 0 이다(§9).
        Array<GameObject*>           m_walkStack;
        Array<ScriptEntry>           m_ordered;
        // 이미 OnCreate/OnStart 를 받은 스크립트다. 재생성된 슬롯과 헷갈리지 않도록
        // 주소가 아니라 InstanceId 로 기억한다. 훑는 것은 재구축 때뿐이다.
        Array<InstanceId>            m_started;
        std::size_t                  m_lastUpdateCount = 0;
        std::size_t                  m_rebuildCount = 0;
        std::uint64_t                m_builtRevision = 0;
    };
}

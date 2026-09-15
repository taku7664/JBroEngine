#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2DSystem/Scripting/ScriptSystem.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/Array.h>

#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    // 어떤 순서로 무엇이 불렸는지 한 줄로 남긴다. 테스트가 보는 것은 이 기록뿐이다.
    JBro::Array<int> callLog;
    JBro::Array<int> createLog;
    JBro::Array<int> fixedLog;
    JBro::Array<int> destroyLog;

    class ProbeScript final : public JBro::GameScript2D
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Tests::ProbeScript";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        void OnCreate() override
        {
            createLog.Add(mark);
        }

        void OnStart() override
        {
            startCount++;
        }

        void OnUpdate(float deltaTime) override
        {
            lastDeltaTime = deltaTime;
            callLog.Add(mark);
        }

        void OnFixedUpdate(float fixedDeltaTime) override
        {
            lastFixedDeltaTime = fixedDeltaTime;
            fixedLog.Add(mark);
        }

        void OnDestroy() override
        {
            destroyLog.Add(mark);
        }

        int   mark = 0;
        int   startCount = 0;
        float lastDeltaTime = -1.0f;
        float lastFixedDeltaTime = -1.0f;
    };

    void ResetLogs()
    {
        callLog.Clear();
        createLog.Clear();
        fixedLog.Clear();
        destroyLog.Clear();
    }

    void TestScriptsRunInLayerThenHierarchyOrder()
    {
        ResetLogs();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::System::ScriptSystem scripts;
        scripts.Initialize(canvas);

        // 두 레이어. 뒤에 만든 레이어가 더 바깥 순서를 받는다.
        JBro::Layer& front = canvas.CreateLayer("front");

        // 기본 레이어에 부모 → 자식 → 손자 사슬을 세운다.
        JBro::GameObject* root = canvas.CreateObject("root");
        JBro::GameObject* child = canvas.CreateObject("child");
        JBro::GameObject* grandChild = canvas.CreateObject("grandchild");
        child->SetParent(root);
        grandChild->SetParent(child);

        // 계층의 손자를 먼저 붙여서, 순서가 부착 순서가 아니라 깊이로 정해지는지 본다.
        canvas.AttachComponent<ProbeScript>(grandChild)->mark = 3;
        canvas.AttachComponent<ProbeScript>(root)->mark = 1;
        canvas.AttachComponent<ProbeScript>(child)->mark = 2;

        // 앞 레이어의 오브젝트는 기본 레이어 전부보다 뒤에 와야 한다.
        JBro::GameObject* onFront = canvas.CreateObject("on front");
        Check(canvas.SetObjectLayer(onFront, front.GetId()), "the probe object must move layers");
        canvas.AttachComponent<ProbeScript>(onFront)->mark = 9;

        scripts.Update(canvas, 1.0f / 60.0f);
        Check(scripts.GetLastUpdateCount() == 4, "every active script must run once per update");
        Check(callLog.Size() == 4, "the log must hold one entry per script");
        Check(callLog[0] == 1 && callLog[1] == 2 && callLog[2] == 3,
            "scripts must run parents before children inside one layer");
        Check(callLog[3] == 9, "layer order must outrank hierarchy depth");

        scripts.Shutdown(canvas);
    }

    void TestStartHappensOnceAndBeforeTheSameFrameUpdate()
    {
        ResetLogs();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::System::ScriptSystem scripts;
        scripts.Initialize(canvas);

        JBro::GameObject* object = canvas.CreateObject("scripted");
        auto* script = canvas.AttachComponent<ProbeScript>(object);
        script->mark = 1;

        scripts.Update(canvas, 0.25f);
        Check(createLog.Size() == 1 && script->startCount == 1,
            "a new script must be created and started exactly once");
        Check(callLog.Size() == 1 && script->lastDeltaTime == 0.25f,
            "the start hooks must be followed by an update in the same frame");

        scripts.Update(canvas, 0.5f);
        Check(createLog.Size() == 1 && script->startCount == 1,
            "an already started script must not be started again");
        Check(callLog.Size() == 2, "an already started script must keep updating");

        // 나중에 붙은 스크립트는 그 프레임에 시작한다.
        JBro::GameObject* late = canvas.CreateObject("late");
        auto* lateScript = canvas.AttachComponent<ProbeScript>(late);
        lateScript->mark = 2;
        scripts.Update(canvas, 0.5f);
        Check(createLog.Size() == 2 && lateScript->startCount == 1,
            "a script attached later must start on its first update");

        scripts.Shutdown(canvas);
        Check(destroyLog.Size() == 2, "shutdown must destroy every started script");
    }

    void TestFixedStepsReuseTheOrderAndSkipUnstartedScripts()
    {
        ResetLogs();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::System::ScriptSystem scripts;
        scripts.Initialize(canvas);

        JBro::GameObject* first = canvas.CreateObject("first");
        canvas.AttachComponent<ProbeScript>(first)->mark = 1;

        // 아직 한 번도 Update 를 돌지 않았으므로 고정 스텝은 아무것도 부르면 안 된다.
        scripts.FixedUpdate(canvas, 1.0f / 60.0f);
        Check(fixedLog.IsEmpty(), "a script that never started must not receive a fixed step");

        scripts.Update(canvas, 0.016f);
        scripts.FixedUpdate(canvas, 1.0f / 60.0f);
        scripts.FixedUpdate(canvas, 1.0f / 60.0f);
        Check(fixedLog.Size() == 2, "each fixed step must reach every started script once");
        Check(fixedLog[0] == 1 && fixedLog[1] == 1, "fixed steps must reuse the established order");

        scripts.Shutdown(canvas);
    }

    void TestDisabledAndDestroyedScriptsLeaveTheSchedule()
    {
        ResetLogs();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::System::ScriptSystem scripts;
        scripts.Initialize(canvas);

        JBro::GameObject* kept = canvas.CreateObject("kept");
        JBro::GameObject* dropped = canvas.CreateObject("dropped");
        auto* keptScript = canvas.AttachComponent<ProbeScript>(kept);
        auto* droppedScript = canvas.AttachComponent<ProbeScript>(dropped);
        keptScript->mark = 1;
        droppedScript->mark = 2;

        scripts.Update(canvas, 0.016f);
        Check(callLog.Size() == 2, "both scripts must run while both are active");

        droppedScript->SetEnabled(false);
        callLog.Clear();
        scripts.Update(canvas, 0.016f);
        Check(callLog.Size() == 1 && callLog[0] == 1,
            "a disabled script must drop out of the schedule");

        droppedScript->SetEnabled(true);
        callLog.Clear();
        scripts.Update(canvas, 0.016f);
        Check(callLog.Size() == 2, "re-enabling must put the script back in the schedule");
        Check(createLog.Size() == 2, "re-enabling must not start the script a second time");

        Check(canvas.DestroyObject(dropped), "the dropped object must be destroyed");
        canvas.FlushPendingDestroy();
        callLog.Clear();
        scripts.Update(canvas, 0.016f);
        Check(callLog.Size() == 1 && callLog[0] == 1,
            "a destroyed script must leave the schedule without being touched");
        // 죽은 스크립트의 id 가 시작 목록에 남으면 그 목록은 세션 내내 자란다.
        Check(scripts.GetStartedCount() == 1,
            "a destroyed script must not keep a slot in the started list");

        // 껐다 켠 것은 여전히 살아 있으므로 목록에 남아야 한다.
        keptScript->SetEnabled(false);
        scripts.Update(canvas, 0.016f);
        Check(scripts.GetStartedCount() == 1,
            "a merely disabled script must keep its started slot");
        keptScript->SetEnabled(true);
        createLog.Clear();
        scripts.Update(canvas, 0.016f);
        Check(createLog.IsEmpty(), "a re-enabled script must not be created again");

        scripts.Shutdown(canvas);
    }

    // ── 순회 중 파괴 (A2 재현) ────────────────────────────────────────────
    //
    // `ProjectRule.md` §8 은 "`Canvas` 가 순회 깊이 가드를 소유하고 `ForEach<T>` 와
    // **스크립트 실행 목록 순회**에 적용한다" 이고, 순회 중 파괴는 지연 큐로 가야 한다(D-45).
    //
    // 크래시에 기대어 재지 않는다. 풀의 슬롯 메모리는 파괴 뒤에도 남아 있어서 죽은 객체
    // 위에서 가상 함수를 불러도 그냥 도는 수가 있고, 그러면 "안 터졌으니 괜찮다" 는
    // 잘못된 결론이 난다. 그래서 파수병 값을 두고 **파괴된 뒤에 불렸는지를 직접 본다**.
    JBro::Canvas*    activeCanvas = nullptr;
    JBro::GameObject* victimObject = nullptr;
    bool victimRanWhileAlive = false;
    bool victimRanAfterDestruction = false;

    class VictimScript final : public JBro::GameScript2D
    {
    public:
        static constexpr int AliveMark = 0x5A5A1234;
        static constexpr const char* StaticTypeName()
        {
            return "Tests::VictimScript";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        ~VictimScript() override
        {
            // 파괴자가 지나갔다는 표식이다. 슬롯은 재사용 전까지 이 값을 들고 있다.
            m_liveMark = 0;
        }

        void OnUpdate(float deltaTime) override
        {
            (void)deltaTime;
            if (m_liveMark == AliveMark)
            {
                victimRanWhileAlive = true;
                return;
            }
            victimRanAfterDestruction = true;
        }

    private:
        int m_liveMark = AliveMark;
    };

    class KillerScript final : public JBro::GameScript2D
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Tests::KillerScript";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        void OnUpdate(float deltaTime) override
        {
            (void)deltaTime;
            if (activeCanvas == nullptr || victimObject == nullptr)
            {
                return;
            }
            // 사용자 스크립트가 흔히 하는 일이다 - 훅 안에서 다른 오브젝트를 지운다.
            activeCanvas->DestroyObject(victimObject);
            victimObject = nullptr;
        }
    };

    void TestDestroyingAnObjectFromAScriptHookIsDeferred()
    {
        ResetLogs();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::System::ScriptSystem scripts;
        scripts.Initialize(canvas);

        activeCanvas = &canvas;
        victimRanWhileAlive = false;
        victimRanAfterDestruction = false;

        // 먼저 만든 것이 먼저 돈다(같은 레이어·같은 깊이면 InstanceId 순서다).
        JBro::GameObject* killer = canvas.CreateObject("killer");
        JBro::GameObject* victim = canvas.CreateObject("victim");
        victimObject = victim;
        canvas.AttachComponent<KillerScript>(killer);
        canvas.AttachComponent<VictimScript>(victim);

        scripts.Update(canvas, 0.016f);

        Check(false == victimRanAfterDestruction,
            "a script must never receive OnUpdate after its own destructor ran");
        Check(victimRanWhileAlive,
            "a destroy requested inside a script hook must be deferred to a safe point");

        canvas.FlushPendingDestroy();
        victimRanWhileAlive = false;
        scripts.Update(canvas, 0.016f);
        Check(false == victimRanWhileAlive && false == victimRanAfterDestruction,
            "the deferred destroy must take effect before the next frame");

        activeCanvas = nullptr;
        victimObject = nullptr;
        scripts.Shutdown(canvas);
    }

    // ── 깊이 우선 순회 (A3) ──────────────────────────────────────────────
    //
    // 구 엔진은 루트마다 서브트리를 통째로 내려간다. 깊이로 평면 정렬하면 "부모 먼저" 는
    // 지켜지지만 **형제 서브트리가 섞인다** - 한 덩어리로 돌 줄 알았던 오브젝트 묶음
    // 사이로 남의 자식이 끼어든다.
    void TestSiblingSubtreesDoNotInterleave()
    {
        ResetLogs();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::System::ScriptSystem scripts;
        scripts.Initialize(canvas);

        // 만드는 차례가 A, A의 자식, B, B의 자식이다.
        JBro::GameObject* rootA = canvas.CreateObject("A");
        JBro::GameObject* childA = canvas.CreateObject("A1");
        JBro::GameObject* rootB = canvas.CreateObject("B");
        JBro::GameObject* childB = canvas.CreateObject("B1");
        childA->SetParent(rootA);
        childB->SetParent(rootB);

        canvas.AttachComponent<ProbeScript>(rootA)->mark = 1;
        canvas.AttachComponent<ProbeScript>(childA)->mark = 2;
        canvas.AttachComponent<ProbeScript>(rootB)->mark = 3;
        canvas.AttachComponent<ProbeScript>(childB)->mark = 4;

        scripts.Update(canvas, 0.016f);

        Check(callLog.Size() == 4, "every script must run once");
        // 깊이로 정렬하면 1, 3, 2, 4 가 된다. 서브트리를 내려가면 1, 2, 3, 4 다.
        Check(callLog[0] == 1 && callLog[1] == 2 && callLog[2] == 3 && callLog[3] == 4,
            "a root subtree must run to the end before the next root starts");

        scripts.Shutdown(canvas);
    }

    // 한 오브젝트 안의 차례는 **컴포넌트 배열 자리**다. `InstanceId` 로 정렬하면
    // 에디터가 컴포넌트를 떼었다 되돌렸을 때(D-85 가 원래 자리로 보낸다) 배열에서
    // 첫째인 것이 실행은 꼴찌가 된다.
    void TestScriptsInOneObjectFollowTheComponentSlotOrder()
    {
        ResetLogs();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::System::ScriptSystem scripts;
        scripts.Initialize(canvas);

        JBro::GameObject* object = canvas.CreateObject("two scripts");
        auto* first = canvas.AttachComponent<ProbeScript>(object);
        auto* second = canvas.AttachComponent<ProbeScript>(object);
        first->mark = 1;
        second->mark = 2;

        scripts.Update(canvas, 0.016f);
        Check(callLog.Size() == 2 && callLog[0] == 1 && callLog[1] == 2,
            "attachment order must be the order they run in");

        // 나중에 붙인 것을 맨 앞자리로 보낸다. 되돌리기가 하는 일과 같다.
        Check(object->SetComponentIndex(second, 0), "the component must move to the first slot");
        callLog.Clear();
        scripts.Update(canvas, 0.016f);
        Check(callLog.Size() == 2 && callLog[0] == 2 && callLog[1] == 1,
            "moving a component to another slot must move its turn with it");

        scripts.Shutdown(canvas);
    }

    // ── 지연 재구축 (A1) ─────────────────────────────────────────────────
    //
    // 목록은 바뀐 것이 있을 때만 다시 세운다. 매 프레임 다시 세우면 수집·정렬이 전부
    // 프레임 비용이 된다.
    void TestTheOrderIsRebuiltOnlyWhenSomethingChangedIt()
    {
        ResetLogs();
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::System::ScriptSystem scripts;
        scripts.Initialize(canvas);

        JBro::GameObject* parent = canvas.CreateObject("parent");
        JBro::GameObject* child = canvas.CreateObject("child");
        canvas.AttachComponent<ProbeScript>(parent)->mark = 1;
        canvas.AttachComponent<ProbeScript>(child)->mark = 2;

        scripts.Update(canvas, 0.016f);
        const std::size_t afterFirst = scripts.GetRebuildCount();
        Check(afterFirst >= 1, "the first update must build the order");

        // 아무것도 건드리지 않은 프레임 셋.
        scripts.Update(canvas, 0.016f);
        scripts.Update(canvas, 0.016f);
        scripts.Update(canvas, 0.016f);
        Check(scripts.GetRebuildCount() == afterFirst,
            "an untouched frame must not rebuild the execution order");

        // 스크립트를 껐다 켜는 것은 순서를 바꾸지 않는다 - 목록에 남고 도는 것만 고른다.
        JBro::Ref<ProbeScript> childScript = child->GetComponent<ProbeScript>();
        Check(childScript.Get() != nullptr, "the child script must be reachable");
        childScript->SetEnabled(false);
        callLog.Clear();
        scripts.Update(canvas, 0.016f);
        Check(callLog.Size() == 1 && callLog[0] == 1, "a disabled script must not run");
        Check(scripts.GetRebuildCount() == afterFirst,
            "enabling and disabling must not rebuild the order");
        childScript->SetEnabled(true);

        // D-45 가 이름을 댄 트리거들. 하나씩 세어 본다.
        const std::size_t beforeParent = scripts.GetRebuildCount();
        child->SetParent(parent);
        scripts.Update(canvas, 0.016f);
        Check(scripts.GetRebuildCount() == beforeParent + 1,
            "SetParent must mark the order stale");

        const std::size_t beforeLayer = scripts.GetRebuildCount();
        JBro::Layer& extra = canvas.CreateLayer("extra");
        Check(canvas.SetObjectLayer(parent, extra.GetId()), "the object must change layers");
        scripts.Update(canvas, 0.016f);
        Check(scripts.GetRebuildCount() > beforeLayer,
            "creating a layer and moving an object into it must mark the order stale");

        const std::size_t beforeAttach = scripts.GetRebuildCount();
        JBro::GameObject* late = canvas.CreateObject("late");
        canvas.AttachComponent<ProbeScript>(late)->mark = 3;
        scripts.Update(canvas, 0.016f);
        Check(scripts.GetRebuildCount() == beforeAttach + 1,
            "attaching a script must mark the order stale");

        // 스크립트가 아닌 컴포넌트를 붙이는 것은 순서와 상관이 없다. 매 프레임 스폰이
        // 목록을 헌 것으로 만들면 더티 플래그를 둔 뜻이 없어진다.
        const std::size_t beforePlain = scripts.GetRebuildCount();
        canvas.AttachComponent<JBro::Component::Transform2D>(late);
        scripts.Update(canvas, 0.016f);
        Check(scripts.GetRebuildCount() == beforePlain,
            "attaching a component that is not a script must not rebuild the order");

        scripts.Shutdown(canvas);
    }
}

int RunScriptSchedulingTests()
{
    TestScriptsRunInLayerThenHierarchyOrder();
    TestStartHappensOnceAndBeforeTheSameFrameUpdate();
    TestFixedStepsReuseTheOrderAndSkipUnstartedScripts();
    TestDisabledAndDestroyedScriptsLeaveTheSchedule();
    TestDestroyingAnObjectFromAScriptHookIsDeferred();
    TestSiblingSubtreesDoNotInterleave();
    TestScriptsInOneObjectFollowTheComponentSlotOrder();
    TestTheOrderIsRebuiltOnlyWhenSomethingChangedIt();
    std::cout << "Script scheduling tests passed.\n";
    return 0;
}

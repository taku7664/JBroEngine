#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
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
}

int RunScriptSchedulingTests()
{
    TestScriptsRunInLayerThenHierarchyOrder();
    TestStartHappensOnceAndBeforeTheSameFrameUpdate();
    TestFixedStepsReuseTheOrderAndSkipUnstartedScripts();
    TestDisabledAndDestroyedScriptsLeaveTheSchedule();
    std::cout << "Script scheduling tests passed.\n";
    return 0;
}

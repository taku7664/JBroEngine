#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Handle/Rigidbody2D.h>
#include <JBro/Framework2D/Handle/Transform2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/ServiceContext.h>
#include <JBro/Runtime/ScriptRegistry.h>
#include <JBro/Runtime/SystemContext.h>
#include <JBro/Script/Macros.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include "TestClock.h"

#include <cstring>
#include <iostream>
#include <stdexcept>

// 스크립트의 오브젝트 만들기·찾기(D-301). 스크립트가 쓰는 길 그대로 `GetServiceContext().Objects` 와 핸들만 부른다.
namespace
{
    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    JBro::Bool Same(const JBro::Handle::GameObject& left, const JBro::Handle::GameObject& right)
    {
        return left.IsValid() && left.GetInstanceId() == right.GetInstanceId();
    }

    JBro::Int32 g_frame = 0;

    // 만들어진 스크립트가 처음 시작한 프레임과 그 뒤로 몇 번 갱신됐는지 적는다.
    JBRO_SCRIPT(ObjectServiceCounter) final : public JBro::GameScript2D
    {
        JBRO_SCRIPT_BODY(ObjectServiceCounter)

    public:
        void OnStart() override
        {
            startFrame = g_frame;
        }

        void OnUpdate() override
        {
            ++updates;
        }

        JBro::Int32 startFrame = -1;
        JBro::Int32 updates = 0;
    };

    // 첫 갱신 안에서(스크립트 순회 중에) 오브젝트를 만들고 스크립트를 붙인다.
    JBRO_SCRIPT(ObjectServiceSpawner) final : public JBro::GameScript2D
    {
        JBRO_SCRIPT_BODY(ObjectServiceSpawner)

    public:
        void OnUpdate() override
        {
            if (spawned.IsValid())
            {
                return;
            }
            spawned = JBro::GetServiceContext().Objects.Create("Spawned");
            counter = spawned.AddScript<ObjectServiceCounter>();
            spawnFrame = g_frame;
            foundInSameFrame = Same(JBro::GetServiceContext().Objects.Find("Spawned"), spawned);
        }

        JBro::Handle::GameObject spawned;
        JBro::Ref<ObjectServiceCounter> counter;
        JBro::Int32 spawnFrame = -1;
        JBro::Bool foundInSameFrame = false;
    };

    void TestCreateFindAndLayers(JBro::Framework2D& framework)
    {
        const JBro::Service::ObjectService& objects = JBro::GetServiceContext().Objects;
        JBro::Canvas& canvas = *framework.GetCanvas();
        framework.CreateLayer("UI");

        // 만들기: 기본 트랜스폼이 붙고, 부모를 주면 그 레이어를 따른다.
        JBro::Handle::GameObject player = objects.Create("Player");
        Check(player.IsValid() && 0 == std::strcmp(player.GetName(), "Player"), "Create makes a named object");
        Check(player.GetComponent<JBro::Handle::Transform2D>().IsValid(), "the framework attaches its default transform");
        Check(player.GetLayer().GetId() == canvas.GetDefaultLayer(), "without a layer or parent it goes to the default layer");
        JBro::Handle::GameObject weapon = objects.Create("Weapon", player);
        JBro::Handle::GameObject muzzle = objects.Create("Muzzle", weapon);
        Check(Same(weapon.GetParent(), player) && player.GetChildCount() == 1 && Same(player.GetChild(0), weapon),
            "a parent puts the new object under it");

        JBro::Handle::Layer ui = objects.GetLayer("UI");
        Check(ui.IsValid() && 0 == std::strcmp(ui.GetName(), "UI"), "GetLayer finds a layer by name");
        Check(false == objects.GetLayer("Nowhere").IsValid(), "an unknown layer is an empty handle");
        JBro::Handle::GameObject hud = objects.Create("HUD", {}, ui);
        JBro::Handle::GameObject hpBar = objects.Create("HpBar", hud);
        Check(hud.GetLayer() == ui && hpBar.GetLayer() == ui, "a child takes its parent's layer");

        // `[]` 는 한 단계만 본다. 레이어는 뿌리를, 오브젝트는 직계 자식을 본다.
        Check(Same(ui["HUD"]["HpBar"], hpBar), "layer[name][name] walks one level at a time");
        Check(false == ui["HpBar"].IsValid(), "a layer's [] sees only its roots");
        Check(false == ui["Player"].IsValid(), "and only its own roots");
        Check(Same(player["Weapon"], weapon) && false == player["Muzzle"].IsValid(), "an object's [] sees only its children");
        Check(Same(player.FindInChildren("Muzzle"), muzzle), "FindInChildren goes as deep as needed");

        // 찾기는 이름만 본다. 태그는 함수로만 찾는다.
        JBro::Handle::GameObject first = objects.Create("Enemy");
        JBro::Handle::GameObject second = objects.Create("Enemy");
        first.SetTag("Foe");
        second.SetTag("Foe");
        Check(Same(objects.Find("HpBar"), hpBar), "Find looks through the whole canvas");
        Check(Same(objects.Find("Enemy"), first), "the first by hierarchy order wins");
        JBro::Array<JBro::Handle::GameObject> found;
        objects.FindAll("Enemy", found);
        Check(found.Size() == 2, "FindAll gives every match");
        objects.FindAllWithTag("Foe", found);
        Check(found.Size() == 2 && Same(objects.FindWithTag("Foe"), first), "tags are found with the tag functions");
        Check(false == objects.Find("Foe").IsValid(), "and Find does not look at tags");

        // 붙이기: 빌트인은 핸들, 스크립트는 Ref<T>.
        JBro::Handle::Rigidbody2D body = first.AddComponent<JBro::Handle::Rigidbody2D>();
        Check(body.IsValid() && Same(body.GetGameObject(), first), "AddComponent gives the new component's handle");

        // 지우기를 요청한 순간 모든 사본이 무효이고 찾기에서 빠진다 - 순회 중이라 실제 파괴는 안전 지점까지 미뤄진다.
        const JBro::Handle::GameObject copy = second;
        {
            JBro::Canvas::IterationGuard guard(canvas);
            second.Destroy();
            Check(false == copy.IsValid(), "every copy goes invalid as soon as destroy is requested");
            objects.FindAll("Enemy", found);
            Check(found.Size() == 1 && Same(found[0], first), "and the object drops out of finds");
            Check(canvas.GetPendingDestroyCount() == 1, "while the canvas still waits for a safe point");
        }
        canvas.FlushPendingDestroy();

        // 부모 바꾸기.
        hpBar.SetParent(player);
        Check(Same(player["HpBar"], hpBar) && false == hud["HpBar"].IsValid(), "SetParent moves the object");
        hpBar.SetParent({});
        Check(false == hpBar.GetParent().IsValid(), "an empty parent makes it a root");
    }

    void TestScriptsCreatedDuringUpdate(JBro::Framework2D& framework)
    {
        JBro::Canvas& canvas = *framework.GetCanvas();
        JBro::Object::GameObject* host = canvas.CreateObject("Spawner");
        ObjectServiceSpawner* spawner = canvas.AttachScript<ObjectServiceSpawner>(host);
        Check(spawner != nullptr, "the spawner attaches");

        g_frame = 1;
        framework.Update();
        Check(spawner->spawned.IsValid() && spawner->foundInSameFrame, "an object made during update is there at once");
        ObjectServiceCounter* counter = spawner->counter.Get();
        Check(counter != nullptr, "AddScript gives a reference to the new script");
        // **재었다**(D-301 (5)): 스크립트 순회 중에 붙인 스크립트는 그 프레임에는 돌지 않고 다음 프레임에 OnStart·OnUpdate 를 받는다.
        Check(counter->startFrame == -1 && counter->updates == 0, "a script added during update does not run that frame");
        g_frame = 2;
        framework.Update();
        Check(counter->startFrame == 2 && counter->updates == 1, "it starts and updates on the next frame");
    }
}

JBro::Int32 RunObjectServiceTests()
{
    // 이 시험만의 스크립트 표다. 전역 표에 시험 타입을 남기지 않는다.
    JBro::ScriptRegistry scripts;
    Check(scripts.Register(JBro::MakeScriptTypeInfo<ObjectServiceCounter>()), "the counter registers by name");
    JBro::ScriptRegistry::Bind(&scripts);
    try
    {
        // 묶이지 않았으면 서비스는 빈 값을 준다.
        Check(false == JBro::GetServiceContext().Objects.Create("Loose").IsValid(), "without a canvas nothing is made");

        JBro::Framework2D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        Check(framework.Initialize(context), "the framework initializes");
        Check(framework.BindScriptContexts(), "binding the script contexts plugs the object system in");
        Check(JBro::GetSystemContext().Objects != nullptr, "the object system is on the shared context");

        TestCreateFindAndLayers(framework);
        TestScriptsCreatedDuringUpdate(framework);

        framework.UnbindScriptContexts();
        Check(JBro::GetSystemContext().Objects == nullptr, "unbinding takes it off again");
        framework.Shutdown();
    }
    catch (...)
    {
        JBro::ScriptRegistry::Bind(nullptr);
        throw;
    }
    JBro::ScriptRegistry::Bind(nullptr);
    std::cout << "Object service tests passed.\n";
    return 0;
}

#include <JBro/Core/Core.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Framework2D/Layer2D.h>
#include <JBro/Framework3D/Framework3D.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Runtime/Ref.h>

#include <iostream>
#include <stdexcept>

namespace
{
    template<typename T>
    concept HasLayerOpacity = requires(T& layer)
    {
        layer.SetOpacity(0.5f);
    };

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    void TestObjectComponentSkeletonCompiles()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());

        // 캔버스는 기본 레이어 하나를 가지고 시작한다.
        Check(canvas.GetLayerCount() == 1, "canvas must start with the default layer");
        Check(canvas.GetDefaultLayer() != JBro::InvalidLayerIndex, "default layer must be set");

        // 새 레이어 추가·이동·제거.
        JBro::Layer& background = canvas.CreateLayer("Background");
        JBro::Layer& foreground = canvas.CreateLayer("Foreground");
        Check(canvas.GetLayerCount() == 3, "created layers must be counted");
        foreground.SetVisible(false);
        Check(false == foreground.IsVisible(), "runtime layer visibility must remain dimension independent");
        Check(canvas.MoveLayer(foreground.GetIndex(), 0), "layer order must be mutable");
        Check(canvas.GetLayerAt(0)->GetIndex() == foreground.GetIndex(), "moved layer must appear at requested slot");
        const JBro::LayerIndex backgroundIndex = background.GetIndex();
        Check(canvas.DestroyLayer(backgroundIndex), "non-default layer must be destroyable");
        Check(canvas.GetLayerCount() == 2, "destroy must shrink the layer list");
        Check(canvas.FindLayer(backgroundIndex) == nullptr,
            "a destroyed layer lookup must return null without indexing outside the layer array");
        Check(false == canvas.MoveLayer(backgroundIndex, 0),
            "moving a missing layer must fail without indexing outside the layer array");
        Check(false == canvas.DestroyLayer(backgroundIndex),
            "destroying a missing layer must fail without indexing outside the layer array");
        JBro::GameObject* object = canvas.CreateObject("layer lookup");
        Check(object != nullptr && false == canvas.SetObjectLayer(object, backgroundIndex),
            "assigning a missing layer must fail without indexing outside the layer array");

        static_assert(false == HasLayerOpacity<JBro::Layer>,
            "runtime Layer must not expose Framework2D composition state");
    }

    void TestFramework2DBootstraps()
    {
        JBro::Framework2D framework;
        JBro::FrameworkContext context;
        Check(framework.Initialize(context), "framework must initialize with a default allocator fallback");
        Check(framework.GetCanvas() != nullptr, "framework must own a canvas");
        Check(framework.GetCanvas()->GetLayerCount() == 1, "framework canvas must have the default layer");
        JBro::Layer2D* defaultState =
            framework.GetLayer2D(framework.GetCanvas()->GetDefaultLayer());
        Check(defaultState != nullptr, "Framework2D must own state for its default runtime layer");
        JBro::Layer* foreground = framework.CreateLayer("Foreground");
        Check(foreground != nullptr, "Framework2D must create a runtime layer and its 2D state together");
        JBro::Layer2D* foregroundState = framework.GetLayer2D(foreground->GetIndex());
        Check(foregroundState != nullptr, "created Framework2D layer must expose its 2D state");
        foregroundState->SetOpacity(0.5f);
        foregroundState->SetBlendMode(JBro::Layer2D::BlendMode::Additive);
        Check(foregroundState->GetOpacity() == 0.5f
            && foregroundState->GetBlendMode() == JBro::Layer2D::BlendMode::Additive,
            "Layer2D must retain Framework2D composition settings");
        const JBro::LayerIndex foregroundIndex = foreground->GetIndex();
        Check(framework.DestroyLayer(foregroundIndex),
            "Framework2D must destroy runtime layer and 2D state together");
        Check(framework.GetLayer2D(foregroundIndex) == nullptr,
            "destroyed runtime layer must not retain accessible Framework2D state");

        JBro::Canvas* runtimeCanvas = framework.GetCanvas();
        JBro::Layer& directLayer = runtimeCanvas->CreateLayer("Direct runtime layer");
        const JBro::LayerIndex directLayerIndex = directLayer.GetIndex();
        Check(framework.GetLayer2D(directLayerIndex) != nullptr,
            "direct runtime layer creation must also create Framework2D state");
        Check(runtimeCanvas->DestroyLayer(directLayerIndex),
            "direct runtime layer destruction must succeed");
        Check(framework.GetLayer2D(directLayerIndex) == nullptr,
            "direct runtime layer destruction must also release Framework2D state");

        framework.Update(1.0f / 60.0f);
        framework.Shutdown();
        Check(framework.GetCanvas() == nullptr, "shutdown must release the canvas");
    }

    void TestFramework3DBootstrapsRuntimeCanvas()
    {
        static_assert(std::is_base_of_v<JBro::ComponentBase, JBro::Component::Transform3D>);
        static_assert(std::is_base_of_v<JBro::ComponentBase, JBro::Component::Camera3D>);
        static_assert(std::is_base_of_v<JBro::ComponentBase, JBro::Component::MeshRenderer3D>);
        static_assert(std::is_base_of_v<JBro::ComponentBase, JBro::Component::Rigidbody3D>);
        static_assert(std::is_base_of_v<JBro::ComponentBase, JBro::Component::Collider3D>);

        JBro::Framework3D framework;
        JBro::FrameworkContext context;
        Check(framework.Initialize(context), "Framework3D must initialize its runtime canvas");
        JBro::Canvas* canvas = framework.GetCanvas();
        Check(canvas != nullptr, "Framework3D must expose its runtime canvas");
        JBro::GameObject* object = canvas->CreateObject("3D object");
        Check(object != nullptr, "Framework3D canvas must create objects without Framework2D");
        auto* transform = canvas->AttachComponent<JBro::Component::Transform3D>(object);
        Check(transform != nullptr
            && transform->GetOwner().GetInstanceId() == object->GetInstanceId(),
            "Framework3D canvas must own polymorphic 3D components");
        framework.Shutdown();
        Check(framework.GetCanvas() == nullptr, "Framework3D shutdown must release its runtime canvas");
    }

    // D-48: 부착·분리·활성 전환 훅이 정확히 그 지점에서 한 번씩 불린다.
    class LifecycleProbe final : public JBro::ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Test::LifecycleProbe";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        void OnAttached() override
        {
            ++attached;
            ownerAtAttach = GetOwner().GetInstanceId();
            idAtAttach = GetInstanceId();
        }

        void OnDetached() override
        {
            ++detached;
            if (detachedObserver != nullptr)
            {
                ++(*detachedObserver);
            }
        }
        void OnEnabled() override   { ++enabled; }
        void OnDisabled() override  { ++disabled; }

        int attached = 0;
        int detached = 0;
        int enabled = 0;
        int disabled = 0;
        JBro::InstanceId ownerAtAttach = JBro::InvalidInstanceId;
        JBro::InstanceId idAtAttach = JBro::InvalidInstanceId;
        int* detachedObserver = nullptr;
    };

    void TestComponentLifecycleHooks()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        auto* object = canvas.CreateObject("lifecycle owner");
        auto* probe = canvas.AttachComponent<LifecycleProbe>(object);
        Check(probe != nullptr && probe->attached == 1 && probe->detached == 0,
            "attaching a component must run its attach hook exactly once");
        Check(probe->ownerAtAttach == object->GetInstanceId()
            && probe->idAtAttach != JBro::InvalidInstanceId,
            "the attach hook must see a settled owner and identity");

        probe->SetEnabled(false);
        probe->SetEnabled(false);
        Check(probe->disabled == 1 && probe->enabled == 0,
            "only an actual change of enablement may run the hook");
        probe->SetEnabled(true);
        Check(probe->enabled == 1, "re-enabling must run the enable hook");

        int detachedSeen = 0;
        probe->detachedObserver = &detachedSeen;
        Check(canvas.DetachComponent(object, probe),
            "detaching an attached component must succeed");
        Check(detachedSeen == 1,
            "detaching must run the detach hook before the component leaves its owner");
    }

    // D-45: 순회 중 파괴 요청은 안전 지점까지 미뤄진다. live 배열이 순회 도중 흔들리면
    // 바깥 순회가 무효화되므로, 요청은 받되 수행은 flush 에서 한다.
    void TestDestroyDuringIterationIsDeferred()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        auto* first = canvas.CreateObject("first");
        auto* second = canvas.CreateObject("second");
        auto* third = canvas.CreateObject("third");
        Check(canvas.AttachComponent<LifecycleProbe>(first) != nullptr
            && canvas.AttachComponent<LifecycleProbe>(second) != nullptr
            && canvas.AttachComponent<LifecycleProbe>(third) != nullptr,
            "deferred destroy fixture must attach one probe per object");
        Check(canvas.GetObjectCount() == 3, "fixture must start with three objects");

        std::size_t visited = 0;
        canvas.ForEach<LifecycleProbe>([&](LifecycleProbe& probe)
        {
            ++visited;
            JBro::GameObject* owner = JBro::Internal::CanvasAccess::GetOwner(probe);
            Check(canvas.DestroyObject(owner),
                "a destroy request made while iterating must be accepted");
            Check(canvas.IsIterating(), "the guard must report an active iteration");
        });

        Check(visited == 3,
            "every live component must be visited even though each asked to be destroyed");
        Check(canvas.GetObjectCount() == 3,
            "nothing may actually be destroyed before the safe point");
        Check(canvas.GetPendingDestroyCount() == 3, "all three requests must be queued");

        canvas.FlushPendingDestroy();
        Check(canvas.GetObjectCount() == 0 && canvas.GetPendingDestroyCount() == 0,
            "flushing at the safe point must perform every queued destroy");

        // 순회 밖의 요청은 즉시 수행된다.
        auto* immediate = canvas.CreateObject("immediate");
        Check(canvas.DestroyObject(immediate) && canvas.GetObjectCount() == 0
            && canvas.GetPendingDestroyCount() == 0,
            "a destroy request outside iteration must not be queued");
    }

    // D-54: 상속 활성은 캐시로 읽고, 변경 시점에만 부분 트리로 전파된다.
    void TestActiveInHierarchyIsCachedAndPropagates()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        auto* root = canvas.CreateObject("root");
        auto* middle = canvas.CreateObject("middle");
        auto* leaf = canvas.CreateObject("leaf");
        middle->SetParent(root);
        leaf->SetParent(middle);
        Check(root->IsActiveInHierarchy() && middle->IsActiveInHierarchy()
            && leaf->IsActiveInHierarchy(),
            "a fresh chain must start active all the way down");

        root->SetActive(false);
        Check(false == root->IsActiveInHierarchy()
            && false == middle->IsActiveInHierarchy()
            && false == leaf->IsActiveInHierarchy(),
            "disabling an ancestor must reach every descendant");
        Check(middle->IsActiveSelf() && leaf->IsActiveSelf(),
            "inherited inactivity must not overwrite a descendant's own flag");

        root->SetActive(true);
        Check(leaf->IsActiveInHierarchy(), "re-enabling an ancestor must restore descendants");

        middle->SetActive(false);
        Check(root->IsActiveInHierarchy() && false == leaf->IsActiveInHierarchy(),
            "disabling a middle node must reach below it and not above it");

        // 비활성 부모에서 떼어내면 자기 값만 남는다.
        leaf->SetParent(nullptr);
        Check(leaf->IsActiveInHierarchy(),
            "detaching from an inactive parent must recompute the cache");
        leaf->SetParent(middle);
        Check(false == leaf->IsActiveInHierarchy(),
            "reattaching under an inactive parent must recompute it again");
    }

    void TestRefIsPod()
    {
        // Stage B0 의 계약. 여기서 다시 잡아두면 이후 회귀 시 즉시 눈에 띈다.
        static_assert(sizeof(JBro::Ref<JBro::ComponentBase>) == sizeof(JBro::InstanceRef),
            "Ref<T> must not grow beyond InstanceRef");
        static_assert(std::is_standard_layout_v<JBro::Ref<JBro::ComponentBase>>,
            "Ref<T> must be standard layout");
        static_assert(std::is_trivially_copyable_v<JBro::Ref<JBro::ComponentBase>>,
            "Ref<T> must be trivially copyable");
    }

    void TestStableTypeIdIsStable()
    {
        constexpr JBro::ComponentTypeId a = JBro::MakeStableTypeId("Transform2D");
        constexpr JBro::ComponentTypeId b = JBro::MakeStableTypeId("Transform2D");
        constexpr JBro::ComponentTypeId c = JBro::MakeStableTypeId("Rigidbody2D");
        static_assert(a == b, "same name must hash to the same id");
        static_assert(a != c, "different names must hash to different ids");
        Check(a != 0, "stable type id must not collide with the invalid sentinel");
    }

    void TestCanvasCollectsComponents()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        auto* object = canvas.CreateObject("plural lookup");
        auto* first = canvas.AttachComponent<JBro::Component::Collider2D>(object);
        canvas.AttachComponent<JBro::Component::Transform2D>(object);
        auto* second = canvas.AttachComponent<JBro::Component::Collider2D>(object);
        object->SetActive(false);
        second->SetEnabled(false);

        JBro::Array<JBro::Component::Collider2D*> results;
        results.Reserve(8);
        canvas.FindComponentsRaw<JBro::Component::Collider2D>(object, results);
        Check(results.Size() == 2 && results[0] == first && results[1] == second,
            "plural canvas lookup must preserve attachment order regardless of activation");
        auto* storage = results.Data();
        canvas.FindComponentsRaw<JBro::Component::Collider2D>(object, results);
        Check(results.Size() == 2 && results.Data() == storage && results.Capacity() == 8,
            "plural lookup must replace results and reuse reserved storage");

        auto* third = canvas.AttachComponent<JBro::Component::Collider2D>(object);
        Check(canvas.DetachComponent(object, first), "first collider must detach");
        canvas.FindComponentsRaw<JBro::Component::Collider2D>(object, results);
        Check(results.Size() == 2 && results[0] == second && results[1] == third,
            "plural lookup must preserve remaining attachment order after detachment");
        Check(canvas.FindComponentRaw<JBro::Component::Collider2D>(object) == second
            && object->GetComponent<JBro::Component::Collider2D>().Get() == second,
            "single lookup must keep the earliest remaining component after detachment");

        auto* empty = canvas.CreateObject("no colliders");
        canvas.FindComponentsRaw<JBro::Component::Collider2D>(empty, results);
        Check(results.IsEmpty(), "no matches must clear previous results");
        results.Add(second);
        canvas.FindComponentsRaw<JBro::Component::Collider2D>(nullptr, results);
        Check(results.IsEmpty(), "null owner must clear results");
        results.Add(second);
        JBro::Canvas other(JBro::CreateDefaultAllocator());
        other.FindComponentsRaw<JBro::Component::Collider2D>(object, results);
        Check(results.IsEmpty(), "foreign canvas owner must not expose components");
    }

    void TestFramework2DComponentsArePolymorphic()
    {
        static_assert(std::is_base_of_v<JBro::ComponentBase, JBro::Component::Transform2D>);
                static_assert(std::is_base_of_v<JBro::ComponentBase, JBro::Component::Camera2D>);
        static_assert(std::is_base_of_v<JBro::ComponentBase, JBro::Component::SpriteRenderer2D>);
        static_assert(std::is_base_of_v<JBro::ComponentBase, JBro::Component::Rigidbody2D>);
        static_assert(std::is_base_of_v<JBro::ComponentBase, JBro::Component::Collider2D>);

        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* object = canvas.CreateObject("multi-component");
        Check(object != nullptr, "component test object must be created");

        JBro::Component::Collider2D* first =
            canvas.AttachComponent<JBro::Component::Collider2D>(object);
        JBro::Component::Collider2D* second =
            canvas.AttachComponent<JBro::Component::Collider2D>(object);

        Check(first != nullptr, "first same-type component must attach");
        Check(second != nullptr, "second same-type component must attach");
        Check(first != second, "same-type components must have distinct addresses");
        Check(
            first->GetTypeId() == JBro::MakeStableTypeId(JBro::Component::Collider2D::StaticTypeName()),
            "component type id must derive from its stable type name");
        Check(
            canvas.FindComponentRaw<JBro::Component::Collider2D>(object) == first,
            "single component lookup must return the first matching component");
        Check(
            object->GetComponents<JBro::Component::Collider2D>().Size() == 2,
            "plural component lookup must retain every same-type component");
    }
}

int RunCanvasFoundationTests()
{
    TestObjectComponentSkeletonCompiles();
    TestFramework2DBootstraps();
    TestFramework3DBootstrapsRuntimeCanvas();
    TestComponentLifecycleHooks();
    TestDestroyDuringIterationIsDeferred();
    TestActiveInHierarchyIsCachedAndPropagates();
    TestRefIsPod();
    TestStableTypeIdIsStable();
    TestFramework2DComponentsArePolymorphic();
    TestCanvasCollectsComponents();
    std::cout << "Canvas foundation tests passed.\n";
    return 0;
}

#include <JBro/Core/Core.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Framework2D/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Framework2D.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/Ref.h>

#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition) throw std::runtime_error(message);
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
        foreground.SetOpacity(0.5f);
        foreground.SetBlendMode(JBro::LayerBlendMode::Additive);
        Check(canvas.MoveLayer(foreground.GetIndex(), 0), "layer order must be mutable");
        Check(canvas.GetLayerAt(0)->GetIndex() == foreground.GetIndex(), "moved layer must appear at requested slot");
        Check(canvas.DestroyLayer(background.GetIndex()), "non-default layer must be destroyable");
        Check(canvas.GetLayerCount() == 2, "destroy must shrink the layer list");
    }

    void TestFramework2DBootstraps()
    {
        JBro::Framework2D framework;
        JBro::FrameworkContext context;
        Check(framework.Initialize(context), "framework must initialize with a default allocator fallback");
        Check(framework.GetCanvas() != nullptr, "framework must own a canvas");
        Check(framework.GetCanvas()->GetLayerCount() == 1, "framework canvas must have the default layer");
        framework.Update(1.0f / 60.0f);
        framework.Shutdown();
        Check(framework.GetCanvas() == nullptr, "shutdown must release the canvas");
    }

    void TestRefIsPod()
    {
        // Stage B0 의 계약. 여기서 다시 잡아두면 이후 회귀 시 즉시 눈에 띈다.
        static_assert(sizeof(JBro::Ref<JBro::GameObject>) == sizeof(JBro::InstanceRef),
            "Ref<T> must not grow beyond InstanceRef");
        static_assert(std::is_standard_layout_v<JBro::Ref<JBro::GameObject>>,
            "Ref<T> must be standard layout");
        static_assert(std::is_trivially_copyable_v<JBro::Ref<JBro::GameObject>>,
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
        canvas.GetComponents<JBro::Component::Collider2D>(object, results);
        Check(results.Size() == 2 && results[0] == first && results[1] == second,
            "plural canvas lookup must preserve attachment order regardless of activation");
        auto* storage = results.Data();
        canvas.GetComponents<JBro::Component::Collider2D>(object, results);
        Check(results.Size() == 2 && results.Data() == storage && results.Capacity() == 8,
            "plural lookup must replace results and reuse reserved storage");

        auto* third = canvas.AttachComponent<JBro::Component::Collider2D>(object);
        Check(canvas.DetachComponent(object, first), "first collider must detach");
        canvas.GetComponents<JBro::Component::Collider2D>(object, results);
        Check(results.Size() == 2 && results[0] == second && results[1] == third,
            "plural lookup must preserve remaining attachment order after detachment");
        Check(canvas.GetComponent<JBro::Component::Collider2D>(object) == second
            && object->GetComponent<JBro::Component::Collider2D>().Get() == second,
            "single lookup must keep the earliest remaining component after detachment");

        auto* empty = canvas.CreateObject("no colliders");
        canvas.GetComponents<JBro::Component::Collider2D>(empty, results);
        Check(results.IsEmpty(), "no matches must clear previous results");
        results.Add(second);
        canvas.GetComponents<JBro::Component::Collider2D>(nullptr, results);
        Check(results.IsEmpty(), "null owner must clear results");
        results.Add(second);
        JBro::Canvas other(JBro::CreateDefaultAllocator());
        other.GetComponents<JBro::Component::Collider2D>(object, results);
        Check(results.IsEmpty(), "foreign canvas owner must not expose components");
    }

    void TestFramework2DComponentsArePolymorphic()
    {
        static_assert(std::is_base_of_v<JBro::ComponentBase, JBro::Component::Transform2D>);
        static_assert(std::is_base_of_v<JBro::ComponentBase, JBro::Component::WorldTransform2D>);
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
            canvas.GetComponent<JBro::Component::Collider2D>(object) == first,
            "single component lookup must return the first matching component");
        Check(
            object->GetComponents<JBro::Component::Collider2D>().Size() == 2,
            "plural component lookup must retain every same-type component");
    }
}

int RunWorldCanvasFoundationTests()
{
    TestObjectComponentSkeletonCompiles();
    TestFramework2DBootstraps();
    TestRefIsPod();
    TestStableTypeIdIsStable();
    TestFramework2DComponentsArePolymorphic();
    TestCanvasCollectsComponents();
    std::cout << "World/canvas foundation tests passed.\n";
    return 0;
}

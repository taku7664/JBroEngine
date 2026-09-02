#include <JBro/Core/Core.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Framework2D/Canvas/Canvas.h>
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
}

int RunWorldCanvasFoundationTests()
{
    TestObjectComponentSkeletonCompiles();
    TestFramework2DBootstraps();
    TestRefIsPod();
    TestStableTypeIdIsStable();
    std::cout << "World/canvas foundation tests passed.\n";
    return 0;
}

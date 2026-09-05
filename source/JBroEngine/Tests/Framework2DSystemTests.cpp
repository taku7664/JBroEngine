#include <JBro/Core/Core.h>
#include <JBro/Framework2D/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/System/Transform2DSystem.h>
#include <JBro/Runtime/GameObject.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    bool NearlyEqual(float left, float right)
    {
        return std::fabs(left - right) <= 0.0001f;
    }

    void TestTransformHierarchyPropagation()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* parent = canvas.CreateObject("parent");
        JBro::GameObject* child = canvas.CreateObject("child");
        Check(parent != nullptr && child != nullptr, "transform objects must be created");

        JBro::Component::Transform2D* parentLocal =
            canvas.AttachComponent<JBro::Component::Transform2D>(parent);
        JBro::Component::WorldTransform2D* parentWorld =
            canvas.AttachComponent<JBro::Component::WorldTransform2D>(parent);
        JBro::Component::Transform2D* childLocal =
            canvas.AttachComponent<JBro::Component::Transform2D>(child);
        JBro::Component::WorldTransform2D* childWorld =
            canvas.AttachComponent<JBro::Component::WorldTransform2D>(child);
        Check(
            parentLocal != nullptr
                && parentWorld != nullptr
                && childLocal != nullptr
                && childWorld != nullptr,
            "transform components must attach");

        parentLocal->position = {2.0f, 1.0f};
        parentLocal->rotation = 1.57079632679f;
        childLocal->position = {1.0f, 0.0f};
        child->SetParent(parent);

        JBro::System::Transform2DSystem system;
        system.Initialize(canvas);
        system.Update(canvas, 1.0f / 60.0f);

        Check(NearlyEqual(parentWorld->position.x, 2.0f), "parent world x must match local x");
        Check(NearlyEqual(parentWorld->position.y, 1.0f), "parent world y must match local y");
        Check(NearlyEqual(childWorld->position.x, 2.0f), "parent rotation must affect child world x");
        Check(NearlyEqual(childWorld->position.y, 2.0f), "parent rotation must affect child world y");
        Check(NearlyEqual(childWorld->rotation, parentLocal->rotation), "world rotation must be decomposed");
        Check(false == childWorld->dirty, "updated world transform must be clean");

        childLocal->SetEnabled(false);
        childWorld->position = {99.0f, 99.0f};
        system.Update(canvas, 1.0f / 60.0f);
        Check(
            NearlyEqual(childWorld->position.x, 99.0f)
                && NearlyEqual(childWorld->position.y, 99.0f),
            "disabled transform must pass through the shared active gate");
        system.Shutdown(canvas);
    }
}

int RunFramework2DSystemTests()
{
    TestTransformHierarchyPropagation();
    std::cout << "Framework2D system tests passed.\n";
    return 0;
}

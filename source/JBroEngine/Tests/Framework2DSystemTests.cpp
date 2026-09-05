#include <JBro/Core/Core.h>
#include <JBro/Framework2D/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/System/Physics2DSystem.h>
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

    void TestPhysicsGravityIntegration()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* object = canvas.CreateObject("falling body");
        Check(object != nullptr, "physics object must be created");

        JBro::Component::Transform2D* transform =
            canvas.AttachComponent<JBro::Component::Transform2D>(object);
        JBro::Component::WorldTransform2D* world =
            canvas.AttachComponent<JBro::Component::WorldTransform2D>(object);
        JBro::Component::Rigidbody2D* body =
            canvas.AttachComponent<JBro::Component::Rigidbody2D>(object);
        Check(
            transform != nullptr && world != nullptr && body != nullptr,
            "physics components must attach");

        JBro::System::Physics2DSystem system;
        system.Initialize(canvas);
        system.FixedUpdate(canvas, 0.5f);

        Check(NearlyEqual(body->linearVelocity.y, -4.905f), "gravity must update velocity");
        Check(NearlyEqual(transform->position.y, -2.4525f), "velocity must update position");
        Check(world->dirty, "physics movement must invalidate the world transform");

        body->SetEnabled(false);
        const float disabledPosition = transform->position.y;
        system.FixedUpdate(canvas, 0.5f);
        Check(
            NearlyEqual(transform->position.y, disabledPosition),
            "disabled rigidbody must pass through the shared active gate");
        system.Shutdown(canvas);
    }

    void TestPhysicsQueries()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* boxObject = canvas.CreateObject("box");
        JBro::GameObject* circleObject = canvas.CreateObject("circle");
        Check(boxObject != nullptr && circleObject != nullptr, "query objects must be created");

        JBro::Component::Transform2D* boxTransform =
            canvas.AttachComponent<JBro::Component::Transform2D>(boxObject);
        JBro::Component::Collider2D* boxCollider =
            canvas.AttachComponent<JBro::Component::Collider2D>(boxObject);
        JBro::Component::Collider2D* secondBoxCollider =
            canvas.AttachComponent<JBro::Component::Collider2D>(boxObject);
        JBro::Component::Transform2D* circleTransform =
            canvas.AttachComponent<JBro::Component::Transform2D>(circleObject);
        JBro::Component::Collider2D* circleCollider =
            canvas.AttachComponent<JBro::Component::Collider2D>(circleObject);
        Check(
            boxTransform != nullptr
                && boxCollider != nullptr
                && secondBoxCollider != nullptr
                && circleTransform != nullptr
                && circleCollider != nullptr,
            "query components must attach");

        boxTransform->position = {2.0f, 0.0f};
        boxCollider->shape = JBro::Component::ColliderShape2D::Box;
        boxCollider->size = {2.0f, 2.0f};
        secondBoxCollider->shape = JBro::Component::ColliderShape2D::Box;
        secondBoxCollider->offset = {0.25f, 0.0f};
        secondBoxCollider->size = {0.5f, 0.5f};
        circleTransform->position = {5.0f, 0.0f};
        circleCollider->shape = JBro::Component::ColliderShape2D::Circle;
        circleCollider->radius = 1.0f;

        JBro::System::Physics2DSystem system;
        JBro::Collision2D hit;
        Check(
            system.Raycast(canvas, {0.0f, 0.0f}, {1.0f, 0.0f}, 10.0f, hit),
            "ray must hit the nearest collider");
        Check(hit.other == boxObject, "ray must select the nearest object");
        Check(NearlyEqual(hit.point.x, 1.0f), "box hit point must be on its near face");
        Check(NearlyEqual(hit.normal.x, -1.0f), "box hit normal must face the ray");

        JBro::Array<JBro::GameObject*> overlaps;
        system.OverlapBox(canvas, {{1.5f, -0.5f}, {5.5f, 0.5f}}, overlaps);
        Check(overlaps.Size() == 2, "overlap box must include box and circle");

        boxCollider->SetEnabled(false);
        secondBoxCollider->SetEnabled(false);
        Check(
            system.Raycast(canvas, {0.0f, 0.0f}, {1.0f, 0.0f}, 10.0f, hit),
            "ray must continue past a disabled collider");
        Check(hit.other == circleObject, "disabled collider must not participate in queries");
        Check(NearlyEqual(hit.point.x, 4.0f), "circle hit point must be on its near edge");
    }
}

int RunFramework2DSystemTests()
{
    TestTransformHierarchyPropagation();
    TestPhysicsGravityIntegration();
    TestPhysicsQueries();
    std::cout << "Framework2D system tests passed.\n";
    return 0;
}

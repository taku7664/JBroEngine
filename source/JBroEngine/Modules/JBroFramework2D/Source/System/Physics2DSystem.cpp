#include <JBro/Framework2D/System/Physics2DSystem.h>

#include <JBro/Framework2D/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Runtime/GameObject.h>

namespace JBro::System
{
    int Physics2DSystem::GetExecutionOrder() const
    {
        return 200;
    }

    void Physics2DSystem::SetGravity(Vec2 gravity)
    {
        m_gravity = gravity;
    }

    Vec2 Physics2DSystem::GetGravity() const
    {
        return m_gravity;
    }

    void Physics2DSystem::OnInitialize(Canvas& canvas)
    {
        (void)canvas;
    }

    void Physics2DSystem::OnFixedUpdate(Canvas& canvas, float fixedDeltaTime)
    {
        if (fixedDeltaTime <= 0.0f)
        {
            return;
        }

        canvas.ForEach<Component::Rigidbody2D>(
            [this, &canvas, fixedDeltaTime](Component::Rigidbody2D& body)
        {
            if (false == body.IsActiveComponent()
                || body.bodyType == Component::BodyType2D::Static)
            {
                return;
            }

            GameObject* owner = body.GetOwner();
            Component::Transform2D* transform =
                canvas.GetComponent<Component::Transform2D>(owner);
            if (transform == nullptr || false == transform->IsActiveComponent())
            {
                return;
            }

            if (body.bodyType == Component::BodyType2D::Dynamic)
            {
                if (body.mass <= 0.0f)
                {
                    return;
                }

                body.linearVelocity.x +=
                    m_gravity.x * body.gravityScale * fixedDeltaTime;
                body.linearVelocity.y +=
                    m_gravity.y * body.gravityScale * fixedDeltaTime;

                if (body.linearDamping > 0.0f)
                {
                    float dampingFactor = 1.0f - body.linearDamping * fixedDeltaTime;
                    if (dampingFactor < 0.0f)
                    {
                        dampingFactor = 0.0f;
                    }
                    body.linearVelocity.x *= dampingFactor;
                    body.linearVelocity.y *= dampingFactor;
                }
            }

            transform->position.x += body.linearVelocity.x * fixedDeltaTime;
            transform->position.y += body.linearVelocity.y * fixedDeltaTime;
            if (false == body.fixedRotation)
            {
                transform->rotation += body.angularVelocity * fixedDeltaTime;
            }

            Component::WorldTransform2D* world =
                canvas.GetComponent<Component::WorldTransform2D>(owner);
            if (world != nullptr)
            {
                world->dirty = true;
            }
        });
    }

    void Physics2DSystem::OnShutdown(Canvas& canvas)
    {
        (void)canvas;
    }
}

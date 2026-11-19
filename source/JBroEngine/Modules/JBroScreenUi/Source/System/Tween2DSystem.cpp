#include <JBro/ScreenUi/System/Tween2DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Component/Tween2D.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>
#include <JBro/Types/Angle.h>
#include <JBro/Types/Easing.h>

#include <cmath>

namespace JBro::System
{
    namespace
    {
        Float Mix(Float from, Float to, Float t)
        {
            return from + (to - from) * t;
        }

        Bool Finite(Float value)
        {
            return std::isfinite(value.Get());
        }

        Bool Finite(Vector2 value)
        {
            return Finite(value.x) && Finite(value.y);
        }

        Bool Finite(const Color& value)
        {
            return Finite(value.R) && Finite(value.G) && Finite(value.B) && Finite(value.A);
        }
    }

    Int32 Tween2DSystem::GetExecutionOrder() const
    {
        return ExecutionOrder;
    }

    void Tween2DSystem::OnUpdate(Canvas& canvas, Float deltaTime)
    {
        const Float step = Float(std::isfinite(deltaTime.Get()) && deltaTime > 0.0f ? deltaTime.Get() : 0.0f);
        canvas.ForEach<Component::Tween2D>([&](Component::Tween2D& tween)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(tween);
            tween.finished = false;
            if (owner == nullptr || false == tween.IsActiveComponent())
            {
                return;
            }
            if (tween.playOnStart && false == tween.started)
            {
                tween.started = true;
                tween.playing = true;
                tween.time = 0.0f;
            }
            if (false == tween.playing)
            {
                return;
            }
            // 진행도 0..1. 길이가 0 이거나 수가 아니면 바로 끝이다.
            const Float duration = Float(std::isfinite(tween.duration.Get()) && tween.duration > 0.0f ? tween.duration.Get() : 0.0f);
            tween.time = Float(std::isfinite(tween.time.Get()) ? tween.time.Get() : 0.0f) + step;
            Float t = 1.0f;
            if (duration > 0.0f)
            {
                t = tween.time / duration;
            }
            switch (tween.loop)
            {
            case Component::TweenLoop::Loop:
                // 시계는 한 바퀴 안에 둔다 - 오래 돌아도 정밀도가 깎이지 않는다.
                if (duration > 0.0f && tween.time >= duration)
                {
                    tween.time = Float(std::fmod(tween.time.Get(), duration.Get()));
                }
                t = Float(duration > 0.0f ? (tween.time / duration).Get() : 1.0f);
                break;
            case Component::TweenLoop::PingPong:
            {
                if (duration > 0.0f && tween.time >= duration * 2.0f)
                {
                    tween.time = Float(std::fmod(tween.time.Get(), duration.Get() * 2.0f));
                }
                const Float cycle = Float(duration > 0.0f ? (tween.time / duration).Get() : 2.0f);
                t = cycle <= 1.0f ? cycle : Float(2.0f) - cycle;
                break;
            }
            case Component::TweenLoop::Once:
            default:
                // 델타를 더해 가면 길이에 정확히 닿지 않는다(1/60 을 60 번 더하면 1 이 아니다) - 1e-5 안이면 닿은 것이다.
                if (t >= Float(1.0f) - Float(1e-5f))
                {
                    t = 1.0f;
                    tween.time = duration;
                    tween.playing = false;
                    tween.finished = true;
                    // 사슬(D-360): 다음 오브젝트의 트윈을 처음부터. 자기 자신은 무시한다.
                    Object::GameObject* nextOwner = Internal::GameObjectHandleAccess::Resolve(tween.next);
                    auto* chained = nextOwner != nullptr && nextOwner != owner ? canvas.FindComponentRaw<Component::Tween2D>(nextOwner) : nullptr;
                    if (chained != nullptr)
                    {
                        chained->time = 0.0f;
                        chained->playing = true;
                        chained->started = true;
                    }
                }
                break;
            }
            const Float eased = Ease(tween.ease, t);
            switch (tween.target)
            {
            case Component::TweenTarget::Scale:
            case Component::TweenTarget::Position:
            {
                auto* transform = canvas.FindComponentRaw<Component::Transform2D>(owner);
                if (transform == nullptr || false == Finite(tween.fromVector) || false == Finite(tween.toVector))
                {
                    return;
                }
                const Vector2 value{ Mix(tween.fromVector.x, tween.toVector.x, eased), Mix(tween.fromVector.y, tween.toVector.y, eased) };
                if (tween.target == Component::TweenTarget::Position)
                {
                    transform->position = value;
                }
                else
                {
                    transform->scale = value;
                }
                break;
            }
            case Component::TweenTarget::Rotation:
            {
                auto* transform = canvas.FindComponentRaw<Component::Transform2D>(owner);
                if (transform == nullptr || false == Finite(tween.fromValue) || false == Finite(tween.toValue))
                {
                    return;
                }
                transform->SetRotation(Degree(Mix(tween.fromValue, tween.toValue, eased)));
                break;
            }
            case Component::TweenTarget::Tint:
            {
                auto* sprite = canvas.FindComponentRaw<Component::SpriteRenderer2D>(owner);
                if (sprite == nullptr || false == Finite(tween.fromColor) || false == Finite(tween.toColor))
                {
                    return;
                }
                sprite->tint = { Mix(tween.fromColor.R, tween.toColor.R, eased), Mix(tween.fromColor.G, tween.toColor.G, eased),
                    Mix(tween.fromColor.B, tween.toColor.B, eased), Mix(tween.fromColor.A, tween.toColor.A, eased) };
                break;
            }
            case Component::TweenTarget::Alpha:
            default:
            {
                auto* sprite = canvas.FindComponentRaw<Component::SpriteRenderer2D>(owner);
                if (sprite == nullptr || false == Finite(tween.fromValue) || false == Finite(tween.toValue))
                {
                    return;
                }
                sprite->tint.A = Mix(tween.fromValue, tween.toValue, eased);
                break;
            }
            }
        });
    }
}

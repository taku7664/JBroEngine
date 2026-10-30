#include <JBro/Framework2D/Handle/Tween2D.h>

#include <JBro/Framework2D/Component/Tween2D.h>

namespace JBro::Handle
{
    Component::Tween2D* Tween2D::Resolve(const char* operation) const
    {
        return static_cast<Component::Tween2D*>(ResolveComponent(StaticTypeName(), operation));
    }

// 읽기·쓰기 멤버는 모두 같은 꼴이다 - 풀리지 않으면 기본값을 주거나 아무것도 하지 않는다.
#define JBRO_TWEEN2D_ACCESSOR(Type, Getter, Setter, field)        \
    Type Tween2D::Getter() const                                   \
    {                                                               \
        const Component::Tween2D* component = Resolve(#Getter);    \
        if (component == nullptr)                                   \
        {                                                           \
            return {};                                              \
        }                                                           \
        return component->field;                                    \
    }                                                               \
    void Tween2D::Setter(Type value)                               \
    {                                                               \
        Component::Tween2D* component = Resolve(#Setter);          \
        if (component == nullptr)                                   \
        {                                                           \
            return;                                                 \
        }                                                           \
        component->field = value;                                   \
    }

    JBRO_TWEEN2D_ACCESSOR(Component::TweenTarget, GetTarget, SetTarget, target)
    JBRO_TWEEN2D_ACCESSOR(EaseKind, GetEase, SetEase, ease)
    JBRO_TWEEN2D_ACCESSOR(Float, GetDuration, SetDuration, duration)
    JBRO_TWEEN2D_ACCESSOR(Component::TweenLoop, GetLoop, SetLoop, loop)
    JBRO_TWEEN2D_ACCESSOR(Bool, GetPlayOnStart, SetPlayOnStart, playOnStart)
    JBRO_TWEEN2D_ACCESSOR(Vector2, GetFromVector, SetFromVector, fromVector)
    JBRO_TWEEN2D_ACCESSOR(Vector2, GetToVector, SetToVector, toVector)
    JBRO_TWEEN2D_ACCESSOR(Color, GetFromColor, SetFromColor, fromColor)
    JBRO_TWEEN2D_ACCESSOR(Color, GetToColor, SetToColor, toColor)
    JBRO_TWEEN2D_ACCESSOR(Float, GetFromValue, SetFromValue, fromValue)
    JBRO_TWEEN2D_ACCESSOR(Float, GetToValue, SetToValue, toValue)
    JBRO_TWEEN2D_ACCESSOR(Bool, GetPlaying, SetPlaying, playing)
    JBRO_TWEEN2D_ACCESSOR(GameObject, GetNext, SetNext, next)

#undef JBRO_TWEEN2D_ACCESSOR

    Float Tween2D::GetTime() const
    {
        const Component::Tween2D* component = Resolve("GetTime");
        return component != nullptr ? component->time : Float(0.0f);
    }

    Bool Tween2D::GetFinished() const
    {
        const Component::Tween2D* component = Resolve("GetFinished");
        return component != nullptr ? component->finished : Bool(false);
    }

    void Tween2D::Play()
    {
        Component::Tween2D* component = Resolve("Play");
        if (component != nullptr)
        {
            component->playing = true;
            component->started = true;
        }
    }

    void Tween2D::Stop()
    {
        Component::Tween2D* component = Resolve("Stop");
        if (component != nullptr)
        {
            component->playing = false;
        }
    }

    void Tween2D::Restart()
    {
        Component::Tween2D* component = Resolve("Restart");
        if (component != nullptr)
        {
            component->time = 0.0f;
            component->playing = true;
            component->started = true;
        }
    }
}

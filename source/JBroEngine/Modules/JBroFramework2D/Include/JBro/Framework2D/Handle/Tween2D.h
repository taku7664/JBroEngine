#pragma once

#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Easing.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>

#include <type_traits>

namespace JBro::Component
{
    class Tween2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Tween2D` 다(D-302·D-357). `Play`·`Stop`·`Restart` 로 돌린다. 이번 프레임의 상태(`GetTime`·`GetFinished`)는 시스템이 채우고 읽기만 한다.
    class Tween2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Tween2D";
        }

        Component::TweenTarget GetTarget() const;
        void SetTarget(Component::TweenTarget value);
        EaseKind GetEase() const;
        void SetEase(EaseKind value);
        Float GetDuration() const;
        void SetDuration(Float value);
        Component::TweenLoop GetLoop() const;
        void SetLoop(Component::TweenLoop value);
        Bool GetPlayOnStart() const;
        void SetPlayOnStart(Bool value);
        Vector2 GetFromVector() const;
        void SetFromVector(Vector2 value);
        Vector2 GetToVector() const;
        void SetToVector(Vector2 value);
        Color GetFromColor() const;
        void SetFromColor(Color value);
        Color GetToColor() const;
        void SetToColor(Color value);
        Float GetFromValue() const;
        void SetFromValue(Float value);
        Float GetToValue() const;
        void SetToValue(Float value);
        Bool GetPlaying() const;
        void SetPlaying(Bool value);
        Float GetTime() const;
        Bool GetFinished() const;
        // 지금 자리에서 돌린다 / 멈춘다 / 처음부터 돌린다.
        void Play();
        void Stop();
        void Restart();

    private:
        Component::Tween2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Tween2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Tween2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Tween2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(Tween2D, name)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::Tween2D);

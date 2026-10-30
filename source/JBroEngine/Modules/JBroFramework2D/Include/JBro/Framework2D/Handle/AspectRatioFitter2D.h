#pragma once

#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Types/Float.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>

#include <type_traits>

namespace JBro::Component
{
    class AspectRatioFitter2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `AspectRatioFitter2D` 다(D-302·D-358). 같은 오브젝트의 사각형을 폭/높이 비율로 맞춘다.
    class AspectRatioFitter2D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::AspectRatioFitter2D";
        }

        Component::AspectMode GetMode() const;
        void SetMode(Component::AspectMode value);
        Float GetAspect() const;
        void SetAspect(Float value);

    private:
        Component::AspectRatioFitter2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(AspectRatioFitter2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<AspectRatioFitter2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<AspectRatioFitter2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(AspectRatioFitter2D, name)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::AspectRatioFitter2D);

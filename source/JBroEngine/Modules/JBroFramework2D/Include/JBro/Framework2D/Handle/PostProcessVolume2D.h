#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/UInt.h>

#include <type_traits>

namespace JBro::Component
{
    class PostProcessVolume2D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `PostProcessVolume2D` 다(D-302). 데이터는 엔진의 `Component::PostProcessVolume2D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 카메라가 안에 있으면 그 프로필을 섞는 후처리 볼륨이다(D-346). 무게(`SetWeight`)를 올리고 내려 효과를 서서히 켜고 끈다.
    // 프로필은 이번 실행의 자리(`AssetHandle`)로 바꾼다 - 저장되는 id 는 읽기만 한다.
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class PostProcessVolume2D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<PostProcessVolume2D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::PostProcessVolume2D";
        }

        Component::PostProcessVolumeShape2D GetShape() const;
        void SetShape(Component::PostProcessVolumeShape2D value);
        Vector2 GetSize() const;
        void SetSize(Vector2 value);
        Float GetRadius() const;
        void SetRadius(Float value);
        Float GetBlendDistance() const;
        void SetBlendDistance(Float value);
        Int32 GetPriority() const;
        void SetPriority(Int32 value);
        Float GetWeight() const;
        void SetWeight(Float value);
        // 저장되는 프로필 id 다. 캔버스를 열 때 `GetPostProcessProfile` 로 풀린다.
        AssetId GetPostProcessProfileId() const;
        // 섞는 프로필이다. 바꾸면 다음 프레임부터 섞는다. 저장되는 id 는 바뀌지 않는다.
        AssetHandle GetPostProcessProfile() const;
        void SetPostProcessProfile(AssetHandle value);

    private:
        Component::PostProcessVolume2D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(PostProcessVolume2D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<PostProcessVolume2D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<PostProcessVolume2D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(PostProcessVolume2D, target)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::PostProcessVolume2D);

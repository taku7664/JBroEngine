#pragma once

#include <JBro/Framework3D/ComponentTypes3D.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/ComponentHandleReflection.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

#include <type_traits>

namespace JBro::Component
{
    class Animator3D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `Animator3D` 다(D-302, D-459). 데이터는 엔진의 `Component::Animator3D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 재생 명령은 쌓였다가 다음 프레임의 애니메이터 시스템이 처리한다(스크립트 뒤, 렌더 추출 앞 - 같은 프레임의 자세에 들어간다).
    // 클립은 애니메이터의 `animationClipIds` 목록의 **번호**다. 레이어는 0..3 이고 0 이 전신 바탕이다.
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다. 명령이 넘치면(한 프레임 16 개) 거짓이다.
    class Animator3D final : public Internal::ComponentHandle
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Animator3D";
        }

        // 레이어에서 그 번호의 클립을 처음부터 재생한다. 반복은 클립이 정한다(모델 에디터의 클립 설정, D-462).
        Bool Play(UInt32 layer, Int32 clip);
        // 지금 클립에서 그 번호의 클립으로 `seconds` 초에 걸쳐 넘어간다. 레이어가 비어 있으면 곧장 재생한다.
        Bool CrossFade(UInt32 layer, Int32 clip, Float seconds);
        Bool Stop(UInt32 layer);
        // 레이어의 가중치(0..1)다. 레이어 0 은 늘 바탕이라 보지 않는다.
        Bool SetLayerWeight(UInt32 layer, Float weight);
        // 참이면 더하는 레이어, 거짓이면 덮어쓰는 레이어다.
        Bool SetLayerAdditive(UInt32 layer, Bool additive);
        // 그 뼈와 그 가지만 섞는다(`blendDepth` 뼈에 걸쳐 서서히). 빈 글자나 null 이면 마스크를 지운다. 없는 뼈면 다음 프레임에 무시된다.
        Bool SetLayerMask(UInt32 layer, const char* rootBone, UInt32 blendDepth = 0);

        // 마지막으로 셈한 프레임의 레이어 상태다.
        Float GetLayerTime(UInt32 layer) const;
        Bool IsPlaying(UInt32 layer) const;
        // 레이어가 재생 중인 클립의 번호다. 없으면 -1.
        Int32 GetLayerClip(UInt32 layer) const;

        Float GetSpeed() const;
        void SetSpeed(Float value);

    private:
        Component::Animator3D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(Animator3D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<Animator3D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Animator3D>, "a component handle must stay trivially copyable");
}

// 스크립트 필드로 저장한다(`JBRO_FIELD(Animator3D, body)`).
JBRO_DEFINE_COMPONENT_HANDLE_TYPE(JBro::Handle::Animator3D);

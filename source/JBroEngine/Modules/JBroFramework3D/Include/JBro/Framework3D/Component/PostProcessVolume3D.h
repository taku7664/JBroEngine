#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`PostProcessVolume3D`)로만 만진다(D-302).
// 정규화한 이름으로 써도 스크립트 대상 빌드에서는 여기서 컴파일이 실패한다.
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::PostProcessVolume3D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Framework3D/ComponentTypes3D.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Reflection/Math3DReflection.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro::Component
{
    // **후처리 볼륨**(D-346, postprocess-plan §2.5). 게임 카메라가 이 볼륨 안에 있으면 그 프로필을 카메라의 프로필 위에 섞는다 - 동굴에 들어가면
    // 어두워지고 물속이면 푸르러지는 자리다. 모양은 볼륨이 직접 든다(물리와 따로 논다, 사용자 결정).
    //
    // 무게는 `weight` x 거리 비율이다. 모양 안이면 1, 모양 밖 `blendDistance` 안이면 경계에서 멀어질수록 0 으로 줄고, 그 밖은 0 이다. `Global` 은
    // 늘 1 이다. 볼륨은 `priority` 가 낮은 것부터 섞어 높은 것이 위에 온다(같으면 캔버스의 차례). 프로필에서 덮어쓴 필드만 섞는다.
    // 모양은 오브젝트의 위치·회전·크기를 따른다 - 크기는 `size`·`radius` 에 곱한다.
    class PostProcessVolume3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::PostProcessVolume3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(PostProcessVolume3D)

        JBRO_FIELD(PostProcessVolumeShape3D, shape) = PostProcessVolumeShape3D::Global;
        // `Box` 의 크기다(로컬 단위, 가운데가 오브젝트의 위치).
        JBRO_FIELD(Vector3, size) { 1.0f, 1.0f, 1.0f };
        // `Sphere` 의 반지름이다(로컬 단위).
        JBRO_FIELD(Float, radius, Range(0.0f, 10000.0f)) = 0.5f;
        // 모양 밖에서 무게가 0 까지 줄어드는 거리다(월드 단위). 0 이면 경계에서 바로 0 이다.
        JBRO_FIELD(Float, blendDistance, Range(0.0f, 10000.0f)) = 0.0f;
        JBRO_FIELD(Int32, priority) = 0;
        JBRO_FIELD(Float, weight, Range(0.0f, 1.0f)) = 1.0f;
        JBRO_FIELD(AssetId, postProcessProfileId);
        JBRO_FIELD(AssetHandle, postProcessProfile, NoSerialize() | ReadOnly() | Hidden());
    };
}

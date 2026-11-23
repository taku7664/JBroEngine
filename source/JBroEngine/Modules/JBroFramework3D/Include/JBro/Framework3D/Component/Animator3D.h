#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::Animator3D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro::Component
{
    // **애니메이터**(D-457, skeletal-plan §3.3). 뼈대의 자세(뼈 번호 → 위치·회전·크기)를 들고 클립을 재생한다. 뼈는 오브젝트가 아니다.
    // 스킨 메시는 자기나 가장 가까운 조상의 애니메이터 자세로 그린다(`MeshRenderer3D` 는 그대로다). 자세와 재생 상태는 `Animation3DSystem` 이 든다 -
    // 여기는 저장되는 설정뿐이다. 레이어 1 이상(상하체 분리)과 마스크는 스크립트가 건다(인스펙터의 레이어 편집은 모델 에디터 단계).
    class Animator3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Animator3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Animator3D)

        // 뼈대(모델의 하위 Skeleton)다. 비면 이 오브젝트나 자식의 스킨 메시가 쓰는 뼈대다.
        JBRO_FIELD(AssetId,     skeletonId);
        JBRO_FIELD(AssetHandle, skeleton,  NoSerialize() | ReadOnly() | Tooltip("skeletonId 에서 해석된 값") | Hidden());
        // 레이어 0(전신 바탕)이 시작할 때 재생하는 클립이다.
        JBRO_FIELD(AssetId,     clipId);
        JBRO_FIELD(AssetHandle, clip,      NoSerialize() | ReadOnly() | Tooltip("clipId 에서 해석된 값") | Hidden());
        JBRO_FIELD(Bool,        playOnStart) = true;
        JBRO_FIELD(Bool,        loop) = true;
        // 모든 레이어의 재생 속도에 곱한다. 0 이면 멈춘다.
        JBRO_FIELD(Float,       speed) = 1.0f;
    };
}

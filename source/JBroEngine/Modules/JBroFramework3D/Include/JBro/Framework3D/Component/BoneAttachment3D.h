#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`BoneAttachment3D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::BoneAttachment3D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/TextStore.h>

namespace JBro::Component
{
    // **뼈에 붙이기**(D-460, skeletal-plan §3.3). 이 오브젝트를 가장 가까운 조상의 `Animator3D` 의 소켓(또는 뼈)에 붙인다 - 칼을 손에, 모자를 머리에.
    // `socket` 은 이름이다: 뼈대 소켓 → 그 애니메이터를 따르는 메시의 소켓 → 뼈 이름 순서로 찾는다(뼈 이름이면 오프셋이 없다).
    // 붙이기 시스템이 자세 셈 뒤(렌더 추출 앞)에 소켓의 월드 자리를 이 오브젝트의 월드 캐시에 쓰고 자식에게 내린다. 이 오브젝트의 `Transform3D`
    // 로컬 값은 **소켓에서의 덧 오프셋**이다(부모가 아니라 소켓 기준). 소켓을 찾지 못하거나 애니메이터가 없으면 보통 트랜스폼대로 둔다.
    // 뼈를 덮어쓰기(IK)는 하지 않는다 - 따르기만 한다.
    //
    // 이름은 컴포넌트에 없다(D-51): `socket` 은 호스트 `TextStore` 의 번호이고 시스템은 칸의 판번호로 바뀐 것을 안다 - 프레임마다 이름을 견주지 않는다.
    class BoneAttachment3D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::BoneAttachment3D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        // 떼이면 제 글자 칸을 돌려준다. 컴포넌트는 호스트의 캔버스만 떼므로 호스트의 저장소다.
        void OnDetached() override
        {
            TextStore::Get().Destroy(socket);
            socket = {};
            ComponentBase::OnDetached();
        }

        JBRO_REFLECT_BODY(BoneAttachment3D)

        // 소켓이나 뼈의 이름이다.
        JBRO_FIELD(TextId, socket);
    };
}

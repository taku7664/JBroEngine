#pragma once

// 컴포넌트 데이터다. **스크립트는 이 헤더에 닿지 않는다** - 프렐류드의 핸들(`Navigation2D`)로만 만진다(D-302).
#if defined(JBRO_SCRIPT_TARGET)
#error "A script reaches Component::Navigation2D through its handle in <JBro/ScriptAPI.h>, not the component data header."
#endif

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Bool.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>

namespace JBro::Component
{
    // **탐색 연결**이다(D-358, ui-plan §7.2). 위젯 오브젝트에 두면 게임패드·키보드의 방향 이동이 방향 탐색 대신 여기 적힌 오브젝트로 간다. 빈 방향은 전처럼 가장 가까운
    // 위젯을 찾는다. 가리킨 오브젝트에 켜진 위젯이 없거나 모달·그룹 밖이면 그 방향은 없는 것이다.
    class Navigation2D final : public ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::Navigation2D";
        }

        ComponentTypeId GetTypeId() const override
        {
            return MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Navigation2D)

        JBRO_FIELD(Handle::GameObject, up, Tooltip("위로 가면 포커스를 받을 위젯 오브젝트"));
        JBRO_FIELD(Handle::GameObject, down, Tooltip("아래로 가면 포커스를 받을 위젯 오브젝트"));
        JBRO_FIELD(Handle::GameObject, left, Tooltip("왼쪽으로 가면 포커스를 받을 위젯 오브젝트"));
        JBRO_FIELD(Handle::GameObject, right, Tooltip("오른쪽으로 가면 포커스를 받을 위젯 오브젝트"));
    };
}

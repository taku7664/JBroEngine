#pragma once

#include <JBro/Editor/Command/ComponentAddress.h>
#include <JBro/Editor/EditorActions.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/Array.h>

#include <cstdint>

namespace JBro
{
    class EditorApplication;

    // 훅 하나가 그릴 때 받는 것이다(D-220).
    struct ComponentMenuContext
    {
        EditorApplication* editor = nullptr;
        // **대상은 이것으로 가리킨다.** 같은 타입이 둘이면 몇째인지까지 담는다. 커맨드에 넘기는 것도 이것이다.
        ComponentAddress address;
        // 그리는 동안만 유효하다. 들고 있다가 다음 프레임에 쓰지 않는다 - 주소로 다시 찾는다.
        ComponentBase* component = nullptr;
        // 우클릭한 자리다. 오브젝트 메뉴에서만 차 있고 인스펙터 머리 메뉴에서는 비어 있다.
        ObjectPlacement placement;
        // 등록할 때 넘긴 값 그대로다. 함수 포인터만으로는 등록한 패널에 닿을 수 없어서 둔다.
        void* user = nullptr;
    };

    // 이미 열린 메뉴 안에 항목을 그린다. 항목 글자는 로컬라이징 키로 찾은 글자다.
    // **오브젝트의 컴포넌트 목록이나 계층을 바꿨으면(지우기·붙이기·떼기·옮기기) 거짓을 돌려준다** - 그 오브젝트나
    // 컴포넌트가 더 이상 없을 수 있고, 부르는 쪽이 도는 슬롯 배열이 그 자리에서 바뀐다. 부르는 쪽은 그 프레임에
    // 그것을 더 그리지 않는다(`EditorActions::DrawObjectMenu` 와 같은 계약). 필드 값만 바꿨으면 참이다.
    // 편집은 커맨드로만 한다.
    // 메뉴가 열린 동안 매 프레임 불리므로 힙 할당·문자열 생성을 하지 않는다.
    using ComponentMenuDraw = bool (*)(const ComponentMenuContext& context);

    // 누가 등록했는가의 표지다. 등록한 쪽이 자기 주소 같은 값을 넘기고, 뗄 때 같은 값으로 한 번에 뗀다.
    using ComponentMenuOwner = const void*;

    // **컴포넌트 타입마다 우클릭 메뉴에 더할 항목의 표다**(D-220).
    //
    // 컴포넌트는 에디터를 모른다 - 타입별 메뉴 지식은 이 표에 모인다. 오브젝트 메뉴(계층·캔버스 뷰)와
    // 인스펙터 컴포넌트 머리 메뉴가 같은 표를 묻는다. 등록은 타입으로 하고, 부르기는 인스턴스마다 한다.
    //
    // `FindFieldExtra` 와 달리 **실행 중에 등록받고 한 타입에 여럿을 쌓는다** - 외부 에디터도 등록해야 하고,
    // 두 등록자가 같은 타입에 걸어도 하나가 사라지면 안 된다. 한 타입의 항목은 등록한 순서로 선다.
    //
    // **메뉴를 그리는 동안은 표를 바꾸지 않는다.** 그 사이의 등록·떼기는 거절한다(도는 배열이 그 자리에서 바뀐다).
    // DLL 이 등록했다면 내리기 전에 `Unregister` 한다 - 남은 함수 포인터는 내려간 코드를 가리킨다.
    // 메인 스레드 전용이다.
    class ComponentMenuTable
    {
    public:
        ComponentMenuTable() = default;
        ComponentMenuTable(const ComponentMenuTable&) = delete;
        ComponentMenuTable& operator=(const ComponentMenuTable&) = delete;

        // 받았으면 참이다. 타입이 `InvalidComponentTypeId` 이거나 함수·표지가 없거나, 같은 (타입, 함수, 표지)가 이미 있거나,
        // 그리는 중이면 받지 않는다.
        bool Register(ComponentTypeId typeId, ComponentMenuDraw draw, ComponentMenuOwner owner,
            void* user = nullptr);
        // 그 표지로 등록한 것을 모두 뗀다. 뗀 개수다. 그리는 중이면 떼지 않고 0 이다.
        std::uint32_t Unregister(ComponentMenuOwner owner);

        // 그 타입에 항목이 하나라도 있는가. 없으면 메뉴에 그 타입의 줄을 세우지 않는다.
        bool Has(ComponentTypeId typeId) const;
        std::uint32_t Count(ComponentTypeId typeId) const;

        // `context.address.typeId` 의 항목을 등록 순서로 그린다. 등록자가 바뀌는 자리에 구분선을 넣는다.
        // 항목마다 `context.user` 를 그 등록의 값으로 바꿔 넘긴다. 어느 훅이든 거짓을 돌려주면 거기서 멈추고
        // 거짓이다. 이미 열린 메뉴 안에서 부른다.
        // `separatorFirst` 가 참이면 **항목을 하나라도 그릴 때만** 맨 앞에 구분선을 넣는다 - 앞의 항목과 가르되,
        // 그 타입에 항목이 없으면 빈 구분선이 겹쳐 서지 않는다.
        bool DrawItems(const ComponentMenuContext& context, bool separatorFirst = false);

        bool IsDrawing() const;

    private:
        struct Entry
        {
            ComponentTypeId typeId = 0;
            ComponentMenuDraw draw = nullptr;
            ComponentMenuOwner owner = nullptr;
            void* user = nullptr;
        };

        Array<Entry> m_entries;
        std::uint32_t m_drawDepth = 0;
    };
}

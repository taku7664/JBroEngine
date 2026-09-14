#pragma once

#include <JBro/Editor/Command/SetPropertyCommand.h>

#include <JBro/Runtime/Component.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

namespace JBro
{
    struct PropertyTable;

    // 컴포넌트 하나의 값을 글자로 떠 둔 것이다(D-72).
    //
    // **바이트를 들고 있을 수는 없다.** 컴포넌트는 풀이 소유하고, 되살리면 주소가
    // 달라진다. 메모리를 가진 값(배열·문자열)은 얕은 복사가 되면 먼저 죽는 쪽이
    // 남은 쪽을 망가뜨리기도 한다. 그래서 잎사귀마다 길과 글자를 적어 둔다.
    //
    // **뜨는 쪽과 되돌리는 쪽이 한 군데에 있다.** 오브젝트 삭제와 컴포넌트 제거가
    // 둘 다 이 길을 쓴다 - 따로 걸어 내려가면 한쪽만 고쳐지는 날이 온다.
    struct ComponentValue
    {
        SetPropertyCommand::Path path;
        String text;
    };

    struct ComponentSnapshot
    {
        ComponentTypeId typeId = 0;
        bool enabled = true;
        Array<ComponentValue> values;
    };

    // 컴포넌트의 값을 뜬다.
    //
    // **프로퍼티를 등록하지 않은 타입이면 거짓이다.** 되살려도 값이 비므로,
    // 뜨지 못한 것을 뜬 척하면 부르는 쪽이 성공했다고 말하며 값을 잃는다(D-76).
    bool CaptureComponent(ComponentBase& component, ComponentSnapshot& out);

    // 떠 둔 값을 컴포넌트에 도로 써 넣는다. 켜짐 여부까지 되돌린다.
    bool ApplyComponent(ComponentBase& component, const ComponentSnapshot& snapshot);
}

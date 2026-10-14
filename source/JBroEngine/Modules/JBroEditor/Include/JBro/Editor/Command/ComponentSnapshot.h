#pragma once

#include <JBro/Editor/Command/ComponentAddress.h>
#include <JBro/Editor/Command/SetPropertyCommand.h>

#include <JBro/Runtime/Component.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>
#include <JBro/Types/Bool.h>

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
        // 컴포넌트를 뜬 것인가, 스크립트를 뜬 것인가(D-302). 붙여넣기가 어느 목록에 붙일지 이것으로 안다.
        AttachedKind kind = AttachedKind::Component;
        ComponentTypeId typeId = 0;
        Bool enabled = true;
        Array<ComponentValue> values;
        // 되살릴 번호다(D-302). **되살리기**(지운 것 되돌리기·붙여넣기의 다시 하기)만 이 번호로 다시 붙여, 그것을 가리키던 컴포넌트 핸들 필드가 다시 찾게 한다.
        // 뜰 때의 번호로 시작하고, 나무를 새 번호로 처음 붙여넣으면 그때 받은 번호로 바뀐다(오브젝트의 `instanceId` 와 같다).
        InstanceId instanceId = InvalidInstanceId;
        // 뜬 원본의 번호다. 바뀌지 않는다 - 붙여넣은 나무 안의 참조를 사본으로 옮길 때 원본을 알아본다.
        InstanceId sourceInstanceId = InvalidInstanceId;
    };

    // 컴포넌트의 값을 뜬다.
    //
    // **프로퍼티를 등록하지 않은 타입이거나, 저장할 값 중 하나라도 글자로 뜨지 못하면
    // 거짓이다.** 되살려도 그 값이 비므로, 뜨지 못한 것을 뜬 척하면 부르는 쪽이
    // 성공했다고 말하며 값을 잃는다(D-76). 컨테이너도 통째로 뜬다(D-86).
    // 컴포넌트와 스크립트가 같은 길이다(D-302).
    Bool CaptureAttached(AttachedRef attached, ComponentSnapshot& out);
    Bool CaptureComponent(ComponentBase& component, ComponentSnapshot& out);

    // 떠 둔 값을 도로 써 넣는다. 켜짐 여부까지 되돌린다. 목록(컴포넌트·스크립트)이나 타입이 다르면 거짓이다.
    Bool ApplyAttached(AttachedRef attached, const ComponentSnapshot& snapshot);
    Bool ApplyComponent(ComponentBase& component, const ComponentSnapshot& snapshot);
}

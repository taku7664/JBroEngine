#pragma once

#include <JBro/Editor/Command/ComponentAddress.h>
#include <JBro/Editor/Command/CompoundCommand.h>
#include <JBro/Editor/Command/SetPropertyCommand.h>
#include <JBro/Editor/EditorObjectRegistry.h>
#include <JBro/Editor/ScalarRun.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

#include <cstdint>

namespace JBro
{
    struct TypeDescriptor;

    // 목록 위젯이 알려 주는 편집 하나다(D-86).
    //
    // **편집을 값이 아니라 연산으로 든다.** 여럿 고른 대상의 배열은 길이도 내용도 저마다
    // 다르므로, 주된 대상의 결과 배열을 그대로 옮기면 나머지를 뭉갠다. "1번 원소에 0.5 를
    // 더한다" 는 연산은 대상마다 다시 적용할 수 있다 - 숫자를 델타로 옮기는 D-83 과 같다.
    struct ListEdit
    {
        enum class Kind : std::uint8_t
        {
            SetElement,
            Add,
            Remove,
            Move,
        };

        Kind kind = Kind::SetElement;
        // SetElement·Remove·Move 가 가리키는 원소 번호다.
        std::uint32_t index = 0;
        // Move 의 목적지다. 목록 위젯이 이미 보정한 원소 번호다(슬롯 번호가 아니다).
        std::uint32_t to = 0;
        // SetElement 의 값이다. `deltaCount` 가 0 이 아니면 숫자 원소에 더할 델타이고,
        // 0 이면 `text` 를 코덱으로 그대로 쓴다(bool·enum·문자열처럼 델타가 없는 값).
        std::uint32_t deltaCount = 0;
        float delta[ScalarRun::MaxCount] = {};
        String text;
    };

    // 배열 하나에 편집을 적용한다. **그 배열에 맞지 않는 편집이면 거짓이다** -
    // 원소가 모자라거나, 델타 개수가 원소의 숫자 수와 다르거나, 옮길 방법이 없는 원소다.
    // 거짓일 때 배열이 반쯤 바뀌어 있을 수 있으니 부르는 쪽이 되돌린다.
    bool ApplyListEdit(const TypeDescriptor& arrayType, void* array, const ListEdit& edit);

    // 대상마다 편집을 **차례대로 다시 적용하고** 그 결과를 한 되돌리기로 묶는다.
    //
    // 대상의 값은 전후를 뜬 뒤 도로 되돌려 놓는다 - 쓰는 것은 커맨드의 몫이다(D-71).
    // 편집이 맞지 않는 대상과 아무것도 바뀌지 않은 대상은 빠진다. 전부 빠지면 빈 묶음이다.
    OwnerPtr<CompoundCommand> MakeListEditCommand(
        EditorObjectRegistry& registry,
        const Array<ComponentAddress>& targets,
        const SetPropertyCommand::Path& path,
        const Array<ListEdit>& edits);
}

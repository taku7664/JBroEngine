#pragma once

#include <JBro/Editor/EditorCommand.h>

#include <JBro/Types/Array.h>

namespace JBro
{
    // 여러 편집을 **한 되돌리기**로 묶는다.
    //
    // 여럿을 골라 놓고 값 하나를 고치면 편집이 대상 수만큼 생기는데, 그것이
    // 스택에 따로 쌓이면 Ctrl+Z 를 그 수만큼 눌러야 한다. 사용자가 한 일은
    // 하나였으므로 되돌리기도 하나여야 한다.
    //
    // 기존 엔진은 대상 목록과 델타를 든 전용 커맨드로 같은 일을 했다
    // (`CSetObjectTransformCommand`). 우리는 묶는 쪽을 따로 두는데, 그래야
    // "단추 하나가 두 값을 바꾼다" 같은 다른 자리에도 그대로 쓰인다 -
    // 기존에도 그 자리를 위한 커맨드가 따로 있었다.
    class CompoundCommand final : public EditorCommand
    {
    public:
        explicit CompoundCommand(const char* name);

        // `Execute` 전에만 넣는다. 실행한 뒤에 늘리면 되돌리기가 무엇을
        // 되돌리는지가 갈린다.
        bool Add(OwnerPtr<EditorCommand> command);
        std::size_t GetCount() const;

        const char* GetName() const override;

        // **전부 되거나 하나도 안 된다.** 중간에 실패하면 앞서 성공한 것을
        // 되돌리고 거짓을 돌려준다 - 반쯤 적용된 편집이 스택에 오르지 않은 채
        // 남는 것이 가장 나쁘다.
        bool Execute() override;
        void Undo() override;
        void Redo() override;

        bool CanMerge(const EditorCommand& newer) const override;
        bool TryMerge(const EditorCommand& newer) override;

    private:
        const char* m_name = nullptr;
        Array<OwnerPtr<EditorCommand>> m_commands;
        bool m_executed = false;
    };
}

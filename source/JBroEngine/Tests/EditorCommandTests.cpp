#include <JBro/Editor/EditorCommand.h>

#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    // 되돌리기가 실제로 값을 되돌리는지 보려면 무언가를 바꾸는 커맨드가 있어야 한다.
    // 진짜 컴포넌트 대신 정수 하나를 쓴다 - 스택의 계약만 보는 자리다.
    class SetNumberCommand final : public JBro::EditorCommand
    {
    public:
        SetNumberCommand(int& target, int oldValue, int newValue, int tag)
            : m_target(&target)
            , m_oldValue(oldValue)
            , m_newValue(newValue)
            , m_tag(tag)
        {
        }

        const char* GetName() const override
        {
            return "Set Number";
        }
        bool Execute() override
        {
            if (m_failExecute)
            {
                return false;
            }
            *m_target = m_newValue;
            ++executes;
            return true;
        }
        void Undo() override
        {
            *m_target = m_oldValue;
            ++undos;
        }
        void Redo() override
        {
            *m_target = m_newValue;
            ++redos;
        }
        bool TryMerge(const JBro::EditorCommand& newer) override
        {
            const auto* other = dynamic_cast<const SetNumberCommand*>(&newer);
            if (other == nullptr || other->m_tag != m_tag)
            {
                return false;
            }
            // 처음 값은 이쪽 것을 지킨다 - 드래그 전체가 한 번에 되돌아가야 한다.
            m_newValue = other->m_newValue;
            return true;
        }

        void FailNextExecute()
        {
            m_failExecute = true;
        }

        static int executes;
        static int undos;
        static int redos;

    private:
        int* m_target = nullptr;
        int m_oldValue = 0;
        int m_newValue = 0;
        // 같은 대상을 가리키는지 나타내는 표식이다. 실제 커맨드에서는 컴포넌트와
        // 프로퍼티 경로가 이 역할을 한다.
        int m_tag = 0;
        bool m_failExecute = false;
    };

    int SetNumberCommand::executes = 0;
    int SetNumberCommand::undos = 0;
    int SetNumberCommand::redos = 0;

    JBro::OwnerPtr<JBro::EditorCommand> MakeSet(int& target, int from, int to, int tag = 0)
    {
        return JBro::MakeOwnerPtr<SetNumberCommand>(target, from, to, tag);
    }

    void TestTheStackUndoesAndRedoes()
    {
        JBro::EditorCommandManager commands;
        int value = 0;

        Check(false == commands.CanUndo(), "nothing has been done yet");
        Check(false == commands.CanRedo(), "and nothing to put back");
        Check(false == commands.IsDirty(), "so nothing is unsaved");

        Check(commands.Execute(MakeSet(value, 0, 1)), "the first edit must go through");
        Check(value == 1, "and must have happened");
        Check(commands.Execute(MakeSet(value, 1, 2)), "and the second");
        Check(value == 2, "and that one too");
        Check(commands.GetUndoCount() == 2, "both are on the stack");
        Check(commands.IsDirty(), "and the document is unsaved");

        Check(commands.Undo() && value == 1, "undo must put back the value before it");
        Check(commands.Undo() && value == 0, "and again");
        Check(false == commands.CanUndo(), "that was all of them");
        Check(false == commands.Undo(), "undoing nothing must be refused");
        Check(commands.GetRedoCount() == 2, "and both are waiting to be redone");

        Check(commands.Redo() && value == 1, "redo must put it back");
        Check(commands.Redo() && value == 2, "and again");
        Check(false == commands.Redo(), "redoing nothing must be refused");

        // **새 편집은 앞으로 가는 길을 끊는다.** 되돌린 자리에서 다른 것을 하면
        // 원래 있던 미래는 더 이상 닿을 수 없다.
        Check(commands.Undo() && value == 1, "step back once");
        Check(commands.CanRedo(), "there is a future to go back to");
        Check(commands.Execute(MakeSet(value, 1, 9)), "but we do something else");
        Check(false == commands.CanRedo(), "and that future is gone");
        Check(value == 9, "the new edit stands");
    }

    void TestAFailedEditIsNotRemembered()
    {
        JBro::EditorCommandManager commands;
        int value = 0;
        auto failing = JBro::MakeOwnerPtr<SetNumberCommand>(value, 0, 1, 0);
        failing->FailNextExecute();

        Check(false == commands.Execute(std::move(failing)),
            "an edit that cannot be applied must report that");
        // **일어나지 않은 일을 되돌릴 수는 없다.** 실패한 커맨드가 스택에 남으면
        // 다음 Ctrl+Z 가 아무 일도 없던 자리를 되돌린다.
        Check(false == commands.CanUndo(), "and must not be on the stack");
        Check(value == 0, "and must not have changed anything");

        Check(false == commands.Execute({}), "nothing at all must be refused too");
    }

    // **드래그 하나가 되돌리기 하나다.** 기존 엔진에서 가져온 규칙이고, 마우스를
    // 끄는 동안 프레임마다 쌓이면 되돌리기를 백 번 눌러야 원래대로 온다.
    //
    // 여기서는 ImGui 컨텍스트가 없다 - 그때는 합치지 않는 것이 계약이다.
    // 마우스를 모르는 곳에서 조용히 합치면 서로 다른 편집이 하나로 뭉친다.
    void TestEditsDoNotMergeWithoutAMouse()
    {
        JBro::EditorCommandManager commands;
        int value = 0;
        Check(commands.Execute(MakeSet(value, 0, 1, 7)), "the first edit");
        Check(commands.Execute(MakeSet(value, 1, 2, 7)), "and another on the same target");
        Check(commands.GetUndoCount() == 2,
            "with no mouse to group them, each edit stands on its own");
        Check(commands.Undo() && value == 1, "so undo steps back one edit");
    }

    void TestSavingIsTrackedByRevisionNotByAFlag()
    {
        JBro::EditorCommandManager commands;
        int value = 0;
        Check(false == commands.IsDirty(), "a fresh document is saved");

        commands.Execute(MakeSet(value, 0, 1));
        Check(commands.IsDirty(), "an edit makes it unsaved");
        commands.MarkSaved();
        Check(false == commands.IsDirty(), "saving clears that");

        commands.Execute(MakeSet(value, 1, 2));
        Check(commands.IsDirty(), "and the next edit sets it again");
        // **되돌려도 저장된 상태로 돌아가지는 않는다.** 판번호는 앞으로만 간다 -
        // 되돌리기도 하나의 변경이기 때문이다. 불리언 하나로 두면 여기서 틀린다.
        commands.Undo();
        Check(commands.IsDirty(), "undoing is itself a change, not a return to saved");
    }

    void TestClearingForgetsEverything()
    {
        JBro::EditorCommandManager commands;
        int value = 0;
        commands.Execute(MakeSet(value, 0, 1));
        commands.Undo();
        Check(commands.CanRedo(), "there is something to redo");

        commands.Clear();
        Check(false == commands.CanUndo() && false == commands.CanRedo(),
            "clearing must empty both stacks");
        Check(false == commands.IsDirty(), "and start counting again");
    }

    // 스택은 끝없이 자라지 않는다. 기존 엔진에는 상한이 없어서 오래 켜 둔 편집기가
    // 되돌리기 스택만으로 계속 자란다.
    void TestTheStackHasACeiling()
    {
        JBro::EditorCommandManager commands;
        int value = 0;
        for (int index = 0; index < 400; ++index)
        {
            // 표식을 다르게 주어 합쳐지지 않게 한다.
            Check(commands.Execute(MakeSet(value, index, index + 1, index)),
                "every edit must go through");
        }
        Check(commands.GetUndoCount() == 256, "the oldest edits must fall off the bottom");
        Check(value == 400, "and the newest must still stand");

        // 가장 오래된 것을 버렸으므로 처음까지는 못 돌아간다. 끝까지 되돌려 본다.
        while (commands.Undo())
        {
        }
        Check(value == 400 - 256, "undo reaches as far back as the stack kept");
    }
}

int RunEditorCommandTests()
{
    TestTheStackUndoesAndRedoes();
    TestAFailedEditIsNotRemembered();
    TestEditsDoNotMergeWithoutAMouse();
    TestSavingIsTrackedByRevisionNotByAFlag();
    TestClearingForgetsEverything();
    TestTheStackHasACeiling();
    std::cout << "Editor command tests passed.\n";
    return 0;
}

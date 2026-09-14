#include <JBro/Editor/Command/CompoundCommand.h>

#include <utility>

namespace JBro
{
    CompoundCommand::CompoundCommand(const char* name)
        : m_name(name != nullptr ? name : "Edit")
    {
    }

    bool CompoundCommand::Add(OwnerPtr<EditorCommand> command)
    {
        if (m_executed || command.Get() == nullptr)
        {
            return false;
        }
        m_commands.Add(std::move(command));
        return true;
    }

    std::size_t CompoundCommand::GetCount() const
    {
        return m_commands.Size();
    }

    const char* CompoundCommand::GetName() const
    {
        return m_name;
    }

    bool CompoundCommand::Execute()
    {
        if (m_commands.IsEmpty())
        {
            // 묶을 것이 없다. 빈 것을 스택에 올리면 Ctrl+Z 가 헛걸음한다.
            return false;
        }
        for (std::size_t index = 0; index < m_commands.Size(); ++index)
        {
            if (m_commands[index]->Execute())
            {
                continue;
            }
            // 여기까지 성공한 것을 역순으로 되돌린다.
            for (std::size_t done = index; done > 0; --done)
            {
                m_commands[done - 1]->Undo();
            }
            return false;
        }
        m_executed = true;
        return true;
    }

    void CompoundCommand::Undo()
    {
        // **역순이다.** 뒤의 편집이 앞의 것에 기대고 있을 수 있다.
        for (std::size_t index = m_commands.Size(); index > 0; --index)
        {
            m_commands[index - 1]->Undo();
        }
    }

    void CompoundCommand::Redo()
    {
        for (std::size_t index = 0; index < m_commands.Size(); ++index)
        {
            m_commands[index]->Redo();
        }
    }

    bool CompoundCommand::CanMerge(const EditorCommand& newer) const
    {
        const auto* other = dynamic_cast<const CompoundCommand*>(&newer);
        if (other == nullptr || other->m_commands.Size() != m_commands.Size())
        {
            return false;
        }
        for (std::size_t index = 0; index < m_commands.Size(); ++index)
        {
            if (false == m_commands[index]->CanMerge(*other->m_commands[index]))
            {
                return false;
            }
        }
        return true;
    }

    bool CompoundCommand::TryMerge(const EditorCommand& newer)
    {
        // **먼저 전부 물어본다.** 합치다 중간에 거절당하면 절반만 합쳐진
        // 상태로 남고, 그것을 되돌릴 방법이 없다.
        if (false == CanMerge(newer))
        {
            return false;
        }
        const auto& other = static_cast<const CompoundCommand&>(newer);
        for (std::size_t index = 0; index < m_commands.Size(); ++index)
        {
            m_commands[index]->TryMerge(*other.m_commands[index]);
        }
        return true;
    }
}

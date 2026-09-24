#include <JBro/Editor/Command/AssetFileCommands.h>

#include <JBro/Editor/EditorApplication.h>

#include <utility>

namespace JBro
{
    CreateAssetFolderCommand::CreateAssetFolderCommand(EditorApplication& editor,
        String relativePath)
        : m_editor(&editor)
        , m_relativePath(std::move(relativePath))
    {
    }

    CreateAssetFolderCommand::~CreateAssetFolderCommand()
    {
        // 되돌리기 스택에서 밀려났다. 휴지통에 든 채였으면 그것으로 끝이다.
        if (m_editor != nullptr && false == m_trashPath.empty())
        {
            m_editor->DropFromTrash(m_trashPath.c_str());
        }
    }

    const char* CreateAssetFolderCommand::GetName() const
    {
        return "Create Folder";
    }

    bool CreateAssetFolderCommand::Execute()
    {
        return m_editor != nullptr && m_editor->CreateAssetFolderNow(m_relativePath.c_str());
    }

    void CreateAssetFolderCommand::Undo()
    {
        if (m_editor == nullptr)
        {
            return;
        }
        m_trashPath = m_editor->MoveAssetToTrash(m_relativePath.c_str());
    }

    void CreateAssetFolderCommand::Redo()
    {
        if (m_editor == nullptr)
        {
            return;
        }
        // 되돌리기가 옮겨 둔 것을 그대로 돌려놓는다. 새로 만들면 그 사이에 넣은 파일을 잃는다.
        if (false == m_trashPath.empty()
            && m_editor->RestoreFromTrash(m_trashPath.c_str(), m_relativePath.c_str()))
        {
            m_trashPath.clear();
            return;
        }
        m_editor->CreateAssetFolderNow(m_relativePath.c_str());
    }

    MoveAssetCommand::MoveAssetCommand(EditorApplication& editor, String fromRelative,
        String toRelative)
        : m_editor(&editor)
        , m_from(std::move(fromRelative))
        , m_to(std::move(toRelative))
    {
    }

    const char* MoveAssetCommand::GetName() const
    {
        return "Move Asset";
    }

    const String& MoveAssetCommand::GetTarget() const
    {
        return m_to;
    }

    bool MoveAssetCommand::MoveTo(const String& fromRelative, const String& toRelative)
    {
        return m_editor != nullptr
            && m_editor->MoveAssetPathNow(fromRelative.c_str(), toRelative.c_str());
    }

    bool MoveAssetCommand::Execute()
    {
        return MoveTo(m_from, m_to);
    }

    void MoveAssetCommand::Undo()
    {
        MoveTo(m_to, m_from);
    }

    void MoveAssetCommand::Redo()
    {
        MoveTo(m_from, m_to);
    }

    DeleteAssetCommand::DeleteAssetCommand(EditorApplication& editor, String relativePath)
        : m_editor(&editor)
        , m_relativePath(std::move(relativePath))
    {
    }

    DeleteAssetCommand::~DeleteAssetCommand()
    {
        if (m_editor != nullptr && false == m_trashPath.empty())
        {
            m_editor->DropFromTrash(m_trashPath.c_str());
        }
    }

    const char* DeleteAssetCommand::GetName() const
    {
        return "Delete Asset";
    }

    bool DeleteAssetCommand::Execute()
    {
        if (m_editor == nullptr)
        {
            return false;
        }
        m_trashPath = m_editor->MoveAssetToTrash(m_relativePath.c_str());
        return false == m_trashPath.empty();
    }

    void DeleteAssetCommand::Undo()
    {
        if (m_editor == nullptr || m_trashPath.empty())
        {
            return;
        }
        if (m_editor->RestoreFromTrash(m_trashPath.c_str(), m_relativePath.c_str()))
        {
            m_trashPath.clear();
        }
    }

    void DeleteAssetCommand::Redo()
    {
        if (m_editor == nullptr)
        {
            return;
        }
        m_trashPath = m_editor->MoveAssetToTrash(m_relativePath.c_str());
    }
}

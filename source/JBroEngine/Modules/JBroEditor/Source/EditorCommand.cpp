#include <JBro/Editor/EditorCommand.h>

#include <imgui.h>

#include <utility>

namespace JBro
{
    bool EditorCommandManager::Execute(OwnerPtr<EditorCommand> command,
        const char* documentKey)
    {
        if (command.Get() == nullptr || false == command->Execute())
        {
            return false;
        }

        // 다른 문서끼리 합쳐질 걱정은 하지 않는다. 합치기는 커맨드가 스스로 받아들여야
        // 일어나고(`TryMerge` 의 기본은 거짓이다), 받아들이는 것은 같은 대상을 끄는
        // 같은 종류뿐이다 - 여기서 문서까지 견주면 잴 수 없는 줄이 하나 는다.
        if (ContinuesDrag() && false == m_undo.IsEmpty()
            && m_undo[m_undo.Size() - 1].command->TryMerge(*command))
        {
            // 합쳐졌다. 값은 위의 `Execute` 가 이미 적용했으므로 이 커맨드는 버린다.
            m_redo.Clear();
            Touch(documentKey);
            return true;
        }

        Entry entry;
        entry.command = std::move(command);
        entry.documentKey = documentKey;
        PushUndo(std::move(entry));
        m_redo.Clear();
        Touch(documentKey);
        return true;
    }

    void EditorCommandManager::Touch(const char* documentKey)
    {
        // 판번호는 무엇이 움직이든 올라간다 - 에셋 참조를 다시 잇는 자리가 이것을 본다.
        ++m_revision;
        if (documentKey == nullptr)
        {
            m_canvasRevision = m_revision;
        }
    }

    bool EditorCommandManager::ContinuesDrag()
    {
        // ImGui 컨텍스트가 없으면(창 없는 테스트) 드래그도 없다. 커맨드마다 따로 쌓인다.
        ImGuiContext* context = ImGui::GetCurrentContext();
        const bool mouseDown =
            context != nullptr && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const float downDuration = context != nullptr
            ? ImGui::GetIO().MouseDownDuration[ImGuiMouseButton_Left]
            : -1.0f;

        // 누른 시간이 줄었으면 새로 누른 것이다. 놓은 순간을 직접 볼 수 없어서
        // 이렇게 알아낸다.
        if (false == mouseDown || downDuration < m_lastMouseDownDuration)
        {
            m_mergingDrag = false;
        }
        m_lastMouseDownDuration = downDuration;

        const bool continues = mouseDown && m_mergingDrag;
        // 드래그 중 첫 커맨드면 다음 프레임부터 그것에 합친다.
        m_mergingDrag = mouseDown;
        return continues;
    }

    void EditorCommandManager::PushUndo(Entry entry)
    {
        if (m_undo.Size() >= MaxUndoDepth)
        {
            // 가장 오래된 것을 버린다. Array 에는 앞에서 빼는 길이 없으므로 한 칸씩
            // 당긴다 - 상한에 닿았을 때만 도는 길이고, 편집기의 한 프레임에 한 번이다.
            for (std::size_t index = 1; index < m_undo.Size(); ++index)
            {
                m_undo[index - 1] = std::move(m_undo[index]);
            }
            m_undo.Resize(m_undo.Size() - 1);
        }
        m_undo.Add(std::move(entry));
    }

    bool EditorCommandManager::Undo()
    {
        if (m_undo.IsEmpty())
        {
            return false;
        }
        Entry entry = std::move(m_undo[m_undo.Size() - 1]);
        m_undo.Resize(m_undo.Size() - 1);
        entry.command->Undo();
        const char* documentKey = entry.documentKey;
        m_redo.Add(std::move(entry));
        Touch(documentKey);
        // 되돌린 뒤에 이어서 드래그로 합치면 안 된다. 방금 되살린 값 위에 덮인다.
        m_mergingDrag = false;
        return true;
    }

    bool EditorCommandManager::Redo()
    {
        if (m_redo.IsEmpty())
        {
            return false;
        }
        Entry entry = std::move(m_redo[m_redo.Size() - 1]);
        m_redo.Resize(m_redo.Size() - 1);
        entry.command->Redo();
        const char* documentKey = entry.documentKey;
        PushUndo(std::move(entry));
        Touch(documentKey);
        m_mergingDrag = false;
        return true;
    }

    void EditorCommandManager::Clear()
    {
        m_undo.Clear();
        m_redo.Clear();
        m_mergingDrag = false;
        m_lastMouseDownDuration = -1.0f;
        m_revision = 0;
        m_canvasRevision = 0;
        m_savedCanvasRevision = 0;
    }

    bool EditorCommandManager::CanUndo() const
    {
        return false == m_undo.IsEmpty();
    }

    bool EditorCommandManager::CanRedo() const
    {
        return false == m_redo.IsEmpty();
    }

    std::size_t EditorCommandManager::GetUndoCount() const
    {
        return m_undo.Size();
    }

    std::size_t EditorCommandManager::GetRedoCount() const
    {
        return m_redo.Size();
    }

    void EditorCommandManager::MarkSaved()
    {
        m_savedCanvasRevision = m_canvasRevision;
    }

    bool EditorCommandManager::IsDirty() const
    {
        return m_canvasRevision != m_savedCanvasRevision;
    }

    std::uint64_t EditorCommandManager::GetRevision() const
    {
        return m_revision;
    }
}

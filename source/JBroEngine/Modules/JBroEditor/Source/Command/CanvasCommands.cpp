#include <JBro/Editor/Command/CanvasCommands.h>

#include <JBro/Canvas/Canvas.h>

namespace JBro
{
    SetCanvasBackgroundCommand::SetCanvasBackgroundCommand(Canvas& canvas, const Color& color)
        : m_canvas(&canvas)
        , m_before(canvas.GetBackgroundColor())
        , m_after(color)
    {
    }

    const char* SetCanvasBackgroundCommand::GetName() const
    {
        return "Set Canvas Background";
    }

    bool SetCanvasBackgroundCommand::Execute()
    {
        if (m_canvas == nullptr)
        {
            return false;
        }
        m_canvas->SetBackgroundColor(m_after);
        return true;
    }

    void SetCanvasBackgroundCommand::Undo()
    {
        if (m_canvas != nullptr)
        {
            m_canvas->SetBackgroundColor(m_before);
        }
    }

    void SetCanvasBackgroundCommand::Redo()
    {
        Execute();
    }

    bool SetCanvasBackgroundCommand::CanMerge(const EditorCommand& newer) const
    {
        // **같은 캔버스의 같은 편집만** 합친다. 타입이 다르면 다른 일이고, 캔버스가
        // 다르면 되살릴 값이 남의 것이 된다.
        const auto* other = dynamic_cast<const SetCanvasBackgroundCommand*>(&newer);
        return other != nullptr && other->m_canvas == m_canvas;
    }

    bool SetCanvasBackgroundCommand::TryMerge(const EditorCommand& newer)
    {
        if (false == CanMerge(newer))
        {
            return false;
        }
        // **나중 값만 흡수한다.** 되살릴 값은 끌기가 시작되기 전의 것, 곧 처음 것이다.
        m_after = static_cast<const SetCanvasBackgroundCommand&>(newer).m_after;
        return true;
    }
}

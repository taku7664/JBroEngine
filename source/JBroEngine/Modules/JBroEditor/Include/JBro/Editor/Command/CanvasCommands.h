#pragma once

#include <JBro/Editor/EditorCommand.h>
#include <JBro/Types/Color.h>

namespace JBro
{
    class Canvas;

    // 캔버스 자신의 값을 고치는 커맨드다(D-186).
    //
    // 오브젝트도 레이어도 아닌 값이 캔버스에 붙기 시작한 첫 자리다. 기존 엔진의 캔버스
    // 인스펙터에는 배경색 말고도 뷰포트 목록이 있었지만, 우리에게는 뷰포트라는 것이 없다.
    //
    // **배경색은 끌어서 고치는 값**이라 병합이 필요하다. 색 고르개를 한 번 끄는 동안
    // 값이 프레임마다 바뀌는데, 그때마다 되돌리기가 한 칸씩 쌓이면 끌기 한 번을 되돌리는 데
    // 수십 번이 든다. 같은 캔버스를 잇달아 고치는 커맨드는 하나로 합친다.
    class SetCanvasBackgroundCommand final : public EditorCommand
    {
    public:
        SetCanvasBackgroundCommand(Canvas& canvas, const Color& color);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;
        bool CanMerge(const EditorCommand& newer) const override;
        bool TryMerge(const EditorCommand& newer) override;

    private:
        Canvas* m_canvas = nullptr;
        Color m_before;
        Color m_after;
    };
}

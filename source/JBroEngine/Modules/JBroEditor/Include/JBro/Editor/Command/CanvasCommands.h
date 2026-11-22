#pragma once

#include <JBro/Canvas/Environment.h>
#include <JBro/Canvas/Viewport.h>
#include <JBro/Editor/EditorCommand.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    class Canvas;

    // 캔버스 자신의 값을 고치는 커맨드다(D-186).
    //
    // 오브젝트도 레이어도 아닌 값이 캔버스에 붙기 시작한 첫 자리다. 뷰포트 목록은 아래 `SetCanvasViewportsCommand` 다(D-438).
    //
    // **배경색은 끌어서 고치는 값**이라 병합이 필요하다. 색 고르개를 한 번 끄는 동안
    // 값이 프레임마다 바뀌는데, 그때마다 되돌리기가 한 칸씩 쌓이면 끌기 한 번을 되돌리는 데
    // 수십 번이 든다. 같은 캔버스를 잇달아 고치는 커맨드는 하나로 합친다.
    class SetCanvasBackgroundCommand final : public EditorCommand
    {
    public:
        SetCanvasBackgroundCommand(Canvas& canvas, const Color& color);

        const char* GetName() const override;
        Bool Execute() override;
        void Undo() override;
        void Redo() override;
        Bool CanMerge(const EditorCommand& newer) const override;
        Bool TryMerge(const EditorCommand& newer) override;

    private:
        Canvas* m_canvas = nullptr;
        Color m_before;
        Color m_after;
    };

    // **뷰포트 목록을 고치는 커맨드다**(D-438, viewport-plan §4.5). 칸 하나·더하기·빼기·옮기기 모두 목록 전체를 앞뒤로 뜬다 -
    // 뷰포트는 몇 개뿐이고, 한 손짓이 여럿을 바꿔도(빼면 뒤의 번호가 당겨진다) 커맨드는 하나다.
    // `mergeKey` 가 0 이 아니면 같은 키의 잇단 커맨드를 합친다 - 사각형 칸을 끄는 동안 프레임마다 쌓이지 않게(배경색 커맨드와 같다).
    class SetCanvasViewportsCommand final : public EditorCommand
    {
    public:
        SetCanvasViewportsCommand(Canvas& canvas, const Array<CanvasViewport>& after, UInt32 mergeKey = 0);

        const char* GetName() const override;
        Bool Execute() override;
        void Undo() override;
        void Redo() override;
        Bool CanMerge(const EditorCommand& newer) const override;
        Bool TryMerge(const EditorCommand& newer) override;

    private:
        Canvas* m_canvas = nullptr;
        Array<CanvasViewport> m_before;
        Array<CanvasViewport> m_after;
        UInt32 m_mergeKey = 0;
    };

    // **캔버스 환경을 고치는 커맨드다**(D-447). 환경 전체를 앞뒤로 뜬다. `mergeKey` 가 0 이 아니면 같은 키의 잇단 커맨드를 합친다 - 색·세기 칸을 끄는 동안
    // 프레임마다 쌓이지 않게(뷰포트 커맨드와 같다). 풀린 하늘 핸들은 캔버스가 지킨다(`Canvas::SetEnvironment`).
    class SetCanvasEnvironmentCommand final : public EditorCommand
    {
    public:
        SetCanvasEnvironmentCommand(Canvas& canvas, const CanvasEnvironment& after, UInt32 mergeKey = 0);

        const char* GetName() const override;
        Bool Execute() override;
        void Undo() override;
        void Redo() override;
        Bool CanMerge(const EditorCommand& newer) const override;
        Bool TryMerge(const EditorCommand& newer) override;

    private:
        Canvas* m_canvas = nullptr;
        CanvasEnvironment m_before;
        CanvasEnvironment m_after;
        UInt32 m_mergeKey = 0;
    };
}

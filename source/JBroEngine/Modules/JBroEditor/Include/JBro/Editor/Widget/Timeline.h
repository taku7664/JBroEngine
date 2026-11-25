#pragma once

#include <JBro/Editor/Widget/Common.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::Widget
{
    // 표식 트랙 한 줄에서 이번 프레임에 일어난 일이다. 부르는 쪽이 제 자료에 옮긴다(위젯은 자료를 모른다).
    struct TimelineMarkerEdit
    {
        // 누르기 시작한 표식이다(고르기).
        Int32 pressed = -1;
        // 끄는 중인 표식과 그 시간이다. 끄는 동안은 초안만 옮긴다.
        Int32 dragging = -1;
        Float dragTime = 0.0f;
        // 손을 뗀 표식이다. 이때 커맨드 하나로 남긴다.
        Int32 released = -1;
        // 빈 자리를 두 번 눌렀다. 그 시간에 더한다.
        Bool add = false;
        Float addTime = 0.0f;
    };

    // **눈금 있는 시간 축**이다(모델 에디터의 타임라인, D-465). 위에서부터 눈금 줄과 트랙 줄을 쌓고 `End` 가 재생 선을 그 위로 긋는다.
    // 왼쪽 `labelWidth` 는 줄 이름 칸이고 그 오른쪽이 시간 칸이다 - 모든 줄이 같은 시간 칸을 쓰므로 같은 시간은 같은 x 다.
    //
    //   ┌──────┬ 0 ─── 0.25 ─── 0.5 ─── 0.75 ─── 1 ┐   ← Ruler: 누르거나 끌면 그 시간으로
    //   │ 이벤트 │    ◆            ◆                 │   ← MarkerTrack: 표식 끌기, 빈 자리 두 번 누르기
    //   └──────┴──────────│────────────────────────┘   ← End: 재생 선
    class Timeline
    {
    public:
        Timeline(const char* id, Float duration, Float labelWidth = 0.0f);
        ~Timeline();
        Timeline(const Timeline&) = delete;
        Timeline& operator=(const Timeline&) = delete;

        // 눈금 줄이다. 누르거나 끌면 `time` 을 그 시간으로 바꾸고 참이다.
        Bool Ruler(Float& time);
        // 표식 트랙 한 줄이다. `times` 는 표식마다의 시간, `names` 는 표식에 마우스를 올리면 뜨는 이름(비어도 된다)이다.
        // `selected` 표식은 고른 색이다. `emptyTooltip` 은 빈 자리에 올렸을 때 뜬다.
        TimelineMarkerEdit MarkerTrack(const char* id, const char* icon, const char* label, ArrayView<const Float> times,
            ArrayView<const char* const> names, Int32 selected, const char* emptyTooltip);
        // 재생 선을 긋고 줄 쌓기를 닫는다. 소멸자도 부르지만(선 없이) 보통은 시간과 함께 부른다.
        void End(Float time);

        // 시간 칸의 x 와 시간을 오간다(화면 좌표). 칸 밖은 끝으로 자른다.
        Float XAt(Float time) const;
        Float TimeAt(Float x) const;
        // 마지막으로 그린 트랙 줄의 칸이다(시험이 누를 자리를 찾는다).
        Float GetTrackLeft() const { return m_trackLeft; }
        Float GetTrackWidth() const { return m_trackWidth; }
        Float GetLastRowTop() const { return m_lastRowTop; }
        Float GetLastRowHeight() const { return m_lastRowHeight; }

        // 눈금 줄 하나와 트랙 `tracks` 줄이 차지하는 높이다. 부르는 쪽이 칸을 미리 잡는다.
        static Float Height(UInt32 tracks);

    private:
        void DrawLabel(const char* icon, const char* label, Float top, Float height) const;
        void NextRow(Float bottom);

        Float m_duration = 1.0f;
        Float m_left = 0.0f;
        Float m_trackLeft = 0.0f;
        Float m_trackWidth = 1.0f;
        Float m_top = 0.0f;
        Float m_bottom = 0.0f;
        Float m_lastRowTop = 0.0f;
        Float m_lastRowHeight = 0.0f;
        Bool m_ended = false;
    };
}

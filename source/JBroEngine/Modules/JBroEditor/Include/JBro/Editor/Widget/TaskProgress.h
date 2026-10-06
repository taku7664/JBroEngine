#pragma once

#include <JBro/Editor/Widget/Common.h>
#include <JBro/Task/TaskGroup.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    class TaskManager;
}

namespace JBro::Widget
{
    // 태스크 묶음 하나를 한 줄로 줄인 것이다. 그리지 않고 센다 - 상태 표시줄과 시험이 ImGui 없이 쓴다.
    // `done`·`total` 은 하위 작업을 태스크마다 더한 것이고, 빈 묶음은 `0 / 0` 이다.
    struct TaskGroupSummary
    {
        Bool found = false;
        UInt32 done = 0;
        UInt32 total = 0;
        UInt32 failed = 0;
        UInt32 taskCount = 0;
        TaskState state = TaskState::Pending;
        const char* nameKey = nullptr;
    };

    // 없는 묶음(끝나 지워진 것)이면 `found` 가 거짓이다.
    TaskGroupSummary SummarizeTaskGroup(const TaskManager& tasks, TaskGroupId group);

    // 태스크 묶음 하나의 진행이다(D-217). 바(기본)나 원으로 하위 작업 `끝난 수 / 전체` 를 보이고, `TaskList()` 면 그 아래에
    // 태스크마다 상태 표시·이름·`끝난 수/전체` 를 늘어놓는다. 기존 엔진은 프로젝트 로딩(`RenderProjectLoadingPopup`)과 빌드 진행을
    // 화면마다 따로 그렸다 - 그것을 이 위젯 하나로 모은다. 목록 줄 머리는 기존 엔진처럼 진행 중이면 스피너, 끝났으면 체크다.
    //
    // 묶음은 번호로 받고 그릴 때마다 찾는다(D-212: 관리자가 준 포인터는 다음 `Update` 전까지만 유효하다). 끝나 지워진 묶음이면
    // 아무것도 그리지 않는다. 묶음과 태스크의 이름은 로컬라이징 키로 보고 번역하며, 키가 아니면(파일 이름) 그대로 보인다.
    class TaskProgress
    {
    public:
        TaskProgress(const TaskManager& tasks, TaskGroupId group);

        TaskProgress& Bar();
        TaskProgress& Circle();
        TaskProgress& TaskList(Bool show = true);
        // 바는 폭·높이(0 이면 남은 폭·한 줄 높이), 원은 `x` 가 지름이다.
        TaskProgress& Size(ImVec2 size);
        // 목록이 이보다 길면 이만큼만 보이고 굴린다.
        TaskProgress& MaxRows(UInt32 rows);

        // 묶음이 없으면 아무것도 그리지 않고 거짓이다.
        Bool Draw() const;
        Bool operator()() const;

    private:
        const TaskManager& m_tasks;
        TaskGroupId m_group = InvalidTaskGroupId;
        Bool m_circle = false;
        Bool m_list = false;
        ImVec2 m_size = ImVec2(0.0f, 0.0f);
        UInt32 m_maxRows = 8;
    };

    // 상태 표시줄의 도는 묶음 한 칸이다(13 번). 스피너·묶음 이름·짧은 로딩 바·그 밖에 도는 묶음 수(`+N`)를 한 줄에 그린다.
    // 한 칸 전체가 누를 자리이고 누르면 참이다. 묶음이 없으면 아무것도 그리지 않고 거짓이다.
    Bool TaskStatusItem(const TaskManager& tasks, TaskGroupId group, UInt32 othersRunning, Float barWidth);
    // 상태 표시줄의 마지막 알림 글자다. 남은 폭의 오른쪽 끝에 무게 색으로 그린다. 누르면 참이다.
    Bool StatusMessage(const char* text, Severity severity);
    // 태스크 목록 창의 묶음 한 칸이다. 묶음 이름과 로딩 바, 그 아래 태스크 목록이다. 묶음이 없으면 거짓이다.
    Bool TaskGroupSection(const TaskManager& tasks, TaskGroupId group);
}

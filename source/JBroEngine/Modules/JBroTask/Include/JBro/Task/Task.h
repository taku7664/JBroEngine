#pragma once

#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/String.h>

#include <atomic>
#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

// 태스크 관리자의 일 한 덩어리다(D-209). 기존 엔진 `Engine/Core/Task` 의 `CTask` 자리이고 접두사만 뗐다.
//
// **본문과 마무리는 가상 함수다.** 기존 엔진은 `std::function` 본문과 `EndCallback` 을 받았는데, 람다가
// 캡처한 `SafePtr` 가 워커에서 소멸하면 비원자 카운트가 메인과 경쟁한다(`SafePtr.h` 머리 주석). 들고 갈 값은
// 파생 클래스의 멤버로 두고, 태스크 객체 자체는 메인 스레드가 만들고 부순다.
//
// **스레드 계약**: `Run` 은 워커에서 돈다(워커가 없으면 메인). 그 안에서 `SafePtr`·`OwnerPtr`·`Ref<T>`·
// `GameObjectHandle` 을 만들거나 복사하거나 파괴하지 않는다. 값과 raw 포인터만 쓰고, 그 대상의 수명은 등록한
// 쪽이 태스크가 끝날 때까지 보장한다. 풀에 넣는 것 같은 마무리는 `OnFinished`(메인 스레드)가 한다.
namespace JBro
{
    class TaskGroup;
    class TaskManager;

    enum class TaskState : std::uint8_t
    {
        Pending,
        Running,
        Completed,
        Failed,
        Canceled,
    };

    // 하위 작업 하나가 왜 실패했는지다. 예: 어느 파일을 못 읽었는지.
    struct TaskFailure
    {
        UInt32 subTask = 0;
        String reason;
    };

    // 끝난 태스크의 결과다. 상태는 `Completed`·`Failed`·`Canceled` 중 하나다.
    // `failures` 는 태스크가 들고 있는 목록을 가리키므로 태스크가 사는 동안만 유효하다.
    struct TaskResult
    {
        TaskState state = TaskState::Pending;
        UInt32 numSubTasks = 0;
        UInt32 succeededSubTasks = 0;
        UInt32 failedSubTasks = 0;
        ArrayView<const TaskFailure> failures;
    };

    class Task
    {
    public:
        // `numSubTasks` 는 진행률의 분모다. 리소스 40 개를 읽는 태스크는 40 을 넣어 `0/40` 에서 시작한다 -
        // 등록하는 쪽이 개수를 미리 안다. 0 이면 할 일이 없는 태스크이고 `0/0` 으로 끝난다.
        explicit Task(String name, UInt32 numSubTasks = 1);
        virtual ~Task();
        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        const String& GetName() const;
        UInt32 GetNumSubTasks() const;
        // 아래 셋은 워커가 올리는 원자 값이라 메인 스레드가 언제 읽어도 된다(현황표).
        UInt32 GetSucceededSubTasks() const;
        UInt32 GetFailedSubTasks() const;
        TaskState GetState() const;
        Bool IsFinished() const;

        // 취소 표시만 한다. 아직 시작하지 않았으면 `Run` 을 부르지 않고 `Canceled` 로 끝나고, 돌고 있으면
        // 본문이 `IsCancelRequested` 를 보고 멈춰야 한다. 어느 스레드에서 불러도 된다.
        void RequestCancel();
        Bool IsCancelRequested() const;

        // 끝난 뒤(`IsFinished`)에 메인 스레드에서만 읽는다. 끝나기 전에는 실패 목록을 워커가 쓰고 있다.
        TaskResult GetResult() const;

    protected:
        // 워커에서 돈다. 하위 작업 하나를 끝낼 때마다 `SucceedSubTask` 나 `FailSubTask` 를 부른다.
        // 돌아올 때까지 알리지 않은 하위 작업은 성공으로 센다(취소로 멈췄으면 세지 않는다).
        // 던진 예외는 실패 사유가 된다.
        virtual void Run() = 0;
        // 메인 스레드에서 한 번 불린다. 취소되어 `Run` 이 불리지 않았어도 불린다.
        virtual void OnFinished(const TaskResult& result);

        // 알린 수가 `numSubTasks` 를 넘으면 세지 않고 거짓이다. `Run` 안에서만 부른다.
        Bool SucceedSubTask();
        Bool FailSubTask(UInt32 subTask, const char* reason);

    private:
        friend class TaskGroup;
        friend class TaskManager;

        Bool ClaimSubTask();

        String m_name;
        UInt32 m_numSubTasks = 1;
        std::atomic<std::uint32_t> m_succeededSubTasks = 0;
        std::atomic<std::uint32_t> m_failedSubTasks = 0;
        // 알린 수의 합이다. 넘치는 알림을 거르는 데만 쓴다.
        std::atomic<std::uint32_t> m_reportedSubTasks = 0;
        std::atomic<TaskState> m_state = TaskState::Pending;
        std::atomic<bool> m_cancelRequested = false;
        // `Run` 이 도는 동안은 그 워커만, 끝난 뒤에는 메인만 만진다. 끝남은 관리자의 잠금이 넘겨준다.
        Array<TaskFailure> m_failures;
        // 제출 때 메인 스레드가 한 번 적고, 그 뒤로는 읽기만 한다.
        TaskGroup* m_group = nullptr;
        // 순서 묶음에서 이 태스크가 끝나면 큐에 넣을 다음 태스크다.
        Task* m_next = nullptr;
    };
}

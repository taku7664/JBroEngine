#pragma once

#include <JBro/Editor/EditorObjectRegistry.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // 되돌릴 수 있는 편집 하나다(D-71).
    //
    // 기존 엔진의 `IEditorCommand` 를 그대로 따른다 — 이름·실행·되돌리기·다시하기,
    // 그리고 **합치기**. 거기서 얻은 것들을 같이 가져왔고, 다른 점만 아래에 적는다.
    //
    // **`Execute` 가 성공해야 쌓인다.** 실패한 편집이 스택에 남으면 되돌리기가
    // 일어나지 않은 일을 되돌린다.
    class EditorCommand
    {
    public:
        virtual ~EditorCommand() = default;

        // 메뉴와 로그에 보이는 이름이다. 살아 있는 동안 바뀌지 않는다.
        virtual const char* GetName() const = 0;
        virtual Bool Execute() = 0;
        virtual void Undo() = 0;
        virtual void Redo() = 0;

        // **이 커맨드가 다룬 오브젝트**다(D-268). 만든 것·지운 것·옮긴 것·컴포넌트를 붙인 것이다. 없으면 무효 번호다.
        // 가이드가 "그 오브젝트에 그 일이 일어났다" 를 행동마다 따로 묻지 않고 여기서 읽는다. 번호는 되돌리기에도
        // 살아남는다(`EditorObjectRegistry::Rebind`).
        virtual EditorObjectId GetSubject() const
        {
            return InvalidEditorObjectId;
        }

        // **드래그 하나가 되돌리기 하나여야 한다.** 슬라이더를 끄는 동안 프레임마다
        // 커맨드가 생기는데, 그것을 다 쌓으면 되돌리기를 백 번 눌러야 원래대로 온다.
        // 같은 대상에 대한 편집이면 `newer` 의 결과값만 흡수하고 참을 돌려준다 —
        // 그러면 매니저가 `newer` 를 버린다.
        //
        // `newer` 는 **이미 `Execute` 되어 값이 적용된 뒤**에 들어온다.
        // 기본은 합치지 않음이다.
        // 합쳐질 수 있는가. **바꾸지 않고 묻기만 한다.**
        //
        // `TryMerge` 만 있으면 여럿을 묶은 커맨드가 곤란해진다 - 앞의 것 몇 개를
        // 합친 뒤에 하나가 거절하면 이미 절반만 합쳐진 상태이고, 되돌릴 방법이
        // 없다. 먼저 전부 물어보고 나서 전부 합친다.
        virtual Bool CanMerge(const EditorCommand& newer) const
        {
            (void)newer;
            return false;
        }

        virtual Bool TryMerge(const EditorCommand& newer)
        {
            (void)newer;
            return false;
        }
    };

    // 되돌리기 스택이다.
    //
    // **드래그 경계를 스스로 알아낸다.** 기존 엔진에서 가져온 가장 값진 부분이다 —
    // 인스펙터도 기즈모도 "지금부터 한 덩어리" 라고 말해 줄 필요가 없다.
    // `Execute` 는 값이 바뀐 프레임에만 불리므로 "손을 뗀 순간" 을 볼 수 없는데,
    // 마우스 왼쪽 버튼의 누른 시간은 누르는 동안 단조 증가하고 새로 누르면 0 으로
    // 돌아간다. 그 값이 줄었거나 버튼이 안 눌렸으면 새 덩어리로 본다.
    class EditorCommandManager
    {
    public:
        EditorCommandManager() = default;
        ~EditorCommandManager() = default;
        EditorCommandManager(const EditorCommandManager&) = delete;
        EditorCommandManager& operator=(const EditorCommandManager&) = delete;

        // **어느 문서를 고치는 커맨드인가**(D-191, 기존 `documentKey`). 널이면 캔버스다.
        // 에셋 파일을 지우고 이름을 바꾸는 것은 되돌릴 수 있어야 하지만(같은 Ctrl+Z),
        // 그것 때문에 캔버스가 "저장 안 됨" 이 되면 안 된다 - 캔버스는 아무것도 바뀌지 않았다.
        static constexpr const char* AssetDatabase = "__AssetDatabase";

        // 실행하고 쌓는다. 실행이 실패하면 쌓지 않고 거짓을 돌려준다.
        Bool Execute(OwnerPtr<EditorCommand> command, const char* documentKey = nullptr);
        Bool Undo();
        Bool Redo();
        void Clear();

        Bool CanUndo() const;
        Bool CanRedo() const;
        std::size_t GetUndoCount() const;
        std::size_t GetRedoCount() const;

        // **저장했는지는 불리언이 아니라 판번호로 본다.** 고치고 되돌려 원래대로
        // 왔는데도 "저장 안 됨" 으로 남는 것을 막으려고 기존 엔진이 택한 방식이고,
        // 여기서도 같다. 지금은 문서가 캔버스 하나뿐이라 판번호도 하나다 —
        // 기존 엔진은 스프라이트·이펙트 편집기가 따로 있어서 문서마다 두었다.
        // **저장할 것이 있는 문서는 캔버스뿐이다.** 에셋 파일 작업은 실행하는 순간 디스크에
        // 적히므로 "적지 않은 것" 이 남지 않는다 - 그래서 문서마다 판번호를 두지 않고,
        // 캔버스의 것 하나만 센다. 저장할 문서가 둘이 되면 그때 표로 바꾼다.
        void MarkSaved();
        Bool IsDirty() const;
        // **문서를 가리지 않는 판번호**다. 되돌리기·다시하기까지 포함해 무엇이든 움직이면
        // 올라간다 - 에셋 참조를 다시 잇는 자리(`BindCanvasAssets`)가 이것을 본다.
        UInt64 GetRevision() const;

        // ── 실행 기록(D-268) ─────────────────────────────────
        //
        // 최근에 **실행한**(되돌리기·다시하기가 아닌) 커맨드의 이름과 다룬 오브젝트다. 가이드가 단계에 들어선 뒤
        // 무엇이 실행됐는지를 여기서 읽는다 - 편집 메뉴의 삭제처럼 한 손짓이 커맨드를 여럿 실행하면 마지막 하나만
        // 봐서는 앞의 것을 놓친다. 합쳐진 실행(드래그)도 한 번으로 센다.
        struct ExecutedCommand
        {
            // 커맨드의 `GetName()` 이다. 이름은 리터럴이라 커맨드가 버려진 뒤에도 가리킬 수 있다.
            const char* name = nullptr;
            EditorObjectId subject = InvalidEditorObjectId;
        };
        static constexpr UInt64 ExecutedHistory = 32;
        // 지금까지 실행한 수다. 늘기만 한다.
        UInt64 GetExecuteCount() const noexcept { return m_executeCount; }
        // `serial` 번째(1 부터) 실행이다. 오래되어 기록에서 밀려났거나 아직 없으면 거짓이다.
        Bool GetExecuted(UInt64 serial, ExecutedCommand& out) const noexcept;

    private:
        void RecordExecuted(const EditorCommand& command) noexcept;

        // **기존 엔진에는 상한이 없다.** 오래 켜 둔 편집기가 되돌리기 스택만으로
        // 계속 자란다. 넘치면 가장 오래된 것부터 버린다 - 한 시간 전으로 돌아가는
        // 일은 없고, 그 대가로 메모리가 끝없이 늘지 않는다.
        static constexpr std::size_t MaxUndoDepth = 256;

        // 커맨드 하나와 그것이 고친 문서. 되돌리기·다시하기가 그 문서를 다시 움직인다.
        struct Entry
        {
            OwnerPtr<EditorCommand> command;
            // 리터럴이다. 복사하지 않는다. 널이면 캔버스다.
            const char* documentKey = nullptr;
        };

        void Touch(const char* documentKey);
        void PushUndo(Entry entry);
        // 드래그 덩어리가 이어지는 중인지 본다. 매니저 밖에서는 볼 일이 없다.
        Bool ContinuesDrag();

        Array<Entry> m_undo;
        Array<Entry> m_redo;
        Bool m_mergingDrag = false;
        // 직전 실행 시점의 왼쪽 버튼 누른 시간. 새 누름을 알아내는 데 쓴다.
        Float m_lastMouseDownDuration = -1.0f;
        UInt64 m_revision = 0;
        // 캔버스를 고친 마지막 판번호와, 그중 저장된 것.
        UInt64 m_canvasRevision = 0;
        UInt64 m_savedCanvasRevision = 0;
        ExecutedCommand m_executed[ExecutedHistory] = {};
        UInt64 m_executeCount = 0;
    };
}

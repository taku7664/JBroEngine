#pragma once

#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>

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
        virtual bool Execute() = 0;
        virtual void Undo() = 0;
        virtual void Redo() = 0;

        // **드래그 하나가 되돌리기 하나여야 한다.** 슬라이더를 끄는 동안 프레임마다
        // 커맨드가 생기는데, 그것을 다 쌓으면 되돌리기를 백 번 눌러야 원래대로 온다.
        // 같은 대상에 대한 편집이면 `newer` 의 결과값만 흡수하고 참을 돌려준다 —
        // 그러면 매니저가 `newer` 를 버린다.
        //
        // `newer` 는 **이미 `Execute` 되어 값이 적용된 뒤**에 들어온다.
        // 기본은 합치지 않음이다.
        virtual bool TryMerge(const EditorCommand& newer)
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

        // 실행하고 쌓는다. 실행이 실패하면 쌓지 않고 거짓을 돌려준다.
        bool Execute(OwnerPtr<EditorCommand> command);
        bool Undo();
        bool Redo();
        void Clear();

        bool CanUndo() const;
        bool CanRedo() const;
        std::size_t GetUndoCount() const;
        std::size_t GetRedoCount() const;

        // **저장했는지는 불리언이 아니라 판번호로 본다.** 고치고 되돌려 원래대로
        // 왔는데도 "저장 안 됨" 으로 남는 것을 막으려고 기존 엔진이 택한 방식이고,
        // 여기서도 같다. 지금은 문서가 캔버스 하나뿐이라 판번호도 하나다 —
        // 기존 엔진은 스프라이트·이펙트 편집기가 따로 있어서 문서마다 두었다.
        void MarkSaved();
        bool IsDirty() const;
        std::uint64_t GetRevision() const;

    private:
        // **기존 엔진에는 상한이 없다.** 오래 켜 둔 편집기가 되돌리기 스택만으로
        // 계속 자란다. 넘치면 가장 오래된 것부터 버린다 - 한 시간 전으로 돌아가는
        // 일은 없고, 그 대가로 메모리가 끝없이 늘지 않는다.
        static constexpr std::size_t MaxUndoDepth = 256;

        void PushUndo(OwnerPtr<EditorCommand> command);
        // 드래그 덩어리가 이어지는 중인지 본다. 매니저 밖에서는 볼 일이 없다.
        bool ContinuesDrag();

        Array<OwnerPtr<EditorCommand>> m_undo;
        Array<OwnerPtr<EditorCommand>> m_redo;
        bool m_mergingDrag = false;
        // 직전 실행 시점의 왼쪽 버튼 누른 시간. 새 누름을 알아내는 데 쓴다.
        float m_lastMouseDownDuration = -1.0f;
        std::uint64_t m_revision = 0;
        std::uint64_t m_savedRevision = 0;
    };
}

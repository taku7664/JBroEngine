#pragma once

#include <JBro/Editor/EditorGuideFocus.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Delegate.h>

#include <cstdint>

namespace JBro
{
    class EditorApplication;
    class GameObject;

    // 가이드(D-251, `tasks/guide-focus-plan.md` §2.6)다. 단계마다 가이드 포커스로 한 자리를 가리키고,
    // 끝나는 조건이 맞으면 다음 단계로 간다. **가이드는 편집을 대신하지 않는다** - 편집은 사람이 하고,
    // 평소처럼 커맨드를 거친다.

    enum class GuideStepEnd : std::uint8_t
    {
        // 말풍선의 다음 단추를 누른다.
        NextButton,
        // 경로 끝의 대상을 누른다(메뉴 항목·단추).
        TargetActivated,
        // `condition` 이 참이 된다("오브젝트가 골라졌다").
        Condition
    };

    struct GuideStep
    {
        GuideFocusPath path;
        // 경로를 데이터에서 짓는다(선택한 오브젝트의 첫 컴포넌트처럼). 걸려 있으면 `path` 대신 이것을 단계에 들어갈 때
        // 한 번 부른다. 거짓이면 가리킬 것이 없는 것이라 그 단계를 건너뛴다.
        Delegate<bool(EditorApplication&, GuideFocusPath&)> buildPath;
        // 말풍선의 글자. 로컬라이징 키와 키가 없을 때의 영어 원문이다(§11.2).
        const char* titleKey = nullptr;
        const char* titleFallback = nullptr;
        const char* bodyKey = nullptr;
        const char* bodyFallback = nullptr;
        GuideStepEnd end = GuideStepEnd::NextButton;
        Delegate<bool(EditorApplication&)> condition;
        // 글자를 치는 단계인가(값을 입력한다, 목록을 검색한다).
        bool keyboard = false;

        // ── 말풍선의 단추 ─────────────────────────────────────
        //
        // **어느 단추를 둘지는 가이드를 쓴 사람이 단계마다 정한다.** 사용자가 반드시 해 봐야 하는 단계는 건너뛰기를 막고,
        // 되돌아가면 앞뒤가 맞지 않는 단계는 이전을 막는다.
        //
        // 건너뛰기는 가이드를 통째로 끝낸다. **막아도 Esc 는 된다** - 사용자가 빠져나갈 길은 하나 남아 있어야 한다.
        bool canSkip = true;
        // 이전은 앞 단계로 돌아간다(가리킬 것이 없는 단계는 건너 더 앞으로 간다). 편집은 되돌리지 않는다 - 그것은 Ctrl+Z 다.
        bool canGoBack = true;
        // 다음을 둔다. `end` 가 `NextButton` 이면 켜지 않아도 늘 있다 - 없으면 그 단계를 나갈 길이 없다.
        // 조건·대상 누름으로 넘어가는 단계에서 켜면 사람이 그것을 하지 않고도 넘어갈 수 있다.
        bool canGoNext = false;
    };

    struct Guide
    {
        // 저장되지 않는 이름이다. 메뉴와 시험이 이것으로 찾는다.
        const char* id = nullptr;
        const char* titleKey = nullptr;
        const char* titleFallback = nullptr;
        Array<GuideStep> steps;
    };

    // 가이드 한 편을 몬다. `EditorApplication` 이 하나 들고, 막을 그린 뒤 프레임마다 `Update` 를 부른다.
    class EditorGuide
    {
    public:
        EditorGuide() = default;
        EditorGuide(const EditorGuide&) = delete;
        EditorGuide& operator=(const EditorGuide&) = delete;

        // 첫 단계로 들어간다. 단계가 없거나 어느 단계도 가리킬 것이 없으면 거짓이고 켜지 않는다.
        // `guide` 는 끝날 때까지 살아 있어야 한다(내장 가이드는 프로세스와 같이 산다).
        bool Start(const Guide& guide, EditorApplication& editor, EditorGuideFocus& focus);
        // 멈추고 가이드 포커스를 끈다.
        void Stop(EditorGuideFocus& focus);
        bool IsRunning() const noexcept { return m_guide != nullptr; }
        const Guide* GetGuide() const noexcept { return m_guide; }
        std::uint32_t GetStepIndex() const noexcept { return m_step; }
        const GuideStep* GetStep() const noexcept;
        // 마지막 단계를 마쳐 끝났는가. 읽으면 지운다 - 에디터가 "마쳤습니다" 알림을 한 번 띄운다.
        bool ConsumeFinished() noexcept;

        // 지금 단계의 말풍선에 둘 단추다.
        bool ShowsSkip() const noexcept;
        bool ShowsBack() const noexcept;
        // 이전을 두었고 돌아갈 단계가 있다(첫 단계가 아니다).
        bool CanGoBackNow() const noexcept;
        // 다음을 둔다. 단계가 켰거나, 다음 단추로 끝나는 단계거나, **이전으로 돌아온 단계**다 - 돌아온 단계는 조건이 이미 맞아도
        // 저절로 넘어가지 않고(넘어가면 이전이 고장 난 것처럼 보인다) 다음을 기다린다.
        bool ShowsNext() const noexcept;
        // 이전으로 들어온 단계인가.
        bool IsRevisiting() const noexcept { return m_revisiting; }
        // **마지막 단계를 해냈고 확인을 기다린다.** 조건이나 대상 누름으로 끝나는 마지막 단계는 해내자마자 닫지 않는다 -
        // 사람이 결과를 보고 확인을 눌러야 끝난다. 이때 건너뛰기는 없고(건너뛸 것이 없다) 이전은 단계가 정한 대로다.
        bool IsConfirming() const noexcept { return m_confirming; }

        // 한 프레임을 나아간다. 가이드 포커스가 꺼졌으면(Esc) 멈춘다. 경로가 끊긴 단계는 로그를 남기고 건너뛴다.
        void Update(EditorApplication& editor, EditorGuideFocus& focus, GuideFocusAction action);

    private:
        // `from` 부터 가리킬 것이 있는 첫 단계로 들어간다. 없으면 끝난 것이다.
        bool EnterStep(std::uint32_t from, EditorApplication& editor, EditorGuideFocus& focus);
        // 단계 하나에 들어간다. 가리킬 것이 없으면 거짓이고 아무것도 바꾸지 않는다.
        bool TryEnter(std::uint32_t index, EditorApplication& editor, EditorGuideFocus& focus);

        const Guide* m_guide = nullptr;
        std::uint32_t m_step = 0;
        bool m_finished = false;
        bool m_revisiting = false;
        bool m_confirming = false;
    };

    // 에디터에 들어 있는 가이드다.
    namespace EditorGuides
    {
        std::uint32_t GetBuiltinCount();
        const Guide& GetBuiltin(std::uint32_t index);
        // 없으면 nullptr 이다.
        const Guide* FindBuiltin(const char* id);

        // 계층에서 오브젝트까지 가는 경로(레이어 창 → 레이어 줄 → 조상 줄들 → 그 줄)를 `path` 뒤에 붙인다.
        // 조상이 경로 용량을 넘으면 거짓이다.
        bool AppendObjectPath(EditorApplication& editor, GameObject& object, GuideFocusPath& path);
    }
}

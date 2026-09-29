#pragma once

#include <JBro/Editor/EditorGuideFocus.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Delegate.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/String.h>

#include <cstdint>

namespace JBro
{
    class EditorApplication;
    class GameObject;
    class YamlWriter;

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

    // 단계에 들어설 때 적어 두는 값이다. 조건은 **지금 상태가 아니라 들어설 때와 달라졌는가**를 묻는다 - "선택한 오브젝트가 있다"
    // 로 물으면 이미 골라 둔 사람에게는 그 단계가 보이지도 않고 지나가고, "컴포넌트가 둘 이상" 으로 물으면 이미 둘인 오브젝트에서
    // 아무것도 하지 않아도 끝난다. 값의 뜻은 그 단계의 `onEnter` 와 `condition` 이 함께 정한다.
    struct GuideStepMemo
    {
        std::uint64_t values[4] = {};
    };

    // 가이드에 나오는 글자 하나다. **키 · 원문 · 원문의 로케일**을 함께 든다(D-267). 가이드는 코드가 아니라 데이터로도
    // 오므로(에이전트가 그 자리에서 짓는다) 키만으로는 무엇을 보일지 모른다 - 표에 키가 없으면 원문을 보인다.
    // 고르는 차례는 `Loc::TextFor` 가 정한다.
    struct GuideText
    {
        const char* key = nullptr;
        const char* string = nullptr;
        // 원문의 로케일(`ko-KR`). 비우면 영어 원문으로 본다(`en-US`).
        const char* locale = nullptr;
    };

    struct GuideStep
    {
        GuideFocusPath path;
        // 경로를 데이터에서 짓는다(선택한 오브젝트의 첫 컴포넌트처럼). 걸려 있으면 `path` 대신 이것을 단계에 들어갈 때
        // 한 번 부른다. 거짓이면 가리킬 것이 없는 것이라 그 단계를 건너뛴다.
        Delegate<bool(EditorApplication&, GuideFocusPath&)> buildPath;
        // **경로가 끊기면 다른 길을 짓는다**(D-267). 같은 일에 들어가는 길이 여럿이면(계층 줄의 우클릭 · 캔버스 뷰의 우클릭 ·
        // 편집 메뉴) 하나가 막혀도 다른 길로 데려간다. 참이면 그 길로 다시 가고, 거짓이면 길이 더 없는 것이다 -
        // 그때는 `retreatOnBreak` 와 건너뛰기가 전처럼 이어받는다.
        Delegate<bool(EditorApplication&, GuideFocusPath&)> nextRoute;
        // 말풍선의 글자다.
        GuideText title;
        GuideText body;
        GuideStepEnd end = GuideStepEnd::NextButton;
        // 단계에 들어설 때 한 번 부른다(이전으로 돌아와 다시 들어설 때도). 비었으면 적어 둔 값은 전부 0 이다.
        Delegate<void(EditorApplication&, GuideStepMemo&)> onEnter;
        // 적어 둔 값을 고칠 수 있다 - 단계 안에서 기준이 바뀌면(다른 오브젝트를 골랐다) 그 자리에서 다시 잡는다.
        Delegate<bool(EditorApplication&, GuideStepMemo&)> condition;
        // **다음을 지금 누를 수 없는 까닭**이다(이미 번역된 글자). 비었거나 nullptr 을 돌려주면 누를 수 있다. 까닭이 있으면
        // 다음은 회색이고 그 까닭을 띄운다(§11.1) - 오브젝트를 고르지 않았는데 값 바꾸기로 넘어가면 가리킬 것이 없다.
        // 해낸 뒤의 확인에는 걸지 않는다.
        Delegate<const char*(EditorApplication&)> nextBlockedReason;
        // **이 단계가 끊기면 돌아갈 단계**다. 음수면 앞으로 건너뛴다. 값 바꾸기 도중에 선택을 비우거나 오브젝트를 지우면 뒤의
        // 단계도 모두 가리킬 것이 없다 - 건너뛰면 가이드가 말없이 끝나므로, 그것을 다시 마련하는 단계로 돌아간다.
        std::int32_t retreatOnBreak = -1;
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
        GuideText title;
        Array<GuideStep> steps;
    };

    class GuideStepBinding;

    // **글자로 적힌 가이드를 읽은 것**이다(D-267, `tasks/guide-focus-plan.md` §2.8). 가이드에 적힌 것은 "무엇을 하게 할지"
    // (`Do: object.delete`)이고, 그 일이 어느 패널의 어느 줄을 거쳐 가는지는 에디터의 행동 표가 안다 - 적는 쪽(에이전트)은
    // 화면의 모양을 몰라도 된다. 글자와 단계의 판단을 모두 들고 있으므로 가이드가 끝날 때까지 살아 있어야 한다.
    class LoadedGuide
    {
    public:
        LoadedGuide();
        ~LoadedGuide();
        LoadedGuide(const LoadedGuide&) = delete;
        LoadedGuide& operator=(const LoadedGuide&) = delete;

        const Guide& Get() const noexcept { return m_guide; }

    private:
        friend struct GuideLoader;

        Guide m_guide;
        String m_id;
        String m_title[3];
        Array<OwnerPtr<GuideStepBinding>> m_bindings;
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
        // 다음을 지금 누를 수 없는 까닭이다. 누를 수 있으면 nullptr.
        const char* WhyNextBlocked(EditorApplication& editor) const;
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
        GuideStepMemo m_memo;
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

        // **글자로 적힌 가이드를 읽는다**(D-267). 형식은 `tasks/guide-focus-plan.md` §2.8.1 다. 모르는 행동·길·키를 만나면
        // 추측하지 않고 줄 번호와 함께 `error` 에 적고 거짓이다(`out` 은 손대지 않는다).
        bool Parse(const char* text, std::size_t length, OwnerPtr<LoadedGuide>& out, String& error);

        // **가이드를 적는 쪽이 쓸 수 있는 행동 목록**이다(D-267). 행동 이름 · 받는 인자 · 들어가는 길 · 끝나는 방식을 적는다.
        // 에디터 안의 에이전트는 이것만 보고 가이드를 짓는다 - 패널의 모양이 바뀌어도 이 목록의 이름은 그대로다.
        void WriteCatalog(YamlWriter& writer);
    }
}

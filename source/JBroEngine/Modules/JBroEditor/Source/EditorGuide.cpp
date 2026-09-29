#include <JBro/Editor/EditorGuide.h>

#include <JBro/Canvas/Layer.h>
#include <JBro/Core/Log.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Runtime/GameObject.h>

#include <cstring>

namespace JBro
{
    namespace
    {
        // **내장 가이드도 글자로 적는다**(D-267). 에이전트가 짓는 가이드와 같은 형식·같은 행동 표를 거친다 - 길이 둘이면
        // 한쪽만 고쳐지고, 형식이 실제로 쓸 만한지는 쓰는 곳이 있어야 드러난다. 원문은 영어이고 번역은 로케일 표에 있다.
        constexpr const char* AddComponentGuide = R"(Id: guide.add_component
Title:
  Key: guide.add_component_title
  String: Adding a Component
  Loc: en-US
Steps:
  - Id: pick
    Do: object.select
    Title:
      Key: guide.add_component_select_title
      String: Pick an Object
      Loc: en-US
    Body:
      Key: guide.add_component_select_body
      String: "Pick an object in the Layers window. If you already picked one, press Next. If there is none, right-click an empty spot and add one - it is picked for you."
      Loc: en-US
  - Do: field.edit
    RetreatTo: pick
    Title:
      Key: guide.add_component_field_title
      String: Change a Value
      Loc: en-US
    Body:
      Key: guide.add_component_field_body
      String: "Drag or type in this field to change the value. Press Next when you are done."
      Loc: en-US
  - Do: component.add
    RetreatTo: pick
    Title:
      Key: guide.add_component_add_title
      String: Add a Component
      Loc: en-US
    Body:
      Key: guide.add_component_add_body
      String: "Open this list and pick a component to attach."
      Loc: en-US
)";

        constexpr const char* BuildGameGuide = R"(Id: guide.build_game
Title:
  Key: guide.build_game_title
  String: Building the Game
  Loc: en-US
Steps:
  - Do: game.build
    Title:
      Key: guide.build_game_step_title
      String: Build Game
      Loc: en-US
    Body:
      Key: guide.build_game_step_body
      String: "Open the File menu and choose Build Game to pack the project into a game you can run."
      Loc: en-US
)";

        Array<OwnerPtr<LoadedGuide>> BuildBuiltins()
        {
            Array<OwnerPtr<LoadedGuide>> guides;
            for (const char* text : { AddComponentGuide, BuildGameGuide })
            {
                OwnerPtr<LoadedGuide> guide;
                String error;
                if (false == EditorGuides::Parse(text, std::strlen(text), guide, error))
                {
                    // 내장 가이드가 읽히지 않는 것은 에디터의 고장이다. 시험이 잡는다(`EditorGuideTests`).
                    Log::Write(LogLevel::Error, "editor", "built-in guide does not parse: %s", error.c_str());
                    continue;
                }
                guides.Add(std::move(guide));
            }
            return guides;
        }

        Array<OwnerPtr<LoadedGuide>>& Builtins()
        {
            static Array<OwnerPtr<LoadedGuide>> guides = BuildBuiltins();
            return guides;
        }
    }

    const GuideStep* EditorGuide::GetStep() const noexcept
    {
        if (m_guide == nullptr || m_step >= m_guide->steps.Size())
        {
            return nullptr;
        }
        return &m_guide->steps[m_step];
    }

    bool EditorGuide::ConsumeFinished() noexcept
    {
        const bool finished = m_finished;
        m_finished = false;
        return finished;
    }

    bool EditorGuide::Start(const Guide& guide, EditorApplication& editor, EditorGuideFocus& focus)
    {
        m_guide = &guide;
        m_finished = false;
        m_revisiting = false;
        m_confirming = false;
        m_results.Clear();
        for (std::uint32_t index = 0; index < guide.steps.Size(); ++index)
        {
            m_results.Add(0);
        }
        if (false == EnterStep(0, editor, focus))
        {
            m_guide = nullptr;
            focus.End();
            return false;
        }
        return true;
    }

    void EditorGuide::Stop(EditorGuideFocus& focus)
    {
        m_guide = nullptr;
        m_step = 0;
        focus.End();
    }

    bool EditorGuide::TryEnter(std::uint32_t index, EditorApplication& editor, EditorGuideFocus& focus)
    {
        const GuideStep& step = m_guide->steps[index];
        GuideFocusPath path = step.path;
        if (step.buildPath.IsBound())
        {
            path = {};
            if (false == step.buildPath.Invoke(editor, path))
            {
                Log::Write(LogLevel::Warning, "editor", "guide %s: step %u has nothing to point at; skipped",
                    m_guide->id, index + 1);
                return false;
            }
        }
        if (false == focus.Begin(path))
        {
            return false;
        }
        focus.SetKeyboardAllowed(step.keyboard);
        m_step = index;
        m_memo = {};
        if (step.onEnter.IsBound())
        {
            step.onEnter.Invoke(editor, m_memo);
        }
        return true;
    }

    bool EditorGuide::EnterStep(std::uint32_t from, EditorApplication& editor, EditorGuideFocus& focus)
    {
        for (std::uint32_t index = from; index < m_guide->steps.Size(); ++index)
        {
            if (TryEnter(index, editor, focus))
            {
                m_revisiting = false;
                m_confirming = false;
                return true;
            }
            // 받을 결과가 비었다(남긴 단계를 건너뛰었거나 그 오브젝트가 사라졌다). 남긴 단계로 돌아가 다시 하게 한다.
            const std::int32_t retreat = m_guide->steps[index].retreatOnMissing;
            if (retreat >= 0 && static_cast<std::uint32_t>(retreat) < index
                && TryEnter(static_cast<std::uint32_t>(retreat), editor, focus))
            {
                Log::Write(LogLevel::Info, "editor", "guide %s: step %u has nothing to point at; back to step %u",
                    m_guide->id, index + 1, static_cast<std::uint32_t>(retreat) + 1);
                m_revisiting = false;
                m_confirming = false;
                return true;
            }
        }
        return false;
    }

    std::uint64_t EditorGuide::GetResult(std::uint32_t step) const noexcept
    {
        return step < m_results.Size() ? m_results[step] : 0;
    }

    bool EditorGuide::ShowsSkip() const noexcept
    {
        const GuideStep* step = GetStep();
        return step != nullptr && step->canSkip && false == m_confirming;
    }

    bool EditorGuide::ShowsBack() const noexcept
    {
        const GuideStep* step = GetStep();
        return step != nullptr && step->canGoBack;
    }

    bool EditorGuide::CanGoBackNow() const noexcept
    {
        return ShowsBack() && m_step > 0;
    }

    const char* EditorGuide::WhyNextBlocked(EditorApplication& editor) const
    {
        const GuideStep* step = GetStep();
        if (step == nullptr || m_confirming || false == step->nextBlockedReason.IsBound())
        {
            return nullptr;
        }
        const char* reason = step->nextBlockedReason.Invoke(editor);
        return reason != nullptr && reason[0] != '\0' ? reason : nullptr;
    }

    bool EditorGuide::ShowsNext() const noexcept
    {
        const GuideStep* step = GetStep();
        return step != nullptr && (step->canGoNext || step->end == GuideStepEnd::NextButton || m_revisiting || m_confirming);
    }

    void EditorGuide::Update(EditorApplication& editor, EditorGuideFocus& focus, GuideFocusAction action)
    {
        if (m_guide == nullptr)
        {
            return;
        }
        if (false == focus.IsActive())
        {
            // Esc 로 막이 걷혔다. 가이드도 거기서 멈춘다.
            m_guide = nullptr;
            m_step = 0;
            return;
        }
        // **단추는 둔 것만 듣는다.** 말풍선에 없는 단추의 손짓이 오면(부르는 쪽이 잘못 넘겼다) 무시한다.
        if (action == GuideFocusAction::Skip && ShowsSkip())
        {
            Stop(focus);
            return;
        }
        if (action == GuideFocusAction::Back && CanGoBackNow())
        {
            // 가리킬 것이 있는 가장 가까운 앞 단계로 간다. 하나도 없으면 제자리다.
            for (std::uint32_t index = m_step; index > 0; --index)
            {
                if (TryEnter(index - 1, editor, focus))
                {
                    m_revisiting = true;
                    m_confirming = false;
                    return;
                }
            }
            return;
        }
        const GuideStep& step = m_guide->steps[m_step];
        const bool nextPressed = action == GuideFocusAction::Next && ShowsNext() && WhyNextBlocked(editor) == nullptr;
        if (m_confirming)
        {
            // 해낸 뒤다. 확인만 기다린다 - 끊김도 조건도 더 보지 않는다(결과를 보고 있는 사람의 화면이 넘어가면 안 된다).
            if (nextPressed)
            {
                m_finished = true;
                Stop(focus);
                return;
            }
            // **해낸 일이 대상을 없앴다**(지운 오브젝트의 삭제 항목, D-267). 사라진 자리에 빈 테두리를 남기지 않고 경로의 첫 칸(그 일을
            // 한 패널)으로 물러난다. 기다렸다가 끊긴 것으로 치면 다 한 일에 경고가 남는다.
            const GuideFocusPath& path = focus.GetPath();
            if (path.count > 1 && focus.GetUnseenSeconds() > 0.0f)
            {
                GuideFocusPath rest;
                rest.Push(path.targets[0], path.open[0]);
                focus.Begin(rest);
            }
            return;
        }
        bool done = nextPressed;
        // 돌아온 단계는 다음을 기다린다. 조건이 이미 맞아 저절로 넘어가면 이전을 눌러도 제자리로 튕겨 온다.
        if (false == m_revisiting)
        {
            switch (step.end)
            {
            case GuideStepEnd::NextButton:
                break;
            case GuideStepEnd::TargetActivated:
                done = done || focus.ConsumeActivated();
                break;
            case GuideStepEnd::Condition:
                done = done || (step.condition.IsBound() && step.condition.Invoke(editor, m_memo));
                break;
            }
        }
        const bool broken = focus.IsBroken();
        if (broken && false == done && step.nextRoute.IsBound())
        {
            // 같은 일에 드는 다른 길이 있으면 그리로 간다(계층 줄이 검색에 가려졌으면 캔버스 뷰로).
            GuideFocusPath path;
            if (step.nextRoute.Invoke(editor, path) && focus.Begin(path))
            {
                focus.SetKeyboardAllowed(step.keyboard);
                return;
            }
        }
        if (broken && step.retreatOnBreak >= 0 && static_cast<std::uint32_t>(step.retreatOnBreak) < m_step)
        {
            // 가리킬 것을 다시 마련하는 단계로 돌아간다. 돌아온 것이 아니라 새로 들어선 것이다 - 조건으로 넘어가야 한다.
            Log::Write(LogLevel::Info, "editor", "guide %s: step %u lost its target; back to step %u",
                m_guide->id, m_step + 1, static_cast<std::uint32_t>(step.retreatOnBreak) + 1);
            if (TryEnter(static_cast<std::uint32_t>(step.retreatOnBreak), editor, focus))
            {
                m_revisiting = false;
                m_confirming = false;
                return;
            }
        }
        if (broken)
        {
            // 가리킬 것이 사라졌다(오브젝트를 지웠다). 멈춰 있으면 막만 남으니 다음으로 간다. 로그는 가이드 포커스가 남겼다.
            done = true;
        }
        if (false == done)
        {
            return;
        }
        // 해낸 단계는 남길 것을 적는다. 끊겨서 넘어가는 단계는 남기지 않는다 - 뒤 단계가 그것을 받으면 되돌아온다.
        if (m_step < m_results.Size())
        {
            m_results[m_step] = false == broken && step.result.IsBound() ? step.result.Invoke(editor, m_memo) : 0;
        }
        const bool last = m_step + 1 >= m_guide->steps.Size();
        if (last && false == nextPressed && false == broken)
        {
            // 마지막 일을 해냈다. 곧바로 닫지 않고 확인을 기다린다.
            m_confirming = true;
            return;
        }
        if (false == EnterStep(m_step + 1, editor, focus))
        {
            // 끊겨서 끝난 것은 마친 것이 아니다 - "마쳤습니다" 를 띄우면 사용자는 마지막 단계를 한 줄 안다.
            m_finished = false == broken;
            Stop(focus);
        }
    }

    namespace EditorGuides
    {
        std::uint32_t GetBuiltinCount()
        {
            return static_cast<std::uint32_t>(Builtins().Size());
        }

        const Guide& GetBuiltin(std::uint32_t index)
        {
            return Builtins()[index]->Get();
        }

        const Guide* FindBuiltin(const char* id)
        {
            if (id == nullptr)
            {
                return nullptr;
            }
            for (const OwnerPtr<LoadedGuide>& guide : Builtins())
            {
                if (std::strcmp(guide->Get().id, id) == 0)
                {
                    return &guide->Get();
                }
            }
            return nullptr;
        }

        bool AppendObjectPath(EditorApplication& editor, GameObject& object, GuideFocusPath& path)
        {
            const Layer* layer = object.GetLayer();
            if (layer == nullptr)
            {
                return false;
            }
            // 조상을 뿌리부터 적어야 한다. 부모 사슬은 아래에서 위로 가므로 먼저 모아 뒤집는다.
            GameObject* chain[GuideFocusPath::Capacity];
            std::uint32_t depth = 0;
            for (GameObject* walk = &object; walk != nullptr; walk = walk->GetParent())
            {
                if (depth >= GuideFocusPath::Capacity)
                {
                    return false;
                }
                chain[depth] = walk;
                ++depth;
            }
            if (false == path.Push(GuideFocusTargets::Panel("Hierarchy"))
                || false == path.Push(GuideFocusTargets::HierarchyLayer(layer->GetId())))
            {
                return false;
            }
            for (std::uint32_t index = depth; index > 0; --index)
            {
                if (false == path.Push(GuideFocusTargets::HierarchyObject(editor.GetObjectIds().Track(chain[index - 1]))))
                {
                    return false;
                }
            }
            return true;
        }
    }
}

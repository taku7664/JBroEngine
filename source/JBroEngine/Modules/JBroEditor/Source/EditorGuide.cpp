#include <JBro/Editor/EditorGuide.h>

#include <JBro/Canvas/Layer.h>
#include <JBro/Core/Log.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/GameObject.h>

#include <cstring>

namespace JBro
{
    namespace
    {
        // ── 조건과 경로 ───────────────────────────────────────────

        // 선택한 오브젝트의 에디터 번호다. 없으면 0 이다.
        std::uint64_t SelectedObjectId(EditorApplication& editor)
        {
            GameObject* object = editor.GetSelectedObject();
            return object != nullptr ? editor.GetObjectIds().Track(object) : 0;
        }

        // [0] 들어설 때 선택되어 있던 오브젝트.
        void RememberSelection(EditorApplication& editor, GuideStepMemo& memo)
        {
            memo.values[0] = SelectedObjectId(editor);
        }

        // 들어설 때와 다른 오브젝트가 선택됐다. 추가한 오브젝트는 곧 선택되므로 추가해도 넘어간다.
        bool SelectionChanged(EditorApplication& editor, GuideStepMemo& memo)
        {
            const std::uint64_t now = SelectedObjectId(editor);
            return now != 0 && now != memo.values[0];
        }

        // [0] 들어설 때 선택되어 있던 오브젝트, [1] 그 오브젝트의 컴포넌트 수.
        void RememberComponentCount(EditorApplication& editor, GuideStepMemo& memo)
        {
            const GameObject* object = editor.GetSelectedObject();
            memo.values[0] = SelectedObjectId(editor);
            memo.values[1] = object != nullptr ? object->GetComponents().Size() : 0;
        }

        // 고른 오브젝트의 컴포넌트가 늘었다. **다른 오브젝트로 옮겨 고르면 그 오브젝트로 기준을 다시 잡는다** - 옮긴 것만으로는
        // 붙인 것이 아니지만, 옮긴 뒤에 그 오브젝트에 붙인 것은 붙인 것이다.
        bool ComponentAdded(EditorApplication& editor, GuideStepMemo& memo)
        {
            const GameObject* object = editor.GetSelectedObject();
            if (object == nullptr)
            {
                return false;
            }
            const std::uint64_t now = SelectedObjectId(editor);
            if (now != memo.values[0])
            {
                memo.values[0] = now;
                memo.values[1] = object->GetComponents().Size();
                return false;
            }
            return object->GetComponents().Size() > memo.values[1];
        }

        // 오브젝트를 고르지 않았으면 다음으로 가지 못한다.
        const char* NeedSelectedObject(EditorApplication& editor)
        {
            if (editor.GetSelectedObject() != nullptr)
            {
                return nullptr;
            }
            return Loc::TextOr(LocKeys::GuideNeedSelectedObject, "pick an object first");
        }

        // 선택한 오브젝트의 첫 컴포넌트와 그 맨 위 필드다. 2D 면 `Transform2D.position`, 3D 면 `Transform3D` 의 것이다 -
        // 타입을 이름으로 고정하지 않으므로 두 프레임워크에서 같은 가이드가 돈다.
        bool FirstFieldPath(EditorApplication& editor, GuideFocusPath& path)
        {
            const GameObject* object = editor.GetSelectedObject();
            if (object == nullptr || object->GetComponents().Size() == 0)
            {
                return false;
            }
            const ComponentTypeId typeId = object->GetComponents()[0].typeId;
            const PropertyTable* table = PropertyRegistry::Lookup(typeId);
            if (table == nullptr)
            {
                return false;
            }
            for (std::uint32_t index = 0; index < table->count; ++index)
            {
                const PropertyInfo& property = table->properties[index];
                // 인스펙터가 그리지 않는 필드는 가리킬 수 없다(`DrawFieldsInto` 와 같은 거르기).
                if (property.type == nullptr || property.Address == nullptr)
                {
                    continue;
                }
                return path.Push(GuideFocusTargets::Panel("Inspector"))
                    && path.Push(GuideFocusTargets::InspectorComponent(typeId))
                    && path.Push(GuideFocusTargets::InspectorField(typeId, property.name));
            }
            return false;
        }

        GuideStep MakeStep(const char* titleKey, const char* titleFallback, const char* bodyKey, const char* bodyFallback)
        {
            GuideStep step;
            step.titleKey = titleKey;
            step.titleFallback = titleFallback;
            step.bodyKey = bodyKey;
            step.bodyFallback = bodyFallback;
            return step;
        }

        Array<Guide> BuildBuiltins()
        {
            Array<Guide> guides;

            // ── 컴포넌트 추가하기 ─────────────────────────────────
            {
                Guide guide;
                guide.id = "guide.add_component";
                guide.titleKey = LocKeys::GuideAddComponentTitle;
                guide.titleFallback = "Adding a Component";

                GuideStep select = MakeStep(LocKeys::GuideAddComponentSelectTitle, "Pick an Object",
                    LocKeys::GuideAddComponentSelectBody,
                    "Pick an object in the Layers window. If you already picked one, press Next. If there is none, right-click an empty spot and add one - it is picked for you.");
                select.path.Push(GuideFocusTargets::Panel("Hierarchy"));
                select.end = GuideStepEnd::Condition;
                select.onEnter = Delegate<void(EditorApplication&, GuideStepMemo&)>::Bind<&RememberSelection>();
                select.condition = Delegate<bool(EditorApplication&, GuideStepMemo&)>::Bind<&SelectionChanged>();
                // 이미 골라 둔 사람은 다시 고를 필요 없이 다음을 누른다. 고른 것이 없으면 다음은 회색이다.
                select.canGoNext = true;
                select.nextBlockedReason = Delegate<const char*(EditorApplication&)>::Bind<&NeedSelectedObject>();
                guide.steps.Add(std::move(select));

                GuideStep field = MakeStep(LocKeys::GuideAddComponentFieldTitle, "Change a Value",
                    LocKeys::GuideAddComponentFieldBody,
                    "Drag or type in this field to change the value. Press Next when you are done.");
                field.buildPath = Delegate<bool(EditorApplication&, GuideFocusPath&)>::Bind<&FirstFieldPath>();
                field.end = GuideStepEnd::NextButton;
                field.keyboard = true;
                field.nextBlockedReason = Delegate<const char*(EditorApplication&)>::Bind<&NeedSelectedObject>();
                // 도중에 선택을 비우거나 오브젝트를 지우면 필드가 사라진다. 고르는 단계로 돌아간다.
                field.retreatOnBreak = 0;
                guide.steps.Add(std::move(field));

                GuideStep add = MakeStep(LocKeys::GuideAddComponentAddTitle, "Add a Component",
                    LocKeys::GuideAddComponentAddBody, "Open this list and pick a component to attach.");
                add.path.Push(GuideFocusTargets::Panel("Inspector"));
                add.path.Push(GuideFocusTargets::InspectorAddComponent(), GuideFocusOpen::User);
                add.end = GuideStepEnd::Condition;
                add.onEnter = Delegate<void(EditorApplication&, GuideStepMemo&)>::Bind<&RememberComponentCount>();
                add.condition = Delegate<bool(EditorApplication&, GuideStepMemo&)>::Bind<&ComponentAdded>();
                add.retreatOnBreak = 0;
                // 목록 위의 검색 칸에 칠 수 있어야 한다.
                add.keyboard = true;
                guide.steps.Add(std::move(add));

                guides.Add(std::move(guide));
            }

            // ── 게임 빌드하기 ─────────────────────────────────────
            {
                Guide guide;
                guide.id = "guide.build_game";
                guide.titleKey = LocKeys::GuideBuildGameTitle;
                guide.titleFallback = "Building the Game";

                GuideStep build = MakeStep(LocKeys::GuideBuildGameStepTitle, "Build Game",
                    LocKeys::GuideBuildGameStepBody,
                    "Open the File menu and choose Build Game to pack the project into a game you can run.");
                // 메뉴는 사용자가 연다. 열리면 구멍이 그 안의 항목으로 옮겨 간다.
                build.path.Push(GuideFocusTargets::Menu("menu.file"), GuideFocusOpen::User);
                build.path.Push(GuideFocusTargets::Menu("menu.build_game"));
                build.end = GuideStepEnd::TargetActivated;
                guide.steps.Add(std::move(build));

                guides.Add(std::move(guide));
            }
            return guides;
        }

        Array<Guide>& Builtins()
        {
            static Array<Guide> guides = BuildBuiltins();
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
        }
        return false;
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
            return Builtins()[index];
        }

        const Guide* FindBuiltin(const char* id)
        {
            if (id == nullptr)
            {
                return nullptr;
            }
            for (const Guide& guide : Builtins())
            {
                if (std::strcmp(guide.id, id) == 0)
                {
                    return &guide;
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

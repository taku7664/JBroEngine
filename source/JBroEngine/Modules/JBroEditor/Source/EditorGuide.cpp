#include <JBro/Editor/EditorGuide.h>

#include <JBro/Canvas/Layer.h>
#include <JBro/Core/Log.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/GameObject.h>

#include <cstring>

namespace JBro
{
    namespace
    {
        // ── 조건과 경로 ───────────────────────────────────────────

        bool HasSelectedObject(EditorApplication& editor)
        {
            return editor.GetSelectedObject() != nullptr;
        }

        bool SelectedHasSecondComponent(EditorApplication& editor)
        {
            const GameObject* object = editor.GetSelectedObject();
            return object != nullptr && object->GetComponents().Size() >= 2;
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
                    "Pick an object in the Layers window. If there is none, right-click an empty spot to add one.");
                select.path.Push(GuideFocusTargets::Panel("Hierarchy"));
                select.end = GuideStepEnd::Condition;
                select.condition = Delegate<bool(EditorApplication&)>::Bind<&HasSelectedObject>();
                guide.steps.Add(std::move(select));

                GuideStep field = MakeStep(LocKeys::GuideAddComponentFieldTitle, "Change a Value",
                    LocKeys::GuideAddComponentFieldBody,
                    "Drag or type in this field to change the value. Press Next when you are done.");
                field.buildPath = Delegate<bool(EditorApplication&, GuideFocusPath&)>::Bind<&FirstFieldPath>();
                field.end = GuideStepEnd::NextButton;
                field.keyboard = true;
                guide.steps.Add(std::move(field));

                GuideStep add = MakeStep(LocKeys::GuideAddComponentAddTitle, "Add a Component",
                    LocKeys::GuideAddComponentAddBody, "Open this list and pick a component to attach.");
                add.path.Push(GuideFocusTargets::Panel("Inspector"));
                add.path.Push(GuideFocusTargets::InspectorAddComponent(), GuideFocusOpen::User);
                add.end = GuideStepEnd::Condition;
                add.condition = Delegate<bool(EditorApplication&)>::Bind<&SelectedHasSecondComponent>();
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

    bool EditorGuide::EnterStep(std::uint32_t from, EditorApplication& editor, EditorGuideFocus& focus)
    {
        for (std::uint32_t index = from; index < m_guide->steps.Size(); ++index)
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
                    continue;
                }
            }
            if (focus.Begin(path))
            {
                focus.SetKeyboardAllowed(step.keyboard);
                m_step = index;
                return true;
            }
        }
        return false;
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
        if (action == GuideFocusAction::Skip)
        {
            Stop(focus);
            return;
        }
        const GuideStep& step = m_guide->steps[m_step];
        bool done = false;
        switch (step.end)
        {
        case GuideStepEnd::NextButton:
            done = action == GuideFocusAction::Next;
            break;
        case GuideStepEnd::TargetActivated:
            done = focus.ConsumeActivated();
            break;
        case GuideStepEnd::Condition:
            done = step.condition.IsBound() && step.condition.Invoke(editor);
            break;
        }
        const bool broken = focus.IsBroken();
        if (broken)
        {
            // 가리킬 것이 사라졌다(오브젝트를 지웠다). 멈춰 있으면 막만 남으니 다음으로 간다. 로그는 가이드 포커스가 남겼다.
            done = true;
        }
        if (false == done)
        {
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

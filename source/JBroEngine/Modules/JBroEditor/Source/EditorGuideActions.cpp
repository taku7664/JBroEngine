#include <JBro/Editor/EditorGuide.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Core/Log.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Core/Yaml.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/GameObject.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace JBro
{
    // 글자로 적힌 가이드(D-267, `tasks/guide-focus-plan.md` §2.8)다.
    //
    // 가이드에 적힌 것은 **무엇을 하게 할지**다(`Do: object.delete`). 그 일이 어느 패널의 어느 줄을 거쳐 가는지는 여기 있는
    // 행동 표가 안다 - 오브젝트 삭제는 계층 줄의 우클릭 메뉴로도, 캔버스 뷰의 우클릭 메뉴로도, 편집 메뉴로도 간다.
    // 적는 쪽(에디터 안의 에이전트)은 화면의 모양을 몰라도 되고, 패널이 바뀌면 이 표만 고친다.

    namespace
    {
        // ── 길 ────────────────────────────────────────────────────────

        // 행동에 들어가는 길이다. 적힌 이름은 `RouteName` 에 있다.
        enum class GuideRoute : std::uint8_t
        {
            Hierarchy,
            CanvasView,
            EditMenu,
            Inspector,
            MainMenu
        };

        const char* RouteName(GuideRoute route)
        {
            switch (route)
            {
            case GuideRoute::Hierarchy:
                return "hierarchy";
            case GuideRoute::CanvasView:
                return "canvas_view";
            case GuideRoute::EditMenu:
                return "edit_menu";
            case GuideRoute::Inspector:
                return "inspector";
            case GuideRoute::MainMenu:
                return "main_menu";
            }
            return "";
        }

        // 행동이 받는 인자다.
        enum class GuideParam : std::uint8_t
        {
            None = 0,
            // 오브젝트 하나. `InstanceId` 이거나 `Selection`(단계에 들어설 때 선택한 것)이다.
            Object = 1 << 0,
            // 컴포넌트 타입 이름(`Transform2D`).
            Component = 1 << 1,
            // 필드 이름(`position`).
            Field = 1 << 2
        };

        constexpr bool HasParam(std::uint8_t params, GuideParam param)
        {
            return (params & static_cast<std::uint8_t>(param)) != 0;
        }

        constexpr std::uint8_t Params(GuideParam a, GuideParam b = GuideParam::None)
        {
            return static_cast<std::uint8_t>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
        }

        struct GuideActionInfo;
    }

    // 글자로 적힌 단계 하나의 판단이다. 가이드의 `GuideStep` 은 이것에 걸린 델리게이트를 부른다 - 단계마다 인자(어느 오브젝트,
    // 어느 컴포넌트)가 달라 자유 함수 하나로는 부를 수 없다. `LoadedGuide` 가 들고, 자리가 바뀌지 않게 힙에 둔다.
    class GuideStepBinding
    {
    public:
        enum class ObjectRef : std::uint8_t
        {
            None,
            Id,
            Selection
        };

        const GuideActionInfo* action = nullptr;
        ObjectRef objectRef = ObjectRef::None;
        InstanceId objectId = InvalidInstanceId;
        bool hasComponent = false;
        ComponentTypeId componentType = 0;
        bool hasField = false;
        NameId fieldName = InvalidNameId;
        bool routeFixed = false;
        GuideRoute fixedRoute = GuideRoute::Hierarchy;

        // 단계에 들어설 때 찾은 오브젝트다. 지워지면 비는 것으로 "지웠다" 를 안다.
        SafePtr<GameObject> object;
        bool resolved = false;
        std::uint32_t routeIndex = 0;

        // 글자의 원본이다. `GuideText` 가 가리킨다.
        String texts[6];

        bool BuildPath(EditorApplication& editor, GuideFocusPath& path);
        bool NextRoute(EditorApplication& editor, GuideFocusPath& path);
        void OnEnter(EditorApplication& editor, GuideStepMemo& memo);
        bool Condition(EditorApplication& editor, GuideStepMemo& memo);
        const char* NextBlocked(EditorApplication& editor);

    private:
        bool ResolveObject(EditorApplication& editor);
        bool TryRoute(EditorApplication& editor, std::uint32_t index, GuideFocusPath& path);
    };

    namespace
    {
        // 행동 하나다. 이름은 **저장되는 이름**이다 - 에이전트가 적고, 바꾸면 이미 적힌 가이드가 깨진다.
        struct GuideActionInfo
        {
            const char* name = nullptr;
            // 목록(`WriteCatalog`)에 적는 한 줄 설명이다. 에이전트가 읽는 것이라 화면 글자가 아니다(§11.2 밖).
            const char* summary = nullptr;
            std::uint8_t params = 0;
            // 오브젝트 인자가 없어도 되는가(`object.select` 는 "아무거나 골라라" 가 된다).
            bool objectOptional = false;
            GuideRoute routes[3] = {};
            std::uint32_t routeCount = 0;
            GuideStepEnd end = GuideStepEnd::NextButton;
            bool keyboard = false;
            bool canGoNext = false;
            bool (*build)(GuideStepBinding&, EditorApplication&, GuideRoute, GuideFocusPath&) = nullptr;
            void (*enter)(GuideStepBinding&, EditorApplication&, GuideStepMemo&) = nullptr;
            bool (*done)(GuideStepBinding&, EditorApplication&, GuideStepMemo&) = nullptr;
            const char* (*blocked)(GuideStepBinding&, EditorApplication&) = nullptr;
        };

        // ── 판단의 조각 ──────────────────────────────────────────────

        std::uint64_t TrackedId(EditorApplication& editor, GameObject* object)
        {
            return object != nullptr ? editor.GetObjectIds().Track(object) : 0;
        }

        std::uint64_t SelectedObjectId(EditorApplication& editor)
        {
            return TrackedId(editor, editor.GetSelectedObject());
        }

        bool Is2D(EditorApplication& editor)
        {
            return editor.GetFrameworkKind() != FrameworkKind::Framework3D;
        }

        bool HasComponent(const GameObject& object, ComponentTypeId typeId)
        {
            for (const auto& slot : object.GetComponents())
            {
                if (slot.typeId == typeId)
                {
                    return true;
                }
            }
            return false;
        }

        const char* NeedSelectedObject()
        {
            return Loc::TextOr(LocKeys::GuideNeedSelectedObject, "pick an object first");
        }

        // ── object.select ────────────────────────────────────────────

        bool BuildSelect(GuideStepBinding& binding, EditorApplication& editor, GuideRoute route, GuideFocusPath& path)
        {
            GameObject* target = binding.object.TryGet();
            if (route == GuideRoute::Hierarchy)
            {
                if (target != nullptr)
                {
                    return EditorGuides::AppendObjectPath(editor, *target, path);
                }
                return path.Push(GuideFocusTargets::Panel("Hierarchy"));
            }
            if (false == Is2D(editor))
            {
                // 3D 의 캔버스 뷰는 오브젝트를 누른 자리로 고르지 않는다(D-136) - 가리킬 사각형이 없다.
                return false;
            }
            if (false == path.Push(GuideFocusTargets::Panel("CanvasView")))
            {
                return false;
            }
            return target == nullptr || path.Push(GuideFocusTargets::CanvasViewObject(TrackedId(editor, target)));
        }

        // [0] 들어설 때 선택되어 있던 오브젝트.
        void EnterSelect(GuideStepBinding&, EditorApplication& editor, GuideStepMemo& memo)
        {
            memo.values[0] = SelectedObjectId(editor);
        }

        // 들어설 때와 다른 오브젝트가 선택됐다(정해 둔 오브젝트가 있으면 그것이). 추가한 오브젝트는 곧 선택되므로 추가해도 넘어간다.
        bool DoneSelect(GuideStepBinding& binding, EditorApplication& editor, GuideStepMemo& memo)
        {
            const std::uint64_t now = SelectedObjectId(editor);
            if (now == 0 || now == memo.values[0])
            {
                return false;
            }
            GameObject* target = binding.object.TryGet();
            return target == nullptr || editor.GetSelectedObject() == target;
        }

        const char* BlockedSelect(GuideStepBinding& binding, EditorApplication& editor)
        {
            GameObject* selected = editor.GetSelectedObject();
            if (selected == nullptr)
            {
                return NeedSelectedObject();
            }
            GameObject* target = binding.object.TryGet();
            if (binding.objectRef != GuideStepBinding::ObjectRef::None && selected != target)
            {
                return Loc::TextOr(LocKeys::GuideNeedTargetObject, "select the highlighted object first");
            }
            return nullptr;
        }

        // ── object.delete ────────────────────────────────────────────

        bool BuildDelete(GuideStepBinding& binding, EditorApplication& editor, GuideRoute route, GuideFocusPath& path)
        {
            GameObject* target = binding.object.TryGet();
            if (target == nullptr)
            {
                return false;
            }
            const std::uint64_t id = TrackedId(editor, target);
            switch (route)
            {
            case GuideRoute::Hierarchy:
                // 조상 줄은 기구가 펼치고, 그 줄에서는 사용자가 우클릭해 메뉴를 연다.
                if (false == EditorGuides::AppendObjectPath(editor, *target, path))
                {
                    return false;
                }
                path.targets[path.count - 1] = GuideFocusTargets::HierarchyObjectMenu(id);
                path.open[path.count - 1] = GuideFocusOpen::User;
                break;
            case GuideRoute::CanvasView:
                if (false == Is2D(editor)
                    || false == path.Push(GuideFocusTargets::Panel("CanvasView"))
                    || false == path.Push(GuideFocusTargets::CanvasViewObject(id), GuideFocusOpen::User))
                {
                    return false;
                }
                break;
            case GuideRoute::EditMenu:
                // 편집 메뉴의 삭제는 **선택한 것**을 지운다. 그것 하나만 고른다 - 선택은 편집이 아니므로 커맨드 없이 한다(§2.3).
                editor.SetSelectedObject(target);
                if (false == path.Push(GuideFocusTargets::Menu("menu.edit"), GuideFocusOpen::User))
                {
                    return false;
                }
                break;
            default:
                return false;
            }
            return path.Push(GuideFocusTargets::Action("object.delete"));
        }

        // 가리킨 오브젝트가 사라졌다. 어느 길로 지웠는지는 묻지 않는다.
        bool DoneDelete(GuideStepBinding& binding, EditorApplication&, GuideStepMemo&)
        {
            return binding.resolved && binding.object.TryGet() == nullptr;
        }

        // ── field.edit ───────────────────────────────────────────────

        // 선택한 오브젝트의 컴포넌트와 필드다. 정해 두지 않았으면 첫 컴포넌트의 맨 위 필드다 - 2D 면 `Transform2D.position`,
        // 3D 면 `Transform3D` 의 것이라 두 프레임워크에서 같은 가이드가 돈다.
        bool BuildField(GuideStepBinding& binding, EditorApplication& editor, GuideRoute, GuideFocusPath& path)
        {
            const GameObject* object = editor.GetSelectedObject();
            if (object == nullptr || object->GetComponents().Size() == 0)
            {
                return false;
            }
            ComponentTypeId typeId = object->GetComponents()[0].typeId;
            if (binding.hasComponent)
            {
                if (false == HasComponent(*object, binding.componentType))
                {
                    return false;
                }
                typeId = binding.componentType;
            }
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
                if (binding.hasField && property.name != binding.fieldName)
                {
                    continue;
                }
                return path.Push(GuideFocusTargets::Panel("Inspector"))
                    && path.Push(GuideFocusTargets::InspectorComponent(typeId))
                    && path.Push(GuideFocusTargets::InspectorField(typeId, property.name));
            }
            return false;
        }

        const char* BlockedNeedSelection(GuideStepBinding&, EditorApplication& editor)
        {
            return editor.GetSelectedObject() != nullptr ? nullptr : NeedSelectedObject();
        }

        // ── component.add ────────────────────────────────────────────

        bool BuildAddComponent(GuideStepBinding&, EditorApplication& editor, GuideRoute, GuideFocusPath& path)
        {
            if (editor.GetSelectedObject() == nullptr)
            {
                return false;
            }
            return path.Push(GuideFocusTargets::Panel("Inspector"))
                && path.Push(GuideFocusTargets::InspectorAddComponent(), GuideFocusOpen::User);
        }

        // [0] 들어설 때 선택되어 있던 오브젝트, [1] 그 오브젝트의 컴포넌트 수, [2] 정해 둔 타입이 이미 붙어 있었는가.
        void EnterAddComponent(GuideStepBinding& binding, EditorApplication& editor, GuideStepMemo& memo)
        {
            const GameObject* object = editor.GetSelectedObject();
            memo.values[0] = SelectedObjectId(editor);
            memo.values[1] = object != nullptr ? object->GetComponents().Size() : 0;
            memo.values[2] = object != nullptr && binding.hasComponent && HasComponent(*object, binding.componentType) ? 1 : 0;
        }

        // 고른 오브젝트의 컴포넌트가 늘었다(정해 둔 타입이 있으면 그것이 새로 붙었다). **다른 오브젝트로 옮겨 고르면 그 오브젝트로
        // 기준을 다시 잡는다** - 옮긴 것만으로는 붙인 것이 아니지만, 옮긴 뒤에 그 오브젝트에 붙인 것은 붙인 것이다.
        bool DoneAddComponent(GuideStepBinding& binding, EditorApplication& editor, GuideStepMemo& memo)
        {
            const GameObject* object = editor.GetSelectedObject();
            if (object == nullptr)
            {
                return false;
            }
            const std::uint64_t now = SelectedObjectId(editor);
            if (now != memo.values[0])
            {
                EnterAddComponent(binding, editor, memo);
                return false;
            }
            if (binding.hasComponent)
            {
                return memo.values[2] == 0 && HasComponent(*object, binding.componentType);
            }
            return object->GetComponents().Size() > memo.values[1];
        }

        // ── game.build ───────────────────────────────────────────────

        bool BuildGameBuild(GuideStepBinding&, EditorApplication&, GuideRoute, GuideFocusPath& path)
        {
            // 메뉴는 사용자가 연다. 열리면 구멍이 그 안의 항목으로 옮겨 간다.
            return path.Push(GuideFocusTargets::Menu("menu.file"), GuideFocusOpen::User)
                && path.Push(GuideFocusTargets::Menu("menu.build_game"));
        }

        // ── 표 ───────────────────────────────────────────────────────

        const GuideActionInfo* Actions(std::uint32_t& count)
        {
            static const GuideActionInfo actions[] = {
                {
                    "object.select",
                    "Select an object. Without Object, any object will do; with Object, that one. Ends when the selection changes to it.",
                    Params(GuideParam::Object), true,
                    { GuideRoute::Hierarchy, GuideRoute::CanvasView }, 2,
                    GuideStepEnd::Condition, false, true,
                    &BuildSelect, &EnterSelect, &DoneSelect, &BlockedSelect,
                },
                {
                    "object.delete",
                    "Delete an object by right-clicking it (layers window or canvas view) or through the Edit menu. Ends when the object is gone.",
                    Params(GuideParam::Object), false,
                    { GuideRoute::Hierarchy, GuideRoute::CanvasView, GuideRoute::EditMenu }, 3,
                    GuideStepEnd::Condition, false, false,
                    &BuildDelete, nullptr, &DoneDelete, nullptr,
                },
                {
                    "field.edit",
                    "Change a field of the selected object in the inspector. Without Component/Field, the first field of the first component. Ends with Next.",
                    Params(GuideParam::Component, GuideParam::Field), false,
                    { GuideRoute::Inspector }, 1,
                    GuideStepEnd::NextButton, true, false,
                    &BuildField, nullptr, nullptr, &BlockedNeedSelection,
                },
                {
                    "component.add",
                    "Add a component to the selected object from the inspector list. With Component, ends when that type is attached; otherwise when any is.",
                    Params(GuideParam::Component), false,
                    { GuideRoute::Inspector }, 1,
                    GuideStepEnd::Condition, true, false,
                    &BuildAddComponent, &EnterAddComponent, &DoneAddComponent, nullptr,
                },
                {
                    "game.build",
                    "Open the File menu and choose Build Game. Ends when Build Game is pressed.",
                    0, false,
                    { GuideRoute::MainMenu }, 1,
                    GuideStepEnd::TargetActivated, false, false,
                    &BuildGameBuild, nullptr, nullptr, nullptr,
                },
            };
            count = static_cast<std::uint32_t>(sizeof(actions) / sizeof(actions[0]));
            return actions;
        }

        const GuideActionInfo* FindAction(const char* name)
        {
            std::uint32_t count = 0;
            const GuideActionInfo* actions = Actions(count);
            for (std::uint32_t index = 0; index < count; ++index)
            {
                if (std::strcmp(actions[index].name, name) == 0)
                {
                    return &actions[index];
                }
            }
            return nullptr;
        }

        const char* EndName(GuideStepEnd end)
        {
            switch (end)
            {
            case GuideStepEnd::NextButton:
                return "next";
            case GuideStepEnd::TargetActivated:
                return "target";
            case GuideStepEnd::Condition:
                return "done";
            }
            return "";
        }
    }

    // ── 단계의 판단 ──────────────────────────────────────────────────

    bool GuideStepBinding::ResolveObject(EditorApplication& editor)
    {
        object = {};
        resolved = false;
        GameObject* found = nullptr;
        if (objectRef == ObjectRef::Selection)
        {
            found = editor.GetSelectedObject();
        }
        else if (objectRef == ObjectRef::Id)
        {
            if (Canvas* canvas = editor.GetCanvas())
            {
                canvas->ForEachObject([&](GameObject& candidate) {
                    if (found == nullptr && candidate.GetInstanceId() == objectId)
                    {
                        found = &candidate;
                    }
                });
            }
        }
        if (found != nullptr)
        {
            object = found->SafeFromThis();
            resolved = true;
            return true;
        }
        // 오브젝트를 받지 않는 행동(선택한 것에 하는 일, 메뉴)이거나 없어도 되는 행동이면 찾을 것이 없는 것이 맞다.
        return objectRef == ObjectRef::None
            && (false == HasParam(action->params, GuideParam::Object) || action->objectOptional);
    }

    bool GuideStepBinding::TryRoute(EditorApplication& editor, std::uint32_t index, GuideFocusPath& path)
    {
        path = {};
        const GuideRoute route = action->routes[index];
        if (routeFixed && route != fixedRoute)
        {
            return false;
        }
        if (false == action->build(*this, editor, route, path) || path.IsEmpty())
        {
            return false;
        }
        routeIndex = index;
        return true;
    }

    bool GuideStepBinding::BuildPath(EditorApplication& editor, GuideFocusPath& path)
    {
        if (false == ResolveObject(editor))
        {
            Log::Write(LogLevel::Warning, "editor", "guide: %s found no object %llu",
                action->name, static_cast<unsigned long long>(objectId));
            return false;
        }
        for (std::uint32_t index = 0; index < action->routeCount; ++index)
        {
            if (TryRoute(editor, index, path))
            {
                return true;
            }
        }
        return false;
    }

    bool GuideStepBinding::NextRoute(EditorApplication& editor, GuideFocusPath& path)
    {
        for (std::uint32_t index = routeIndex + 1; index < action->routeCount; ++index)
        {
            if (TryRoute(editor, index, path))
            {
                Log::Write(LogLevel::Info, "editor", "guide: %s goes by %s instead", action->name, RouteName(action->routes[index]));
                return true;
            }
        }
        return false;
    }

    void GuideStepBinding::OnEnter(EditorApplication& editor, GuideStepMemo& memo)
    {
        if (action->enter != nullptr)
        {
            action->enter(*this, editor, memo);
        }
    }

    bool GuideStepBinding::Condition(EditorApplication& editor, GuideStepMemo& memo)
    {
        return action->done != nullptr && action->done(*this, editor, memo);
    }

    const char* GuideStepBinding::NextBlocked(EditorApplication& editor)
    {
        return action->blocked != nullptr ? action->blocked(*this, editor) : nullptr;
    }

    LoadedGuide::LoadedGuide() = default;
    LoadedGuide::~LoadedGuide() = default;

    // ── 읽기 ────────────────────────────────────────────────────────

    struct GuideLoader
    {
        const YamlDocument& document;
        String& error;

        bool Fail(std::uint32_t node, const char* format, const char* detail = "")
        {
            char message[256] = {};
            std::snprintf(message, sizeof(message), format, detail);
            char line[320] = {};
            std::snprintf(line, sizeof(line), "line %zu: %s", document.GetLine(node), message);
            error = line;
            return false;
        }

        bool CheckKeys(std::uint32_t node, const char* const* allowed, std::size_t allowedCount)
        {
            for (std::size_t index = 0; index < document.GetCount(node); ++index)
            {
                const char* key = document.GetKey(node, index);
                bool known = false;
                for (std::size_t check = 0; check < allowedCount; ++check)
                {
                    known = known || std::strcmp(key, allowed[check]) == 0;
                }
                if (false == known)
                {
                    return Fail(document.GetValue(node, index), "unknown key '%s'", key);
                }
            }
            return true;
        }

        // `Key` · `String` · `Loc` 세 줄로 적힌 글자다. 셋 다 있어야 한다 - 키만 있으면 표에 없을 때 보일 것이 없고,
        // 원문만 있으면 나중에 옮길 자리가 없고, 로케일이 없으면 원문을 언제 보일지 모른다.
        bool ReadText(std::uint32_t parent, const char* name, String* out)
        {
            const std::uint32_t node = document.Find(parent, name);
            if (node == YamlDocument::InvalidNode)
            {
                return Fail(parent, "missing '%s'", name);
            }
            if (document.GetKind(node) != YamlKind::Map)
            {
                return Fail(node, "'%s' needs Key, String and Loc", name);
            }
            static const char* const keys[] = { "Key", "String", "Loc" };
            if (false == CheckKeys(node, keys, 3))
            {
                return false;
            }
            for (int index = 0; index < 3; ++index)
            {
                if (false == document.FindScalar(node, keys[index], out[index]) || out[index].empty())
                {
                    return Fail(node, "'%s' needs a non-empty value", keys[index]);
                }
            }
            return true;
        }

        bool ReadBool(std::uint32_t step, const char* key, bool& value)
        {
            const std::uint32_t node = document.Find(step, key);
            if (node != YamlDocument::InvalidNode && false == document.FindBool(step, key, value))
            {
                return Fail(node, "'%s' must be true or false", key);
            }
            return true;
        }

        bool ReadStep(std::uint32_t node, LoadedGuide& loaded, Array<String>& stepIds)
        {
            if (document.GetKind(node) != YamlKind::Map)
            {
                return Fail(node, "a step must be a map");
            }
            static const char* const keys[] = { "Id", "Do", "Object", "Component", "Field", "Via", "Title", "Body",
                "End", "Keyboard", "Skip", "Back", "Next", "RetreatTo" };
            if (false == CheckKeys(node, keys, sizeof(keys) / sizeof(keys[0])))
            {
                return false;
            }
            String doName;
            if (false == document.FindScalar(node, "Do", doName))
            {
                return Fail(node, "a step needs 'Do'");
            }
            const GuideActionInfo* action = FindAction(doName.c_str());
            if (action == nullptr)
            {
                return Fail(node, "unknown action '%s'", doName.c_str());
            }

            OwnerPtr<GuideStepBinding> binding = MakeOwnerPtr<GuideStepBinding>();
            binding->action = action;

            String value;
            if (document.FindScalar(node, "Object", value))
            {
                if (false == HasParam(action->params, GuideParam::Object))
                {
                    return Fail(node, "'%s' takes no Object", action->name);
                }
                if (value == "Selection")
                {
                    binding->objectRef = GuideStepBinding::ObjectRef::Selection;
                }
                else
                {
                    char* end = nullptr;
                    const unsigned long long id = std::strtoull(value.c_str(), &end, 10);
                    if (end == value.c_str() || *end != '\0' || id == 0)
                    {
                        return Fail(node, "Object must be an instance id or Selection, not '%s'", value.c_str());
                    }
                    binding->objectRef = GuideStepBinding::ObjectRef::Id;
                    binding->objectId = static_cast<InstanceId>(id);
                }
            }
            else if (HasParam(action->params, GuideParam::Object) && false == action->objectOptional)
            {
                return Fail(node, "'%s' needs Object", action->name);
            }
            if (document.FindScalar(node, "Component", value))
            {
                if (false == HasParam(action->params, GuideParam::Component))
                {
                    return Fail(node, "'%s' takes no Component", action->name);
                }
                // 화면에 보이는 타입 이름(`Transform2D`)으로 적는다. 접두어는 코드가 쓰는 것이다(§11.3).
                String full = value.find("::") == String::npos ? String("Component::") + value : value;
                binding->hasComponent = true;
                binding->componentType = MakeStableTypeId(full.c_str());
            }
            if (document.FindScalar(node, "Field", value))
            {
                if (false == HasParam(action->params, GuideParam::Field))
                {
                    return Fail(node, "'%s' takes no Field", action->name);
                }
                binding->hasField = true;
                binding->fieldName = MakeNameId(value.c_str());
            }
            if (document.FindScalar(node, "Via", value) && value != "auto")
            {
                bool found = false;
                for (std::uint32_t index = 0; index < action->routeCount; ++index)
                {
                    if (value == RouteName(action->routes[index]))
                    {
                        binding->routeFixed = true;
                        binding->fixedRoute = action->routes[index];
                        found = true;
                    }
                }
                if (false == found)
                {
                    return Fail(node, "'%s' cannot go by that route", action->name);
                }
            }

            GuideStep step;
            step.end = action->end;
            if (document.FindScalar(node, "End", value))
            {
                if (value == "next")
                {
                    step.end = GuideStepEnd::NextButton;
                }
                else if (value == "target")
                {
                    step.end = GuideStepEnd::TargetActivated;
                }
                else if (value == "done" && action->done != nullptr)
                {
                    step.end = GuideStepEnd::Condition;
                }
                else
                {
                    return Fail(node, "End must be next, target or done (done only where the action has one), not '%s'", value.c_str());
                }
            }
            step.keyboard = action->keyboard;
            step.canGoNext = action->canGoNext;
            if (false == ReadBool(node, "Keyboard", step.keyboard) || false == ReadBool(node, "Skip", step.canSkip)
                || false == ReadBool(node, "Back", step.canGoBack) || false == ReadBool(node, "Next", step.canGoNext))
            {
                return false;
            }
            if (false == ReadText(node, "Title", binding->texts) || false == ReadText(node, "Body", binding->texts + 3))
            {
                return false;
            }

            String id;
            document.FindScalar(node, "Id", id);
            String retreat;
            document.FindScalar(node, "RetreatTo", retreat);
            if (false == retreat.empty())
            {
                // 되돌아갈 단계는 **앞 단계**여야 한다 - 뒤로 "되돌아가면" 건너뛰기와 구분되지 않는다.
                bool earlier = false;
                for (std::size_t index = 0; index < stepIds.Size(); ++index)
                {
                    if (stepIds[index] == retreat)
                    {
                        step.retreatOnBreak = static_cast<std::int32_t>(index);
                        earlier = true;
                    }
                }
                if (false == earlier)
                {
                    return Fail(node, "RetreatTo '%s' names no earlier step", retreat.c_str());
                }
            }

            GuideStepBinding* raw = binding.Get();
            step.title = { raw->texts[0].c_str(), raw->texts[1].c_str(), raw->texts[2].c_str() };
            step.body = { raw->texts[3].c_str(), raw->texts[4].c_str(), raw->texts[5].c_str() };
            step.buildPath = Delegate<bool(EditorApplication&, GuideFocusPath&)>::Bind<&GuideStepBinding::BuildPath>(raw);
            step.nextRoute = Delegate<bool(EditorApplication&, GuideFocusPath&)>::Bind<&GuideStepBinding::NextRoute>(raw);
            step.onEnter = Delegate<void(EditorApplication&, GuideStepMemo&)>::Bind<&GuideStepBinding::OnEnter>(raw);
            if (action->done != nullptr)
            {
                step.condition = Delegate<bool(EditorApplication&, GuideStepMemo&)>::Bind<&GuideStepBinding::Condition>(raw);
            }
            if (action->blocked != nullptr)
            {
                step.nextBlockedReason = Delegate<const char*(EditorApplication&)>::Bind<&GuideStepBinding::NextBlocked>(raw);
            }
            stepIds.Add(std::move(id));
            loaded.m_bindings.Add(std::move(binding));
            loaded.m_guide.steps.Add(std::move(step));
            return true;
        }

        bool Read(LoadedGuide& loaded)
        {
            const std::uint32_t root = document.GetRoot();
            if (root == YamlDocument::InvalidNode || document.GetKind(root) != YamlKind::Map)
            {
                return Fail(root, "a guide must be a map");
            }
            static const char* const keys[] = { "Id", "Title", "Steps" };
            if (false == CheckKeys(root, keys, 3))
            {
                return false;
            }
            if (false == document.FindScalar(root, "Id", loaded.m_id) || loaded.m_id.empty())
            {
                return Fail(root, "a guide needs 'Id'");
            }
            if (false == ReadText(root, "Title", loaded.m_title))
            {
                return false;
            }
            const std::uint32_t steps = document.Find(root, "Steps");
            if (steps == YamlDocument::InvalidNode || document.GetKind(steps) != YamlKind::Sequence || document.GetCount(steps) == 0)
            {
                return Fail(root, "a guide needs at least one step under 'Steps'");
            }
            Array<String> stepIds;
            for (std::size_t index = 0; index < document.GetCount(steps); ++index)
            {
                if (false == ReadStep(document.GetElement(steps, index), loaded, stepIds))
                {
                    return false;
                }
            }
            loaded.m_guide.id = loaded.m_id.c_str();
            loaded.m_guide.title = { loaded.m_title[0].c_str(), loaded.m_title[1].c_str(), loaded.m_title[2].c_str() };
            return true;
        }
    };

    namespace EditorGuides
    {
        bool Parse(const char* text, std::size_t length, OwnerPtr<LoadedGuide>& out, String& error)
        {
            YamlDocument document;
            YamlError yamlError;
            if (text == nullptr || false == document.Parse(text, length, yamlError))
            {
                char line[320] = {};
                std::snprintf(line, sizeof(line), "line %zu: %s", yamlError.line, yamlError.message.c_str());
                error = line;
                return false;
            }
            OwnerPtr<LoadedGuide> loaded = MakeOwnerPtr<LoadedGuide>();
            GuideLoader loader{ document, error };
            if (false == loader.Read(*loaded))
            {
                return false;
            }
            out = std::move(loaded);
            return true;
        }

        void WriteCatalog(YamlWriter& writer)
        {
            writer.BeginMap("Text");
            writer.WriteString("Format", "Title and Body are maps of Key (localization key), String (the text), Loc (locale of String)");
            writer.EndMap();
            writer.BeginMap("Object");
            writer.WriteString("Format", "an instance id, or Selection for the object selected when the step begins");
            writer.EndMap();
            writer.BeginMap("Step");
            writer.WriteString("Keys", "Id Do Object Component Field Via Title Body End Keyboard Skip Back Next RetreatTo");
            writer.WriteString("End", "next (the Next button), target (pressing the last widget), done (the action's own check)");
            writer.WriteString("Via", "auto (default: the first route that can be drawn, others if it breaks) or one route name");
            writer.EndMap();
            writer.BeginSequence("Actions");
            std::uint32_t count = 0;
            const GuideActionInfo* actions = Actions(count);
            for (std::uint32_t index = 0; index < count; ++index)
            {
                const GuideActionInfo& action = actions[index];
                writer.BeginMap(nullptr);
                writer.WriteString("Do", action.name);
                writer.WriteString("Summary", action.summary);
                if (HasParam(action.params, GuideParam::Object))
                {
                    writer.WriteString("Object", action.objectOptional ? "optional" : "required");
                }
                if (HasParam(action.params, GuideParam::Component))
                {
                    writer.WriteString("Component", "optional");
                }
                if (HasParam(action.params, GuideParam::Field))
                {
                    writer.WriteString("Field", "optional");
                }
                writer.BeginSequence("Via");
                for (std::uint32_t route = 0; route < action.routeCount; ++route)
                {
                    writer.WriteStringItem(RouteName(action.routes[route]));
                }
                writer.EndSequence();
                writer.WriteString("End", EndName(action.end));
                writer.EndMap();
            }
            writer.EndSequence();
        }
    }
}

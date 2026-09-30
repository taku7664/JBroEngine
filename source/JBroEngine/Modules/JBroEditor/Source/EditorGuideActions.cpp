#include <JBro/Editor/EditorGuide.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/ComponentRegistry.h>
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
    // 글자로 적힌 가이드(D-267, `tasks/guide-focus-plan.md` §2.8)와 그 행동 표(D-268, §2.9)다.
    //
    // 가이드에 적힌 것은 **무엇을 하게 할지**다(`Do: object.delete`). 그 일이 어느 패널의 어느 줄을 거쳐 가는지는 이 파일이 안다.
    //
    // **행동은 한 줄이다**(D-268). 거의 모든 편집은 메뉴 항목 하나이고, 메뉴는 몇 개뿐이다 - 오브젝트 우클릭 메뉴 하나에 일곱
    // 항목이 같은 길(계층 줄 우클릭 · 캔버스 뷰 우클릭)로 들어간다. 그래서 길은 **메뉴가** 한 번 짓고, 행동은 "어느 메뉴들에
    // 있는가" 와 "어느 커맨드가 실행되면 끝인가" 만 적는다. 끝나는 판정은 커맨드 관리자의 실행 기록을 한 곳에서 읽는다.
    // 메뉴 항목이 아닌 일(선택 · 인스펙터의 필드)만 전용 함수를 둔다.

    namespace
    {
        // ── 길과 메뉴 ─────────────────────────────────────────────

        // 화면에서 들어가는 길이다. 적힌 이름은 `RouteName` 에 있다.
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

        // 행동이 들어 있는 메뉴(그릇)다. 행동 표에 비트로 적는다.
        enum GuideMenu : std::uint8_t
        {
            // 오브젝트 하나를 두고 여는 메뉴(`EditorActions::DrawObjectMenu`) - 계층 줄 · 캔버스 뷰의 오브젝트를 우클릭한다.
            ObjectMenu = 1 << 0,
            // 빈자리 메뉴(`DrawBackgroundMenu`) - 계층의 빈 곳 · 캔버스 뷰의 빈 곳을 우클릭한다.
            BackgroundMenu = 1 << 1,
            // 메뉴 막대의 `편집`. 선택한 것에 하므로 오브젝트가 있으면 그것 하나만 고른다.
            EditMenu = 1 << 2,
            // 메뉴 막대의 `파일`.
            FileMenu = 1 << 3,
            // 인스펙터의 컴포넌트 추가 칸. 컴포넌트를 적었으면 목록의 그 항목까지 가고, 아니면 칸이 끝이다(반례 ④).
            InspectorAdd = 1 << 4
        };

        // 메뉴가 오브젝트를 받는가. 받는 메뉴는 오브젝트가 적혔을 때만, 받지 않는 메뉴는 적히지 않았을 때만 길이 된다 -
        // "오브젝트 추가" 는 `Parent` 가 있으면 부모의 우클릭 메뉴로, 없으면 빈자리 메뉴로 간다.
        enum class MenuObject : std::uint8_t
        {
            Required,
            Forbidden,
            Optional
        };

        struct MenuRoute
        {
            GuideMenu menu;
            GuideRoute route;
            MenuObject object;
        };

        // **메뉴마다 들어가는 길이다. 이 표가 한 번 짓는다.** 차례가 `Via: auto` 의 차례다.
        // 인스펙터 칸이 맨 앞이다 - 그 칸을 쓰는 행동(컴포넌트 추가)은 오브젝트 메뉴에도 있지만, 붙인 것이 바로 보이는 칸이 먼저다.
        constexpr MenuRoute MenuRoutes[] = {
            { InspectorAdd, GuideRoute::Inspector, MenuObject::Optional },
            { ObjectMenu, GuideRoute::Hierarchy, MenuObject::Required },
            { ObjectMenu, GuideRoute::CanvasView, MenuObject::Required },
            { BackgroundMenu, GuideRoute::Hierarchy, MenuObject::Forbidden },
            { BackgroundMenu, GuideRoute::CanvasView, MenuObject::Forbidden },
            { EditMenu, GuideRoute::EditMenu, MenuObject::Optional },
            { FileMenu, GuideRoute::MainMenu, MenuObject::Forbidden },
        };
        constexpr std::uint32_t MenuRouteCount = sizeof(MenuRoutes) / sizeof(MenuRoutes[0]);

        // 커맨드가 다룬 오브젝트가 단계의 오브젝트와 어떤 사이여야 끝인가.
        enum class Subject : std::uint8_t
        {
            // 상관없다(붙여넣기).
            Any,
            // 그 오브젝트다(지우기 · 부모 해제 · 컴포넌트 추가). 오브젝트가 적히지 않았으면 지금 선택한 것이다.
            Same,
            // 그 오브젝트의 자식이다(자식 오브젝트 추가 · 자식으로 붙여넣기). 적히지 않았으면 상관없다.
            ChildOf
        };

        // 행동이 받는 오브젝트 인자다.
        enum class ObjectParam : std::uint8_t
        {
            None,
            Optional,
            Required
        };

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
            Selection,
            // 앞 단계가 남긴 것(`$단계Id`, D-269).
            Step
        };

        const GuideActionInfo* action = nullptr;
        ObjectRef objectRef = ObjectRef::None;
        InstanceId objectId = InvalidInstanceId;
        std::uint32_t refStep = 0;
        bool hasComponent = false;
        ComponentTypeId componentType = 0;
        bool hasField = false;
        NameId fieldName = InvalidNameId;
        bool routeFixed = false;
        GuideRoute fixedRoute = GuideRoute::Hierarchy;

        // 단계에 들어설 때 찾은 오브젝트와 그 에디터 번호다. 번호를 따로 든다 - 지우는 일을 판정할 때 오브젝트는 이미 없다.
        SafePtr<GameObject> object;
        std::uint64_t objectEditorId = 0;
        std::uint32_t routeIndex = 0;

        // 글자의 원본이다. `GuideText` 가 가리킨다.
        String texts[6];

        bool BuildPath(EditorApplication& editor, GuideFocusPath& path);
        bool NextRoute(EditorApplication& editor, GuideFocusPath& path);
        void OnEnter(EditorApplication& editor, GuideStepMemo& memo);
        bool Condition(EditorApplication& editor, GuideStepMemo& memo);
        const char* NextBlocked(EditorApplication& editor);
        std::uint64_t Result(EditorApplication& editor, GuideStepMemo& memo);

    private:
        bool ResolveObject(EditorApplication& editor);
        bool TryRoute(EditorApplication& editor, std::uint32_t index, GuideFocusPath& path);
        bool CommandRan(EditorApplication& editor, GuideStepMemo& memo);
    };

    namespace
    {
        // 행동 하나다. 이름은 **저장되는 이름**이다 - 에이전트가 적고, 바꾸면 이미 적힌 가이드가 깨진다.
        //
        // 메뉴 항목인 행동은 `menus` 와 `command` 두 칸이 전부다. 그 항목에 `GuideFocusTargets::Action(name)` 표식을 단다.
        // 메뉴 항목이 아닌 행동만 `routes`·`build`·`enter`·`done` 을 쓴다.
        struct GuideActionInfo
        {
            const char* name = nullptr;
            // 목록(`WriteCatalog`)에 적는 한 줄 설명이다. 에이전트가 읽는 것이라 화면 글자가 아니다(§11.2 밖).
            const char* summary = nullptr;
            std::uint8_t menus = 0;
            // 이 커맨드가 실행되면 끝이다(`EditorCommand::GetName`). 없으면 항목을 누르면 끝이다(복사 · 실행 취소 · 게임 빌드).
            const char* command = nullptr;
            Subject subject = Subject::Any;
            ObjectParam object = ObjectParam::None;
            // 오브젝트 인자의 이름이다. 자식을 만드는 일은 `Parent` 로 적는 것이 읽기 쉽다.
            const char* objectKey = "Object";
            bool takesComponent = false;
            bool takesField = false;
            // 적힌 컴포넌트를 목록의 항목(`GuideFocusTargets::ComponentListItem`)으로 가리키는가. 그러면 그 단계는 키보드를 닫는다.
            bool pointsAtListItem = false;
            bool keyboard = false;
            bool canGoNext = false;
            // 끝나며 오브젝트를 남기는가(D-269). 커맨드로 끝나면 그 커맨드가 다룬 오브젝트, 선택이면 끝날 때 선택된 오브젝트다.
            bool leavesObject = false;
            // ── 메뉴 항목이 아닌 행동 ──
            GuideRoute routes[2] = {};
            std::uint32_t routeCount = 0;
            GuideStepEnd end = GuideStepEnd::NextButton;
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

        // ── 메뉴의 길 ─────────────────────────────────────────────

        // 메뉴를 여는 데까지의 경로다. 끝의 항목은 부르는 쪽이 붙인다.
        bool BuildMenuEntry(const MenuRoute& entry, EditorApplication& editor, GameObject* target, GuideFocusPath& path)
        {
            switch (entry.menu)
            {
            case ObjectMenu:
            {
                const std::uint64_t id = TrackedId(editor, target);
                if (entry.route == GuideRoute::Hierarchy)
                {
                    // 조상 줄은 기구가 펴고, 그 줄에서는 사용자가 우클릭해 메뉴를 연다.
                    if (false == EditorGuides::AppendObjectPath(editor, *target, path))
                    {
                        return false;
                    }
                    path.targets[path.count - 1] = GuideFocusTargets::HierarchyObjectMenu(id);
                    path.open[path.count - 1] = GuideFocusOpen::User;
                    return true;
                }
                // 3D 의 캔버스 뷰는 오브젝트를 누른 자리로 고르지 않는다(D-136) - 우클릭할 사각형이 없다.
                return Is2D(editor) && path.Push(GuideFocusTargets::Panel("CanvasView"))
                    && path.Push(GuideFocusTargets::CanvasViewObject(id), GuideFocusOpen::User);
            }
            case BackgroundMenu:
                if (entry.route == GuideRoute::Hierarchy)
                {
                    return path.Push(GuideFocusTargets::Panel("Hierarchy"))
                        && path.Push(GuideFocusTargets::HierarchyBackground(), GuideFocusOpen::User);
                }
                return Is2D(editor) && path.Push(GuideFocusTargets::Panel("CanvasView"))
                    && path.Push(GuideFocusTargets::CanvasViewBackground(), GuideFocusOpen::User);
            case EditMenu:
                // 편집 메뉴는 **선택한 것**에 한다. 그것 하나만 고른다 - 선택은 편집이 아니므로 커맨드 없이 한다(§2.3).
                if (target != nullptr)
                {
                    editor.SetSelectedObject(target);
                }
                return path.Push(GuideFocusTargets::Menu("menu.edit"), GuideFocusOpen::User);
            case FileMenu:
                return path.Push(GuideFocusTargets::Menu("menu.file"), GuideFocusOpen::User);
            case InspectorAdd:
                if (target != nullptr)
                {
                    editor.SetSelectedObject(target);
                }
                return editor.GetSelectedObject() != nullptr && path.Push(GuideFocusTargets::Panel("Inspector"))
                    && path.Push(GuideFocusTargets::InspectorAddComponent(), GuideFocusOpen::User);
            }
            return false;
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
            if (false == Is2D(editor) || false == path.Push(GuideFocusTargets::Panel("CanvasView")))
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
        // 선택은 커맨드가 아니라 실행 기록에 남지 않는다 - 그래서 이 행동은 판정을 따로 둔다.
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

        // ── field.edit ───────────────────────────────────────────────

        // 선택한 오브젝트의 컴포넌트와 필드다. 정해 두지 않았으면 첫 컴포넌트의 맨 위 필드다 - 2D 면 `Transform2D.position`,
        // 3D 면 `Transform3D` 의 것이라 두 프레임워크에서 같은 가이드가 돈다.
        bool BuildField(GuideStepBinding& binding, EditorApplication& editor, GuideRoute, GuideFocusPath& path)
        {
            if (GameObject* target = binding.object.TryGet())
            {
                editor.SetSelectedObject(target);
            }
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

        // ── 표 ───────────────────────────────────────────────────────

        GuideActionInfo MenuAction(const char* name, const char* summary, std::uint8_t menus, const char* command,
            Subject subject, ObjectParam object, const char* objectKey = "Object", bool leavesObject = false)
        {
            GuideActionInfo info;
            info.name = name;
            info.summary = summary;
            info.menus = menus;
            info.command = command;
            info.subject = subject;
            info.object = object;
            info.objectKey = objectKey;
            info.leavesObject = leavesObject;
            info.end = command != nullptr ? GuideStepEnd::Condition : GuideStepEnd::TargetActivated;
            return info;
        }

        const GuideActionInfo* Actions(std::uint32_t& count)
        {
            static const GuideActionInfo actions[] = {
                // ── 메뉴 항목: 한 줄씩 ──
                MenuAction("object.create", "Create an object. With Parent, as its child (from the parent's right-click menu); without, at the root (right-click an empty spot).",
                    ObjectMenu | BackgroundMenu, "Create Object", Subject::ChildOf, ObjectParam::Optional, "Parent", true),
                MenuAction("object.delete", "Delete an object.",
                    ObjectMenu | EditMenu, "Delete Object", Subject::Same, ObjectParam::Required),
                MenuAction("object.unparent", "Move an object out of its parent to the root.",
                    ObjectMenu, "Move In Hierarchy", Subject::Same, ObjectParam::Required, "Object", true),
                MenuAction("object.copy", "Copy an object. Ends when Copy is pressed.",
                    ObjectMenu | EditMenu, nullptr, Subject::Any, ObjectParam::Required),
                MenuAction("object.paste", "Paste the copied objects at the root.",
                    BackgroundMenu | EditMenu, "Paste Objects", Subject::Any, ObjectParam::None, "Object", true),
                MenuAction("object.paste_as_child", "Paste the copied objects as children of an object.",
                    ObjectMenu | EditMenu, "Paste Objects", Subject::ChildOf, ObjectParam::Required, "Object", true),
                MenuAction("edit.undo", "Undo the last edit. Ends when Undo is pressed.",
                    EditMenu, nullptr, Subject::Any, ObjectParam::None),
                MenuAction("edit.redo", "Redo the last undone edit. Ends when Redo is pressed.",
                    EditMenu, nullptr, Subject::Any, ObjectParam::None),
                MenuAction("game.build", "Open the File menu and choose Build Game. Ends when Build Game is pressed.",
                    FileMenu, nullptr, Subject::Any, ObjectParam::None),
                [] {
                    // 컴포넌트를 적으면 목록의 그 항목까지 가리킨다(반례 ④). 적지 않으면 목록을 열고 검색에 글자를 쳐서 고른다 -
                    // 그때만 키보드를 연다(`ListKeyboard`). 항목을 가리키는 동안 검색하면 그 항목이 걸러져 사라진다.
                    GuideActionInfo info = MenuAction("component.add",
                        "Add a component, from the inspector list or the object's right-click menu. With Component, points at that type in the list and ends when it is attached; otherwise ends when any is.",
                        InspectorAdd | ObjectMenu, "Add Component", Subject::Same, ObjectParam::Optional, "Object", true);
                    info.takesComponent = true;
                    info.pointsAtListItem = true;
                    info.keyboard = true;
                    return info;
                }(),
                // ── 메뉴 항목이 아닌 것 ──
                [] {
                    GuideActionInfo info;
                    info.name = "object.select";
                    info.summary = "Select an object. Without Object, any object will do; with Object, that one. Ends when the selection changes to it.";
                    info.object = ObjectParam::Optional;
                    info.routes[0] = GuideRoute::Hierarchy;
                    info.routes[1] = GuideRoute::CanvasView;
                    info.routeCount = 2;
                    info.end = GuideStepEnd::Condition;
                    info.canGoNext = true;
                    info.leavesObject = true;
                    info.build = &BuildSelect;
                    info.enter = &EnterSelect;
                    info.done = &DoneSelect;
                    info.blocked = &BlockedSelect;
                    return info;
                }(),
                [] {
                    GuideActionInfo info;
                    info.name = "field.edit";
                    info.summary = "Change a field in the inspector. Without Component/Field, the first field of the first component. Ends with Next.";
                    info.object = ObjectParam::Optional;
                    info.takesComponent = true;
                    info.takesField = true;
                    info.routes[0] = GuideRoute::Inspector;
                    info.routeCount = 1;
                    info.end = GuideStepEnd::NextButton;
                    info.keyboard = true;
                    info.build = &BuildField;
                    info.blocked = &BlockedNeedSelection;
                    return info;
                }(),
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

        // 행동이 갈 수 있는 길의 수와 그 이름이다. 메뉴 항목이면 메뉴 표에서, 아니면 행동이 적은 것에서 온다.
        std::uint32_t RouteCount(const GuideActionInfo& action)
        {
            if (action.build != nullptr)
            {
                return action.routeCount;
            }
            std::uint32_t count = 0;
            for (const MenuRoute& entry : MenuRoutes)
            {
                count += (action.menus & entry.menu) != 0 ? 1 : 0;
            }
            return count;
        }

        // `index` 번째 길이다. 메뉴 항목이면 메뉴 표의 칸을 `entry` 에 준다.
        GuideRoute RouteAt(const GuideActionInfo& action, std::uint32_t index, const MenuRoute** entry)
        {
            if (action.build != nullptr)
            {
                *entry = nullptr;
                return action.routes[index];
            }
            for (const MenuRoute& candidate : MenuRoutes)
            {
                if ((action.menus & candidate.menu) == 0)
                {
                    continue;
                }
                if (index == 0)
                {
                    *entry = &candidate;
                    return candidate.route;
                }
                --index;
            }
            *entry = nullptr;
            return GuideRoute::Hierarchy;
        }

        bool CanGoBy(const GuideActionInfo& action, GuideRoute route)
        {
            for (std::uint32_t index = 0; index < RouteCount(action); ++index)
            {
                const MenuRoute* entry = nullptr;
                if (RouteAt(action, index, &entry) == route)
                {
                    return true;
                }
            }
            return false;
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
        objectEditorId = 0;
        GameObject* found = nullptr;
        if (objectRef == ObjectRef::Selection)
        {
            found = editor.GetSelectedObject();
        }
        else if (objectRef == ObjectRef::Step)
        {
            // 앞 단계가 남긴 번호다. 지웠다 되돌려도 같은 번호에 다시 걸린다(`Rebind`).
            found = editor.GetObjectIds().Resolve(editor.GetGuide().GetResult(refStep));
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
            objectEditorId = TrackedId(editor, found);
            return true;
        }
        // 오브젝트를 적지 않은 단계는 찾을 것이 없는 것이 맞다. 적었는데 없으면 가리킬 것이 없다.
        return objectRef == ObjectRef::None;
    }

    bool GuideStepBinding::TryRoute(EditorApplication& editor, std::uint32_t index, GuideFocusPath& path)
    {
        path = {};
        const MenuRoute* entry = nullptr;
        const GuideRoute route = RouteAt(*action, index, &entry);
        if (routeFixed && route != fixedRoute)
        {
            return false;
        }
        GameObject* target = object.TryGet();
        if (entry == nullptr)
        {
            if (false == action->build(*this, editor, route, path) || path.IsEmpty())
            {
                return false;
            }
        }
        else
        {
            const bool given = target != nullptr;
            if ((entry->object == MenuObject::Required && false == given)
                || (entry->object == MenuObject::Forbidden && given))
            {
                return false;
            }
            if (false == BuildMenuEntry(*entry, editor, target, path))
            {
                return false;
            }
            const bool toListItem = action->pointsAtListItem && hasComponent;
            if (entry->menu != InspectorAdd)
            {
                // 연 메뉴의 그 항목이다. 목록 항목까지 가면 그 항목은 하위 메뉴라 사용자가 연다.
                if (false == path.Push(GuideFocusTargets::Action(action->name), toListItem ? GuideFocusOpen::User : GuideFocusOpen::Auto))
                {
                    return false;
                }
                if (toListItem)
                {
                    // 오브젝트 메뉴의 목록은 갈래마다 하위 메뉴다. 모르는 타입이면(스크립트가 안 실렸다) 갈래를 몰라 이 길은 없다.
                    const ComponentTypeInfo* type = ComponentRegistry::Get().Find(componentType);
                    if (type == nullptr || false == path.Push(GuideFocusTargets::ComponentCategoryMenu(
                            type->category != nullptr ? type->category : ComponentCategory::Default), GuideFocusOpen::User))
                    {
                        return false;
                    }
                }
            }
            // 컴포넌트 추가 칸은 적힌 컴포넌트가 없으면 칸 자체가 끝이다 - 목록에서 아무거나 고른다.
            if (toListItem && false == path.Push(GuideFocusTargets::ComponentListItem(componentType)))
            {
                return false;
            }
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
        for (std::uint32_t index = 0; index < RouteCount(*action); ++index)
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
        for (std::uint32_t index = routeIndex + 1; index < RouteCount(*action); ++index)
        {
            if (TryRoute(editor, index, path))
            {
                const MenuRoute* entry = nullptr;
                Log::Write(LogLevel::Info, "editor", "guide: %s goes by %s instead", action->name,
                    RouteName(RouteAt(*action, index, &entry)));
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
            return;
        }
        // [0] 들어설 때까지 실행된 커맨드 수, [1] 거기까지 훑었다, [2] 맞은 커맨드가 다룬 오브젝트(남길 것).
        memo.values[0] = editor.GetCommands().GetExecuteCount();
        memo.values[1] = memo.values[0];
        memo.values[2] = 0;
    }

    // 들어선 뒤 실행된 커맨드 가운데 이 행동의 커맨드가 이 단계의 오브젝트에 일어났는가. 새로 실행된 것만 훑는다 - 커맨드가
    // 실행된 프레임에만 글자를 견주고, 매 프레임에는 수 하나를 견준다.
    bool GuideStepBinding::CommandRan(EditorApplication& editor, GuideStepMemo& memo)
    {
        EditorCommandManager& commands = editor.GetCommands();
        const std::uint64_t count = commands.GetExecuteCount();
        const std::uint64_t targetId = objectEditorId;
        for (std::uint64_t serial = memo.values[1] + 1; serial <= count; ++serial)
        {
            EditorCommandManager::ExecutedCommand ran;
            if (false == commands.GetExecuted(serial, ran) || ran.name == nullptr
                || std::strcmp(ran.name, action->command) != 0)
            {
                continue;
            }
            bool matches = true;
            switch (action->subject)
            {
            case Subject::Any:
                break;
            case Subject::Same:
                // 적힌 오브젝트가 없으면 지금 선택한 것이다(컴포넌트 추가는 인스펙터에 보이는 것에 붙인다).
                matches = ran.subject == (targetId != 0 ? targetId : SelectedObjectId(editor));
                break;
            case Subject::ChildOf:
                if (targetId != 0)
                {
                    GameObject* made = editor.GetObjectIds().Resolve(ran.subject);
                    GameObject* parent = made != nullptr ? made->GetParent() : nullptr;
                    matches = parent != nullptr && TrackedId(editor, parent) == targetId;
                }
                break;
            }
            if (matches && action->takesComponent && hasComponent)
            {
                GameObject* subject = editor.GetObjectIds().Resolve(ran.subject);
                matches = subject != nullptr && HasComponent(*subject, componentType);
            }
            if (matches)
            {
                memo.values[1] = count;
                memo.values[2] = ran.subject;
                return true;
            }
        }
        memo.values[1] = count;
        return false;
    }

    bool GuideStepBinding::Condition(EditorApplication& editor, GuideStepMemo& memo)
    {
        if (action->done != nullptr)
        {
            return action->done(*this, editor, memo);
        }
        return action->command != nullptr && CommandRan(editor, memo);
    }

    const char* GuideStepBinding::NextBlocked(EditorApplication& editor)
    {
        return action->blocked != nullptr ? action->blocked(*this, editor) : nullptr;
    }

    std::uint64_t GuideStepBinding::Result(EditorApplication& editor, GuideStepMemo& memo)
    {
        if (false == action->leavesObject)
        {
            return 0;
        }
        // 선택은 끝날 때 선택된 것이다(다음으로 넘긴 경우도 같다). 나머지는 맞은 커맨드가 다룬 것이다.
        return action->done == &DoneSelect ? SelectedObjectId(editor) : memo.values[2];
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

        bool ReadObject(std::uint32_t node, const GuideActionInfo& action, GuideStepBinding& binding,
            const Array<String>& stepIds, const Array<const GuideActionInfo*>& stepActions)
        {
            // 오브젝트 인자는 행동이 정한 이름 하나로만 받는다. 다른 이름으로 적으면 모르는 키다.
            const char* otherKey = std::strcmp(action.objectKey, "Object") == 0 ? "Parent" : "Object";
            if (document.Find(node, otherKey) != YamlDocument::InvalidNode)
            {
                char reason[96] = {};
                std::snprintf(reason, sizeof(reason), "'%s' takes no %s", action.name, otherKey);
                return Fail(node, "%s", reason);
            }
            String value;
            if (false == document.FindScalar(node, action.objectKey, value))
            {
                if (action.object == ObjectParam::Required)
                {
                    char reason[96] = {};
                    std::snprintf(reason, sizeof(reason), "'%s' needs %s", action.name, action.objectKey);
                    return Fail(node, "%s", reason);
                }
                return true;
            }
            if (action.object == ObjectParam::None)
            {
                char reason[96] = {};
                std::snprintf(reason, sizeof(reason), "'%s' takes no %s", action.name, action.objectKey);
                return Fail(node, "%s", reason);
            }
            if (value == "Selection")
            {
                binding.objectRef = GuideStepBinding::ObjectRef::Selection;
                return true;
            }
            if (value.size() > 1 && value[0] == '$')
            {
                // 앞 단계가 남긴 것이다. 뒤 단계나 없는 단계를 가리키면 그 결과가 올 때가 없다.
                const char* name = value.c_str() + 1;
                for (std::size_t index = 0; index < stepIds.Size(); ++index)
                {
                    if (stepIds[index] != name)
                    {
                        continue;
                    }
                    if (false == stepActions[index]->leavesObject)
                    {
                        char reason[128] = {};
                        std::snprintf(reason, sizeof(reason), "step '%s' (%s) leaves no object", name, stepActions[index]->name);
                        return Fail(node, "%s", reason);
                    }
                    binding.objectRef = GuideStepBinding::ObjectRef::Step;
                    binding.refStep = static_cast<std::uint32_t>(index);
                    return true;
                }
                return Fail(node, "'%s' names no earlier step", value.c_str());
            }
            char* end = nullptr;
            const unsigned long long id = std::strtoull(value.c_str(), &end, 10);
            if (end == value.c_str() || *end != '\0' || id == 0)
            {
                return Fail(node, "Object must be an instance id, Selection or $step, not '%s'", value.c_str());
            }
            binding.objectRef = GuideStepBinding::ObjectRef::Id;
            binding.objectId = static_cast<InstanceId>(id);
            return true;
        }

        bool ReadStep(std::uint32_t node, LoadedGuide& loaded, Array<String>& stepIds, Array<const GuideActionInfo*>& stepActions)
        {
            if (document.GetKind(node) != YamlKind::Map)
            {
                return Fail(node, "a step must be a map");
            }
            static const char* const keys[] = { "Id", "Do", "Object", "Parent", "Component", "Field", "Via", "Title", "Body",
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
            if (false == ReadObject(node, *action, *binding, stepIds, stepActions))
            {
                return false;
            }

            String value;
            if (document.FindScalar(node, "Component", value))
            {
                if (false == action->takesComponent)
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
                if (false == action->takesField)
                {
                    return Fail(node, "'%s' takes no Field", action->name);
                }
                binding->hasField = true;
                binding->fieldName = MakeNameId(value.c_str());
            }
            if (document.FindScalar(node, "Via", value) && value != "auto")
            {
                bool found = false;
                for (GuideRoute route : { GuideRoute::Hierarchy, GuideRoute::CanvasView, GuideRoute::EditMenu,
                         GuideRoute::Inspector, GuideRoute::MainMenu })
                {
                    if (value == RouteName(route) && CanGoBy(*action, route))
                    {
                        binding->routeFixed = true;
                        binding->fixedRoute = route;
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
                const bool hasDone = action->done != nullptr || action->command != nullptr;
                if (value == "next")
                {
                    step.end = GuideStepEnd::NextButton;
                }
                else if (value == "target")
                {
                    step.end = GuideStepEnd::TargetActivated;
                }
                else if (value == "done" && hasDone)
                {
                    step.end = GuideStepEnd::Condition;
                }
                else
                {
                    return Fail(node, "End must be next, target or done (done only where the action has one), not '%s'", value.c_str());
                }
            }
            // 목록의 항목을 가리키는 단계는 검색을 닫는다 - 글자를 치면 그 항목이 걸러져 사라진다.
            step.keyboard = action->keyboard && false == (action->pointsAtListItem && binding->hasComponent);
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
            if (binding->objectRef == GuideStepBinding::ObjectRef::Step)
            {
                // 받은 것을 잃으면(들어설 때 비었거나 도중에 사라졌다) 그것을 남긴 단계로 돌아간다. `RetreatTo` 를 적었으면 그리로 간다.
                if (step.retreatOnBreak < 0)
                {
                    step.retreatOnBreak = static_cast<std::int32_t>(binding->refStep);
                }
                step.retreatOnMissing = step.retreatOnBreak;
            }

            GuideStepBinding* raw = binding.Get();
            step.title = { raw->texts[0].c_str(), raw->texts[1].c_str(), raw->texts[2].c_str() };
            step.body = { raw->texts[3].c_str(), raw->texts[4].c_str(), raw->texts[5].c_str() };
            step.buildPath = Delegate<bool(EditorApplication&, GuideFocusPath&)>::Bind<&GuideStepBinding::BuildPath>(raw);
            step.nextRoute = Delegate<bool(EditorApplication&, GuideFocusPath&)>::Bind<&GuideStepBinding::NextRoute>(raw);
            step.onEnter = Delegate<void(EditorApplication&, GuideStepMemo&)>::Bind<&GuideStepBinding::OnEnter>(raw);
            if (action->done != nullptr || action->command != nullptr)
            {
                step.condition = Delegate<bool(EditorApplication&, GuideStepMemo&)>::Bind<&GuideStepBinding::Condition>(raw);
            }
            if (action->blocked != nullptr)
            {
                step.nextBlockedReason = Delegate<const char*(EditorApplication&)>::Bind<&GuideStepBinding::NextBlocked>(raw);
            }
            if (action->leavesObject)
            {
                step.result = Delegate<std::uint64_t(EditorApplication&, GuideStepMemo&)>::Bind<&GuideStepBinding::Result>(raw);
            }
            stepIds.Add(std::move(id));
            stepActions.Add(action);
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
            Array<const GuideActionInfo*> stepActions;
            for (std::size_t index = 0; index < document.GetCount(steps); ++index)
            {
                if (false == ReadStep(document.GetElement(steps, index), loaded, stepIds, stepActions))
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
            writer.WriteString("Format", "an instance id, Selection for the object selected when the step begins, or $stepId for the object an earlier step left");
            writer.EndMap();
            writer.BeginMap("Step");
            writer.WriteString("Keys", "Id Do Object Parent Component Field Via Title Body End Keyboard Skip Back Next RetreatTo");
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
                if (action.object != ObjectParam::None)
                {
                    writer.WriteString(action.objectKey, action.object == ObjectParam::Required ? "required" : "optional");
                }
                if (action.takesComponent)
                {
                    writer.WriteString("Component", "optional");
                }
                if (action.takesField)
                {
                    writer.WriteString("Field", "optional");
                }
                writer.BeginSequence("Via");
                GuideRoute written[MenuRouteCount] = {};
                std::uint32_t writtenCount = 0;
                for (std::uint32_t route = 0; route < RouteCount(action); ++route)
                {
                    const MenuRoute* entry = nullptr;
                    const GuideRoute name = RouteAt(action, route, &entry);
                    bool seen = false;
                    for (std::uint32_t check = 0; check < writtenCount; ++check)
                    {
                        seen = seen || written[check] == name;
                    }
                    if (false == seen)
                    {
                        written[writtenCount++] = name;
                        writer.WriteStringItem(RouteName(name));
                    }
                }
                writer.EndSequence();
                writer.WriteString("End", EndName(action.end));
                if (action.leavesObject)
                {
                    writer.WriteString("Leaves", "object");
                }
                writer.EndMap();
            }
            writer.EndSequence();
        }
    }
}

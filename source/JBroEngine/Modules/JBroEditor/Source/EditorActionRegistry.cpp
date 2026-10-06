#include <JBro/Editor/EditorActionRegistry.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/CommandPalette.h>
#include <JBro/Editor/ComponentMenuTable.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/EditorPanelRegistry.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/GuideFocus.h>
#include <JBro/Host/GameBuild.h>
#include <JBro/Runtime/GameObject.h>

#include "Panel/CanvasViewPanel.h"

#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        Bool IsEmptyName(const char* name)
        {
            return name == nullptr || name[0] == '\0';
        }

        // ── 기본 행동의 판정과 할 일 ─────────────────────────────────────
        //
        // 막은 조건이 여럿이면 **먼저 풀어야 하는 것**을 말한다(D-181). 프로젝트가 없는데 "시뮬레이션을 멈추세요" 라고 하면
        // 멈출 것을 찾다 끝난다.

        Bool HasCanvas(const EditorActionContext& context)
        {
            return context.editor->GetCanvas() != nullptr;
        }

        const char* NoProject()
        {
            return Loc::TextOr(LocKeys::BlockedNoProject, "no project is open");
        }

        const char* NothingSelected()
        {
            return Loc::TextOr(LocKeys::InspectorNothingSelected, "nothing is selected");
        }

        const char* ClipboardEmpty()
        {
            return Loc::TextOr(LocKeys::BlockedClipboardEmpty, "nothing has been copied");
        }

        // 오브젝트 메뉴면 우클릭한 것, 아니면 고른 것이다.
        GameObject* Target(const EditorActionContext& context)
        {
            return context.object != nullptr ? context.object : context.editor->GetSelectedObject();
        }

        Bool CanSave(const EditorActionContext& context)
        {
            // **돌고 있는 동안은 저장하지 않는다.** 게임이 만든 상태가 파일이 된다.
            return HasCanvas(context) && false == context.editor->IsSimulationPlaying();
        }
        const char* WhyNoSave(const EditorActionContext& context)
        {
            return false == HasCanvas(context) ? NoProject()
                : Loc::TextOr(LocKeys::PopupSaveBlockedWhilePlaying, "stop the simulation before saving");
        }
        Bool DoSave(EditorActionContext& context)
        {
            context.editor->RequestSaveCanvas();
            return true;
        }

        Bool CanUndo(const EditorActionContext& context)
        {
            return context.editor->GetCommands().CanUndo();
        }
        const char* WhyNoUndo(const EditorActionContext&)
        {
            return Loc::TextOr(LocKeys::BlockedNothingToUndo, "there is nothing to undo");
        }
        Bool DoUndo(EditorActionContext& context)
        {
            return context.editor->GetCommands().Undo();
        }
        Bool CanRedo(const EditorActionContext& context)
        {
            return context.editor->GetCommands().CanRedo();
        }
        const char* WhyNoRedo(const EditorActionContext&)
        {
            return Loc::TextOr(LocKeys::BlockedNothingToRedo, "there is nothing to redo");
        }
        Bool DoRedo(EditorActionContext& context)
        {
            return context.editor->GetCommands().Redo();
        }

        Bool CanCreate(const EditorActionContext& context)
        {
            return HasCanvas(context);
        }
        const char* WhyNoCreate(const EditorActionContext&)
        {
            return NoProject();
        }
        Bool DoCreate(EditorActionContext& context)
        {
            // 오브젝트 메뉴에서는 그 오브젝트의 자식이고(`자식 오브젝트 추가`), 다른 자리에서는 뿌리다.
            GameObject* parent = context.menu == EditorActionMenu::Object ? context.object : nullptr;
            return EditorActions::CreateObject(*context.editor, parent, context.placement) != nullptr;
        }

        // **부모가 없으면 오브젝트 메뉴에 항목 자체를 내지 않는다.** 회색으로 두면 무엇을 해야 켜지는지 알 수 없고,
        // 뿌리 오브젝트에는 영영 켜지지 않는다.
        Bool ShowUnparent(const EditorActionContext& context)
        {
            const GameObject* target = Target(context);
            return context.menu != EditorActionMenu::Object || (target != nullptr && target->GetParent() != nullptr);
        }
        Bool CanUnparent(const EditorActionContext& context)
        {
            const GameObject* target = Target(context);
            return target != nullptr && target->GetParent() != nullptr;
        }
        const char* WhyNoUnparent(const EditorActionContext& context)
        {
            return Target(context) == nullptr ? NothingSelected()
                : Loc::TextOr(LocKeys::BlockedNoParent, "the object has no parent");
        }
        Bool DoUnparent(EditorActionContext& context)
        {
            return EditorActions::Unparent(*context.editor, *Target(context));
        }

        Bool CanCopy(const EditorActionContext& context)
        {
            return context.editor->GetSelectionCount() != 0;
        }
        const char* WhyNoCopy(const EditorActionContext&)
        {
            return NothingSelected();
        }
        Bool DoCopy(EditorActionContext& context)
        {
            return context.editor->CopySelection();
        }

        Bool CanPaste(const EditorActionContext& context)
        {
            return HasCanvas(context) && context.editor->HasClipboard();
        }
        const char* WhyNoPaste(const EditorActionContext& context)
        {
            return false == HasCanvas(context) ? NoProject() : ClipboardEmpty();
        }
        Bool DoPaste(EditorActionContext& context)
        {
            // 빈자리의 붙여넣기는 뿌리에 붙는다. 고른 것 밑이 아니다.
            if (context.menu == EditorActionMenu::Background)
            {
                context.editor->ClearSelection();
            }
            return context.editor->PasteClipboard();
        }

        Bool CanPasteAsChild(const EditorActionContext& context)
        {
            // 자식으로 붙이려면 들어갈 곳이 있어야 한다. 줄에서 연 메뉴는 그 줄이 곧 부모다.
            return CanPaste(context) && Target(context) != nullptr;
        }
        const char* WhyNoPasteAsChild(const EditorActionContext& context)
        {
            // 붙일 것이 없는 것과 들어갈 곳이 없는 것은 다른 이야기다.
            if (false == CanPaste(context))
            {
                return WhyNoPaste(context);
            }
            return NothingSelected();
        }
        Bool DoPasteAsChild(EditorActionContext& context)
        {
            // **고른 것 안으로 붙인다**(D-166, 기존 `PasteObjectsAsChild`).
            if (context.object != nullptr)
            {
                context.editor->SetSelectedObject(context.object);
            }
            return context.editor->PasteClipboard(true);
        }

        Bool CanDelete(const EditorActionContext& context)
        {
            // 줄에서 연 메뉴는 그 오브젝트를 지운다.
            if (context.object != nullptr)
            {
                return HasCanvas(context);
            }
            // 고른 레이어도 지울 대상이다(D-279). 마지막 한 장은 아니다.
            if (context.editor->GetSelectedLayer() != InvalidLayerId)
            {
                return HasCanvas(context) && context.editor->GetCanvas()->GetLayerCount() > 1;
            }
            return context.editor->GetSelectionCount() != 0;
        }
        const char* WhyNoDelete(const EditorActionContext& context)
        {
            if (false == HasCanvas(context))
            {
                return NoProject();
            }
            if (context.object == nullptr && context.editor->GetSelectedLayer() != InvalidLayerId)
            {
                return Loc::TextOr(LocKeys::BlockedLastLayer, "a canvas needs at least one layer");
            }
            return NothingSelected();
        }
        Bool DoDelete(EditorActionContext& context)
        {
            if (context.object != nullptr)
            {
                return EditorActions::DeleteObject(*context.editor, *context.object);
            }
            return EditorActions::DeleteSelection(*context.editor);
        }

        Bool CanPlay(const EditorActionContext& context)
        {
            return HasCanvas(context);
        }
        const char* WhyNoPlay(const EditorActionContext&)
        {
            return NoProject();
        }
        Bool DoPlay(EditorActionContext& context)
        {
            context.editor->ToggleSimulation();
            return true;
        }
        Bool CanPause(const EditorActionContext& context)
        {
            return context.editor->IsSimulationPlaying();
        }
        const char* WhyNoPause(const EditorActionContext&)
        {
            return Loc::TextOr(LocKeys::BlockedNotPlaying, "the simulation is not running");
        }
        Bool DoPause(EditorActionContext& context)
        {
            context.editor->SetSimulationPaused(false == context.editor->IsSimulationPaused());
            return true;
        }
        Bool CanStep(const EditorActionContext& context)
        {
            // 도는 게임은 한 프레임씩 나아갈 것이 없다 - 멈춘 동안만이다.
            return context.editor->IsSimulationPlaying() && context.editor->IsSimulationPaused();
        }
        const char* WhyNoStep(const EditorActionContext& context)
        {
            // 먼저 풀 것을 말한다: 돌지 않으면 재생부터, 돌고 있으면 일시정지부터다.
            if (false == context.editor->IsSimulationPlaying())
            {
                return WhyNoPause(context);
            }
            return Loc::TextOr(LocKeys::BlockedNotPaused, "pause the simulation to step one frame");
        }
        Bool DoStep(EditorActionContext& context)
        {
            context.editor->StepSimulation();
            return true;
        }

        // **게임 빌드**(D-232). 저장된 프로젝트로 빌드한다 - 파일로 연 프로젝트여야 하고 돌고 있지 않아야 한다.
        Bool CanBuildGame(const EditorActionContext& context)
        {
            return false == context.editor->GetProjectFilePath().empty() && false == context.editor->IsSimulationPlaying();
        }
        const char* WhyNoBuildGame(const EditorActionContext& context)
        {
            if (context.editor->GetProjectFilePath().empty())
            {
                return Loc::TextOr(LocKeys::BlockedProjectHasNoFile, "only a project opened from a file can be saved");
            }
            return Loc::TextOr(LocKeys::PopupSaveBlockedWhilePlaying, "stop the simulation before saving");
        }
        Bool DoBuildGame(EditorActionContext& context)
        {
            GameBuildReport report;
            context.editor->BuildGameForProject(report);
            // 빌드의 성패는 알림이 말한다. 행동은 눌렀으면 한 것이다.
            return true;
        }

        constexpr EditorShortcutBinding Bind(ImGuiKey key, Bool control = false, Bool shift = false)
        {
            return EditorShortcutBinding{key, control, shift, false};
        }

        EditorActionInfo Action(const char* name, const char* labelKey, const char* fallback, const char* categoryKey,
            UInt32 menus, Bool (*can)(const EditorActionContext&), const char* (*why)(const EditorActionContext&),
            Bool (*execute)(EditorActionContext&))
        {
            EditorActionInfo info;
            info.name = name;
            info.labelKey = labelKey;
            info.fallbackLabel = fallback;
            info.categoryKey = categoryKey;
            info.menus = menus;
            info.placedByEditor = true;
            info.CanExecute = can;
            info.WhyBlocked = why;
            info.Execute = execute;
            return info;
        }

        // 단축키로 부르는 할 일이다. 행동을 이름으로 다시 찾는다 - 표의 배열은 시험이 행동을 더 올리면 자리를 옮길 수 있다.
        class ActionShortcutHandler final : public IEditorShortcutHandler
        {
        public:
            explicit ActionShortcutHandler(const char* name)
                : m_name(name)
            {
            }
            Bool CanExecute(const EditorApplication& editor) const override
            {
                const EditorActionInfo* action = EditorActionRegistry::Get().Find(m_name);
                EditorActionContext context = MakeContext(editor);
                return action != nullptr && EditorActionUi::CanExecute(*action, context);
            }
            const char* WhyBlocked(const EditorApplication& editor) const override
            {
                const EditorActionInfo* action = EditorActionRegistry::Get().Find(m_name);
                EditorActionContext context = MakeContext(editor);
                return action != nullptr ? EditorActionUi::WhyBlocked(*action, context) : nullptr;
            }
            Bool Execute(EditorApplication& editor) override
            {
                const EditorActionInfo* action = EditorActionRegistry::Get().Find(m_name);
                EditorActionContext context = MakeContext(editor);
                return action != nullptr && EditorActionUi::Execute(*action, context);
            }

        private:
            static EditorActionContext MakeContext(const EditorApplication& editor)
            {
                EditorActionContext context;
                // 행동의 판정은 에디터를 고칠 수 있는 손으로 받는다. 단축키 관리자는 판정을 const 로 묻는다.
                context.editor = const_cast<EditorApplication*>(&editor);
                return context;
            }

            const char* m_name;
        };

        // 컴포넌트 메뉴 표에 올리는 그리기다. `user` 가 행동 이름이다.
        Bool DrawComponentAction(const ComponentMenuContext& menu)
        {
            const char* name = static_cast<const char*>(menu.user);
            const EditorActionInfo* action = EditorActionRegistry::Get().Find(name);
            if (action == nullptr)
            {
                return true;
            }
            EditorActionContext context;
            context.editor = menu.editor;
            context.component = menu.address;
            context.componentPointer = menu.component;
            context.placement = menu.placement;
            context.menu = EditorActionMenu::Component;
            const Bool ran = EditorActionUi::DrawItem(name, context);
            return false == (ran && action->mayRemoveObject);
        }
    }

    EditorActionRegistry& EditorActionRegistry::Get()
    {
        static EditorActionRegistry registry;
        return registry;
    }

    Bool EditorActionRegistry::Register(const EditorActionInfo& info)
    {
        if (IsEmptyName(info.name) || info.Execute == nullptr || Find(info.name) != nullptr)
        {
            return false;
        }
        if (info.panelType != nullptr && EditorPanelRegistry::Get().Find(info.panelType) == nullptr)
        {
            return false;
        }
        if ((info.menus & EditorActionMenu::Component) != 0 && info.componentType == InvalidComponentTypeId)
        {
            return false;
        }
        m_actions.Add(info);
        return true;
    }

    Bool EditorActionRegistry::Unregister(const char* name)
    {
        if (IsEmptyName(name))
        {
            return false;
        }
        for (std::size_t index = 0; index < m_actions.Size(); ++index)
        {
            if (std::strcmp(m_actions[index].name, name) == 0)
            {
                m_actions.RemoveAt(index);
                return true;
            }
        }
        return false;
    }

    const EditorActionInfo* EditorActionRegistry::Find(const char* name) const
    {
        if (IsEmptyName(name))
        {
            return nullptr;
        }
        for (std::size_t index = 0; index < m_actions.Size(); ++index)
        {
            if (std::strcmp(m_actions[index].name, name) == 0)
            {
                return &m_actions[index];
            }
        }
        return nullptr;
    }

    UInt32 EditorActionRegistry::GetCount() const
    {
        return static_cast<std::uint32_t>(m_actions.Size());
    }

    const EditorActionInfo& EditorActionRegistry::GetAt(UInt32 index) const
    {
        return m_actions[index];
    }

    void RegisterBuiltinEditorActions()
    {
        static Bool registered = false;
        if (registered)
        {
            return;
        }
        registered = true;
        // 행동은 패널 종류를 가리킨다. 표가 먼저 서야 한다.
        RegisterBuiltinEditorPanelTypes();

        EditorActionRegistry& actions = EditorActionRegistry::Get();
        using namespace EditorActionMenu;
        // **기본 조합은 기존 엔진과 같다.** 쓰던 사람이 손으로 기억하는 값이라, 바꿀 이유가 없으면 바꾸지 않는다.
        {
            EditorActionInfo info = Action("canvas.save", LocKeys::MenuSaveCanvas, "Save Canvas", LocKeys::MenuFile,
                File, &CanSave, &WhyNoSave, &DoSave);
            info.primary = Bind(ImGuiKey_S, true);
            // 저장은 예외다 - 글자를 치는 중에도 Ctrl+S 는 저장이어야 한다.
            info.whileTyping = true;
            info.icon = Icons::Save;
            actions.Register(info);
        }
        actions.Register(Action("game.build", LocKeys::MenuBuildGame, "Build Game", LocKeys::MenuFile, File,
            &CanBuildGame, &WhyNoBuildGame, &DoBuildGame));
        {
            EditorActionInfo info = Action("edit.undo", LocKeys::MenuUndo, "Undo", LocKeys::MenuEdit, Edit,
                &CanUndo, &WhyNoUndo, &DoUndo);
            info.primary = Bind(ImGuiKey_Z, true);
            actions.Register(info);
        }
        {
            EditorActionInfo info = Action("edit.redo", LocKeys::MenuRedo, "Redo", LocKeys::MenuEdit, Edit,
                &CanRedo, &WhyNoRedo, &DoRedo);
            info.primary = Bind(ImGuiKey_Y, true);
            info.secondary = Bind(ImGuiKey_Z, true, true);
            actions.Register(info);
        }
        {
            EditorActionInfo info = Action("object.create", LocKeys::HierarchyCreateObject, "Create Object", LocKeys::PanelHierarchy,
                Object | Background, &CanCreate, &WhyNoCreate, &DoCreate);
            info.objectMenuLabelKey = LocKeys::HierarchyCreateChild;
            info.objectMenuFallbackLabel = "Create Child";
            info.mayRemoveObject = true;
            actions.Register(info);
        }
        {
            EditorActionInfo info = Action("object.unparent", LocKeys::HierarchyUnparent, "Unparent", LocKeys::PanelHierarchy,
                Object, &CanUnparent, &WhyNoUnparent, &DoUnparent);
            info.IsShown = &ShowUnparent;
            // 부모가 바뀌면 지금 도는 자식 배열이 그 자리에서 달라진다.
            info.mayRemoveObject = true;
            actions.Register(info);
        }
        {
            EditorActionInfo info = Action("object.copy", LocKeys::HierarchyCopy, "Copy", LocKeys::MenuEdit, Edit | Object,
                &CanCopy, &WhyNoCopy, &DoCopy);
            info.primary = Bind(ImGuiKey_C, true);
            actions.Register(info);
        }
        {
            EditorActionInfo info = Action("object.paste", LocKeys::HierarchyPaste, "Paste", LocKeys::MenuEdit,
                Edit | Object | Background, &CanPaste, &WhyNoPaste, &DoPaste);
            info.primary = Bind(ImGuiKey_V, true);
            info.mayRemoveObject = true;
            actions.Register(info);
        }
        {
            // Shift 를 정확히 견주므로 Ctrl+Shift+V 가 위의 Ctrl+V 를 오발동시키지 않는다(기존과 같다).
            EditorActionInfo info = Action("object.paste_as_child", LocKeys::HierarchyPasteAsChild, "Paste As Child", LocKeys::MenuEdit,
                Edit | Object, &CanPasteAsChild, &WhyNoPasteAsChild, &DoPasteAsChild);
            info.primary = Bind(ImGuiKey_V, true, true);
            info.mayRemoveObject = true;
            actions.Register(info);
        }
        {
            EditorActionInfo info = Action("object.delete", LocKeys::HierarchyDelete, "Delete", LocKeys::MenuEdit, Edit | Object,
                &CanDelete, &WhyNoDelete, &DoDelete);
            info.primary = Bind(ImGuiKey_Delete);
            info.mayRemoveObject = true;
            actions.Register(info);
        }
        // **게임이 키를 받는 동안은 재생 제어만 남긴다**(D-214). 게임의 Delete 가 선택한 오브젝트를 지우고 Ctrl+Z 가
        // 편집을 되돌리면 안 된다.
        {
            EditorActionInfo info = Action("simulation.play", LocKeys::MenuSimulationPlay, "Play", LocKeys::MenuSimulation,
                Simulation, &CanPlay, &WhyNoPlay, &DoPlay);
            info.primary = Bind(ImGuiKey_F5);
            info.duringGame = true;
            actions.Register(info);
        }
        {
            EditorActionInfo info = Action("simulation.pause", LocKeys::MenuSimulationPause, "Pause", LocKeys::MenuSimulation,
                Simulation, &CanPause, &WhyNoPause, &DoPause);
            info.primary = Bind(ImGuiKey_F6);
            info.duringGame = true;
            info.icon = Icons::Pause;
            actions.Register(info);
        }
        {
            // 한 프레임 진행(D-242). 기존 엔진에 없던 것이라 재생·일시정지 다음 키를 준다.
            EditorActionInfo info = Action("simulation.step", LocKeys::MenuSimulationStep, "Step One Frame", LocKeys::MenuSimulation,
                Simulation, &CanStep, &WhyNoStep, &DoStep);
            info.primary = Bind(ImGuiKey_F7);
            info.duringGame = true;
            info.icon = Icons::StepFrame;
            actions.Register(info);
        }
        // 패널 종류의 행동은 그 패널이 올린다.
        CanvasViewPanel::RegisterActions();
        CommandPalette::RegisterAction();
    }

    namespace EditorActionUi
    {
        void ResolvePanel(const EditorActionInfo& action, EditorActionContext& context)
        {
            if (context.panel != nullptr || action.panelType == nullptr || context.editor == nullptr)
            {
                return;
            }
            Array<EditorPanel*> panels;
            context.editor->FindPanels(action.panelType, panels);
            for (EditorPanel* panel : panels)
            {
                if (panel->IsFocused())
                {
                    context.panel = panel;
                    return;
                }
            }
            context.panel = panels.IsEmpty() ? nullptr : panels[0];
        }

        Bool CanExecute(const EditorActionInfo& action, const EditorActionContext& context)
        {
            if (context.editor == nullptr)
            {
                return false;
            }
            EditorActionContext resolved = context;
            ResolvePanel(action, resolved);
            // 패널 종류의 행동인데 그 패널이 없으면 할 곳이 없다.
            if (action.panelType != nullptr && resolved.panel == nullptr)
            {
                return false;
            }
            return action.CanExecute == nullptr || action.CanExecute(resolved);
        }

        const char* WhyBlocked(const EditorActionInfo& action, const EditorActionContext& context)
        {
            if (CanExecute(action, context))
            {
                return nullptr;
            }
            EditorActionContext resolved = context;
            ResolvePanel(action, resolved);
            // 그 종류의 패널이 없어 못 하는 것은 행동마다 따로 말하지 않는다 - 판정이 그 자리에서 거절했다.
            if (action.panelType != nullptr && resolved.panel == nullptr)
            {
                return Loc::TextOr(LocKeys::BlockedPanelNotOpen, "the window this works in is not open");
            }
            return action.WhyBlocked != nullptr && resolved.editor != nullptr ? action.WhyBlocked(resolved) : nullptr;
        }

        Bool Execute(const EditorActionInfo& action, EditorActionContext& context)
        {
            if (false == CanExecute(action, context))
            {
                return false;
            }
            ResolvePanel(action, context);
            return action.Execute(context);
        }

        const char* Label(const EditorActionInfo& action, UInt32 menu)
        {
            if (menu == EditorActionMenu::Object && action.objectMenuLabelKey != nullptr)
            {
                return Loc::TextOr(action.objectMenuLabelKey, action.objectMenuFallbackLabel);
            }
            return Loc::TextOr(action.labelKey, action.fallbackLabel != nullptr ? action.fallbackLabel : action.name);
        }

        EditorShortcutText Keys(const EditorApplication& editor, const EditorActionInfo& action)
        {
            return EditorShortcutManager::Describe(editor.GetShortcuts().Find(action.name).primary);
        }

        Bool DrawItem(const char* name, EditorActionContext& context, const char* label, const char* icon)
        {
            const EditorActionInfo* action = EditorActionRegistry::Get().Find(name);
            if (action == nullptr || context.editor == nullptr)
            {
                return false;
            }
            ResolvePanel(*action, context);
            if (action->IsShown != nullptr && false == action->IsShown(context))
            {
                return false;
            }
            // **글자도 할 수 있는지도 표에서 온다**(D-132·D-228). 메뉴에 박아 두면 키를 바꿨을 때 화면만 옛 글자로 남는다.
            // 잠긴 까닭도 같은 표에서 온다(D-181). 항목마다 가이드의 행동 이름을 단다(D-268) - 행동 이름이 곧 표식이다.
            const Bool enabled = CanExecute(*action, context);
            const EditorShortcutText keys = Keys(*context.editor, *action);
            Widget::SetNextItemTarget(GuideFocusTargets::Action(action->name));
            const Bool chosen = Widget::MenuItem(label != nullptr ? label : Label(*action, context.menu), keys.value, enabled,
                enabled ? nullptr : WhyBlocked(*action, context), icon != nullptr ? icon : action->icon);
            return chosen && Execute(*action, context);
        }

        Bool DrawExtensions(UInt32 menu, EditorActionContext& context)
        {
            const EditorActionRegistry& actions = EditorActionRegistry::Get();
            Bool separated = false;
            for (UInt32 index = 0; index < actions.GetCount(); ++index)
            {
                const EditorActionInfo& action = actions.GetAt(index);
                if (action.placedByEditor || (action.menus & menu) == 0 || action.componentType != InvalidComponentTypeId)
                {
                    continue;
                }
                if (action.IsShown != nullptr)
                {
                    EditorActionContext probe = context;
                    ResolvePanel(action, probe);
                    if (false == action.IsShown(probe))
                    {
                        continue;
                    }
                }
                if (false == separated)
                {
                    ImGui::Separator();
                    separated = true;
                }
                EditorActionContext itemContext = context;
                if (DrawItem(action.name, itemContext) && action.mayRemoveObject)
                {
                    return false;
                }
            }
            return true;
        }

        void RegisterShortcuts(EditorShortcutManager& shortcuts)
        {
            const EditorActionRegistry& actions = EditorActionRegistry::Get();
            for (UInt32 index = 0; index < actions.GetCount(); ++index)
            {
                const EditorActionInfo& action = actions.GetAt(index);
                EditorShortcutDesc desc;
                desc.id = action.name;
                desc.labelKey = action.labelKey;
                desc.categoryKey = action.categoryKey;
                desc.scope = action.panelType;
                desc.primary = action.primary;
                desc.secondary = action.secondary;
                desc.blocksGlobal = action.blocksGlobal;
                desc.whileTyping = action.whileTyping;
                desc.duringGame = action.duringGame;
                desc.handler = MakeOwnerPtr<ActionShortcutHandler>(action.name);
                shortcuts.Register(std::move(desc));
            }
        }

        void RegisterComponentMenus(ComponentMenuTable& menus)
        {
            const EditorActionRegistry& actions = EditorActionRegistry::Get();
            for (UInt32 index = 0; index < actions.GetCount(); ++index)
            {
                const EditorActionInfo& action = actions.GetAt(index);
                if ((action.menus & EditorActionMenu::Component) == 0)
                {
                    continue;
                }
                // 행동마다 표지가 다르다 - 같은 그리기 함수로 같은 타입에 여럿을 올려도 겹친 것으로 보지 않게.
                menus.Register(action.componentType, &DrawComponentAction, action.name,
                    const_cast<char*>(action.name));
            }
        }
    }
}

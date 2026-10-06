#include <JBro/Editor/EditorShortcuts.h>

#include <JBro/Editor/EditorActionRegistry.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Types/Bool.h>

namespace JBro::EditorShortcuts
{
    namespace
    {
        // 열거 차례 그대로다. 판정과 할 일은 행동 표(D-284)에 있고, 여기는 열거를 행동 이름으로 바꿀 뿐이다.
        constexpr const char* Names[] = {
            "canvas.save",
            "edit.undo",
            "edit.redo",
            "object.copy",
            "object.paste",
            "object.paste_as_child",
            "object.delete",
            "simulation.play",
            "simulation.pause",
            "simulation.step",
        };
        static_assert(sizeof(Names) / sizeof(Names[0]) == static_cast<std::size_t>(EditorShortcut::Count),
            "every shortcut needs a name, or ActionId returns the wrong one");

        const EditorActionInfo* Find(EditorShortcut id)
        {
            return EditorActionRegistry::Get().Find(ActionId(id));
        }

        EditorActionContext Context(const EditorApplication& editor)
        {
            EditorActionContext context;
            context.editor = const_cast<EditorApplication*>(&editor);
            return context;
        }
    }

    const char* ActionId(EditorShortcut id)
    {
        const std::size_t index = static_cast<std::size_t>(id);
        return Names[index < static_cast<std::size_t>(EditorShortcut::Count) ? index : 0];
    }

    Bool CanExecute(const EditorApplication& editor, EditorShortcut id)
    {
        const EditorActionInfo* action = Find(id);
        return action != nullptr && EditorActionUi::CanExecute(*action, Context(editor));
    }

    const char* WhyBlocked(const EditorApplication& editor, EditorShortcut id)
    {
        const EditorActionInfo* action = Find(id);
        return action != nullptr ? EditorActionUi::WhyBlocked(*action, Context(editor)) : nullptr;
    }

    Bool Execute(EditorApplication& editor, EditorShortcut id)
    {
        const EditorActionInfo* action = Find(id);
        EditorActionContext context = Context(editor);
        return action != nullptr && EditorActionUi::Execute(*action, context);
    }

    EditorShortcutText Describe(const EditorApplication& editor, EditorShortcut id)
    {
        return EditorShortcutManager::Describe(editor.GetShortcuts().Find(ActionId(id)).primary);
    }
}

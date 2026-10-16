#pragma once
#include <JBro/Editor/Agent/Provider.h>

namespace JBro { class EditorApplication; }
namespace JBro::Agent
{
    class Tools final
    {
    public:
        explicit Tools(EditorApplication& editor);
        ~Tools();
        String Context();
        Bool IsTargetValid(const Target& target);
        ToolResult Execute(const String& name, const String& arguments);
        static String Definitions();
    private:
        struct State;
        OwnerPtr<State> m_state;
    };
}

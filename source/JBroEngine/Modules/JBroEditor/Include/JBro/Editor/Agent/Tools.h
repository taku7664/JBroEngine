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
        String Context(ArrayView<const Target> attachments);
        Bool MakeObjectTarget(UInt64 editorObject, Target& target);
        Bool MakeComponentTarget(UInt64 editorObject, UInt64 instance, Target& target);
        Bool MakeAssetTarget(AssetId asset, Target& target);
        Bool IsTargetValid(const Target& target);
        ToolResult Execute(const String& name, const String& arguments);
        static String Definitions();
    private:
        struct State;
        OwnerPtr<State> m_state;
    };
}

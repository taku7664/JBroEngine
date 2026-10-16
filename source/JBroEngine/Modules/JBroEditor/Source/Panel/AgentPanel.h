#pragma once
#include <JBro/Editor/EditorPanel.h>
#include <JBro/Editor/Agent/Provider.h>
#include <JBro/Editor/Agent/Tools.h>

namespace JBro
{
    class AgentPanel final : public UniquePanel
    {
    public:
        static constexpr const char* TypeName = "Agent";
        const char* GetTitle() const override { return TypeName; }
        const char* GetDisplayTitle() const override;
        EditorDock GetPreferredDock() const override { return EditorDock::Right; }
        Bool OnCreate(EditorApplication& editor) override;
        void OnDestroy() override;
        void OnUpdate(Float elapsed) override;
        void OnDraw() override;
        Agent::CodexProvider& GetProvider() { return m_provider; }
    private:
        void Connect();
        void Load();
        void Save();
        EditorApplication* m_editor = nullptr;
        OwnerPtr<Agent::Tools> m_tools;
        Agent::CodexProvider m_provider;
        String m_project, m_executable, m_thread, m_input, m_apiKey, m_settingsPath, m_openedAuth, m_model;
        UInt64 m_scopeRevision = 0;
        std::size_t m_seenMessages = 0, m_seenTailBytes = 0;
    };
}

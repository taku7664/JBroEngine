#include "AgentPanel.h"
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorPaths.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/AgentChat.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/TextField.h>
#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Core/Yaml.h>
#include <JBro/Platform/Platform.h>

namespace JBro
{
    const char* AgentPanel::GetDisplayTitle() const { return Loc::TextOr(LocKeys::PanelAgent, "AI Assistant"); }
    Bool AgentPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        m_project = editor.GetProjectFilePath();
        m_scopeRevision = editor.GetAgentContextRevision();
        m_tools = MakeOwnerPtr<Agent::Tools>(editor);
        Load();
        return true;
    }
    void AgentPanel::OnDestroy()
    {
        Save();
        m_provider.Stop();
        m_apiKey.clear();
        if (m_editor != nullptr)
        {
            m_editor->SetAgentGuidePaused(false);
        }
    }
    void AgentPanel::Load()
    {
        m_thread.clear();
        m_model.clear();
        m_settingsPath.clear();
        m_executable = Agent::CodexProvider::Locate(m_editor->GetPlatform());
        const String preferences = m_editor->GetPreferencesPath();
        if (preferences.empty() || m_project.empty())
        {
            return;
        }
        m_settingsPath = preferences + ".agent-" + std::to_string(MakeNameId(m_project.c_str()).Value) + ".yaml";
        Array<std::byte> bytes;
        auto& platform = m_editor->GetPlatform();
        if (platform.ReadWholeFile(m_settingsPath.c_str(), bytes) && bytes.Size() < 64 * 1024)
        {
            YamlDocument document;
            YamlError error;
            if (document.Parse(reinterpret_cast<const char*>(bytes.Data()), bytes.Size(), error))
            {
                String project;
                if (document.FindScalar(document.GetRoot(), "Project", project) && project == m_project)
                {
                    document.FindScalar(document.GetRoot(), "CodexExecutable", m_executable);
                    document.FindScalar(document.GetRoot(), "Thread", m_thread);
                    document.FindScalar(document.GetRoot(), "Model", m_model);
                }
            }
        }
    }
    void AgentPanel::Save()
    {
        if (m_editor == nullptr || m_settingsPath.empty())
        {
            return;
        }
        YamlWriter writer;
        writer.WriteString("Project", m_project.c_str());
        writer.WriteString("CodexExecutable", m_executable.c_str());
        writer.WriteString("Thread", m_thread.c_str());
        writer.WriteString("Model", m_model.c_str());
        const auto& text = writer.GetText();
        const String temporary = m_settingsPath + ".tmp";
        auto& platform = m_editor->GetPlatform();
        if (platform.WriteWholeFile(temporary.c_str(), {reinterpret_cast<const std::byte*>(text.data()), static_cast<UInt32>(text.size())}))
        {
            platform.MoveFileTo(temporary.c_str(), m_settingsPath.c_str());
        }
    }
    void AgentPanel::Connect()
    {
        Agent::Settings settings;
        settings.executable = m_executable;
        settings.project = EditorPaths::FolderOf(m_project.c_str());
        settings.thread = m_thread;
        settings.model = m_model;
        settings.skillsRoot = m_editor->GetAgentSkillsRoot();
        settings.toolDefinitions = Agent::Tools::Definitions();
        settings.instructions = Agent::CodexProvider::Instructions();
        m_openedAuth.clear();
        m_seenMessages = 0;
        m_provider.Start(m_editor->GetPlatform(), settings, Agent::CodexProvider::ToolHandler::Bind<&Agent::Tools::Execute>(m_tools.Get()));
        Save();
    }
    void AgentPanel::OnUpdate(Float elapsed)
    {
        if (m_editor->GetProjectFilePath() != m_project)
        {
            Save();
            m_provider.ClearConversation();
            m_apiKey.clear();
            m_input.clear();
            m_project = m_editor->GetProjectFilePath();
            m_tools = MakeOwnerPtr<Agent::Tools>(*m_editor);
            Load();
        }
        if (m_scopeRevision != m_editor->GetAgentContextRevision())
        {
            m_scopeRevision = m_editor->GetAgentContextRevision();
            m_provider.Interrupt();
            m_tools = MakeOwnerPtr<Agent::Tools>(*m_editor);
            // The callback must never retain the old Tools instance.
            m_provider.Stop();
        }
        m_provider.Poll(elapsed);
        if (!m_provider.GetThread().empty() && m_provider.GetThread() != m_thread)
        {
            m_thread = m_provider.GetThread();
            Save();
        }
        if (m_provider.GetConnection() == Agent::Connection::Ready && m_model != m_provider.GetModel())
        {
            m_model = m_provider.GetModel();
            Save();
        }
        const String url = m_provider.GetAuthUrl();
        if (!url.empty() && url != m_openedAuth && (url.StartsWith("https://auth.openai.com/") || url.StartsWith("https://chatgpt.com/")))
        {
            m_openedAuth = url;
            m_editor->GetPlatform().OpenPathWithShell(url.c_str());
        }
        if (!IsOpen() && m_editor->IsAgentGuidePaused())
        {
            m_editor->SetAgentGuidePaused(false);
        }
    }
    void AgentPanel::OnDraw()
    {
        const auto connection = m_provider.GetConnection();
        {
            Widget::FormLayout form("##agent.connection");
            form.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::AgentExecutable, "Codex executable")), [&]() {
                Widget::TextField("##agent.executable", m_executable).MaxLength(2047)();
            });
        }
        if (connection == Agent::Connection::Stopped || connection == Agent::Connection::Failed)
        {
            if (Widget::ActionButton(Loc::TextOr(LocKeys::AgentConnect, "Connect"), Widget::Severity::Success, !m_project.empty(),
                Loc::TextOr(LocKeys::AgentProjectRequired, "Open a project first.")))
            {
                Connect();
            }
            if (Widget::Button(Loc::TextOr(LocKeys::AgentInstall, "Codex installation guide")))
            {
                m_editor->GetPlatform().OpenPathWithShell("https://learn.chatgpt.com/docs/cli");
            }
        }
        else if (connection == Agent::Connection::Starting)
        {
            Widget::HintText(Loc::TextOr(LocKeys::AgentConnecting, "Connecting to Codex..."));
        }
        else if (connection == Agent::Connection::SignedOut)
        {
            if (Widget::Button(Loc::TextOr(LocKeys::AgentLoginChatgpt, "Sign in with ChatGPT")))
            {
                m_provider.LoginChatGPT();
            }
            Widget::FormLayout form("##agent.auth");
            form.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::AgentApiKey, "API key")), [&]() {
                Widget::TextField("##agent.api_key", m_apiKey).Password().MaxLength(2047)();
            });
            if (Widget::ActionButton(Loc::TextOr(LocKeys::AgentLoginKey, "Sign in with API key"), Widget::Severity::Info, !m_apiKey.empty()))
            {
                m_provider.LoginApiKey(m_apiKey);
                std::fill(m_apiKey.begin(), m_apiKey.end(), '\0');
                m_apiKey.clear();
            }
        }
        if (!m_provider.GetError().empty())
        {
            Widget::SeverityTextF(Widget::Severity::Error, "%s", m_provider.GetError().c_str());
        }
        if (!m_provider.GetModels().IsEmpty())
        {
            Array<const char*> labels;
            Int32 selected = -1;
            for (const auto& model : m_provider.GetModels())
            {
                if (model.id == m_provider.GetModel())
                {
                    selected = static_cast<Int32>(labels.Size());
                }
                labels.Add(model.label.c_str());
            }
            Widget::DisableScope disabled(connection != Agent::Connection::Ready);
            Widget::FormLayout form("##agent.model");
            form.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::AgentModel, "Model")), [&]() {
                if (Widget::FilterCombo("##agent.model", {labels.Data(), static_cast<UInt32>(labels.Size())}, selected)())
                {
                    m_provider.SetModel(m_provider.GetModels()[selected.Value].id);
                }
            });
        }
        if (connection == Agent::Connection::Running)
        {
            if (Widget::Button(Loc::TextOr(LocKeys::AgentStop, "Stop")))
            {
                m_provider.Interrupt();
            }
        }
        if (m_editor->IsAgentGuidePaused() && Widget::Button(Loc::TextOr(LocKeys::AgentResumeGuide, "Resume guide")))
        {
            m_editor->SetAgentGuidePaused(false);
        }
        if (connection != Agent::Connection::Stopped && Widget::Button(Loc::TextOr(LocKeys::AgentDisconnect, "Disconnect")))
        {
            m_provider.Stop();
        }
        if (Widget::Button(Loc::TextOr(LocKeys::AgentNewChat, "New conversation")))
        {
            m_provider.ClearConversation();
            m_thread.clear();
            Save();
        }
        const auto& messages = m_provider.Messages();
        const auto tailBytes = messages.IsEmpty() ? std::size_t(0) : messages[messages.Size() - 1].text.size();
        Widget::AgentHistory(messages, m_seenMessages != messages.Size() || m_seenTailBytes != tailBytes, m_tools->Context(),
            Delegate<Bool(const Agent::Target&)>::Bind<&Agent::Tools::IsTargetValid>(m_tools.Get()));
        m_seenMessages = messages.Size();
        m_seenTailBytes = tailBytes;
        Widget::TextField("##agent.input", m_input).Multiline(true, 3.0f)();
        if (Widget::ActionButton(Loc::TextOr(LocKeys::AgentSend, "Send"), Widget::Severity::Success,
            connection == Agent::Connection::Ready && !m_input.empty(), Loc::TextOr(LocKeys::AgentNotReady, "Connect and sign in before sending a message.")))
        {
            if (m_provider.Submit(m_input, m_tools->Context()))
            {
                m_input.clear();
            }
        }
    }
}

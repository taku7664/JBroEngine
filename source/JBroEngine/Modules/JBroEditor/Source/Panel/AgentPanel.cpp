#include "AgentPanel.h"
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorPaths.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/AgentChat.h>
#include <JBro/Editor/Widget/DragDrop.h>
#include <JBro/Editor/Widget/AssetDrag.h>
#include <JBro/Editor/Widget/ComponentDrag.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Runtime/GameObject.h>
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
            // The callback must never retain the old Tools instance.
            m_provider.Stop();
            m_tools = MakeOwnerPtr<Agent::Tools>(*m_editor);
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
    void AgentPanel::Attach(const Agent::Target& target)
    {
        if (!m_tools->IsTargetValid(target))
        {
            m_attachmentError = Loc::Text(LocKeys::AgentInvalidTarget);
            return;
        }
        for (const auto& existing : m_attachments)
        {
            if (existing.Equals(target))
            {
                return;
            }
        }
        if (m_attachments.Size() >= 16)
        {
            m_attachmentError = Loc::Text(LocKeys::AgentAttachmentLimit);
            return;
        }
        m_attachments.Add(target);
    }
    void AgentPanel::ReceiveAttachments()
    {
        if (!Widget::BeginDropTarget())
        {
            return;
        }
        Agent::Target target;
        EditorObjectId object = 0;
        if (Widget::AcceptDropValue(Widget::DragKind::HierarchyObject, object))
        {
            m_attachmentError.clear();
            if (m_tools->MakeObjectTarget(object, target))
            {
                Attach(target);
            }
            else
            {
                m_attachmentError = Loc::Text(LocKeys::AgentInvalidTarget);
            }
        }
        Widget::ComponentDragPayload component;
        if (Widget::AcceptDropValue(Widget::DragKind::InspectorComponent, component))
        {
            m_attachmentError.clear();
            if (m_tools->MakeComponentTarget(component.object, component.instance, target))
            {
                Attach(target);
            }
            else
            {
                m_attachmentError = Loc::Text(LocKeys::AgentInvalidTarget);
            }
        }
        Widget::AssetDragHeader asset;
        String paths;
        if (Widget::AcceptAssetDrop(asset, &paths))
        {
            m_attachmentError.clear();
            if (m_tools->MakeAssetTarget(asset.primary, target))
            {
                Attach(target);
                std::size_t start = 0;
                while (start < paths.size())
                {
                    const auto next = paths.find('\n', start);
                    const String path = paths.substr(start, next == String::npos ? String::npos : next - start);
                    const auto* record = m_editor->GetAssetRegistry().FindByPath(path);
                    if (record != nullptr && m_tools->MakeAssetTarget(record->id, target))
                    {
                        Attach(target);
                    }
                    if (next == String::npos)
                    {
                        break;
                    }
                    start = next + 1;
                }
            }
            else
            {
                m_attachmentError = Loc::Text(LocKeys::AgentInvalidTarget);
            }
        }
        Widget::EndDropTarget();
    }
    void AgentPanel::OnDraw()
    {
        const auto& messages = m_provider.Messages();
        const UInt64 tailBytes = messages.IsEmpty() ? UInt64(0) : UInt64(messages[messages.Size() - 1].text.size());
        Widget::AgentChatView view{messages, m_attachments, m_provider.GetModels()};
        const auto fileName = [](const String& path) -> String {
            const auto at = path.find_last_of("/\\");
            return at == String::npos ? path : String(path.substr(at + 1));
        };
        view.project = fileName(m_project);
        const auto extension = view.project.find_last_of('.');
        if (extension != String::npos)
        {
            view.project.resize(extension);
        }
        view.canvas = fileName(m_editor->GetCanvasPath());
        view.projectOpen = !m_project.empty();
        view.connection = m_provider.GetConnection();
        view.model = m_provider.GetModel();
        view.error = m_provider.GetError();
        view.attachmentError = m_attachmentError;
        view.changed = m_seenMessages != messages.Size() || m_seenTailBytes != tailBytes;
        view.validate = Delegate<Bool(const Agent::Target&)>::Bind<&Agent::Tools::IsTargetValid>(m_tools.Get());
        view.receiveDrop = Delegate<void()>::Bind<&AgentPanel::ReceiveAttachments>(this);
        view.guidePaused = m_editor->IsAgentGuidePaused();
        if (const auto* guide = m_editor->GetGuide().GetGuide())
        {
            view.guideTitle = Loc::TextFor(guide->title.key, guide->title.string, guide->title.locale);
            const auto& path = m_editor->GetGuideFocus().GetPath();
            for (UInt32 index = 0; index < path.count; ++index)
            {
                const auto& target = path.targets[index.Value];
                if (target.name == GuideFocusTargets::HierarchyObject(0).name
                    || target.name == GuideFocusTargets::HierarchyObjectMenu(0).name
                    || target.name == GuideFocusTargets::CanvasViewObject(0).name)
                {
                    if (const auto* object = m_editor->GetObjectIds().Resolve(target.key))
                    {
                        view.guideTarget = String(Loc::Text(LocKeys::AgentGuideTarget)) + " · " + object->GetName();
                    }
                }
            }
        }
        m_seenMessages = messages.Size();
        m_seenTailBytes = tailBytes;
        const auto result = Widget::AgentChat(view, m_settingsOpen, m_input, m_executable, m_apiKey);
        if (result.removed >= 0 && static_cast<std::size_t>(result.removed.Value) < m_attachments.Size())
        {
            m_attachments.RemoveAt(result.removed.Value);
            m_attachmentError.clear();
        }
        if (result.model >= 0 && static_cast<std::size_t>(result.model.Value) < m_provider.GetModels().Size())
        {
            m_provider.SetModel(m_provider.GetModels()[result.model.Value].id);
        }
        using Action = Widget::AgentChatAction;
        switch (result.action)
        {
        case Action::Connect:
            Connect();
            break;
        case Action::Disconnect:
            m_provider.Stop();
            break;
        case Action::Install:
            m_editor->GetPlatform().OpenPathWithShell("https://learn.chatgpt.com/docs/cli");
            break;
        case Action::LoginChatGPT:
            m_provider.LoginChatGPT();
            break;
        case Action::LoginKey:
            m_provider.LoginApiKey(m_apiKey);
            std::fill(m_apiKey.begin(), m_apiKey.end(), '\0');
            m_apiKey.clear();
            break;
        case Action::NewChat:
            m_provider.ClearConversation();
            m_thread.clear();
            m_input.clear();
            m_attachments.Clear();
            m_attachmentError.clear();
            Save();
            break;
        case Action::Stop:
            m_provider.Interrupt();
            break;
        case Action::ResumeGuide:
            m_editor->SetAgentGuidePaused(false);
            break;
        case Action::Browse:
            {
                PathBrowseRequest request;
                request.folder = false;
                request.relative = false;
                request.filterName = Loc::Text(LocKeys::AgentExecutable);
                request.filterPattern = "*.exe";
                request.deliver = [](void* user, const String& path) { *static_cast<String*>(user) = path; };
                request.user = &m_executable;
                m_editor->RequestBrowsePath(request);
            }
            break;
        case Action::Send:
            if (m_provider.Submit(m_input, m_tools->Context(m_attachments.View())))
            {
                m_input.clear();
                m_attachments.Clear();
                m_attachmentError.clear();
            }
            break;
        default:
            break;
        }
    }
}

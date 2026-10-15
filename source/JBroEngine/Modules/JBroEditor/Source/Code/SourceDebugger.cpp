#include <JBro/Editor/Code/SourceDebugger.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorPaths.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Host/ScriptDLLLoader.h>
#include "../Panel/CodeEditorPanel.h"

namespace JBro::Code
{
    SourceDebugger::SourceDebugger(EditorApplication& editor) : m_editor(&editor), m_client(editor.GetPlatform()) {}
    SourceDebugger::~SourceDebugger()
    {
        if (m_ownsChannel)
        {
            m_editor->GetPlatform().ClearCodeBreakpoints();
        }
    }
    Bool SourceDebugger::Read(const String& path, String& text) const
    {
        Array<std::byte> bytes;
        if (!m_editor->GetPlatform().ReadWholeFile(path.c_str(), bytes) || bytes.Size() > 8 * 1024 * 1024)
        {
            return false;
        }
        text = bytes.IsEmpty() ? String() : String(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
        if (text.StartsWith("\xef\xbb\xbf"))
        {
            text.erase(0, 3);
        }
        text = text.ReplaceAll("\r\n", "\n");
        return true;
    }
    Bool SourceDebugger::Capture(Array<Source>& sources)
    {
        sources.Clear();
        Array<String> folders, files;
        m_editor->CollectScriptFiles(folders, files);
        for (const auto& file : files)
        {
            Source source;
            source.path = LanguageClient::NormalizePath(EditorPaths::JoinPath(m_editor->GetScriptRoot().c_str(), file.c_str()));
            if (!Read(source.path, source.text))
            {
                return false;
            }
            sources.Add(std::move(source));
        }
        return true;
    }
    Bool SourceDebugger::SourcesCurrent(const Array<Source>& sources) const
    {
        Array<EditorPanel*> panels;
        m_editor->FindPanels(CodeEditorPanel::TypeName, panels);
        for (const auto& source : sources)
        {
            String disk;
            if (!Read(source.path, disk) || disk != source.text)
            {
                return false;
            }
            for (auto* panel : panels)
            {
                const auto* code = static_cast<CodeEditorPanel*>(panel);
                if (code->GetPath() == source.path && code->GetBufferText() != source.text)
                {
                    return false;
                }
            }
        }
        return true;
    }
    void SourceDebugger::BuildStarted()
    {
        m_buildReady = false;
        m_building = true;
        m_buildValid = Capture(m_buildSources);
    }
    void SourceDebugger::BuildFinished(Bool succeeded)
    {
        m_building = false;
        m_buildReady = succeeded && m_buildValid;
        if (m_buildReady && !m_editor->IsScriptReloadPending())
        {
            AcceptBuild();
        }
        else if (m_requested && (!succeeded || !m_buildValid))
        {
            Fail(Loc::TextOr(LocKeys::CodeEditorDebugBuildFailed, "Debugging did not start because the script build failed."));
        }
    }
    void SourceDebugger::ModuleReloaded(Bool succeeded)
    {
        if (succeeded && m_buildReady && !m_building)
        {
            AcceptBuild();
        }
        else if (!m_building && IsActive())
        {
            Fail(Loc::TextOr(LocKeys::CodeEditorDebugSourceMismatch, "The loaded library has no matching source snapshot. Start debugging again."));
        }
    }
    void SourceDebugger::AcceptBuild()
    {
        const auto* module = m_editor->GetScriptModule();
        if (!m_editor->IsScriptModuleLoaded() || module == nullptr || !SourcesCurrent(m_buildSources))
        {
            m_buildReady = false;
            if (m_requested)
            {
                Fail(Loc::TextOr(LocKeys::CodeEditorDebugSourceMismatch, "The source changed during the build or the new library did not load."));
            }
            return;
        }
        m_sources = m_buildSources;
        m_module = module->GetLoadedPath();
        m_buildReady = false;
        m_sentRevision = UInt64(-1);
    }
    void SourceDebugger::Start()
    {
        if (IsActive())
        {
            return;
        }
        if (m_editor->IsScriptDebugServerActive())
        {
            m_editor->GetNotifications().Notify(NotificationLevel::Error,
                Loc::TextOr(LocKeys::NotifyDebugChannelFailed, "Debugging could not start"),
                Loc::TextOr(LocKeys::CodeEditorDebugBusy, "Another debugger already owns the engine debug channel."));
            return;
        }
        if (m_editor->IsSimulationPlaying())
        {
            m_editor->StopSimulation();
        }
        m_requested = true;
        m_attaching = false;
        m_started = false;
        m_stopping = false;
        if (!m_editor->BuildScripts())
        {
            Fail(Loc::TextOr(LocKeys::CodeEditorDebugBuildFailed, "Debugging did not start because the script build could not start."));
        }
    }
    void SourceDebugger::Fail(const String& error)
    {
        m_editor->GetNotifications().Notify(NotificationLevel::Error,
            Loc::TextOr(LocKeys::NotifyDebugChannelFailed, "Debugging could not start"), error.c_str());
        Stop();
    }
    void SourceDebugger::Stop()
    {
        m_requested = false;
        m_attaching = false;
        m_started = false;
        if (m_editor->IsSimulationPlaying())
        {
            m_editor->StopSimulation();
        }
        m_client.Stop();
        if (m_ownsChannel)
        {
            m_editor->GetPlatform().ClearCodeBreakpoints();
        }
        m_stopping = m_client.GetState() == DebugState::Stopping;
        if (!m_stopping && m_ownsChannel)
        {
            m_editor->StopScriptDebugServer();
            m_ownsChannel = false;
        }
    }
    Bool SourceDebugger::IsActive() const { return m_requested || m_attaching || m_started || m_stopping; }
    Bool SourceDebugger::CanPlay() const
    {
        return m_client.GetState() == DebugState::Ready && !m_building && !m_buildReady && !m_stopping
            && SourcesCurrent(m_sources);
    }
    Breakpoints& SourceDebugger::Anchors() { return m_anchors; }
    const DebugClient& SourceDebugger::Client() const { return m_client; }
    Bool SourceDebugger::Matches(const String& path, const String& text) const
    {
        for (const auto& source : m_sources)
        {
            if (source.path == path)
            {
                return source.text == text && source.current;
            }
        }
        return false;
    }
    void SourceDebugger::Poll(Float elapsed)
    {
        m_client.Poll(elapsed);
        if (m_stopping)
        {
            if (m_client.GetState() != DebugState::Stopping)
            {
                if (m_ownsChannel)
                {
                    m_editor->StopScriptDebugServer();
                    m_ownsChannel = false;
                }
                m_stopping = false;
            }
            return;
        }
        if (!IsActive())
        {
            return;
        }
        if (m_requested && !m_attaching && !m_started)
        {
            if (m_editor->GetScriptBuildState() == EditorApplication::ScriptBuildState::Running || m_buildReady)
            {
                return;
            }
            const auto* module = m_editor->GetScriptModule();
            if (m_module.empty() || module == nullptr || module->GetLoadedPath() != m_module || !SourcesCurrent(m_sources))
            {
                Fail(Loc::TextOr(LocKeys::CodeEditorDebugSourceMismatch, "Debugging requires the successfully built source and loaded library to match."));
                return;
            }
            const String folder = m_editor->GetPlatform().GetExecutableFolder();
            String adapter = EditorPaths::JoinPath(folder.c_str(), "DebugAdapter/JBroDebugAdapter.exe");
            if (!m_editor->GetPlatform().FileExists(adapter.c_str()))
            {
                adapter = EditorPaths::JoinPath(folder.c_str(), "JBroDebugAdapter.exe");
            }
            if (!m_editor->StartScriptDebugServer(m_editor->GetPlatform().GetProcessId()))
            {
                Fail(Loc::TextOr(LocKeys::CodeEditorDebugBusy, "The debug channel is already in use."));
                return;
            }
            m_ownsChannel = true;
            if (!m_client.Start(adapter, m_editor->GetPlatform().GetProcessId()))
            {
                Fail(m_client.GetError());
                return;
            }
            for (auto& source : m_sources)
            {
                source.current = true;
                source.synced = true;
                if (source.path.EndsWith(".jscript"))
                {
                    m_client.SetBreakpoints(source.path, m_anchors.Lines(source.path));
                }
            }
            m_sentRevision = m_anchors.GetRevision();
            m_client.Configure();
            m_attaching = true;
        }
        if (m_client.GetState() == DebugState::Failed || m_client.GetState() == DebugState::Idle)
        {
            Fail(m_client.GetError());
            return;
        }
        m_sourceAge += elapsed;
        if (m_sourceAge >= 0.25f || m_sentRevision != m_anchors.GetRevision())
        {
            m_sourceAge = 0.0f;
            const auto* module = m_editor->GetScriptModule();
            if (module == nullptr || module->GetLoadedPath() != m_module)
            {
                Fail(Loc::TextOr(LocKeys::CodeEditorDebugSourceMismatch, "The loaded library no longer matches the debug source snapshot."));
                return;
            }
            for (auto& source : m_sources)
            {
                if (!source.path.EndsWith(".jscript"))
                {
                    continue;
                }
                Array<Source> one;
                one.Add(source);
                const Bool current = SourcesCurrent(one);
                if (!source.synced || source.current != current || m_sentRevision != m_anchors.GetRevision())
                {
                    source.current = current;
                    source.synced = true;
                    m_client.SetBreakpoints(source.path, current ? m_anchors.Lines(source.path) : Array<UInt32>());
                }
            }
            m_sentRevision = m_anchors.GetRevision();
        }
        if (m_attaching && m_client.GetState() == DebugState::Ready)
        {
            m_attaching = false;
            m_requested = false;
            m_started = true;
            if (!SourcesCurrent(m_sources) || !m_editor->StartSimulation())
            {
                Fail(Loc::TextOr(LocKeys::CodeEditorDebugSourceMismatch, "Debugging could not begin with the current source."));
            }
            else
            {
                m_editor->GetNotifications().Notify(NotificationLevel::Info,
                    Loc::TextOr(LocKeys::NotifyDebugStarted, "Debugging started."));
            }
        }
    }
}

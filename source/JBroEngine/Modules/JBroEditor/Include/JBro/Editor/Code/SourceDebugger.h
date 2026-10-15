#pragma once
#include <JBro/Editor/Code/Breakpoints.h>
#include <JBro/Editor/Code/DebugClient.h>

namespace JBro { class EditorApplication; }
namespace JBro::Code
{
    // Owns native-editor debugging orchestration; the adapter owns address resolution.
    class SourceDebugger final
    {
    public:
        explicit SourceDebugger(EditorApplication& editor);
        ~SourceDebugger();
        void Start();
        void Stop();
        void Poll(Float elapsed);
        Bool IsActive() const;
        Bool CanPlay() const;
        Breakpoints& Anchors();
        const DebugClient& Client() const;
        Bool Matches(const String& path, const String& text) const;
        void BuildStarted();
        void BuildFinished(Bool succeeded);
        void ModuleReloaded(Bool succeeded);
    private:
        struct Source { String path, text; Bool synced = false, current = false; };
        Bool Capture(Array<Source>& sources);
        Bool Read(const String& path, String& text) const;
        Bool SourcesCurrent(const Array<Source>& sources) const;
        void AcceptBuild();
        void Fail(const String& error);
        EditorApplication* m_editor;
        Breakpoints m_anchors;
        DebugClient m_client;
        Array<Source> m_sources, m_buildSources;
        String m_module;
        UInt64 m_sentRevision = 0;
        Float m_sourceAge = 0.0f;
        Bool m_requested = false, m_attaching = false, m_started = false, m_stopping = false;
        Bool m_building = false, m_buildReady = false, m_buildValid = false;
        Bool m_ownsChannel = false;
    };
}

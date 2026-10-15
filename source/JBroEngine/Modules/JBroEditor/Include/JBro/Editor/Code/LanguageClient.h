#pragma once
#include <JBro/Editor/Code/Analysis.h>
#include <JBro/Editor/Code/Assistance.h>
#include <JBro/Editor/Code/Summary.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/Float.h>

namespace JBro
{
    class IPlatform;
}
namespace JBro::Code
{
    enum class ClientState { Stopped, Starting, Ready, Failed };
    class LanguageClient final : public EnableSafeFromThis<LanguageClient>
    {
    public:
        explicit LanguageClient(IPlatform& platform);
        ~LanguageClient();
        Bool Start(const String& executable, const String& project, const String& locale);
        void Stop();
        void Poll(Float elapsed);
        void Open(const String& path, const String& text);
        void Change(const String& path, const String& text);
        void Saved(const String& path);
        void Close(const String& path);
        const Analysis* GetAnalysis(const String& path) const;
        Int32 GetVersion(const String& path) const;
        const ScriptSummary* GetSummary(const String& path) const;
        UInt64 EditFieldSettings(const String& path, Int32 version, Position position, const FieldSettings& settings);
        const FieldEditResult* GetFieldEditResult(const String& path) const;
        UInt64 RequestHover(const String& path, Int32 version, Position position);
        const HoverResult* GetHover(const String& path) const;
        void CancelHover(const String& path);
        UInt64 RequestReferences(const String& path, Int32 version, Position position);
        const ReferencesResult* GetReferences(const String& path) const;
        void CancelReferences(const String& path);
        ClientState GetState() const;
        const String& GetError() const;
        static String FileUri(const String& path);
        static String NormalizePath(const String& path);
    private:
        struct State;
        OwnerPtr<State> m_state;
    };
}

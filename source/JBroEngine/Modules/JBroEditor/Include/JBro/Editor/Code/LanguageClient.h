#pragma once
#include <JBro/Editor/Code/Analysis.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/Float.h>

namespace JBro
{
    class IPlatform;
}
namespace JBro::Code
{
    enum class ClientState { Stopped, Starting, Ready, Failed };
    class LanguageClient final
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
        ClientState GetState() const;
        const String& GetError() const;
        static String FileUri(const String& path);
        static String NormalizePath(const String& path);
    private:
        struct State;
        OwnerPtr<State> m_state;
    };
}

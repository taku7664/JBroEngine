#pragma once
#include <JBro/Platform/ProcessChannel.h>
#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Delegate.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/SafePtr.h>

namespace JBro { class IPlatform; }
namespace JBro::Agent
{
    enum class Connection { Stopped, Starting, SignedOut, Ready, Running, Failed };
    enum class MessageRole { User, Assistant, Activity, Error };
    struct ToolResult
    {
        Bool success = false;
        String text;
    };
    struct Target
    {
        enum class Kind { Object, Asset, Component };
        Kind kind = Kind::Object;
        String project, canvas, name;
        AssetId asset;
        UInt64 revision = 0;
        UInt64 instance = 0;
        UInt64 component = 0;
        Bool Equals(const Target& other) const;
    };
    struct Message
    {
        MessageRole role = MessageRole::Activity;
        String text;
        String context;
        Array<Target> targets;
    };
    struct Settings
    {
        String executable, project, skillsRoot, thread;
        String toolDefinitions;
        String instructions;
        String model;
    };
    struct Model { String id, label; };
    // JSON is confined to the provider/tool wire boundary, never exposed as a library type.
    class IProvider
    {
    public:
        virtual ~IProvider() = default;
        virtual void Poll(Float elapsed) = 0;
        virtual Bool Submit(const String& text, const String& context) = 0;
        virtual void Interrupt() = 0;
        virtual void Stop() = 0;
        virtual Connection GetConnection() const = 0;
        virtual const Array<Message>& Messages() const = 0;
    };
    class CodexProvider final : public IProvider
    {
    public:
        using ToolHandler = Delegate<ToolResult(const String&, const String&)>;
        CodexProvider();
        ~CodexProvider();
        Bool Start(IPlatform& platform, const Settings& settings, ToolHandler handler);
        Bool Attach(OwnerPtr<IProcessChannel> channel, const Settings& settings, ToolHandler handler);
        void Poll(Float elapsed) override;
        Bool Submit(const String& text, const String& context) override;
        void Interrupt() override;
        void Stop() override;
        void ClearConversation();
        void LoginChatGPT();
        void LoginApiKey(const String& key);
        Connection GetConnection() const override;
        const Array<Message>& Messages() const override;
        const String& GetError() const;
        const String& GetThread() const;
        const String& GetAuthUrl() const;
        const Array<Model>& GetModels() const;
        const String& GetModel() const;
        Bool SetModel(const String& model);
        static String Locate(IPlatform& platform);
        static String Instructions();
    private:
        struct State;
        OwnerPtr<State> m_state;
    };
}

#include <JBro/Editor/Code/DebugClient.h>
#include <JBro/Platform/Platform.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace JBro;
    using Json = nlohmann::json;
    void Check(Bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
    struct ChannelData
    {
        String incoming, outgoing;
        Bool running = true, writable = true, destroyed = false;
    };
    class MemoryChannel final : public IProcessChannel
    {
    public:
        explicit MemoryChannel(ChannelData& value) : data(value) {}
        ~MemoryChannel() override { data.destroyed = true; }
        Bool Write(std::string_view bytes) override { data.outgoing += bytes; return data.writable; }
        Bool Read(String& bytes, Bool error) override
        {
            bytes.clear();
            if (!error)
            {
                bytes.swap(data.incoming);
            }
            return !bytes.empty();
        }
        Bool IsRunning() const override { return data.running; }
        ChannelData& data;
    };
    class MemoryPlatform final : public IPlatform
    {
    public:
        Bool Initialize(const JMemoryContext&) override { return true; }
        void Shutdown() override {}
        WindowHandle OpenPlatformWindow(const WindowDesc&) override { return {}; }
        void ClosePlatformWindow(WindowHandle) override {}
        SurfaceHandle CreateSurface(WindowHandle) override { return {}; }
        void PumpEvents() override {}
        JArrayView<InputEvent> GetInputEvents() const override { return {}; }
        void WaitForEvents(UInt32) override {}
        Bool ShouldClose(WindowHandle) const override { return false; }
        Bool GetWindowState(WindowHandle, WindowState&) const override { return false; }
        DynamicLibrary LoadDynamicLibrary(const char*) override { return {}; }
        void* GetSymbol(DynamicLibrary, const char*) override { return nullptr; }
        void UnloadDynamicLibrary(DynamicLibrary) override {}
        OwnerPtr<IProcessChannel> StartProcessChannel(const char*, const char*) override
        {
            data = {};
            return MakeOwnerPtr<MemoryChannel>(data);
        }
        ChannelData data;
    };
    String Frame(const Json& message)
    {
        const String body = message.dump();
        return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    }
    Json Request(MemoryPlatform& platform, const char* command)
    {
        Json found;
        String& bytes = platform.data.outgoing;
        while (!bytes.empty())
        {
            const auto end = bytes.find("\r\n\r\n");
            Check(end != String::npos, "outgoing DAP header is complete");
            const auto length = std::stoull(bytes.substr(16, end - 16));
            Json value = Json::parse(bytes.substr(end + 4, length));
            bytes.erase(0, end + 4 + length);
            if (value.value("command", "") == command)
            {
                found = std::move(value);
            }
        }
        Check(!found.is_null(), command);
        return found;
    }
    void Respond(MemoryPlatform& platform, Code::DebugClient& client, const Json& request, Json body = Json::object(), Bool success = true)
    {
        platform.data.incoming += Frame({{"type", "response"}, {"request_seq", request["seq"]},
            {"command", request["command"]}, {"success", success.Get()}, {"body", std::move(body)}, {"message", "engine rejected breakpoint"}});
        client.Poll(0.01f);
    }
    void Event(MemoryPlatform& platform, Code::DebugClient& client, const char* event, Json body = Json::object())
    {
        platform.data.incoming += Frame({{"type", "event"}, {"event", event}, {"body", std::move(body)}});
        client.Poll(0.01f);
    }
    void Attach(MemoryPlatform& platform, Code::DebugClient& client)
    {
        Check(client.Start("JBroDebugAdapter.exe", 123), "native debugger starts");
        const Json initialize = Request(platform, "initialize");
        const String reply = Frame({{"type", "response"}, {"request_seq", initialize["seq"]}, {"command", "initialize"}, {"success", true}});
        platform.data.incoming = reply.substr(0, 9);
        client.Poll(0.01f);
        Check(client.GetState() == Code::DebugState::Starting, "partial DAP frame waits");
        platform.data.incoming = reply.substr(9);
        client.Poll(0.01f);
        const Json attach = Request(platform, "attach");
        Check(attach["arguments"]["processId"] == 123, "attaches to exact editor process");
        Respond(platform, client, attach);
        Event(platform, client, "initialized");
    }
    void TestConfigurationAndAcknowledgment()
    {
        MemoryPlatform platform;
        Code::DebugClient client(platform);
        Attach(platform, client);
        const String path = "C:/project/Probe.jscript";
        client.SetBreakpoints(path, {4, 9});
        client.Configure();
        const Json set = Request(platform, "setBreakpoints");
        Check(set["arguments"]["breakpoints"].size() == 2, "both source breakpoints sent");
        Check(client.GetState() == Code::DebugState::Configuring, "configuration waits for set response");
        Respond(platform, client, set, {{"breakpoints", Json::array({{{"id", 1}, {"line", 4}, {"verified", false}},
            {{"id", 2}, {"line", 9}, {"verified", false}, {"message", "No executable code"}}})}});
        const Json configuration = Request(platform, "configurationDone");
        Check(!(*client.GetBreakpoints(path))[0].verified, "resolved location alone is not verified");
        Event(platform, client, "breakpoint", {{"reason", "changed"}, {"breakpoint", {{"id", 1}, {"line", 5}, {"verified", true}}}});
        Check((*client.GetBreakpoints(path))[0].verified && (*client.GetBreakpoints(path))[0].actualLine == 5,
            "engine acknowledgment changes verified state and actual line");
        Check((*client.GetBreakpoints(path))[0].requestedLine == 4, "requested line is retained");
        Respond(platform, client, configuration);
        Check(client.GetState() == Code::DebugState::Ready && !(*client.GetBreakpoints(path))[1].verified,
            "configuration barrier accepts unresolved lines as pending");
        Event(platform, client, "stopped", {{"threadId", 42}});
        Check(client.GetState() == Code::DebugState::Stopped, "stopped event updates state");
        client.Continue();
        const Json resume = Request(platform, "continue");
        Check(resume["arguments"]["threadId"] == 42, "continue uses stopped thread");
        Respond(platform, client, resume);
        Check(client.GetState() == Code::DebugState::Ready, "continue response resumes state");
        client.Stop();
        Check(client.GetState() == Code::DebugState::Stopping && !platform.data.destroyed, "disconnect retains channel for cleanup");
        Respond(platform, client, Request(platform, "disconnect"));
        Check(client.GetState() == Code::DebugState::Idle && platform.data.destroyed, "disconnect response releases process");
        Check(!(*client.GetBreakpoints(path))[0].verified, "stopping invalidates installed markers");
    }
    void TestStaleRepliesAndFailures()
    {
        MemoryPlatform platform;
        Code::DebugClient client(platform);
        Attach(platform, client);
        const String path = "C:/project/Probe.jscript";
        client.SetBreakpoints(path, {3});
        const Json first = Request(platform, "setBreakpoints");
        client.SetBreakpoints(path, {7});
        const Json second = Request(platform, "setBreakpoints");
        Respond(platform, client, first, {{"breakpoints", Json::array({{{"id", 1}, {"line", 3}, {"verified", true}}})}});
        Check((*client.GetBreakpoints(path))[0].requestedLine == 7 && !(*client.GetBreakpoints(path))[0].verified,
            "superseded response cannot restore old marker");
        Event(platform, client, "breakpoint", {{"reason", "changed"}, {"breakpoint", {{"id", 1}, {"line", 3}, {"verified", true}}}});
        Check(!(*client.GetBreakpoints(path))[0].verified, "removed breakpoint event cannot verify a new marker");
        Respond(platform, client, second, {{"breakpoints", Json::array({{{"id", 2}, {"line", 7}, {"verified", false}}})}});
        client.Configure();
        Respond(platform, client, Request(platform, "configurationDone"), Json::object(), false);
        Check(client.GetState() == Code::DebugState::Failed && client.GetError() == "engine rejected breakpoint",
            "engine install failure prevents ready and preserves the adapter diagnostic verbatim");
        Check(!platform.data.destroyed, "failure sends graceful disconnect before killing adapter");
        client.Poll(3.0f);
        Check(platform.data.destroyed, "failure cleanup has finite deadline");

        Check(client.Start("adapter.exe", 12), "failed debugger can restart");
        platform.data.incoming = "Content-Length: 999999999\r\n\r\n";
        client.Poll(0.01f);
        Check(client.GetState() == Code::DebugState::Failed, "oversized DAP frame rejected");
        client.Poll(3.0f);
        Check(client.Start("adapter.exe", 12), "bad frame can restart");
        client.Poll(16.0f);
        Check(client.GetState() == Code::DebugState::Failed, "startup deadline prevents infinite wait");
        client.Poll(3.0f);
        Check(client.Start("adapter.exe", 12), "timeout cleanup permits restart");
        platform.data.incoming = "Content-Length: 2\r\nContent-Length: 1\r\n\r\n{}";
        client.Poll(0.01f);
        Check(client.GetState() == Code::DebugState::Failed, "duplicate DAP frame lengths rejected");
        client.Poll(3.0f);
        Check(client.Start("adapter.exe", 12), "malformed frame cleanup permits restart");
        platform.data.running = false;
        client.Poll(0.01f);
        Check(client.GetState() == Code::DebugState::Failed, "adapter exit never leaves a ready session");
    }
}
JBro::Int32 RunNativeDebugTests()
{
    TestConfigurationAndAcknowledgment();
    TestStaleRepliesAndFailures();
    std::cout << "Native debugger: DAP framing, configuration, acknowledgment, stale results and cleanup passed\n";
    return 0;
}

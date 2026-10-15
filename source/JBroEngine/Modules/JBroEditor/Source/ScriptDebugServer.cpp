#include <JBro/Editor/ScriptDebugServer.h>

#include <JBro/Core/Log.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Platform/Platform.h>

#include <cstdio>
#include <cstdlib>
#include <JBro/Types/Int.h>

namespace JBro
{
    namespace
    {
        // 16진 글자 하나를 주소로 읽는다. `0x` 는 있어도 없어도 된다.
        UInt64 ParseHex(const String& text)
        {
            return UInt64(static_cast<std::uint64_t>(std::strtoull(text.c_str(), nullptr, 16)));
        }
    }

    ScriptDebugServer::ScriptDebugServer() = default;

    ScriptDebugServer::~ScriptDebugServer()
    {
        Stop();
    }

    Bool ScriptDebugServer::Start(IPlatform& platform, UInt32 processId)
    {
        if (m_pipe.Get() != nullptr)
        {
            return false;
        }
        char name[64] = {};
        std::snprintf(name, sizeof(name), "jbro-debug-%lu", static_cast<unsigned long>(processId.Value));
        m_pipe = platform.OpenLocalPipe(name);
        if (m_pipe.Get() == nullptr)
        {
            Log::Write(LogLevel::Warning, "debug", "the debug channel %s could not open", name);
            return false;
        }
        Log::Write(LogLevel::Info, "debug", "the debug channel %s is open", name);
        return true;
    }

    void ScriptDebugServer::Stop()
    {
        m_pipe.Reset();
        m_inbox.clear();
    }

    Bool ScriptDebugServer::IsActive() const
    {
        return m_pipe.Get() != nullptr;
    }

    void ScriptDebugServer::Send(const String& line)
    {
        if (m_pipe.Get() == nullptr)
        {
            return;
        }
        String packet = line;
        packet.push_back('\n');
        m_pipe->Send(packet.data(), packet.size());
    }

    void ScriptDebugServer::Poll(EditorApplication& editor)
    {
        if (m_pipe.Get() == nullptr)
        {
            return;
        }
        m_pipe->Receive(m_inbox);
        std::size_t newline = m_inbox.find('\n');
        while (newline != String::npos)
        {
            String line = m_inbox.Substr(0, newline);
            if (false == line.empty() && line[line.size() - 1] == '\r')
            {
                line.resize(line.size() - 1);
            }
            m_inbox.erase(0, newline + 1);
            if (false == line.empty())
            {
                Dispatch(editor, line);
            }
            newline = m_inbox.find('\n');
        }
    }

    void ScriptDebugServer::Dispatch(EditorApplication& editor, const String& line)
    {
        const std::size_t space = line.find(' ');
        const String command = space == String::npos ? line : line.Substr(0, space);
        const String argument = space == String::npos ? String() : line.Substr(space + 1);
        if (command == "setbp")
        {
            const UInt64 address = ParseHex(argument);
            const Bool ok = editor.SetScriptBreakpoint(address);
            char reply[48] = {};
            std::snprintf(reply, sizeof(reply), "bp %llx %d", static_cast<unsigned long long>(address.Value), ok ? 1 : 0);
            Send(reply);
        }
        else if (command == "clearbp")
        {
            editor.ClearScriptBreakpoint(ParseHex(argument));
        }
        else if (command == "continue")
        {
            editor.ContinueScriptBreak();
        }
        else if (command == "stop")
        {
            editor.StopSimulation();
        }
        else
        {
            Log::Write(LogLevel::Warning, "debug", "the adapter sent an unknown command: %s", command.c_str());
        }
    }

    void ScriptDebugServer::NotifyStopped(UInt64 address, const void* context, UInt32 threadId)
    {
        char line[96] = {};
        std::snprintf(line, sizeof(line), "stopped %llx %llx %lu", static_cast<unsigned long long>(address.Value),
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(context)), static_cast<unsigned long>(threadId.Value));
        Send(line);
    }

    void ScriptDebugServer::NotifyResumed()
    {
        Send("resumed");
    }

    void ScriptDebugServer::NotifyModule(Bool loaded, const String& path)
    {
        Send(String("module ") + (loaded ? "1 " : "0 ") + path);
    }

    void ScriptDebugServer::NotifyExit()
    {
        Send("exited");
    }
}

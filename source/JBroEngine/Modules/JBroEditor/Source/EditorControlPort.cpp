#include <JBro/Editor/EditorControlPort.h>

#include <JBro/Core/Log.h>
#include <JBro/Core/Yaml.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorGuide.h>
#include <JBro/Network/Socket.h>

#include <string_view>
#include <charconv>
#include <limits>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>

namespace JBro
{
    struct EditorControlPort::Client
    {
        OwnerPtr<Network::IStreamSocket> socket;
        // 받았지만 끝 표시가 아직 오지 않은 글이다.
        String inbox;
        // 보낼 답과 그 가운데 이미 보낸 바이트 수다. 소켓은 일부만 받을 수 있다.
        String outbox;
        std::size_t sent = 0;
        // 답을 다 보내면 끊는다(너무 긴 글·접속이 너무 많음).
        Bool closeAfterSend = false;
    };

    namespace
    {
        constexpr std::string_view EndLine = "...";

        // 받은 글에서 끝 표시(`...` 한 줄)까지를 떼어 명령과 본문으로 나눈다. 아직 끝나지 않았으면 거짓이다.
        Bool TakeMessage(String& inbox, String& command, String& body)
        {
            Bool haveCommand = false;
            std::size_t bodyStart = 0;
            std::size_t lineStart = 0;
            while (true)
            {
                const std::size_t newline = inbox.find('\n', lineStart);
                if (newline == String::npos)
                {
                    return false;
                }
                std::size_t lineEnd = newline;
                if (lineEnd > lineStart && inbox[lineEnd - 1] == '\r')
                {
                    --lineEnd;
                }
                const std::string_view line(inbox.data() + lineStart, lineEnd - lineStart);
                if (line == EndLine)
                {
                    body.assign(inbox, bodyStart, haveCommand ? lineStart - bodyStart : 0);
                    if (false == haveCommand)
                    {
                        command.clear();
                    }
                    inbox.erase(0, newline + 1);
                    return true;
                }
                if (false == haveCommand)
                {
                    command.assign(line);
                    haveCommand = true;
                    bodyStart = newline + 1;
                }
                lineStart = newline + 1;
            }
        }

        // 까닭은 한 줄이다 - 줄바꿈이 있으면 답의 모양이 깨진다.
        void AppendError(String& reply, std::string_view reason)
        {
            reply += "error: ";
            for (char letter : reason)
            {
                reply += letter == '\n' || letter == '\r' ? ' ' : letter;
            }
            reply += "\n";
            reply += EndLine;
            reply += "\n";
        }

        void AppendOk(String& reply, const String& body)
        {
            reply += "ok\n";
            reply += body;
            if (false == body.empty() && body.back() != '\n')
            {
                reply += "\n";
            }
            reply += EndLine;
            reply += "\n";
        }

        void WriteStatus(EditorApplication& editor, YamlWriter& writer)
        {
            const EditorGuide& guide = editor.GetGuide();
            writer.WriteBool("Running", guide.IsRunning());
            if (false == guide.IsRunning())
            {
                return;
            }
            const Guide& running = *guide.GetGuide();
            writer.WriteString("Guide", running.id != nullptr ? running.id : "");
            // 사람이 읽는 번호와 같게 1 부터 센다.
            writer.WriteInt("Step", static_cast<JBro::Int64>(guide.GetStepIndex()) + 1);
            writer.WriteInt("Steps", static_cast<JBro::Int64>(running.steps.Size()));
            writer.WriteBool("Confirming", guide.IsConfirming());
        }

        constexpr std::size_t LogPageSize = 64;

        void WriteUInt(YamlWriter& writer, const char* name, UInt64 value)
        {
            char digits[32] = {};
            const auto written = std::to_chars(digits, digits + sizeof(digits) - 1, value.Get());
            if (written.ec == std::errc{})
            {
                writer.WriteString(name, digits);
            }
        }

        // The YAML writer deliberately writes raw scalars. Logs can contain newlines and
        // protocol delimiter lines, so only base64 data crosses this line protocol.
        String Base64(const char* source)
        {
            static constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            const char* data = source != nullptr ? source : "";
            const std::size_t length = std::strlen(data);
            String encoded;
            encoded.reserve(((length + 2) / 3) * 4);
            for (std::size_t index = 0; index < length; index += 3)
            {
                const unsigned a = static_cast<unsigned char>(data[index]);
                const unsigned b = index + 1 < length ? static_cast<unsigned char>(data[index + 1]) : 0;
                const unsigned c = index + 2 < length ? static_cast<unsigned char>(data[index + 2]) : 0;
                encoded += Alphabet[a >> 2];
                encoded += Alphabet[((a & 3) << 4) | (b >> 4)];
                encoded += index + 1 < length ? Alphabet[((b & 15) << 2) | (c >> 6)] : '=';
                encoded += index + 2 < length ? Alphabet[c & 63] : '=';
            }
            return encoded;
        }

        Bool ReadUInt(const YamlDocument& document, UInt32 root, const char* key, UInt64& value)
        {
            String raw;
            if (false == document.FindScalar(root, key, raw))
            {
                return false;
            }
            std::uint64_t parsed = 0;
            const auto read = std::from_chars(raw.data(), raw.data() + raw.size(), parsed);
            if (read.ec != std::errc{} || read.ptr != raw.data() + raw.size())
            {
                return false;
            }
            value = parsed;
            return true;
        }

        Bool ParseLogBody(const String& body, YamlDocument& document, String& error)
        {
            if (body.empty())
            {
                return true;
            }
            YamlError failure;
            if (false == document.Parse(body.data(), body.size(), failure)
                || document.GetKind(document.GetRoot()) != YamlKind::Map)
            {
                error = "invalid log cursor YAML";
                return false;
            }
            return true;
        }

        void WriteLogIdentity(YamlWriter& writer)
        {
            writer.WriteInt("Protocol", 1);
            WriteUInt(writer, "Session", Log::GetSessionId());
            WriteUInt(writer, "Clear", Log::GetClearEpoch());
            WriteUInt(writer, "Revision", Log::GetRevision());
            WriteUInt(writer, "GroupCount", static_cast<std::uint64_t>(Log::GetGroupCount()));
            const LogEntry* oldest = Log::GetAt(0);
            const LogEntry* latest = Log::GetAt(Log::GetCount() == 0 ? 0 : Log::GetCount() - 1);
            WriteUInt(writer, "FirstRetained", oldest != nullptr ? oldest->serial : UInt64(0));
            WriteUInt(writer, "Latest", latest != nullptr ? latest->serial : UInt64(0));
        }

        void WriteLogSince(const String& body, String& reply)
        {
            YamlDocument document;
            String error;
            if (false == ParseLogBody(body, document, error))
            {
                AppendError(reply, error);
                return;
            }
            const UInt32 root = document.GetRoot();
            UInt64 after = 0;
            UInt64 requestedSession = 0;
            UInt64 requestedClear = 0;
            if (root != YamlDocument::InvalidNode)
            {
                for (const char* key : { "After", "Session", "Clear" })
                {
                    UInt64* target = key[0] == 'A' ? &after : (key[0] == 'S' ? &requestedSession : &requestedClear);
                    if (document.Find(root, key) != YamlDocument::InvalidNode
                        && false == ReadUInt(document, root, key, *target))
                    {
                        AppendError(reply, "invalid log cursor number");
                        return;
                    }
                }
            }
            const Bool reset = (requestedSession != 0 && requestedSession != Log::GetSessionId())
                || (requestedSession != 0 && requestedClear != Log::GetClearEpoch());
            if (reset)
            {
                after = 0;
            }
            const LogEntry* oldest = Log::GetAt(0);
            const Bool gap = oldest != nullptr && after < oldest->serial - 1;
            YamlWriter writer;
            WriteLogIdentity(writer);
            writer.WriteBool("Reset", reset);
            writer.WriteBool("Gap", gap);
            writer.BeginSequence("Entries");
            std::size_t written = 0;
            UInt64 last = after;
            const std::size_t count = Log::GetCount();
            for (std::size_t index = 0; index < count && written < LogPageSize; ++index)
            {
                const LogEntry* entry = Log::GetAt(index);
                if (entry == nullptr || entry->serial <= after)
                {
                    continue;
                }
                writer.BeginMap(nullptr);
                WriteUInt(writer, "Serial", entry->serial);
                WriteUInt(writer, "TimeMs", entry->timestampMs);
                writer.WriteInt("Level", static_cast<Int64>(entry->level));
                writer.WriteString("Category64", Base64(entry->category).c_str());
                writer.WriteString("Message64", Base64(entry->message).c_str());
                WriteUInt(writer, "GroupId", entry->groupId);
                if (entry->groupId != 0)
                {
                    const LogGroup* group = Log::GetGroupAt(static_cast<std::size_t>(entry->groupId.Get() - 1));
                    if (group != nullptr)
                    {
                        writer.WriteString("GroupKey64", Base64(group->key.c_str()).c_str());
                        writer.WriteString("GroupDisplay64", Base64(group->display).c_str());
                        writer.WriteInt("GroupLevel", static_cast<Int64>(group->latestLevel));
                        WriteUInt(writer, "GroupCount", group->count);
                        WriteUInt(writer, "GroupLatest", group->latestSerial);
                        WriteUInt(writer, "GroupTimeMs", group->latestTimestampMs);
                    }
                }
                writer.EndMap();
                last = entry->serial;
                ++written;
            }
            writer.EndSequence();
            const LogEntry* latest = Log::GetAt(count == 0 ? 0 : count - 1);
            writer.WriteBool("More", latest != nullptr && latest->serial > last);
            AppendOk(reply, writer.GetText());
        }

        void WriteLogGroups(const String& body, String& reply)
        {
            YamlDocument document;
            String error;
            if (false == ParseLogBody(body, document, error))
            {
                AppendError(reply, error);
                return;
            }
            UInt64 afterId = 0;
            const UInt32 root = document.GetRoot();
            if (root != YamlDocument::InvalidNode
                && document.Find(root, "AfterId") != YamlDocument::InvalidNode
                && false == ReadUInt(document, root, "AfterId", afterId))
            {
                AppendError(reply, "invalid group cursor number");
                return;
            }
            YamlWriter writer;
            WriteLogIdentity(writer);
            writer.BeginSequence("Groups");
            const std::size_t start = static_cast<std::size_t>(std::min<std::uint64_t>(afterId.Get(), Log::GetGroupCount()));
            const std::size_t end = std::min(start + LogPageSize, Log::GetGroupCount());
            for (std::size_t index = start; index < end; ++index)
            {
                const LogGroup* group = Log::GetGroupAt(index);
                writer.BeginMap(nullptr);
                WriteUInt(writer, "Id", group->id);
                writer.WriteString("Key64", Base64(group->key.c_str()).c_str());
                writer.WriteString("Display64", Base64(group->display).c_str());
                writer.WriteInt("Level", static_cast<Int64>(group->latestLevel));
                WriteUInt(writer, "Count", group->count);
                WriteUInt(writer, "First", group->firstSerial);
                WriteUInt(writer, "Latest", group->latestSerial);
                WriteUInt(writer, "TimeMs", group->latestTimestampMs);
                writer.EndMap();
            }
            writer.EndSequence();
            writer.WriteBool("More", end < Log::GetGroupCount());
            AppendOk(reply, writer.GetText());
        }

        void HandleCommand(EditorApplication& editor, const String& command, const String& body, String& reply)
        {
            if (command == "log.since" || command == "log.snapshot")
            {
                WriteLogSince(command == "log.snapshot" ? String() : body, reply);
                return;
            }
            if (command == "log.groups")
            {
                WriteLogGroups(body, reply);
                return;
            }
            if (command == "log.clear")
            {
                Log::Clear();
                YamlWriter writer;
                WriteLogIdentity(writer);
                AppendOk(reply, writer.GetText());
                return;
            }
            if (command == "guide.start")
            {
                String error;
                if (false == editor.StartGuideFromText(body.data(), body.size(), error))
                {
                    AppendError(reply, error);
                    return;
                }
                AppendOk(reply, String());
                return;
            }
            if (command == "guide.stop")
            {
                editor.GetGuide().Stop(editor.GetGuideFocus());
                AppendOk(reply, String());
                return;
            }
            if (command == "guide.status")
            {
                YamlWriter writer;
                WriteStatus(editor, writer);
                AppendOk(reply, writer.GetText());
                return;
            }
            if (command == "guide.catalog")
            {
                YamlWriter writer;
                EditorGuides::WriteCatalog(writer);
                AppendOk(reply, writer.GetText());
                return;
            }
            String reason = "unknown command '";
            reason += command;
            reason += "'";
            AppendError(reply, reason);
        }
    }

    EditorControlPort::EditorControlPort() = default;

    EditorControlPort::~EditorControlPort()
    {
        Close();
    }

    Bool EditorControlPort::Open(Network::ISocketProvider& provider, std::uint16_t port)
    {
        Close();
        OwnerPtr<Network::IStreamSocket> listener = provider.CreateStreamSocket();
        if (listener.Get() == nullptr || false == listener->ListenLoopback(port))
        {
            return false;
        }
        m_listener = std::move(listener);
        m_port = port;
        return true;
    }

    void EditorControlPort::Close()
    {
        for (OwnerPtr<Client>& client : m_clients)
        {
            client->socket->Close();
        }
        m_clients.Clear();
        if (m_listener.Get() != nullptr)
        {
            m_listener->Close();
            m_listener.Reset();
        }
        m_port = 0;
    }

    Bool EditorControlPort::IsOpen() const noexcept
    {
        return m_listener.Get() != nullptr;
    }

    std::uint16_t EditorControlPort::GetPort() const noexcept
    {
        return m_port;
    }

    void EditorControlPort::Poll(EditorApplication& editor)
    {
        if (m_listener.Get() == nullptr)
        {
            return;
        }
        Accept();
        for (std::size_t index = 0; index < m_clients.Size();)
        {
            if (Serve(*m_clients[index], editor))
            {
                ++index;
                continue;
            }
            m_clients[index]->socket->Close();
            m_clients.RemoveAt(index);
        }
    }

    void EditorControlPort::Accept()
    {
        while (true)
        {
            OwnerPtr<Network::IStreamSocket> socket = m_listener->Accept();
            if (socket.Get() == nullptr)
            {
                return;
            }
            OwnerPtr<Client> client = MakeOwnerPtr<Client>();
            client->socket = std::move(socket);
            // 넘치는 접속도 까닭을 듣고 끊긴다. 말없이 끊으면 붙은 쪽은 왜 안 되는지 모른다.
            if (m_clients.Size() >= MaxClients)
            {
                AppendError(client->outbox, "too many connections");
                client->closeAfterSend = true;
            }
            m_clients.Add(std::move(client));
        }
    }

    Bool EditorControlPort::Serve(Client& client, EditorApplication& editor)
    {
        Bool peerClosed = false;
        char buffer[4096];
        while (false == client.closeAfterSend)
        {
            std::size_t received = 0;
            const Network::SocketIo io = client.socket->Receive(buffer, sizeof(buffer), received);
            if (io == Network::SocketIo::Ok && received > 0)
            {
                client.inbox.append(buffer, received);
                continue;
            }
            peerClosed = io == Network::SocketIo::Closed || io == Network::SocketIo::Error;
            break;
        }

        String command;
        String body;
        while (false == client.closeAfterSend && TakeMessage(client.inbox, command, body))
        {
            if (command.empty())
            {
                AppendError(client.outbox, "the message has no command");
                continue;
            }
            HandleCommand(editor, command, body, client.outbox);
        }
        if (false == client.closeAfterSend && client.inbox.size() > MaxMessageBytes)
        {
            AppendError(client.outbox, "the message is too long; end it with a line '...'");
            client.inbox.clear();
            client.closeAfterSend = true;
        }

        while (client.sent < client.outbox.size())
        {
            std::size_t sent = 0;
            const Network::SocketIo io = client.socket->Send(client.outbox.data() + client.sent,
                client.outbox.size() - client.sent, sent);
            if (io != Network::SocketIo::Ok)
            {
                if (io != Network::SocketIo::WouldBlock)
                {
                    return false;
                }
                break;
            }
            client.sent += sent;
        }
        if (client.sent == client.outbox.size() && client.sent != 0)
        {
            client.outbox.clear();
            client.sent = 0;
        }
        if (peerClosed)
        {
            if (false == client.outbox.empty())
            {
                Log::Write(LogLevel::Warning, "editor", "control port: a client left before its reply was sent");
            }
            return false;
        }
        return false == (client.closeAfterSend && client.outbox.empty());
    }
}

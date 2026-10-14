#include "DapChannel.h"

#include <Windows.h>

#include <cstdio>
#include <cstdlib>

namespace JBro::DebugAdapter
{
    DapChannel::DapChannel(void* input, void* output)
        : m_input(input)
        , m_output(output)
    {
    }

    Bool DapChannel::ReadExactly(char* buffer, std::size_t length)
    {
        std::size_t done = 0;
        const std::size_t fromPending = length < m_pending.size() ? length : m_pending.size();
        if (fromPending > 0)
        {
            std::memcpy(buffer, m_pending.data(), fromPending);
            m_pending.erase(0, fromPending);
            done = fromPending;
        }
        while (done < length)
        {
            DWORD read = 0;
            if (FALSE == ReadFile(static_cast<HANDLE>(m_input), buffer + done, static_cast<DWORD>(length - done), &read, nullptr) || read == 0)
            {
                return false;
            }
            done += read;
        }
        return true;
    }

    Bool DapChannel::ReadHeaderLine(String& line)
    {
        line.clear();
        while (true)
        {
            char c = 0;
            if (false == ReadExactly(&c, 1))
            {
                return false;
            }
            if (c == '\n')
            {
                if (false == line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                return true;
            }
            line += c;
            // 머리글 한 줄이 이만큼 길 까닭이 없다. 엉뚱한 입력에 메모리를 다 쓰지 않는다.
            if (line.size() > 4096)
            {
                return false;
            }
        }
    }

    Bool DapChannel::Read(JsonValue& message)
    {
        std::size_t contentLength = 0;
        Bool haveLength = false;
        String line;
        while (true)
        {
            if (false == ReadHeaderLine(line))
            {
                return false;
            }
            if (line.empty())
            {
                if (haveLength)
                {
                    break;
                }
                continue;
            }
            constexpr const char* Header = "Content-Length:";
            if (line.rfind(Header, 0) == 0)
            {
                contentLength = static_cast<std::size_t>(std::strtoull(line.c_str() + std::strlen(Header), nullptr, 10));
                haveLength = true;
            }
        }
        // 한 메시지의 상한이다. 클라이언트가 보내는 요청은 작다.
        if (contentLength > 64u * 1024u * 1024u)
        {
            return false;
        }
        String body(contentLength, '\0');
        if (false == ReadExactly(body.data(), contentLength))
        {
            return false;
        }
        return JsonValue::Parse(body.data(), body.size(), message);
    }

    void DapChannel::Send(JsonValue message)
    {
        std::lock_guard<std::mutex> lock(m_writeLock);
        message.Set("seq", m_nextSeq);
        m_nextSeq = m_nextSeq + 1;
        const String body = message.Serialize();
        char header[64] = {};
        std::snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", body.size());
        String packet(header);
        packet += body;
        std::size_t done = 0;
        while (done < packet.size())
        {
            DWORD written = 0;
            if (FALSE == WriteFile(static_cast<HANDLE>(m_output), packet.data() + done, static_cast<DWORD>(packet.size() - done), &written, nullptr)
                || written == 0)
            {
                return;
            }
            done += written;
        }
        FlushFileBuffers(static_cast<HANDLE>(m_output));
    }

    void DapChannel::SendResponse(const JsonValue& request, Bool success, JsonValue body, const char* message)
    {
        JsonValue response = JsonValue::MakeObject();
        response.Set("type", "response");
        response.Set("request_seq", request.GetInt64("seq"));
        response.Set("success", success);
        response.Set("command", request.GetString("command"));
        if (message != nullptr)
        {
            response.Set("message", message);
        }
        if (false == body.IsNull())
        {
            response.Set("body", std::move(body));
        }
        Send(std::move(response));
    }

    void DapChannel::SendEvent(const char* event, JsonValue body)
    {
        JsonValue message = JsonValue::MakeObject();
        message.Set("type", "event");
        message.Set("event", event);
        if (false == body.IsNull())
        {
            message.Set("body", std::move(body));
        }
        Send(std::move(message));
    }
}

#pragma once

#include "Json.h"

#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/String.h>

#include <mutex>

namespace JBro::DebugAdapter
{
    // DAP 메시지를 주고받는 통로다. 메시지는 `Content-Length: N\r\n\r\n` 머리글 뒤의 JSON 이다(Debug Adapter Protocol 의 base protocol).
    // 읽기는 한 스레드만 하고, 쓰기는 어느 스레드든 한다 - 쓰기마다 잠그고 `seq` 를 붙인다.
    class DapChannel
    {
    public:
        // 핸들은 빌린다(표준 입출력이나 시험의 파이프). 닫지 않는다.
        DapChannel(void* input, void* output);

        // 메시지 하나가 올 때까지 기다린다. 입력이 끝났거나 형식이 틀리면 거짓이다.
        Bool Read(JsonValue& message);

        void SendResponse(const JsonValue& request, Bool success, JsonValue body, const char* message = nullptr);
        void SendEvent(const char* event, JsonValue body);

    private:
        Bool ReadExactly(char* buffer, std::size_t length);
        Bool ReadHeaderLine(String& line);
        void Send(JsonValue message);

        void* m_input;
        void* m_output;
        std::mutex m_writeLock;
        Int64 m_nextSeq = 1;
        // 읽었지만 아직 넘기지 않은 바이트다.
        String m_pending;
    };
}

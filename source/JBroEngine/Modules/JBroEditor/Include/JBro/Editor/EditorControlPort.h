#pragma once

#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/String.h>

#include <cstddef>
#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    class EditorApplication;
    namespace Network
    {
        class ISocketProvider;
        class IStreamSocket;
    }

    // 에디터 제어 포트(D-270, `tasks/guide-focus-plan.md` §2.11)다. 에디터 밖의 프로세스(에이전트·도구)가 같은 기계에서
    // TCP 로 붙어 에디터에 일을 시킨다. **루프백에만 연다** - 같은 망의 다른 기계가 에디터를 움직이면 안 된다.
    //
    // 글 하나는 첫 줄이 명령이고 그 뒤가 본문이며, `...` 한 줄로 끝난다(YAML 의 문서 끝 표시). 답도 같은 모양이다 -
    // 첫 줄이 `ok` 나 `error: <까닭>` 이고 본문이 따르며 `...` 로 끝난다. 줄 끝은 `\n` 이고 `\r\n` 도 받는다.
    //
    //   guide.start    본문의 YAML 가이드를 켠다(`EditorApplication::StartGuideFromText`).
    //   guide.stop     돌던 가이드를 멈춘다.
    //   guide.status   돌고 있는지, 어느 가이드의 몇째 단계인지 YAML 로 답한다.
    //   guide.catalog  가이드가 쓸 수 있는 행동 목록을 YAML 로 답한다(`EditorGuides::WriteCatalog`).
    class EditorControlPort
    {
    public:
        static constexpr std::uint16_t DefaultPort = 3663;
        // 끝 표시 없이 이보다 길게 오면 답하고 끊는다. 가이드 한 편은 몇 KB 다.
        static constexpr std::size_t MaxMessageBytes = 64 * 1024;
        static constexpr UInt32 MaxClients = 4;

        EditorControlPort();
        ~EditorControlPort();
        EditorControlPort(const EditorControlPort&) = delete;
        EditorControlPort& operator=(const EditorControlPort&) = delete;

        // provider 는 부른 쪽이 들고 이 포트보다 오래 산다. 포트를 이미 누가 쓰면(에디터를 둘 띄웠다) 거짓이다.
        Bool Open(Network::ISocketProvider& provider, std::uint16_t port);
        void Close();
        Bool IsOpen() const noexcept;
        std::uint16_t GetPort() const noexcept;

        // 메인 스레드에서 프레임마다 부른다. 접속을 받고, 다 온 글을 처리해 답한다.
        // 아무 일이 없으면 힙을 건드리지 않는다.
        void Poll(EditorApplication& editor);

    private:
        struct Client;

        void Accept();
        // 거짓이면 그 접속을 뗀다.
        Bool Serve(Client& client, EditorApplication& editor);

        OwnerPtr<Network::IStreamSocket> m_listener;
        Array<OwnerPtr<Client>> m_clients;
        std::uint16_t m_port = 0;
    };
}

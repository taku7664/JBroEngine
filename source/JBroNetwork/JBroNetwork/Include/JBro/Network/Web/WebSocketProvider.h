#pragma once

#include <JBro/Network/Socket.h>
#include <JBro/Types/SafePtr.h>

// 브라우저(Emscripten)의 소켓 provider 다(network-plan §2.7). **이 파일은 Emscripten 빌드에서만 컴파일되고, 이 저장소의 Windows
// 빌드는 그것을 검증하지 못한다** - 웹 빌드가 서는 날 `[열림]` 을 닫는다.
//
// 브라우저에는 소켓이 없다. 스트림 소켓은 `WebSocket` 위의 **바이트 흐름 흉내**다: 트랜스포트가 쓰는 RFC6455 프레임을 풀어
// 페이로드만 `ws.send` 하고, 받은 메시지를 서버 → 클라이언트 프레임(마스크 없음)으로 다시 감싸 돌려준다. 오프닝 핸드셰이크는
// 브라우저가 스스로 하므로, 트랜스포트가 쓴 HTTP 요청은 여기서 삼키고 알맞은 101 응답을 만들어 돌려준다. 그래서 트랜스포트는
// 플랫폼을 가리지 않고 한 길만 간다. 데이터그램은 없다(null). 피어 연결은 `RTCPeerConnection` + 데이터 채널 넷이다.
#if defined(__EMSCRIPTEN__)

namespace JBro::Network::Web
{
    class WebSocketProvider final : public ISocketProvider
    {
    public:
        OwnerPtr<IStreamSocket> CreateStreamSocket() override;
        OwnerPtr<IDatagramSocket> CreateDatagramSocket() override;
        OwnerPtr<IPeerConnection> CreatePeerConnection(const PeerConnectionDesc& desc) override;
    };
}

#endif

#include "TestCheck.h"

#include <JBro/Network/Testing/MemorySocketProvider.h>

#include <cstring>
#include <iostream>

using namespace JBro::Network;
using namespace JBro::Network::Testing;

namespace
{
    // **데이터그램은 포트로 배달되고, 보낸 이의 주소가 함께 온다.** 3 단계의 UDP 채널이 이 위에 선다.
    void TestDatagramRoundTrip()
    {
        MemorySocketProvider provider;
        JBro::OwnerPtr<IDatagramSocket> server = provider.CreateDatagramSocket();
        JBro::OwnerPtr<IDatagramSocket> client = provider.CreateDatagramSocket();
        Check(nullptr != server.Get() && nullptr != client.Get(), "datagram sockets exist");
        Check(server->Open() && server->Bind(9000), "the server binds 9000");
        Check(client->Open(), "the client opens");

        Endpoint target;
        Check(client->Resolve("memory", 9000, target), "resolve");
        const char ping[] = "ping";
        Check(client->SendTo(target, ping, sizeof(ping)) == SocketIo::Ok, "send");

        std::uint8_t buffer[64];
        std::size_t received = 0;
        Endpoint from;
        Check(server->ReceiveFrom(buffer, sizeof(buffer), received, from) == SocketIo::Ok, "the server receives");
        Check(received == sizeof(ping) && 0 == std::memcmp(buffer, ping, sizeof(ping)), "the same bytes");
        Check(from.IsValid(), "and learns the sender");
        Check(server->ReceiveFrom(buffer, sizeof(buffer), received, from) == SocketIo::WouldBlock, "then nothing");

        const char pong[] = "pong";
        Check(server->SendTo(from, pong, sizeof(pong)) == SocketIo::Ok, "the server replies to the learned address");
        Endpoint serverAddress;
        Check(client->ReceiveFrom(buffer, sizeof(buffer), received, serverAddress) == SocketIo::Ok, "the client receives");
        Check(0 == std::memcmp(buffer, pong, sizeof(pong)), "the reply");
        Check(serverAddress.Equals(target), "from the server's address");
    }

    // **받는 이가 없거나 큐가 차면 조용히 사라진다.** UDP 는 보낸 쪽에 알리지 않는다.
    void TestDatagramDropsSilently()
    {
        MemorySocketProvider provider(256 * 1024, 2);
        JBro::OwnerPtr<IDatagramSocket> a = provider.CreateDatagramSocket();
        JBro::OwnerPtr<IDatagramSocket> b = provider.CreateDatagramSocket();
        Check(a->Open() && b->Open() && b->Bind(9001), "open");
        Endpoint nobody;
        Check(a->Resolve("memory", 9999, nobody), "resolve a port nobody holds");
        Check(a->SendTo(nobody, "x", 1) == SocketIo::Ok, "sending to nobody still returns Ok");
        Endpoint target;
        a->Resolve("memory", 9001, target);
        for (int index = 0; index < 5; ++index)
        {
            Check(a->SendTo(target, &index, sizeof(index)) == SocketIo::Ok, "send");
        }
        std::uint8_t buffer[16];
        std::size_t received = 0;
        Endpoint from;
        int count = 0;
        while (b->ReceiveFrom(buffer, sizeof(buffer), received, from) == SocketIo::Ok)
        {
            ++count;
        }
        Check(count == 2, "only the queue length survives; the rest was dropped");
    }

    // **UDP 가 없는 플랫폼은 null 이다.** 웹을 흉내 낼 때 켠다.
    void TestDatagramCanBeUnavailable()
    {
        MemorySocketProvider provider;
        provider.SetDatagramAvailable(false);
        Check(nullptr == provider.CreateDatagramSocket().Get(), "no datagram socket");
        Check(nullptr != provider.CreateStreamSocket().Get(), "but streams remain");
        Check(nullptr == provider.CreatePeerConnection(PeerConnectionDesc{}).Get(), "and no peer connection yet");
    }
}

int RunMemorySocketTests()
{
    try
    {
        TestDatagramRoundTrip();
        TestDatagramDropsSilently();
        TestDatagramCanBeUnavailable();
    }
    catch (const std::exception& error)
    {
        std::cout << "MemorySocketTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "MemorySocketTests passed\n";
    return 0;
}

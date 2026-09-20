#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2DSystem/Network/Transform2DReplication.h>
#include <JBro/Network/Testing/ManualClock.h>
#include <JBro/Network/Testing/MemorySocketProvider.h>
#include <JBro/NetworkSystem/NetworkHost.h>
#include <JBro/NetworkSystem/System/NetworkSystems.h>
#include <JBro/Runtime/GameObject.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    bool NearlyEqual(float left, float right)
    {
        return std::fabs(left - right) <= 0.0001f;
    }

    // 호스트 한 벌: 캔버스 + 네트워크 + Transform2D 복제 풀 + 수신·송신 시스템. 프레임워크가 세우는 것과 같은 배선이다.
    struct Side
    {
        JBro::Canvas canvas;
        JBro::NetworkHost host;
        JBro::Transform2DReplicatedPool pool;

        Side(JBro::Network::ISocketProvider& provider, JBro::Network::IClock& clock)
            : canvas(JBro::CreateDefaultAllocator())
            , host(&provider, clock)
            , pool(canvas)
        {
            host.BindCanvas(&canvas);
            host.RegisterPool(pool);
            canvas.GetSystems().AddSystem<JBro::System::NetworkReceiveSystem>(host);
            canvas.GetSystems().AddSystem<JBro::System::NetworkSendSystem>(host);
            canvas.GetSystems().Initialize(canvas);
        }

        ~Side()
        {
            canvas.GetSystems().Shutdown(canvas);
            host.UnbindCanvas();
        }

        // 프레임 밖의 소켓 펌프와 고정 스텝 하나.
        void Frame()
        {
            host.Update();
            canvas.BeginFrame();
            canvas.GetSystems().FixedUpdate(canvas, 1.0f / 60.0f);
            canvas.FlushPendingDestroy();
        }
    };

    JBro::Component::Transform2D* FindReplica(Side& server, Side& client, JBro::GameObject* serverObject)
    {
        const JBro::Network::NetworkObjectId id = server.host.FindNetworkId(serverObject->GetInstanceId());
        if (JBro::Network::InvalidNetworkObjectId == id)
        {
            return nullptr;
        }
        const JBro::InstanceId local = client.host.FindLocalObject(id);
        if (JBro::InvalidInstanceId == local)
        {
            return nullptr;
        }
        JBro::Component::Transform2D* found = nullptr;
        client.canvas.ForEachObject([&](JBro::GameObject& object)
        {
            if (object.GetInstanceId() == local)
            {
                found = client.canvas.FindComponentRaw<JBro::Component::Transform2D>(&object);
            }
        });
        return found;
    }

    bool Matches(Side& server, Side& client, JBro::GameObject* serverObject)
    {
        JBro::Component::Transform2D* mine = server.canvas.FindComponentRaw<JBro::Component::Transform2D>(serverObject);
        JBro::Component::Transform2D* theirs = FindReplica(server, client, serverObject);
        if (nullptr == mine || nullptr == theirs)
        {
            return false;
        }
        return NearlyEqual(mine->position.x, theirs->position.x) && NearlyEqual(mine->position.y, theirs->position.y)
            && NearlyEqual(mine->rotation, theirs->rotation) && NearlyEqual(mine->scale.x, theirs->scale.x)
            && NearlyEqual(mine->scale.y, theirs->scale.y);
    }

    // **한 프로세스에서 호스트 둘이 이어지고, 서버 캔버스의 `Transform2D` 풀이 클라이언트 캔버스에 같은 오브젝트·같은 값으로 선다.**
    // 컴포넌트를 새로 만들지 않았다 - `Transform2D` 자체가 복제 대상이고, 오브젝트는 표로 잇는다(network-plan §2.6·§3-5).
    void TestTwoHostsReplicateTransforms()
    {
        JBro::Network::Testing::MemorySocketProvider provider;
        JBro::Network::Testing::ManualClock clock;
        Side server(provider, clock);
        Side client(provider, clock);
        Check(server.host.StartServer(6000), "the server host listens");
        Check(client.host.Connect("memory", 6000), "the client host connects");
        Check(server.host.GetRole() == JBro::Network::NetworkRole::Server, "roles are taken");
        Check(false == client.host.HasAuthority(0) && server.host.HasAuthority(0), "only the server has authority");

        constexpr int Count = 20;
        JBro::GameObject* objects[Count] = {};
        for (int index = 0; index < Count; ++index)
        {
            objects[index] = server.canvas.CreateObject("replicated");
            JBro::Component::Transform2D* transform = server.canvas.AttachComponent<JBro::Component::Transform2D>(objects[index]);
            Check(nullptr != transform, "transform attaches");
            transform->position = { static_cast<float>(index), static_cast<float>(index) * 2.0f };
            transform->rotation = 0.1f * static_cast<float>(index);
            transform->scale = { 1.0f + 0.01f * static_cast<float>(index), 1.0f };
        }

        auto converged = [&]()
        {
            if (client.canvas.GetObjectCount() != static_cast<std::size_t>(Count))
            {
                return false;
            }
            for (int index = 0; index < Count; ++index)
            {
                if (false == Matches(server, client, objects[index]))
                {
                    return false;
                }
            }
            return true;
        };

        bool done = false;
        for (int round = 0; round < 600 && false == done; ++round)
        {
            server.Frame();
            client.Frame();
            clock.Advance(5.0);
            done = converged();
        }
        Check(done, "the client canvas holds twenty objects whose transforms equal the server's");
        Check(client.host.IsConnected() && server.host.GetConnectionCount() == 1, "one connection, ready on both sides");

        // 값이 바뀌면 따라온다.
        for (int index = 0; index < Count; ++index)
        {
            JBro::Component::Transform2D* transform = server.canvas.FindComponentRaw<JBro::Component::Transform2D>(objects[index]);
            transform->position.x += 100.0f;
        }
        done = false;
        for (int round = 0; round < 200 && false == done; ++round)
        {
            server.Frame();
            client.Frame();
            clock.Advance(5.0);
            done = converged();
        }
        Check(done, "changed transforms arrive");

        // 서버에서 없어지면 클라이언트에서도 없어진다.
        server.canvas.DestroyObject(objects[Count - 1]);
        server.canvas.FlushPendingDestroy();
        bool despawned = false;
        for (int round = 0; round < 200 && false == despawned; ++round)
        {
            server.Frame();
            client.Frame();
            clock.Advance(5.0);
            despawned = client.canvas.GetObjectCount() == static_cast<std::size_t>(Count - 1);
        }
        Check(despawned, "the despawn reaches the client canvas");

        // 게임 메시지는 복제와 섞이지 않고 따로 꺼내진다.
        const std::uint32_t payload = 0xBEEF;
        Check(client.host.Send(JBro::Network::ServerConnectionId, 12, &payload, sizeof(payload), JBro::Network::NetChannel::ReliableOrdered),
            "a game message goes out");
        Check(false == client.host.Send(JBro::Network::ServerConnectionId, JBro::Network::ReplicationDeltaMessage, &payload, sizeof(payload),
                  JBro::Network::NetChannel::ReliableOrdered),
            "but the replication range is refused");
        JBro::Network::MessageView view;
        std::uint32_t got = 0;
        for (int round = 0; round < 50 && 0 == got; ++round)
        {
            server.Frame();
            client.Frame();
            clock.Advance(5.0);
            got = server.host.TakeMessages(&view, 1);
        }
        Check(got == 1 && view.messageId == 12 && view.size == sizeof(payload), "the server takes only the game message");
    }

    // **소켓이 없는 플랫폼에서는 모든 시도가 거짓이고 아무것도 깨지지 않는다.** 웹이 아닌 것, 테스트의 가짜 플랫폼이 그것이다.
    // **게임이 켜기 전까지 네트워크는 아무것도 잡지 않는다.** 엔진은 호스트를 늘 세우지만(스크립트가 서비스로
    // 켜고 끌 수 있어야 하므로), 트랜스포트 버퍼도 복제 이력도 역할을 잡은 뒤에 선다. 에디터와 단일 플레이가
    // 쓰지 않는 수십 MB 를 지지 않게 하는 것이 이 테스트가 지키는 것이다.
    void TestNothingIsBuiltUntilTheGameAsks()
    {
        JBro::Network::Testing::MemorySocketProvider provider;
        JBro::Network::Testing::ManualClock clock;
        Side side(provider, clock);

        Check(false == side.host.IsReplicating(), "binding a canvas does not build replication");
        Check(side.host.GetTransport().GetReservedBytes() == 0, "and the transport holds no buffers");
        for (int frame = 0; frame < 3; ++frame)
        {
            side.Frame();
        }
        Check(false == side.host.IsReplicating(), "frames alone do not build it either");
        Check(side.host.GetTransport().GetReservedBytes() == 0, "and still hold nothing");

        Check(side.host.StartServer(7901), "the game turns the network on");
        Check(side.host.IsReplicating(), "now replication stands");
        Check(side.host.GetTransport().GetReservedBytes() > 0, "and the transport took its budget");

        JBro::GameObject* object = side.canvas.CreateObject();
        side.canvas.AttachComponent<JBro::Component::Transform2D>(object);
        side.Frame();
        Check(JBro::Network::InvalidNetworkObjectId != side.host.FindNetworkId(object->GetInstanceId()),
            "the pool registered before the connection is still the pool the server replicates");

        side.host.Disconnect();
        Check(false == side.host.IsReplicating(), "turning it off tears replication down");
        Check(side.host.GetRole() == JBro::Network::NetworkRole::None, "and drops the role");
        side.Frame();
        Check(false == side.host.IsReplicating(), "and it stays down");

        Check(side.host.StartServer(7902), "the game can turn it on again");
        Check(side.host.IsReplicating(), "and replication stands again");
        side.host.Disconnect();
    }

    void TestNoSocketsFailsQuietly()
    {
        JBro::Network::Testing::ManualClock clock;
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::NetworkHost host(nullptr, clock);
        host.BindCanvas(&canvas);
        Check(false == host.HasSockets(), "no sockets");
        Check(false == host.StartServer(1) && false == host.Connect("nowhere", 1), "listening and connecting fail");
        Check(host.GetRole() == JBro::Network::NetworkRole::None && false == host.IsConnected(), "and nothing is connected");
        host.Update();
        host.StepServer();
        host.ApplyClient();
        JBro::Network::NetworkEvent event;
        Check(0 == host.TakeEvents(&event, 1), "no events");
        host.UnbindCanvas();
    }
}

int RunNetworkHostTests()
{
    try
    {
        TestTwoHostsReplicateTransforms();
        TestNothingIsBuiltUntilTheGameAsks();
        TestNoSocketsFailsQuietly();
    }
    catch (const std::exception& error)
    {
        std::cout << "NetworkHostTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "NetworkHostTests passed\n";
    return 0;
}

#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2DSystem/Network/Transform2DReplication.h>
#include <JBro/Network/Testing/ManualClock.h>
#include <JBro/Network/Testing/MemorySocketProvider.h>
#include <JBro/NetworkSystem/NetworkHost.h>
#include <JBro/NetworkSystem/System/NetworkSystems.h>
#include <JBro/Network/ServiceContext.h>
#include <JBro/Runtime/GameObject.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace
{
    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    JBro::Bool NearlyEqual(JBro::Float left, JBro::Float right)
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

    JBro::Component::Transform2D* FindReplica(Side& server, Side& client, JBro::Object::GameObject* serverObject)
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
        client.canvas.ForEachObject([&](JBro::Object::GameObject& object)
        {
            if (object.GetInstanceId() == local)
            {
                found = client.canvas.FindComponentRaw<JBro::Component::Transform2D>(&object);
            }
        });
        return found;
    }

    JBro::Bool Matches(Side& server, Side& client, JBro::Object::GameObject* serverObject)
    {
        JBro::Component::Transform2D* mine = server.canvas.FindComponentRaw<JBro::Component::Transform2D>(serverObject);
        JBro::Component::Transform2D* theirs = FindReplica(server, client, serverObject);
        if (nullptr == mine || nullptr == theirs)
        {
            return false;
        }
        return NearlyEqual(mine->position.x, theirs->position.x) && NearlyEqual(mine->position.y, theirs->position.y)
            && NearlyEqual(mine->GetRotationRadian(), theirs->GetRotationRadian()) && NearlyEqual(mine->scale.x, theirs->scale.x)
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

        constexpr JBro::Int32 Count = 20;
        JBro::Object::GameObject* objects[Count] = {};
        for (JBro::Int32 index = 0; index < Count; ++index)
        {
            objects[index] = server.canvas.CreateObject("replicated");
            JBro::Component::Transform2D* transform = server.canvas.AttachComponent<JBro::Component::Transform2D>(objects[index]);
            Check(nullptr != transform, "transform attaches");
            transform->position = { static_cast<JBro::Float>(index), static_cast<JBro::Float>(index) * 2.0f };
            transform->SetRotationRadian(JBro::Radian(0.1f * static_cast<JBro::Float>(index)));
            transform->scale = { 1.0f + 0.01f * static_cast<JBro::Float>(index), 1.0f };
        }

        auto converged = [&]()
        {
            if (client.canvas.GetObjectCount() != static_cast<std::size_t>(Count))
            {
                return false;
            }
            for (JBro::Int32 index = 0; index < Count; ++index)
            {
                if (false == Matches(server, client, objects[index]))
                {
                    return false;
                }
            }
            return true;
        };

        JBro::Bool done = false;
        for (JBro::Int32 round = 0; round < 600 && false == done; ++round)
        {
            server.Frame();
            client.Frame();
            clock.Advance(5.0);
            done = converged();
        }
        Check(done, "the client canvas holds twenty objects whose transforms equal the server's");
        Check(client.host.IsConnected() && server.host.GetConnectionCount() == 1, "one connection, ready on both sides");

        // 값이 바뀌면 따라온다.
        for (JBro::Int32 index = 0; index < Count; ++index)
        {
            JBro::Component::Transform2D* transform = server.canvas.FindComponentRaw<JBro::Component::Transform2D>(objects[index]);
            transform->position.x += 100.0f;
        }
        done = false;
        for (JBro::Int32 round = 0; round < 200 && false == done; ++round)
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
        JBro::Bool despawned = false;
        for (JBro::Int32 round = 0; round < 200 && false == despawned; ++round)
        {
            server.Frame();
            client.Frame();
            clock.Advance(5.0);
            despawned = client.canvas.GetObjectCount() == static_cast<std::size_t>(Count - 1);
        }
        Check(despawned, "the despawn reaches the client canvas");

        // 게임 메시지는 복제와 섞이지 않고 따로 꺼내진다.
        const JBro::UInt32 payload = 0xBEEF;
        Check(client.host.Send(JBro::Network::ServerConnectionId, 12, &payload, static_cast<JBro::UInt32>(sizeof(payload)), JBro::Network::NetChannel::ReliableOrdered),
            "a game message goes out");
        Check(false == client.host.Send(JBro::Network::ServerConnectionId, JBro::Network::ReplicationDeltaMessage, &payload, static_cast<JBro::UInt32>(sizeof(payload)),
                  JBro::Network::NetChannel::ReliableOrdered),
            "but the replication range is refused");
        JBro::Network::MessageView view;
        JBro::UInt32 got = 0;
        for (JBro::Int32 round = 0; round < 50 && 0 == got; ++round)
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
        for (JBro::Int32 frame = 0; frame < 3; ++frame)
        {
            side.Frame();
        }
        Check(false == side.host.IsReplicating(), "frames alone do not build it either");
        Check(side.host.GetTransport().GetReservedBytes() == 0, "and still hold nothing");

        Check(side.host.StartServer(7901), "the game turns the network on");
        Check(side.host.IsReplicating(), "now replication stands");
        Check(side.host.GetTransport().GetReservedBytes() > 0, "and the transport took its budget");

        JBro::Object::GameObject* object = side.canvas.CreateObject();
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

    // **스크립트가 부르는 표면 그대로 세션이 돌아간다**(network-plan §3-9). 호스트가 스크립트 DLL 에 넘기는 블록을
    // 이 모듈에 묶고, `Probe.cpp` 의 `ProbeNetworkedScript` 가 쓰는 것과 **같은 호출만** 쓴다 - 세션 서비스로 켜고 붙고,
    // 네트워크 서비스로 보내고 꺼내고 권한을 묻는다. 트랜스포트도 소켓도 만지지 않는다.
    //
    // 블록은 한 모듈에 하나씩 묶인다(한 프로세스에 호스트 하나라는 규율 그대로다). 그래서 서버 차례와 클라이언트 차례에
    // 각각 그쪽 블록으로 바꿔 묶는다 - 게임에서는 프로세스마다 한 번 묶고 끝이다.
    void TestScriptServicesDriveASession()
    {
        JBro::Network::Testing::MemorySocketProvider provider;
        JBro::Network::Testing::ManualClock clock;
        Side server(provider, clock);
        Side client(provider, clock);

        const auto bind = [](Side& side)
        {
            JBro::BindNetworkSystemContext(side.host.GetSystemContext());
            JBro::BindNetworkServiceContext(side.host.GetServiceContext());
        };

        // 서버 차례: 스크립트가 켜는 길이다.
        bind(server);
        Check(JBro::GetNetworkServices().Session.GetRole() == JBro::Network::NetworkRole::None, "nothing is on yet");
        Check(JBro::GetNetworkServices().Session.StartServer(7901), "the script surface starts the server");
        Check(JBro::GetNetworkServices().Session.GetRole() == JBro::Network::NetworkRole::Server, "and the role says server");

        // 클라이언트 차례: 스크립트가 붙는 길이다.
        bind(client);
        Check(JBro::GetNetworkServices().Session.Connect("memory", 7901), "the script surface connects");

        for (JBro::Int32 frame = 0; frame < 12; ++frame)
        {
            server.Frame();
            client.Frame();
            clock.Advance(5.0);
        }
        bind(client);
        Check(JBro::GetNetworkServices().Session.IsConnected(), "the client is connected");
        bind(server);
        Check(JBro::GetNetworkServices().Session.GetConnectionCount() == 1, "and the server sees it");

        // 서버 캔버스의 오브젝트는 서버에 권한이 있고, 클라이언트의 복제본은 없다 - 예시 스크립트가 이것으로 갈라진다.
        JBro::Object::GameObject* object = server.canvas.CreateObject(nullptr);
        Check(nullptr != object, "the server makes an object");
        JBro::Component::Transform2D* transform = server.canvas.AttachComponent<JBro::Component::Transform2D>(object);
        Check(nullptr != transform, "with a transform to replicate");
        for (JBro::Int32 frame = 0; frame < 12; ++frame)
        {
            server.Frame();
            client.Frame();
            clock.Advance(5.0);
        }
        bind(server);
        Check(JBro::GetNetworkServices().Network.HasAuthority(object->GetInstanceId()),
            "the server has authority over its own object");
        const JBro::Network::NetworkObjectId networkId = server.host.FindNetworkId(object->GetInstanceId());
        Check(JBro::Network::InvalidNetworkObjectId != networkId, "the object is in the replication table");
        bind(client);
        const JBro::InstanceId replica = client.host.FindLocalObject(networkId);
        Check(JBro::InvalidInstanceId != replica, "the client has a replica");
        Check(false == JBro::GetNetworkServices().Network.HasAuthority(replica),
            "and no authority over it - writing to it would be overwritten by the next snapshot");

        // 사건 메시지: 서버가 알리고 클라이언트가 꺼낸다. 예시 스크립트의 `ProbeScoreEvent` 와 같은 모양이다.
        struct ScoreEvent
        {
            JBro::UInt32 round = 0;
            JBro::Float value = 0.0f;
        };
        ScoreEvent sent;
        sent.round = 3;
        sent.value = 1.5f;
        bind(server);
        Check(JBro::GetNetworkServices().Network.Broadcast(77, sent), "the server broadcasts a POD event");

        // **메시지는 도착한 프레임에 꺼낸다.** `Update` 가 큐를 그 프레임에 온 것으로 갈아 끼우므로, 프레임을 여러 번
        // 돌린 뒤 꺼내면 이미 늦다. 스크립트가 `OnUpdate` 마다 꺼내는 것이 그래서다 - 여기서도 프레임마다 꺼낸다.
        // (`Side::Frame` 은 수신 시스템까지 돌려 큐를 비우므로 여기서는 소켓만 돌린다.)
        bind(client);
        JBro::Network::MessageView inbox[8];
        JBro::UInt32 taken = 0;
        for (JBro::Int32 pump = 0; pump < 16 && 0 == taken; ++pump)
        {
            server.host.Update();
            client.host.Update();
            taken = JBro::GetNetworkServices().Network.TakeMessages(inbox, 8);
            clock.Advance(5.0);
        }
        Check(taken >= 1, "the client takes the event through the script surface");
        Check(inbox[0].messageId == 77 && inbox[0].size == sizeof(ScoreEvent), "with the id and size it was sent with");
        ScoreEvent received;
        std::memcpy(&received, inbox[0].data, sizeof(received));
        Check(received.round == 3 && NearlyEqual(received.value, 1.5f), "and the bytes survive the trip");

        // 끌 때도 스크립트가 끈다.
        bind(server);
        JBro::GetNetworkServices().Session.Disconnect();
        Check(JBro::GetNetworkServices().Session.GetRole() == JBro::Network::NetworkRole::None, "the server is off");
        bind(client);
        JBro::GetNetworkServices().Session.Disconnect();

        // 묶은 것을 돌려놓는다 - 다음 묶음이 죽은 호스트를 보지 않게.
        JBro::BindNetworkSystemContext({});
        JBro::BindNetworkServiceContext({});
    }

    // **소유권을 넘기면 권한이 그 클라이언트로 간다**(network-plan §3-9, 1 단계). 서버가 `SetObjectOwner` 로 지목하고,
    // 클라이언트는 핸드셰이크에서 받은 자기 연결 번호로 "내 것" 을 가린다. 스폰에는 `SpawnDesc::owner` 로 실리고,
    // 스폰 뒤에 바뀌면 `ReplicationOwnerMessage` 가 알린다.
    //
    // 아직 남은 것: 소유자가 쓴 값을 서버가 받아 다른 클라이언트에 퍼뜨리는 길(2 단계)과 예측·되감기다. 그래서 지금
    // 권한은 "써도 되는가" 를 말해 주는 표식이고, 쓴 값이 망을 타고 가지는 않는다.
    void TestOwnershipMovesAuthorityToTheClient()
    {
        JBro::Network::Testing::MemorySocketProvider provider;
        JBro::Network::Testing::ManualClock clock;
        Side server(provider, clock);
        Side client(provider, clock);
        Check(server.host.StartServer(7911), "the server starts");
        Check(client.host.Connect("memory", 7911), "the client connects");
        for (JBro::Int32 frame = 0; frame < 16; ++frame)
        {
            server.Frame();
            client.Frame();
            clock.Advance(5.0);
        }
        Check(server.host.GetConnectionCount() == 1, "they are connected");

        // 클라이언트가 자기 번호를 배웠다 - 소유권 판정의 전제다(프로토콜 2 의 HelloAck).
        const JBro::Network::ConnectionId mine = client.host.GetLocalConnectionId();
        Check(JBro::Network::InvalidConnectionId != mine, "the client learned its own connection id");
        Check(mine == server.host.GetConnectionAt(0), "and it is the number the server gave it");

        JBro::Object::GameObject* object = server.canvas.CreateObject(nullptr);
        Check(nullptr != object, "the server makes an object");
        Check(nullptr != server.canvas.AttachComponent<JBro::Component::Transform2D>(object), "with a transform");
        for (JBro::Int32 frame = 0; frame < 16; ++frame)
        {
            server.Frame();
            client.Frame();
            clock.Advance(5.0);
        }
        const JBro::Network::NetworkObjectId networkId = server.host.FindNetworkId(object->GetInstanceId());
        Check(JBro::Network::InvalidNetworkObjectId != networkId, "the object replicates");
        const JBro::InstanceId replica = client.host.FindLocalObject(networkId);
        Check(JBro::InvalidInstanceId != replica, "and the client has it");

        // 넘기기 전: 서버가 갖고 클라이언트는 없다.
        Check(server.host.HasAuthority(object->GetInstanceId()), "the server starts with authority");
        Check(false == client.host.HasAuthority(replica), "and the client has none");
        Check(server.host.GetObjectOwner(object->GetInstanceId()) == JBro::Network::InvalidConnectionId,
            "the owner is the server");

        // 넘긴다.
        Check(server.host.SetObjectOwner(object->GetInstanceId(), mine), "the server hands the object over");
        for (JBro::Int32 frame = 0; frame < 16; ++frame)
        {
            server.Frame();
            client.Frame();
            clock.Advance(5.0);
        }
        Check(client.host.HasAuthority(replica), "now the client has authority");
        Check(client.host.GetObjectOwner(replica) == mine, "and knows it owns it");
        Check(false == server.host.HasAuthority(object->GetInstanceId()),
            "and the server no longer claims what it gave away");

        // 되찾는다.
        Check(server.host.SetObjectOwner(object->GetInstanceId(), JBro::Network::InvalidConnectionId),
            "the server takes it back");
        for (JBro::Int32 frame = 0; frame < 16; ++frame)
        {
            server.Frame();
            client.Frame();
            clock.Advance(5.0);
        }
        Check(false == client.host.HasAuthority(replica), "the client's authority is gone");
        Check(server.host.HasAuthority(object->GetInstanceId()), "and the server has it again");

        // 늦게 온 클라이언트는 스폰에 실린 소유자를 그대로 받는다.
        Check(server.host.SetObjectOwner(object->GetInstanceId(), mine), "hand it over once more");
        Side late(provider, clock);
        Check(late.host.Connect("memory", 7911), "a late client connects");
        for (JBro::Int32 frame = 0; frame < 24; ++frame)
        {
            server.Frame();
            client.Frame();
            late.Frame();
            clock.Advance(5.0);
        }
        const JBro::InstanceId lateReplica = late.host.FindLocalObject(networkId);
        Check(JBro::InvalidInstanceId != lateReplica, "the late client has the object");
        Check(late.host.GetObjectOwner(lateReplica) == mine, "and was told who owns it");
        Check(false == late.host.HasAuthority(lateReplica), "which is not itself");
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

JBro::Int32 RunNetworkHostTests()
{
    try
    {
        TestTwoHostsReplicateTransforms();
        TestNothingIsBuiltUntilTheGameAsks();
        TestNoSocketsFailsQuietly();
        TestScriptServicesDriveASession();
        TestOwnershipMovesAuthorityToTheClient();
    }
    catch (const std::exception& error)
    {
        std::cout << "NetworkHostTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "NetworkHostTests passed\n";
    return 0;
}

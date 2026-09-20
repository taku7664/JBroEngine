#include "TestCheck.h"

#include <JBro/Network/Replication/ReplicationClient.h>
#include <JBro/Network/Replication/ReplicationServer.h>
#include <JBro/Network/Testing/FakeReplication.h>
#include <JBro/Network/Testing/ManualClock.h>
#include <JBro/Network/Testing/MemorySocketProvider.h>
#include <JBro/Network/Transport.h>

#include <chrono>
#include <cstring>
#include <iostream>

using namespace JBro::Network;
using namespace JBro::Network::Testing;

namespace
{
    // 복제되는 상태 하나. 부착 단계의 Transform2D 어댑터가 실을 것과 닮게 두 값이다.
    struct State
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    constexpr std::uint32_t StateBytes = sizeof(State);

    // 클라이언트 한 벌: 트랜스포트·호스트·풀·복제.
    struct Client
    {
        Transport transport;
        FakeReplicationHost host;
        FakeReplicatedPool pool;
        ReplicationClient replication;

        Client(MemorySocketProvider& provider, ManualClock& clock, const JBro::Uuid& prefab, std::uint32_t elementBytes,
            const ReplicationConfig& config)
            : transport(provider, clock)
            , host(prefab)
            , pool(elementBytes, config.maxObjects)
            , replication(transport, host, config)
        {
            host.AttachPool(pool);
            replication.RegisterPool(pool);
        }

        void Pump()
        {
            transport.Update();
            MessageView views[64];
            std::uint32_t got = 0;
            while ((got = transport.TakeMessages(views, 64)) > 0)
            {
                for (std::uint32_t index = 0; index < got; ++index)
                {
                    replication.HandleMessage(views[index]);
                }
            }
            replication.Apply(1.0f);
        }

        bool IsUdpRoute() const
        {
            ReliableDiagnostics diagnostics;
            return transport.GetReliableDiagnostics(ServerConnectionId, diagnostics) && diagnostics.route == OrderedRoute::Udp;
        }
    };

    struct World
    {
        MemorySocketProvider provider;
        ManualClock clock;
        JBro::Uuid prefab = JBro::Uuid::FromName("test/replicated");
        ReplicationConfig config;
        Transport server;
        FakeReplicationHost serverHost;
        FakeReplicatedPool serverPool;
        ReplicationServer replication;
        Client client;
        ReplicationTick tick = 1;
        std::uint32_t rounds = 0;

        explicit World(const LossyConfig* lossy = nullptr, const ReplicationConfig& replicationConfig = {})
            : config(replicationConfig)
            , server(provider, clock)
            , serverHost(prefab)
            , serverPool(StateBytes, replicationConfig.maxObjects)
            , replication(server, serverHost, replicationConfig)
            , client(provider, clock, prefab, StateBytes, replicationConfig)
        {
            if (nullptr != lossy)
            {
                provider.SetLossy(*lossy);
            }
            serverHost.AttachPool(serverPool);
            replication.RegisterPool(serverPool);
            Check(server.Listen(5000), "listen");
            Check(client.transport.Connect("memory", 5000), "connect");
        }

        void Connect(Client& who)
        {
            for (int round = 0; round < 400; ++round)
            {
                RoundWithoutStep(&who);
                ReliableDiagnostics diagnostics;
                bool serverUdp = false;
                for (std::uint32_t at = 0; at < server.GetConnectionCount(); ++at)
                {
                    if (server.GetReliableDiagnostics(server.GetConnectionAt(at), diagnostics) && diagnostics.route == OrderedRoute::Udp)
                    {
                        serverUdp = true;
                    }
                }
                if (who.transport.GetConnectionState(ServerConnectionId) == ConnectionState::Connected && who.IsUdpRoute() && serverUdp)
                {
                    return;
                }
            }
            Check(false, "the client connects with UDP as the route");
        }

        void PumpServer()
        {
            server.Update();
            MessageView views[64];
            std::uint32_t got = 0;
            while ((got = server.TakeMessages(views, 64)) > 0)
            {
                for (std::uint32_t index = 0; index < got; ++index)
                {
                    replication.HandleMessage(views[index]);
                }
            }
        }

        void RoundWithoutStep(Client* extra)
        {
            PumpServer();
            client.Pump();
            if (nullptr != extra && extra != &client)
            {
                extra->Pump();
            }
            clock.Advance(5.0);
            ++rounds;
        }

        // 한 고정 스텝: 서버가 스냅숏을 찍어 보내고, 양쪽이 소켓을 돌린다.
        void Round(Client* extra = nullptr)
        {
            replication.Step(tick++);
            RoundWithoutStep(extra);
        }

        void Populate(std::uint32_t count)
        {
            for (std::uint32_t index = 0; index < count; ++index)
            {
                State state;
                state.x = static_cast<float>(index);
                state.y = static_cast<float>(index) * 0.5f;
                serverPool.Set(1 + index, &state);
            }
        }

        void Mutate(std::uint32_t count, float delta)
        {
            for (std::uint32_t index = 0; index < count; ++index)
            {
                FakeReplicatedPool::Record* record = serverPool.Find(1 + index);
                if (nullptr == record)
                {
                    continue;
                }
                State state;
                std::memcpy(&state, record->bytes, StateBytes);
                state.x += delta;
                std::memcpy(record->bytes, &state, StateBytes);
            }
        }

        // 클라이언트의 풀이 서버의 풀과 같은가. 오브젝트마다 서버 식별자 → 네트워크 식별자 → 클라이언트 지역 식별자를 거친다.
        bool Converged(Client& who) const
        {
            if (who.host.SpawnedCount() != replication.GetObjectCount() || who.pool.LiveCount() != replication.GetObjectCount())
            {
                return false;
            }
            for (std::uint32_t index = 0; index < 100000; ++index)
            {
                FakeReplicatedPool::Record* serverRecord = const_cast<FakeReplicatedPool&>(serverPool).Find(1 + index);
                if (nullptr == serverRecord)
                {
                    if (1 + index > 4096)
                    {
                        break;
                    }
                    continue;
                }
                const NetworkObjectId id = replication.FindNetworkId(1 + index);
                if (InvalidNetworkObjectId == id)
                {
                    continue;
                }
                const FakeReplicationHost::Spawned* spawned = who.host.FindByNetworkId(id);
                if (nullptr == spawned)
                {
                    return false;
                }
                FakeReplicatedPool::Record* clientRecord = who.pool.Find(spawned->local);
                if (nullptr == clientRecord || 0 != std::memcmp(clientRecord->bytes, serverRecord->bytes, StateBytes))
                {
                    return false;
                }
            }
            return true;
        }
    };

    // **깨끗한 와이어에서 50 개 오브젝트가 스폰되고, 매 스텝 바뀌는 값이 몇 스텝 뒤 클라이언트에서 같아진다.**
    void TestConvergesOnCleanWire()
    {
        World world;
        world.Connect(world.client);
        world.Populate(50);
        for (int round = 0; round < 30; ++round)
        {
            world.Mutate(10, 1.0f);
            world.Round();
        }
        for (int round = 0; round < 40; ++round)
        {
            world.Round();
        }
        Check(world.client.host.SpawnedCount() == 50, "every object was spawned once");
        Check(world.Converged(world.client), "and the client's state equals the server's");
        const ReplicationServerDiagnostics& server = world.replication.GetDiagnostics();
        const ReplicationClientDiagnostics& client = world.client.replication.GetDiagnostics();
        Check(server.fullSnapshotsSent >= 1, "the first delta was a full snapshot");
        Check(client.appliedDeltas > 20, "and deltas kept arriving");
        Check(server.lastDeltaBytes < server.lastSnapshotBytes, "a quiet delta is smaller than the full state");
        Check(server.oversizedTicks == 0, "nothing was too large to send");
    }

    // **유실·중복·재정렬 아래에서도 수렴한다.** ACK 가 사라지면 서버는 옛 기준으로 다시 델타를 내고, 그래도 안 되면 전체를 보낸다.
    void TestConvergesUnderLoss()
    {
        LossyConfig lossy;
        lossy.lossRate = 0.30;
        lossy.duplicateRate = 0.10;
        lossy.reorderDepth = 4;
        lossy.seed = 21;
        World world(&lossy);
        world.Connect(world.client);
        world.Populate(50);
        for (int round = 0; round < 60; ++round)
        {
            world.Mutate(10, 0.25f);
            world.Round();
        }
        bool converged = false;
        for (int round = 0; round < 400 && false == converged; ++round)
        {
            world.Round();
            converged = world.Converged(world.client);
        }
        if (false == converged)
        {
            const ReplicationServerDiagnostics& s = world.replication.GetDiagnostics();
            const ReplicationClientDiagnostics& c = world.client.replication.GetDiagnostics();
            std::cout << "stall: serverTick=" << world.tick << " objects=" << s.objects << " full=" << s.fullSnapshotsSent
                << " oversized=" << s.oversizedTicks << " lastDelta=" << s.lastDeltaBytes << " | client objects="
                << c.objects << " spawned=" << world.client.host.SpawnedCount() << " pool=" << world.client.pool.LiveCount()
                << " latest=" << c.latestTick << " applied=" << c.appliedDeltas << " dropped=" << c.droppedDeltas
                << " fullRecv=" << c.fullSnapshotsReceived << "\n";
            ReliableDiagnostics reliable;
            world.server.GetReliableDiagnostics(world.server.GetConnectionAt(0), reliable);
            std::cout << "  server reliable: unacked=" << reliable.unacked << " queued=" << reliable.queued << " cwnd="
                << reliable.congestionWindow << " rto=" << reliable.rtoMilliseconds << " route=" << static_cast<int>(reliable.route)
                << " ready=" << reliable.udpReady << "\n";
            world.client.transport.GetReliableDiagnostics(ServerConnectionId, reliable);
            std::cout << "  client reliable: unacked=" << reliable.unacked << " queued=" << reliable.queued
                << " standaloneAcks=" << reliable.standaloneAcks << " piggy=" << reliable.piggybackAcks << "\n";
        }
        Check(converged, "the client converges once the wire quiets down");
        const ReplicationClientDiagnostics& client = world.client.replication.GetDiagnostics();
        Check(client.appliedDeltas > 0, "deltas were applied");
        Check(client.droppedDeltas > 0 || world.replication.GetDiagnostics().fullSnapshotsSent > 1,
            "loss showed up as dropped deltas or resent full snapshots");
    }

    // **서버에서 사라진 오브젝트는 클라이언트에서도 사라진다.**
    void TestDespawnPropagates()
    {
        World world;
        world.Connect(world.client);
        world.Populate(50);
        // 스폰은 신뢰 UDP 다. 혼잡 창이 16 에서 시작해 ack 마다 하나씩 커지므로 자리 잡을 시간을 준다.
        for (int round = 0; round < 60; ++round)
        {
            world.Round();
        }
        Check(world.client.host.SpawnedCount() == 50, "fifty live");
        for (std::uint32_t index = 0; index < 10; ++index)
        {
            world.serverPool.Remove(1 + index);
        }
        for (int round = 0; round < 40; ++round)
        {
            world.Round();
        }
        Check(world.replication.GetObjectCount() == 40, "the server forgets the ten");
        Check(world.client.host.DespawnCount() == 10, "the client despawned ten");
        Check(world.client.host.SpawnedCount() == 40 && world.client.pool.LiveCount() == 40, "forty remain");
        Check(world.Converged(world.client), "and the rest still match");
    }

    // **늦게 온 클라이언트는 이미 있는 오브젝트를 전부 스폰으로 받고 전체 스냅숏으로 따라잡는다.**
    void TestLateJoinerCatchesUp()
    {
        World world;
        world.Connect(world.client);
        world.Populate(30);
        for (int round = 0; round < 20; ++round)
        {
            world.Mutate(5, 1.0f);
            world.Round();
        }
        Client joiner(world.provider, world.clock, world.prefab, StateBytes, world.config);
        Check(joiner.transport.Connect("memory", 5000), "the joiner connects");
        world.Connect(joiner);
        for (int round = 0; round < 10; ++round)
        {
            world.Mutate(5, 1.0f);
            world.Round(&joiner);
        }
        for (int round = 0; round < 60; ++round)
        {
            world.Round(&joiner);
        }
        Check(joiner.host.SpawnedCount() == 30, "the joiner spawned all thirty");
        Check(world.Converged(joiner), "and matches the server");
        Check(world.Converged(world.client), "as does the first client");
        Check(world.replication.GetDiagnostics().clients == 2, "the server tracks both");
    }

    // **호스트가 복제 대상이 아니라고 한 오브젝트는 와이어에 오르지 않는다.**
    void TestExcludedObjectsStayLocal()
    {
        World world;
        world.Connect(world.client);
        world.Populate(50);
        for (std::uint32_t index = 0; index < 5; ++index)
        {
            world.serverHost.ExcludeFromReplication(1 + index);
        }
        for (int round = 0; round < 60; ++round)
        {
            world.Round();
        }
        Check(world.replication.GetObjectCount() == 45, "the server replicates forty-five");
        Check(world.client.host.SpawnedCount() == 45, "the client spawned forty-five");
        Check(world.Converged(world.client), "and they match");
    }

    // **`Apply` 는 앞 스냅숏이 있으면 `from` 과 함께 `alpha` 를 그대로 풀에 준다.** 보간은 타입을 아는 어댑터의 일이다.
    void TestApplyPassesInterpolationInputs()
    {
        World world;
        world.Connect(world.client);
        world.Populate(3);
        for (int round = 0; round < 4; ++round)
        {
            world.Mutate(3, 1.0f);
            world.Round();
        }
        world.client.replication.Apply(0.25f);
        const NetworkObjectId id = world.replication.FindNetworkId(1);
        const FakeReplicationHost::Spawned* spawned = world.client.host.FindByNetworkId(id);
        Check(nullptr != spawned, "object 1 exists on the client");
        FakeReplicatedPool::Record* record = world.client.pool.Find(spawned->local);
        Check(nullptr != record, "with a record");
        Check(record->lastAlpha == 0.25f, "the alpha reaches the pool");
        Check(record->lastHadFrom, "along with the previous snapshot's bytes");
    }

    // **처리량 실측(network-plan §2.6 열림 항목).** 2000 오브젝트 × 8 바이트, 매 스텝 10% 변경. 숫자를 찍고 느슨한 상한만 건다.
    void TestThroughputMeasurement()
    {
        ReplicationConfig big;
        big.maxObjects = 4096;
        big.maxEntriesPerSnapshot = 8192;
        World world(nullptr, big);
        world.Connect(world.client);
        world.Populate(2000);
        // 2000 개의 스폰이 신뢰 UDP 로 다 건너갈 때까지 기다린다. 측정은 그 뒤 정상 상태에서 한다.
        for (int round = 0; round < 600 && world.client.host.SpawnedCount() < 2000; ++round)
        {
            world.Round();
        }
        Check(world.client.host.SpawnedCount() == 2000, "all 2000 objects spawned before measuring");
        const auto start = std::chrono::steady_clock::now();
        std::uint64_t deltaBytes = 0;
        constexpr int Steps = 60;
        for (int round = 0; round < Steps; ++round)
        {
            world.Mutate(200, 0.5f);
            world.Round();
            deltaBytes += world.replication.GetDiagnostics().lastDeltaBytes;
        }
        const auto end = std::chrono::steady_clock::now();
        const double totalMilliseconds = std::chrono::duration<double, std::milli>(end - start).count();
        const ReplicationServerDiagnostics& server = world.replication.GetDiagnostics();
        std::cout << "  throughput: 2000 objects, 200 changing per step, " << Steps << " steps in " << totalMilliseconds
            << " ms (" << totalMilliseconds / Steps << " ms per step incl. both transports), snapshot "
            << server.lastSnapshotEntries << " entries / " << server.lastSnapshotBytes << " bytes, delta avg "
            << deltaBytes / Steps << " bytes\n";
        for (int round = 0; round < 6; ++round)
        {
            world.Round();
        }
        Check(world.Converged(world.client), "2000 objects converge");
        Check(server.oversizedTicks == 0, "no tick exceeded the delta budget");
        Check(deltaBytes / Steps < server.lastSnapshotBytes, "the average delta is smaller than the full snapshot");
        Check(totalMilliseconds / Steps < 200.0, "a step stays well under a frame even in a debug build");
    }
}

int RunReplicationTests()
{
    try
    {
        TestConvergesOnCleanWire();
        TestConvergesUnderLoss();
        TestDespawnPropagates();
        TestLateJoinerCatchesUp();
        TestExcludedObjectsStayLocal();
        TestApplyPassesInterpolationInputs();
        TestThroughputMeasurement();
    }
    catch (const std::exception& error)
    {
        std::cout << "ReplicationTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "ReplicationTests passed\n";
    return 0;
}

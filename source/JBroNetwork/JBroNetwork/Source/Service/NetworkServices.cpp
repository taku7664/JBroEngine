#include <JBro/Network/Internal/SystemContext.h>
#include <JBro/Network/Peer/Signaling.h>
#include <JBro/Network/Service/NetworkService.h>
#include <JBro/Network/Service/NetworkSessionService.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro::Service
{
    namespace
    {
        System::INetworkSystem* Network()
        {
            return GetNetworkSystems().Network;
        }
    }

    // ── NetworkSessionService ───────────────────────────────────────────────────────────────────

    Bool NetworkSessionService::StartServer(std::uint16_t port) const
    {
        System::INetworkSystem* network = Network();
        return nullptr != network && network->StartServer(port);
    }

    Bool NetworkSessionService::Connect(const char* host, std::uint16_t port) const
    {
        System::INetworkSystem* network = Network();
        return nullptr != network && network->Connect(host, port);
    }

    void NetworkSessionService::Disconnect() const
    {
        System::INetworkSystem* network = Network();
        if (nullptr != network)
        {
            network->Disconnect();
        }
    }

    Network::NetworkRole NetworkSessionService::GetRole() const
    {
        System::INetworkSystem* network = Network();
        return nullptr != network ? network->GetRole() : Network::NetworkRole::None;
    }

    Bool NetworkSessionService::IsConnected() const
    {
        System::INetworkSystem* network = Network();
        return nullptr != network && network->IsConnected();
    }

    UInt32 NetworkSessionService::GetConnectionCount() const
    {
        System::INetworkSystem* network = Network();
        return nullptr != network ? network->GetConnectionCount() : UInt32(0);
    }

    Network::ConnectionId NetworkSessionService::GetConnectionAt(UInt32 index) const
    {
        System::INetworkSystem* network = Network();
        return nullptr != network ? network->GetConnectionAt(index) : Network::InvalidConnectionId;
    }

    double NetworkSessionService::GetRoundTripMilliseconds(Network::ConnectionId connection) const
    {
        System::INetworkSystem* network = Network();
        return nullptr != network ? network->GetRoundTripMilliseconds(connection) : -1.0;
    }

    double NetworkSessionService::GetUdpLossRate(Network::ConnectionId connection) const
    {
        System::INetworkSystem* network = Network();
        return nullptr != network ? network->GetUdpLossRate(connection) : -1.0;
    }

    UInt32 NetworkSessionService::TakeEvents(Network::NetworkEvent* events, UInt32 capacity) const
    {
        System::INetworkSystem* network = Network();
        return nullptr != network ? network->TakeEvents(events, capacity) : UInt32(0);
    }

    // ── NetworkService ──────────────────────────────────────────────────────────────────────────

    Bool NetworkService::Send(Network::ConnectionId connection, Network::MessageId messageId, const void* data, UInt32 size,
        Network::NetChannel channel) const
    {
        System::INetworkSystem* network = Network();
        if (nullptr == network || Network::IsSignalingMessage(messageId) || Network::IsReplicationMessage(messageId)
            || messageId >= Network::FirstSystemMessageId)
        {
            return false;
        }
        return network->Send(connection, messageId, data, size, channel);
    }

    Bool NetworkService::Broadcast(Network::MessageId messageId, const void* data, UInt32 size, Network::NetChannel channel) const
    {
        System::INetworkSystem* network = Network();
        if (nullptr == network || Network::IsSignalingMessage(messageId) || Network::IsReplicationMessage(messageId)
            || messageId >= Network::FirstSystemMessageId)
        {
            return false;
        }
        return network->Broadcast(messageId, data, size, channel);
    }

    UInt32 NetworkService::TakeMessages(Network::MessageView* messages, UInt32 capacity) const
    {
        System::INetworkSystem* network = Network();
        return nullptr != network ? network->TakeMessages(messages, capacity) : UInt32(0);
    }

    Network::NetworkObjectId NetworkService::FindNetworkId(InstanceId object) const
    {
        System::INetworkSystem* network = Network();
        return nullptr != network ? network->FindNetworkId(object) : Network::InvalidNetworkObjectId;
    }

    InstanceId NetworkService::FindLocalObject(Network::NetworkObjectId id) const
    {
        System::INetworkSystem* network = Network();
        return nullptr != network ? network->FindLocalObject(id) : InvalidInstanceId;
    }

    Bool NetworkService::HasAuthority(InstanceId object) const
    {
        System::INetworkSystem* network = Network();
        return nullptr != network && network->HasAuthority(object);
    }
}

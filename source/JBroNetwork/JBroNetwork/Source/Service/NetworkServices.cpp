#include <JBro/Network/Internal/SystemContext.h>
#include <JBro/Network/Service/NetworkService.h>
#include <JBro/Network/Service/NetworkSessionService.h>

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

    bool NetworkSessionService::StartServer(std::uint16_t port) const
    {
        System::INetworkSystem* network = Network();
        return nullptr != network && network->StartServer(port);
    }

    bool NetworkSessionService::Connect(const char* host, std::uint16_t port) const
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

    bool NetworkSessionService::IsConnected() const
    {
        System::INetworkSystem* network = Network();
        return nullptr != network && network->IsConnected();
    }

    std::uint32_t NetworkSessionService::GetConnectionCount() const
    {
        System::INetworkSystem* network = Network();
        return nullptr != network ? network->GetConnectionCount() : 0;
    }

    Network::ConnectionId NetworkSessionService::GetConnectionAt(std::uint32_t index) const
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

    std::uint32_t NetworkSessionService::TakeEvents(Network::NetworkEvent* events, std::uint32_t capacity) const
    {
        System::INetworkSystem* network = Network();
        return nullptr != network ? network->TakeEvents(events, capacity) : 0;
    }

    // ── NetworkService ──────────────────────────────────────────────────────────────────────────

    bool NetworkService::Send(Network::ConnectionId connection, Network::MessageId messageId, const void* data, std::uint32_t size,
        Network::NetChannel channel) const
    {
        System::INetworkSystem* network = Network();
        if (nullptr == network || Network::IsReplicationMessage(messageId) || messageId >= Network::FirstSystemMessageId)
        {
            return false;
        }
        return network->Send(connection, messageId, data, size, channel);
    }

    bool NetworkService::Broadcast(Network::MessageId messageId, const void* data, std::uint32_t size, Network::NetChannel channel) const
    {
        System::INetworkSystem* network = Network();
        if (nullptr == network || Network::IsReplicationMessage(messageId) || messageId >= Network::FirstSystemMessageId)
        {
            return false;
        }
        return network->Broadcast(messageId, data, size, channel);
    }

    std::uint32_t NetworkService::TakeMessages(Network::MessageView* messages, std::uint32_t capacity) const
    {
        System::INetworkSystem* network = Network();
        return nullptr != network ? network->TakeMessages(messages, capacity) : 0;
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

    bool NetworkService::HasAuthority(InstanceId object) const
    {
        System::INetworkSystem* network = Network();
        return nullptr != network && network->HasAuthority(object);
    }
}

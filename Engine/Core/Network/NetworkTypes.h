#pragma once

#include <cstdint>
#include <cstring>

// ── Connection identity ────────────────────────────────────────────────────────

using NetworkConnectionId = std::uint64_t;
constexpr NetworkConnectionId INVALID_CONNECTION_ID = 0;

// Client code uses SERVER_CONNECTION_ID when calling INetworkManager::Send
// to address the server it is connected to.
constexpr NetworkConnectionId SERVER_CONNECTION_ID = 1;

// ── Enums ──────────────────────────────────────────────────────────────────────

enum class ENetworkRole : std::uint8_t
{
    None,
    Client,
    Server,
};

enum class ENetworkConnectionState : std::uint8_t
{
    Disconnected,
    Connecting,
    Connected,
};

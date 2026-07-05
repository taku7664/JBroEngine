#pragma once

#include <cstdint>
#include <cstring>

// ── Connection identity ────────────────────────────────────────────────────────

using NetworkConnectionId = std::uint64_t;
constexpr NetworkConnectionId INVALID_CONNECTION_ID = 0;

// Client code uses SERVER_CONNECTION_ID when calling INetworkManager::Send
// to address the server it is connected to.
constexpr NetworkConnectionId SERVER_CONNECTION_ID = 1;

// 세션 와이어 프로토콜 버전. 와이어 포맷(세션 hello/프레이밍)이 바뀌면 증가한다.
// hello 시 양측이 교환·검증 → 불일치는 VersionMismatch 로 즉시 거부(desync 방지).
constexpr std::uint32_t NETWORK_PROTOCOL_VERSION = 1;

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

// OnDisconnected 로 전달되는 끊김 사유.
enum class ENetworkDisconnectReason : std::uint8_t
{
    Normal,          // 상대가 정상 종료(또는 로컬 Disconnect 호출).
    Timeout,         // keepalive 무응답 — 연결이 조용히 죽음(TCP 반쪽 열림 등).
    VersionMismatch, // 세션 hello 프로토콜 버전 불일치 — 서버가 거부.
    Error,           // 전송/프로토콜 오류.
};

// 메시지 전달 보장 채널. 게임이 메시지별로 명시(기본 ReliableOrdered).
// 현재는 전 채널이 신뢰 WS 로 전송된다. 웹은 영구히 그렇고(브라우저 UDP 불가),
// 네이티브는 추후 UDP 백엔드가 붙으면 비신뢰 채널이 UDP 로 승격된다(게임코드 무변경).
enum class ENetChannel : std::uint8_t
{
    ReliableOrdered,     // 순서보장 신뢰(기본). 상태 변경·중요 이벤트.
    Unreliable,          // 비신뢰(유실 허용, 최신값만 중요). 위치 등. [현재 신뢰 폴백]
    UnreliableSequenced, // 비신뢰 + 순서역전 폐기(오래된 패킷 버림). [현재 신뢰 폴백]
};

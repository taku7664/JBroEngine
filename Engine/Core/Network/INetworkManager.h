#pragma once

#include "Core/Network/NetworkTypes.h"
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

using FOnNetworkConnected    = std::function<void(NetworkConnectionId)>;
using FOnNetworkDisconnected = std::function<void(NetworkConnectionId, ENetworkDisconnectReason)>;
using FOnNetworkDataReceived = std::function<void(NetworkConnectionId, const void*, std::uint32_t)>;

// 타입드 메시지 저수준 핸들러(바이트 페이로드). 템플릿 파사드가 T 로 캐스팅해 감싼다.
using FNetworkMessageHandler = std::function<void(NetworkConnectionId, const void*, std::uint32_t)>;

// High-level network manager exposed to game code via Engine.Network.
// Platform differences (WinSock TCP vs. Emscripten WebSocket) are hidden
// behind this interface.
//
// Typical client usage:
//   Engine.Network->SetOnConnected([](NetworkConnectionId id) { ... });
//   Engine.Network->SetOnDataReceived([](NetworkConnectionId id, const void* data, uint32_t size) { ... });
//   Engine.Network->Connect("127.0.0.1", 7777);
//   // Each frame the engine calls Update() automatically.
//   Engine.Network->Send(SERVER_CONNECTION_ID, &msg, sizeof(msg));
//
// Typical server usage (Windows only):
//   Engine.Network->StartServer(7777);
//   Engine.Network->Broadcast(&msg, sizeof(msg));
class INetworkManager : public EnableSafeFromThis<INetworkManager>
{
public:
	virtual ~INetworkManager() = default;

	virtual bool Initialize() = 0;
	virtual void Finalize()   = 0;

	// Client: connect to host:port.
	virtual bool Connect(const char* host, std::uint16_t port) = 0;

	// Server: start listening.  Returns false on Web (browsers cannot listen).
	virtual bool StartServer(std::uint16_t port) = 0;

	// Disconnect everything and reset state.
	virtual void Disconnect() = 0;

	// Server: close one client connection.
	virtual void DisconnectClient(NetworkConnectionId id) = 0;

	virtual bool          IsConnected() const = 0;
	virtual bool          IsListening() const = 0;
	virtual ENetworkRole  GetRole()     const = 0;

	// 해당 연결의 최근 왕복시간(ms). 아직 측정 전이면 -1. keepalive ping/pong 기반.
	virtual double        GetRoundTripMs(NetworkConnectionId id) const = 0;

	// Client: use SERVER_CONNECTION_ID to send to the server.
	// Server: specify a client's NetworkConnectionId.
	virtual bool Send(NetworkConnectionId id, const void* data, std::uint32_t size) = 0;

	// Server only: send the same message to all connected clients.
	virtual bool Broadcast(const void* data, std::uint32_t size) = 0;

	// Called automatically by the engine every frame.
	virtual void Update() = 0;

	// OnConnected 는 전송 연결이 아니라 **세션 준비 완료**(hello 프로토콜 버전 합의) 시 발화한다.
	// 즉 콜백이 뜨면 "버전 호환되는 상대와 연결됨"이 보장된다.
	// OnDisconnected 는 사유(ENetworkDisconnectReason)와 함께 온다.
	virtual void SetOnConnected   (FOnNetworkConnected    callback) = 0;
	virtual void SetOnDisconnected(FOnNetworkDisconnected callback) = 0;
	virtual void SetOnDataReceived(FOnNetworkDataReceived callback) = 0;

	// ── 타입드 메시지 저수준(파사드가 호출; 게임 코드는 아래 템플릿을 쓴다) ──────────
	// 메시지 ID 는 게임이 명시한다(1..65535). 0 은 raw(SetOnDataReceived 경로) 예약.
	// 와이어 프레임: [uint16 LE messageId][payload]. 크로스플레이 정합을 위해 ID 는
	// 컴파일러 무관하게 고정값이어야 한다(윈서버↔웹클라 동일 ID 사용).
	virtual void         RegisterMessageType   (const void* typeTag, std::uint16_t messageId) = 0;
	virtual std::uint16_t MessageIdForType     (const void* typeTag) const = 0;
	virtual void         RegisterMessageHandler(std::uint16_t messageId, FNetworkMessageHandler handler) = 0;
	virtual bool         SendMessageBytes      (NetworkConnectionId id, std::uint16_t messageId, const void* data, std::uint32_t size, ENetChannel channel) = 0;
	virtual bool         BroadcastMessageBytes (std::uint16_t messageId, const void* data, std::uint32_t size, ENetChannel channel) = 0;

	// ── 타입드 메시지 파사드(게임 개발자 표면) ──────────────────────────────────────
	// 사용 예:
	//   struct PlayerMove { float X, Y; std::uint32_t Tick; };   // POD 여야 함
	//   Script.Network->RegisterMessage<PlayerMove>(1);
	//   Script.Network->OnMessage<PlayerMove>([](NetworkConnectionId peer, const PlayerMove& m) { ... });
	//   Script.Network->Send(peer, PlayerMove{ x, y, tick });                     // 기본 ReliableOrdered
	//   Script.Network->Send(peer, PlayerMove{...}, ENetChannel::Unreliable);     // 채널 명시
	//   Script.Network->Broadcast(PlayerMove{ ... });
	// 직렬화는 POD memcpy(리틀엔디언 고정). DLL 경계 POD 규약과 일치.
	template<typename T>
	void RegisterMessage(std::uint16_t messageId)
	{
		static_assert(std::is_trivially_copyable_v<T>,
			"네트워크 메시지는 POD(trivially copyable) 여야 한다 — DLL 경계 규약.");
		RegisterMessageType(MessageTypeTag<T>(), messageId);
	}

	template<typename T>
	void OnMessage(std::function<void(NetworkConnectionId, const T&)> callback)
	{
		static_assert(std::is_trivially_copyable_v<T>,
			"네트워크 메시지는 POD(trivially copyable) 여야 한다 — DLL 경계 규약.");
		const std::uint16_t messageId = MessageIdForType(MessageTypeTag<T>());
		RegisterMessageHandler(messageId,
			[cb = std::move(callback)](NetworkConnectionId peer, const void* data, std::uint32_t size)
			{
				if (sizeof(T) == size)
				{
					cb(peer, *static_cast<const T*>(data));
				}
			});
	}

	template<typename T>
	bool Send(NetworkConnectionId id, const T& message, ENetChannel channel = ENetChannel::ReliableOrdered)
	{
		static_assert(std::is_trivially_copyable_v<T>,
			"네트워크 메시지는 POD(trivially copyable) 여야 한다 — DLL 경계 규약.");
		return SendMessageBytes(id, MessageIdForType(MessageTypeTag<T>()),
			&message, static_cast<std::uint32_t>(sizeof(T)), channel);
	}

	template<typename T>
	bool Broadcast(const T& message, ENetChannel channel = ENetChannel::ReliableOrdered)
	{
		static_assert(std::is_trivially_copyable_v<T>,
			"네트워크 메시지는 POD(trivially copyable) 여야 한다 — DLL 경계 규약.");
		return BroadcastMessageBytes(MessageIdForType(MessageTypeTag<T>()),
			&message, static_cast<std::uint32_t>(sizeof(T)), channel);
	}

	// 타입별 고정 태그 주소(바이너리 내 유일·안정). RTTI 불필요.
	template<typename T>
	static const void* MessageTypeTag()
	{
		static const char tag = 0;
		return &tag;
	}
};

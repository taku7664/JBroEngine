#include "pch.h"
#include "WebSocketTransport.h"

#if JBRO_PLATFORM_WEB

#include <cstring>
#include <string>

// The server is always addressed with this fixed ID on the client side.
static constexpr NetworkConnectionId WEB_SERVER_ID = SERVER_CONNECTION_ID; // = 1

// ── Constructor / destructor ───────────────────────────────────────────────────

CWebSocketTransport::CWebSocketTransport() = default;

CWebSocketTransport::~CWebSocketTransport()
{
	Close();
}

// ── INetworkTransport ──────────────────────────────────────────────────────────

bool CWebSocketTransport::Connect(const char* host, std::uint16_t port)
{
	if (!host || m_socket != 0)
	{
		return false;
	}

	// Build the ws:// URL.
	std::string url;
	const bool hasScheme =
		std::strncmp(host, "ws://",  5) == 0 ||
		std::strncmp(host, "wss://", 6) == 0;

	if (hasScheme)
	{
		url = host;
		// Append port only if explicitly given and not already encoded in the URL.
		if (port != 0)
		{
			url += ':';
			url += std::to_string(port);
		}
	}
	else
	{
		url  = "ws://";
		url += host;
		url += ':';
		url += std::to_string(port);
	}

	EmscriptenWebSocketCreateAttributes attrs;
	emscripten_websocket_init_create_attributes(&attrs);
	attrs.url              = url.c_str();
	attrs.protocols        = "binary";
	attrs.createOnMainThread = EM_TRUE;

	m_socket = emscripten_websocket_new(&attrs);
	if (m_socket <= 0)
	{
		m_socket = 0;
		return false;
	}

	m_connectionId = WEB_SERVER_ID;
	m_state        = ENetworkConnectionState::Connecting;

	emscripten_websocket_set_onopen_callback   (m_socket, this, OnOpen);
	emscripten_websocket_set_onmessage_callback(m_socket, this, OnMessage);
	emscripten_websocket_set_onclose_callback  (m_socket, this, OnClose);
	emscripten_websocket_set_onerror_callback  (m_socket, this, OnError);
	return true;
}

void CWebSocketTransport::Close()
{
	if (0 == m_socket)
	{
		return;
	}

	emscripten_websocket_close(m_socket, 1000, "close");
	emscripten_websocket_delete(m_socket);
	m_socket = 0;

	const NetworkConnectionId       prevId    = m_connectionId;
	const ENetworkConnectionState   prevState = m_state;
	m_connectionId = INVALID_CONNECTION_ID;
	m_state        = ENetworkConnectionState::Disconnected;

	if (ENetworkConnectionState::Disconnected != prevState &&
	    INVALID_CONNECTION_ID != prevId &&
	    m_onDisconnected)
	{
		m_onDisconnected(prevId);
	}
}

void CWebSocketTransport::CloseConnection(NetworkConnectionId id)
{
	if (id == m_connectionId)
	{
		Close();
	}
}

ENetworkConnectionState CWebSocketTransport::GetConnectionState(NetworkConnectionId id) const
{
	return (id == m_connectionId) ? m_state : ENetworkConnectionState::Disconnected;
}

bool CWebSocketTransport::Send(
	NetworkConnectionId id, const void* data, std::uint32_t size)
{
	if (id != m_connectionId ||
	    0 == m_socket ||
	    ENetworkConnectionState::Connected != m_state)
	{
		return false;
	}

	return EMSCRIPTEN_RESULT_SUCCESS ==
	       emscripten_websocket_send_binary(
	           m_socket,
	           const_cast<void*>(data),
	           size);
}

void CWebSocketTransport::SetOnConnected(FOnTransportConnected callback)
{
	m_onConnected = std::move(callback);
}

void CWebSocketTransport::SetOnDisconnected(FOnTransportDisconnected callback)
{
	m_onDisconnected = std::move(callback);
}

void CWebSocketTransport::SetOnData(FOnTransportData callback)
{
	m_onData = std::move(callback);
}

// ── Emscripten static callbacks ────────────────────────────────────────────────

EM_BOOL CWebSocketTransport::OnOpen(
	int, const EmscriptenWebSocketOpenEvent*, void* userData)
{
	auto* self  = static_cast<CWebSocketTransport*>(userData);
	self->m_state = ENetworkConnectionState::Connected;
	if (self->m_onConnected)
	{
		self->m_onConnected(self->m_connectionId);
	}
	return EM_TRUE;
}

EM_BOOL CWebSocketTransport::OnMessage(
	int, const EmscriptenWebSocketMessageEvent* e, void* userData)
{
	auto* self = static_cast<CWebSocketTransport*>(userData);
	// Only handle binary frames.
	if (self->m_onData && EM_FALSE == e->isText)
	{
		self->m_onData(
			self->m_connectionId,
			static_cast<const std::uint8_t*>(e->data),
			static_cast<std::uint32_t>(e->numBytes));
	}
	return EM_TRUE;
}

EM_BOOL CWebSocketTransport::OnClose(
	int, const EmscriptenWebSocketCloseEvent*, void* userData)
{
	auto* self = static_cast<CWebSocketTransport*>(userData);
	// Socket is already closed by the browser at this point.
	self->m_socket = 0;
	const NetworkConnectionId id = self->m_connectionId;
	self->m_connectionId = INVALID_CONNECTION_ID;
	self->m_state        = ENetworkConnectionState::Disconnected;
	if (self->m_onDisconnected && INVALID_CONNECTION_ID != id)
	{
		self->m_onDisconnected(id);
	}
	return EM_TRUE;
}

EM_BOOL CWebSocketTransport::OnError(
	int, const EmscriptenWebSocketErrorEvent*, void* userData)
{
	auto* self = static_cast<CWebSocketTransport*>(userData);
	if (self->m_socket != 0)
	{
		emscripten_websocket_delete(self->m_socket);
		self->m_socket = 0;
	}
	const NetworkConnectionId id = self->m_connectionId;
	self->m_connectionId = INVALID_CONNECTION_ID;
	self->m_state        = ENetworkConnectionState::Disconnected;
	if (self->m_onDisconnected && INVALID_CONNECTION_ID != id)
	{
		self->m_onDisconnected(id);
	}
	return EM_TRUE;
}

#endif // JBRO_PLATFORM_WEB

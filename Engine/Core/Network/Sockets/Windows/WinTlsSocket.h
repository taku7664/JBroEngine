#pragma once

#include "Core/Platform/PlatformDefines.h"
#if JBRO_PLATFORM_WINDOWS

#include "Core/Network/Sockets/ISocket.h"

#include <cstdint>
#include <string>
#include <vector>

// 이미 연결된 TCP ISocket 을 감싸 그 위에 TLS(SChannel)를 얹는 데코레이터.
// WS 코덱은 평문 바이트만 다루고, 이 계층이 와이어에서 암복호한다(wss = ws + TLS).
//   - 클라이언트: InitializeSecurityContext 루프로 핸드셰이크, 서버 인증서 검증(옵션 스킵).
//   - 서버: AcceptSecurityContext 루프, 자기 인증서 제시(PCCERT_CONTEXT).
// 논블로킹 — PollConnect 가 TCP 연결→TLS 핸드셰이크 완료까지 구동, Recv/Send 가 복/암호화.
//
// 서버는 인증서가 필요하다. 브라우저가 신뢰하는 실배포엔 도메인+정식 인증서가 있어야 하고,
// 로컬/전용서버 dev 용으론 CreateSelfSignedServerCert 로 즉석 생성한 것을 넘길 수 있다.
class CWinTlsSocket final : public ISocket
{
public:
	// serverCertContext: 서버 모드면 PCCERT_CONTEXT(소유권 이전 안 함, 호출측 유지). 클라면 nullptr.
	// hostName: 클라 SNI/검증 대상 호스트. skipCertValidation: self-signed dev 용(운영 금지).
	CWinTlsSocket(OwnerPtr<ISocket> inner, bool isServer,
		const std::string& hostName, void* serverCertContext, bool skipCertValidation);
	~CWinTlsSocket() override;

	bool Connect(const char* host, std::uint16_t port) override;
	bool Listen(std::uint16_t port) override;         // 미지원(리슨 소켓은 평문 TCP).
	OwnerPtr<ISocket> Accept() override;              // 미지원.
	ESocketConnect PollConnect() override;            // TCP 연결 + TLS 핸드셰이크 구동.
	ESocketIo Recv(void* buffer, std::size_t size, std::size_t& outBytes) override;
	ESocketIo Send(const void* data, std::size_t size, std::size_t& outBytes) override;
	void Close() override;
	bool IsValid() const override;

private:
	enum class ETls : std::uint8_t { TcpConnecting, Handshaking, Established, Failed };

	bool AcquireCredentials();
	ESocketConnect DoHandshake();          // 한 스텝 진행.
	bool FlushOutgoing();                   // 버퍼된 암호문을 inner 로 밀어냄. false=오류.
	bool PullCiphertext();                  // inner 에서 암호문을 m_incoming 로 읽음. false=닫힘/오류.
	void QueryStreamSizes();

private:
	OwnerPtr<ISocket> m_inner;
	bool              m_isServer;
	std::string       m_hostName;
	void*             m_serverCert;         // PCCERT_CONTEXT (호출측 소유).
	bool              m_skipValidation;
	ETls              m_state = ETls::TcpConnecting;

	// SChannel 핸들(불투명 저장 — 헤더에 <security.h> 안 들임).
	bool          m_hasCred = false;
	std::uint64_t m_credHandle[2] = { 0, 0 };  // CredHandle.
	bool          m_hasCtxt = false;
	std::uint64_t m_ctxtHandle[2] = { 0, 0 };  // CtxtHandle.

	std::uint32_t m_headerSize  = 0;
	std::uint32_t m_trailerSize = 0;
	std::uint32_t m_maxMessage  = 0;

	std::vector<std::uint8_t> m_incoming;   // inner 에서 읽은 미처리 암호문.
	std::vector<std::uint8_t> m_outgoing;   // inner 로 보낼 대기 암호문.
	std::vector<std::uint8_t> m_decrypted;  // 복호됐지만 아직 반환 안 한 평문.
};

// SChannel 서버용 자기서명 인증서 생성(dev/로컬 전용). 실패 시 nullptr.
// 반환값은 PCCERT_CONTEXT — 다 쓰면 FreeSelfSignedServerCert 로 해제.
void* CreateSelfSignedServerCert(const wchar_t* subjectCommonName);
void  FreeSelfSignedServerCert(void* certContext);

#endif // JBRO_PLATFORM_WINDOWS

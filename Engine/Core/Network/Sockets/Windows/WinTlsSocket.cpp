#include "pch.h"
#include "WinTlsSocket.h"

#if JBRO_PLATFORM_WINDOWS

#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>
#include <wincrypt.h>
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")

// SCHANNEL_CRED 는 최신 SDK 에서 deprecated 표시(SCH_CREDENTIALS 권장)지만, 후자는
// UNICODE_STRING 등 ntdef 의존(SCHANNEL_USE_BLACKLISTS)이라 여기선 legacy 구조체를 쓴다.
#pragma warning(push)
#pragma warning(disable : 4996)

#include <cstring>

namespace
{
	// SChannel 핸드셰이크 요청 플래그.
	constexpr DWORD ISC_FLAGS_BASE =
		ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
		ISC_RET_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
	constexpr DWORD ASC_FLAGS_BASE =
		ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY |
		ASC_REQ_EXTENDED_ERROR | ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM;

	constexpr std::size_t TLS_RECV_CHUNK = 8192;
}

static_assert(sizeof(CredHandle) <= sizeof(std::uint64_t) * 2, "CredHandle storage too small");
static_assert(sizeof(CtxtHandle) <= sizeof(std::uint64_t) * 2, "CtxtHandle storage too small");

CWinTlsSocket::CWinTlsSocket(OwnerPtr<ISocket> inner, bool isServer,
	const std::string& hostName, void* serverCertContext, bool skipCertValidation)
	: m_inner(std::move(inner))
	, m_isServer(isServer)
	, m_hostName(hostName)
	, m_serverCert(serverCertContext)
	, m_skipValidation(skipCertValidation)
{
}

CWinTlsSocket::~CWinTlsSocket()
{
	Close();
}

bool CWinTlsSocket::Connect(const char* host, std::uint16_t port)
{
	// 클라: inner TCP 연결 시작. TLS 는 PollConnect 에서 TCP 완료 후 진행.
	return m_inner && m_inner->Connect(host, port);
}

bool CWinTlsSocket::Listen(std::uint16_t)          { return false; }
OwnerPtr<ISocket> CWinTlsSocket::Accept()          { return nullptr; }

bool CWinTlsSocket::IsValid() const
{
	return m_inner && m_inner->IsValid() && ETls::Failed != m_state;
}

bool CWinTlsSocket::AcquireCredentials()
{
	SCHANNEL_CRED cred = {};
	cred.dwVersion = SCHANNEL_CRED_VERSION;

	PCCERT_CONTEXT certs[1];
	if (m_isServer)
	{
		if (nullptr == m_serverCert)
		{
			return false;
		}
		certs[0]      = static_cast<PCCERT_CONTEXT>(m_serverCert);
		cred.cCreds   = 1;
		cred.paCred   = certs;
	}
	else
	{
		cred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS |
			(m_skipValidation ? SCH_CRED_MANUAL_CRED_VALIDATION : SCH_CRED_AUTO_CRED_VALIDATION);
	}

	TimeStamp expiry;
	const SECURITY_STATUS ss = AcquireCredentialsHandleA(
		nullptr, const_cast<SEC_CHAR*>(UNISP_NAME_A),
		m_isServer ? SECPKG_CRED_INBOUND : SECPKG_CRED_OUTBOUND,
		nullptr, &cred, nullptr, nullptr,
		reinterpret_cast<CredHandle*>(m_credHandle), &expiry);

	if (SEC_E_OK != ss)
	{
		return false;
	}
	m_hasCred = true;
	return true;
}

bool CWinTlsSocket::FlushOutgoing()
{
	while (false == m_outgoing.empty())
	{
		std::size_t sent = 0;
		const ESocketIo io = m_inner->Send(m_outgoing.data(), m_outgoing.size(), sent);
		if (ESocketIo::Ok == io)
		{
			m_outgoing.erase(m_outgoing.begin(), m_outgoing.begin() + static_cast<std::ptrdiff_t>(sent));
			continue;
		}
		if (ESocketIo::WouldBlock == io)
		{
			break; // 소켓 버퍼 참 — 다음 기회에.
		}
		return false; // Closed/Error.
	}
	return true;
}

bool CWinTlsSocket::PullCiphertext()
{
	std::uint8_t chunk[TLS_RECV_CHUNK];
	std::size_t  got = 0;
	const ESocketIo io = m_inner->Recv(chunk, sizeof(chunk), got);
	if (ESocketIo::Ok == io)
	{
		m_incoming.insert(m_incoming.end(), chunk, chunk + got);
		return true;
	}
	return false; // WouldBlock/Closed/Error — 호출측이 상태로 구분(여기선 진전 없음).
}

void CWinTlsSocket::QueryStreamSizes()
{
	SecPkgContext_StreamSizes sizes = {};
	if (SEC_E_OK == QueryContextAttributes(
		reinterpret_cast<CtxtHandle*>(m_ctxtHandle), SECPKG_ATTR_STREAM_SIZES, &sizes))
	{
		m_headerSize  = sizes.cbHeader;
		m_trailerSize = sizes.cbTrailer;
		m_maxMessage  = sizes.cbMaximumMessage;
	}
}

ESocketConnect CWinTlsSocket::PollConnect()
{
	if (ETls::Failed == m_state)
	{
		return ESocketConnect::Failed;
	}
	if (ETls::Established == m_state)
	{
		return ESocketConnect::Connected;
	}

	if (ETls::TcpConnecting == m_state)
	{
		const ESocketConnect tcp = m_inner->PollConnect();
		if (ESocketConnect::Pending == tcp)
		{
			return ESocketConnect::Pending;
		}
		if (ESocketConnect::Failed == tcp)
		{
			m_state = ETls::Failed;
			return ESocketConnect::Failed;
		}

		// TCP 연결됨 → 자격 취득 후 TLS 핸드셰이크 시작.
		if (false == AcquireCredentials())
		{
			m_state = ETls::Failed;
			return ESocketConnect::Failed;
		}
		m_state = ETls::Handshaking;

		if (false == m_isServer)
		{
			// 클라: 입력 없이 ClientHello 토큰 생성.
			SecBuffer   outBuf = { 0, SECBUFFER_TOKEN, nullptr };
			SecBufferDesc outDesc = { SECBUFFER_VERSION, 1, &outBuf };
			DWORD retFlags = 0;
			TimeStamp expiry;
			const SECURITY_STATUS ss = InitializeSecurityContextA(
				reinterpret_cast<CredHandle*>(m_credHandle), nullptr,
				m_hostName.empty() ? nullptr : const_cast<SEC_CHAR*>(m_hostName.c_str()),
				ISC_FLAGS_BASE | (m_skipValidation ? ISC_REQ_MANUAL_CRED_VALIDATION : 0),
				0, 0, nullptr, 0,
				reinterpret_cast<CtxtHandle*>(m_ctxtHandle), &outDesc, &retFlags, &expiry);

			if (SEC_I_CONTINUE_NEEDED != ss)
			{
				m_state = ETls::Failed;
				return ESocketConnect::Failed;
			}
			m_hasCtxt = true;
			if (nullptr != outBuf.pvBuffer && outBuf.cbBuffer > 0)
			{
				const std::uint8_t* p = static_cast<const std::uint8_t*>(outBuf.pvBuffer);
				m_outgoing.insert(m_outgoing.end(), p, p + outBuf.cbBuffer);
				FreeContextBuffer(outBuf.pvBuffer);
			}
			if (false == FlushOutgoing())
			{
				m_state = ETls::Failed;
				return ESocketConnect::Failed;
			}
		}
		return ESocketConnect::Pending;
	}

	// Handshaking.
	return DoHandshake();
}

ESocketConnect CWinTlsSocket::DoHandshake()
{
	// 대기 송신 우선 밀어냄.
	if (false == FlushOutgoing())
	{
		m_state = ETls::Failed;
		return ESocketConnect::Failed;
	}

	// 암호문 확보.
	PullCiphertext();
	if (m_incoming.empty())
	{
		return ESocketConnect::Pending; // 아직 상대 토큰 없음.
	}

	SecBuffer inBuffers[2];
	inBuffers[0] = { static_cast<unsigned long>(m_incoming.size()), SECBUFFER_TOKEN, m_incoming.data() };
	inBuffers[1] = { 0, SECBUFFER_EMPTY, nullptr };
	SecBufferDesc inDesc = { SECBUFFER_VERSION, 2, inBuffers };

	SecBuffer   outBuf = { 0, SECBUFFER_TOKEN, nullptr };
	SecBufferDesc outDesc = { SECBUFFER_VERSION, 1, &outBuf };

	DWORD retFlags = 0;
	TimeStamp expiry;
	SECURITY_STATUS ss;

	if (m_isServer)
	{
		ss = AcceptSecurityContext(
			reinterpret_cast<CredHandle*>(m_credHandle),
			m_hasCtxt ? reinterpret_cast<CtxtHandle*>(m_ctxtHandle) : nullptr,
			&inDesc, ASC_FLAGS_BASE, 0,
			reinterpret_cast<CtxtHandle*>(m_ctxtHandle), &outDesc, &retFlags, &expiry);
	}
	else
	{
		ss = InitializeSecurityContextA(
			reinterpret_cast<CredHandle*>(m_credHandle),
			reinterpret_cast<CtxtHandle*>(m_ctxtHandle),
			m_hostName.empty() ? nullptr : const_cast<SEC_CHAR*>(m_hostName.c_str()),
			ISC_FLAGS_BASE | (m_skipValidation ? ISC_REQ_MANUAL_CRED_VALIDATION : 0),
			0, 0, &inDesc, 0,
			reinterpret_cast<CtxtHandle*>(m_ctxtHandle), &outDesc, &retFlags, &expiry);
	}

	if (SEC_E_INCOMPLETE_MESSAGE == ss)
	{
		return ESocketConnect::Pending; // 토큰 일부만 도착 — 더 받자(m_incoming 유지).
	}

	m_hasCtxt = true;

	// 출력 토큰 송신.
	if (nullptr != outBuf.pvBuffer && outBuf.cbBuffer > 0)
	{
		const std::uint8_t* p = static_cast<const std::uint8_t*>(outBuf.pvBuffer);
		m_outgoing.insert(m_outgoing.end(), p, p + outBuf.cbBuffer);
		FreeContextBuffer(outBuf.pvBuffer);
		if (false == FlushOutgoing())
		{
			m_state = ETls::Failed;
			return ESocketConnect::Failed;
		}
	}

	// 입력 소비 처리: 잔여(EXTRA) 는 다음 라운드/앱데이터로 보존.
	if (SECBUFFER_EXTRA == inBuffers[1].BufferType && inBuffers[1].cbBuffer > 0)
	{
		const std::size_t extra = inBuffers[1].cbBuffer;
		m_incoming.erase(m_incoming.begin(),
			m_incoming.begin() + static_cast<std::ptrdiff_t>(m_incoming.size() - extra));
	}
	else
	{
		m_incoming.clear();
	}

	if (SEC_E_OK == ss)
	{
		QueryStreamSizes();
		m_state = ETls::Established;
		return ESocketConnect::Connected;
	}
	if (SEC_I_CONTINUE_NEEDED == ss)
	{
		return ESocketConnect::Pending;
	}

	m_state = ETls::Failed;
	return ESocketConnect::Failed;
}

ESocketIo CWinTlsSocket::Recv(void* buffer, std::size_t size, std::size_t& outBytes)
{
	outBytes = 0;
	if (ETls::Established != m_state)
	{
		return (ETls::Failed == m_state) ? ESocketIo::Error : ESocketIo::WouldBlock;
	}

	FlushOutgoing();

	for (;;)
	{
		// 남은 평문 우선 반환.
		if (false == m_decrypted.empty())
		{
			const std::size_t n = (size < m_decrypted.size()) ? size : m_decrypted.size();
			std::memcpy(buffer, m_decrypted.data(), n);
			m_decrypted.erase(m_decrypted.begin(), m_decrypted.begin() + static_cast<std::ptrdiff_t>(n));
			outBytes = n;
			return ESocketIo::Ok;
		}

		// 가진 암호문 복호 시도.
		if (false == m_incoming.empty())
		{
			SecBuffer buffers[4];
			buffers[0] = { static_cast<unsigned long>(m_incoming.size()), SECBUFFER_DATA, m_incoming.data() };
			buffers[1] = { 0, SECBUFFER_EMPTY, nullptr };
			buffers[2] = { 0, SECBUFFER_EMPTY, nullptr };
			buffers[3] = { 0, SECBUFFER_EMPTY, nullptr };
			SecBufferDesc desc = { SECBUFFER_VERSION, 4, buffers };

			const SECURITY_STATUS ss = DecryptMessage(
				reinterpret_cast<CtxtHandle*>(m_ctxtHandle), &desc, 0, nullptr);

			if (SEC_E_OK == ss)
			{
				const std::uint8_t* dataPtr = nullptr;
				std::size_t         dataLen  = 0;
				const std::uint8_t* extraPtr = nullptr;
				std::size_t         extraLen  = 0;
				for (int i = 0; i < 4; ++i)
				{
					if (SECBUFFER_DATA == buffers[i].BufferType && nullptr == dataPtr)
					{
						dataPtr = static_cast<const std::uint8_t*>(buffers[i].pvBuffer);
						dataLen = buffers[i].cbBuffer;
					}
					else if (SECBUFFER_EXTRA == buffers[i].BufferType)
					{
						extraPtr = static_cast<const std::uint8_t*>(buffers[i].pvBuffer);
						extraLen = buffers[i].cbBuffer;
					}
				}
				if (nullptr != dataPtr && dataLen > 0)
				{
					m_decrypted.insert(m_decrypted.end(), dataPtr, dataPtr + dataLen);
				}
				// 잔여 암호문(EXTRA)은 다음 레코드 — 보존.
				std::vector<std::uint8_t> leftover;
				if (nullptr != extraPtr && extraLen > 0)
				{
					leftover.assign(extraPtr, extraPtr + extraLen);
				}
				m_incoming.swap(leftover);
				continue; // 위에서 m_decrypted 반환.
			}
			if (SEC_E_INCOMPLETE_MESSAGE == ss)
			{
				// 레코드 미완 — 더 받는다(아래 Pull).
			}
			else if (SEC_I_CONTEXT_EXPIRED == ss)
			{
				return ESocketIo::Closed; // 상대가 TLS 정상 종료.
			}
			else
			{
				return ESocketIo::Error; // (재협상 SEC_I_RENEGOTIATE 포함) 미지원 — 종료.
			}
		}

		// 더 받아야 함.
		std::uint8_t chunk[TLS_RECV_CHUNK];
		std::size_t  got = 0;
		const ESocketIo io = m_inner->Recv(chunk, sizeof(chunk), got);
		if (ESocketIo::Ok == io)
		{
			m_incoming.insert(m_incoming.end(), chunk, chunk + got);
			continue;
		}
		if (ESocketIo::WouldBlock == io)
		{
			return ESocketIo::WouldBlock; // 지금은 줄 게 없음.
		}
		return io; // Closed/Error.
	}
}

ESocketIo CWinTlsSocket::Send(const void* data, std::size_t size, std::size_t& outBytes)
{
	outBytes = 0;
	if (ETls::Established != m_state)
	{
		return ESocketIo::Error;
	}

	if (false == FlushOutgoing())
	{
		return ESocketIo::Error;
	}

	const std::uint8_t* src = static_cast<const std::uint8_t*>(data);
	std::size_t remaining = size;
	while (remaining > 0)
	{
		const std::size_t chunk = (remaining < m_maxMessage) ? remaining : m_maxMessage;

		std::vector<std::uint8_t> record(m_headerSize + chunk + m_trailerSize);
		std::memcpy(record.data() + m_headerSize, src, chunk);

		SecBuffer buffers[4];
		buffers[0] = { m_headerSize, SECBUFFER_STREAM_HEADER, record.data() };
		buffers[1] = { static_cast<unsigned long>(chunk), SECBUFFER_DATA, record.data() + m_headerSize };
		buffers[2] = { m_trailerSize, SECBUFFER_STREAM_TRAILER, record.data() + m_headerSize + chunk };
		buffers[3] = { 0, SECBUFFER_EMPTY, nullptr };
		SecBufferDesc desc = { SECBUFFER_VERSION, 4, buffers };

		const SECURITY_STATUS ss = EncryptMessage(
			reinterpret_cast<CtxtHandle*>(m_ctxtHandle), 0, &desc, 0);
		if (SEC_E_OK != ss)
		{
			return ESocketIo::Error;
		}

		const std::size_t total = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
		m_outgoing.insert(m_outgoing.end(), record.data(), record.data() + total);

		src       += chunk;
		remaining -= chunk;
	}

	if (false == FlushOutgoing())
	{
		return ESocketIo::Error;
	}

	// 평문 전량을 수용(암호문은 내부 버퍼가 책임). 상위 SendBuffer 관점에선 전송 완료.
	outBytes = size;
	return ESocketIo::Ok;
}

void CWinTlsSocket::Close()
{
	if (m_hasCtxt)
	{
		DeleteSecurityContext(reinterpret_cast<CtxtHandle*>(m_ctxtHandle));
		m_hasCtxt = false;
	}
	if (m_hasCred)
	{
		FreeCredentialsHandle(reinterpret_cast<CredHandle*>(m_credHandle));
		m_hasCred = false;
	}
	if (m_inner)
	{
		m_inner->Close();
	}
}

// ── 자기서명 서버 인증서(dev/로컬 전용) ──────────────────────────────────────────

void* CreateSelfSignedServerCert(const wchar_t* subjectCommonName)
{
	// "CN=<name>" → 인코딩된 subject blob.
	std::wstring subject = L"CN=";
	subject += (nullptr != subjectCommonName) ? subjectCommonName : L"localhost";

	DWORD encodedSize = 0;
	if (FALSE == CertStrToNameW(X509_ASN_ENCODING, subject.c_str(), CERT_X500_NAME_STR,
		nullptr, nullptr, &encodedSize, nullptr))
	{
		return nullptr;
	}
	std::vector<BYTE> encoded(encodedSize);
	if (FALSE == CertStrToNameW(X509_ASN_ENCODING, subject.c_str(), CERT_X500_NAME_STR,
		nullptr, encoded.data(), &encodedSize, nullptr))
	{
		return nullptr;
	}

	CERT_NAME_BLOB subjectBlob = { encodedSize, encoded.data() };

	// 키/컨테이너는 CertCreateSelfSignCertificate 가 임시로 생성·연결(pKeyProvInfo=null).
	PCCERT_CONTEXT cert = CertCreateSelfSignCertificate(
		0, &subjectBlob, 0, nullptr, nullptr, nullptr, nullptr, nullptr);
	return const_cast<void*>(reinterpret_cast<const void*>(cert)); // PCCERT_CONTEXT(const*) → void*.
}

void FreeSelfSignedServerCert(void* certContext)
{
	if (nullptr != certContext)
	{
		CertFreeCertificateContext(static_cast<PCCERT_CONTEXT>(certContext));
	}
}

#pragma warning(pop)

#endif // JBRO_PLATFORM_WINDOWS

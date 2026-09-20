# 네트워크 계획 (기존 엔진 재검토 뒤 재설계)

> 계약은 `docs/ProjectRule.md`, 결정은 `tasks/todo.md` Decisions 다. 이 문서는 그 둘을 향해 가는 순서와
> 상태를 적는다. 상태는 항목마다 `[완료]` `[진행]` `[제안]` `[가정]` `[열림]` 으로 붙인다.
> `[제안]` 은 **사용자 확인 전**이다. 2026-09-20 에 §0 의 방향(별도 프로젝트·폴링 유지·전용 컨테이너·복제 (A)·WebRTC 포함·
> 호스트 둘 검증)이 확인돼 D-122 가 됐다. 같은 날 §2.2 의 뜻이 정정됐다 - 제약은 네트워크의 의존이 아니라 **엔진 트리의 불변**이다.

## 0. 확정된 방향 (2026-09-20, D-122)

1. **별도 VS 프로젝트로 판다.** 엔진은 협업 중이므로, 꼭 붙여야 하는 순간(§3 의 5 단계) 전까지 **`source/JBroEngine/` 아래의
   파일은 네트워크 작업 때문에 바뀌지 않는다.** 네트워크가 무엇에 의존하는지는 제약이 아니다 - `JBroCore` 의 컨테이너든
   `JBroCanvas` 의 풀이든 필요하면 참조한다. 참조는 엔진 파일을 바꾸지 않는다(§2.2).
2. **와이어는 기존 엔진의 하이브리드를 계승한다.** 전 플랫폼 WebSocket(RFC6455) 기준선 위에 네이티브 전용 UDP 를 덧대고,
   그 UDP 위에 기존 엔진이 끝까지 만든 Reliable UDP 엔진을 얹는다(§1.2·§2.5).
3. **메인 스레드 폴링으로 시작한다.** 기존 엔진과 같다. 다만 트랜스포트 밖으로 나가는 경계는 전부 "POD 큐에서 꺼내 가기"
   모양으로 잡아, 나중에 소켓 I/O 를 워커로 옮겨도 서비스와 스크립트 API 가 바뀌지 않게 한다(§2.4).
4. **STL 컨테이너는 쓰지 않는다.** `Array` / `Table` / `String` / `OwnerPtr` 로 간다. 매 프레임 경로의 버퍼는 초기화 때 잡은
   고정 크기 원형 배열이다(§2.5·§4).
5. **동기화는 (A) 컴포넌트 풀 스냅숏 델타까지 엔진이 맡는다.** 별도 동기화 컴포넌트(`NetworkTransform2D` 같은 것)는 만들지
   않는다. 메시지만 제공하는 0번은 (A) 의 기반으로 들어간다(§2.6).
6. **WebRTC DataChannel 을 범위에 넣는다.** 웹끼리 P2P 로 이어져 웹이 호스트가 될 수 있게 한다. 기존 엔진의 "웹은 서버 불가"
   규약은 "웹은 WebSocket 을 받을 수 없다" 로 고쳐 적는다(§2.7).
7. **검증은 테스트 프로젝트 안에서 호스트 둘을 띄워 루프백으로 잇는다.** 그러려면 트랜스포트가 프로세스 전역 상태를 갖지 않아야
   한다. 유실 주입은 기존 엔진의 `CLossyUdpSocket` 데코레이터를 옮겨 쓴다(§3).

## 1. 기존 엔진의 네트워크 - 무엇이 있었고 무엇이 없었나

위치는 `C:\Users\박주형\source\repos\JBroEngine\Engine\Core\Network` 다. 설계 README 는 커밋 `38d95f08` 에 있다가
마크다운 정리 커밋 `0f152597` 에서 지워졌다. 아래는 헤더·커밋 이력·`Docs/WorkNotes/NetworkDeployGuide.md` 로 확인한 것이다.

### 1.1 계층

```
INetworkManager      세션(hello·프로토콜 버전 검증·keepalive·RTT·끊김 사유) + [uint16 msgId][payload] 프레이밍
                     + 채널 라우팅(신뢰 → WS 또는 UDP-신뢰, 비신뢰 → UDP, 안 되면 WS 폴백)
INetworkTransport    메시지 단위 WS. 네이티브 = ISocket + WS 코덱(+ SChannel 데코레이터로 wss), 웹 = emscripten
CUdpChannel          토큰 부트스트랩 + [token][flags][seq][ack][frag][msgId][payload] + CReliableEndpoint
ISocket / IUdpSocket 논블로킹 소켓. Windows 검증됨, POSIX 는 "미검증" 표시
```

- 스크립트에는 `ScriptCore::Network`(`SafePtr<INetworkManager>`)로 노출됐다. 게임은 `RegisterMessage<T>(id)` /
  `OnMessage<T>(callback)` / `Send(peer, T, channel)` / `Broadcast` 를 쓴다.
- 메시지는 POD 만 허용(`static_assert(is_trivially_copyable)`)하고 memcpy 로 직렬화한다. 리틀엔디언 고정.
- 메시지 ID 는 게임이 숫자(1..0xFEFF)로 명시한다. 윈 서버와 웹 클라가 다른 컴파일러로 빌드되므로 타입 태그 주소를 와이어에
  쓸 수 없다는 이유가 주석에 있다. 시스템 메시지는 `0xFF00~` 예약.
- 콜백 안에서 `Disconnect`/`Finalize` 를 불러도 되게 teardown 을 `Update` 말미로 미룬다(재진입 안전).
- `OnConnected` 는 소켓 연결이 아니라 **hello 로 버전이 맞은 뒤** 뜬다. 불일치는 `VersionMismatch` 로 즉시 거부.

### 1.2 Reliable UDP - 실제로 끝까지 갔다

커밋 순서: `d8759880`(ACK·재전송·RTO·dedup, "3b") → `0cd17474`(Ordered 재정렬 버퍼 + Unordered 채널, "3c") →
`c96afd76`(프래그먼트 + AIMD 혼잡 윈도우 + 매니저 통합, "3d/3e") → `a6a37be8`(지연 ACK 피기백).

- `CReliableEndpoint` 가 엔진이다. 한 연결(서버-피어 하나 또는 클라-서버)당 하나. **소켓을 직접 만지지 않고** `FEmit` 콜백으로
  송출하며, 시계도 호출측이 밀리초로 주입한다. 이 두 성질 덕에 컨테이너만 바꾸면 그대로 옮길 수 있다.
- 채널 넷: `ReliableOrdered`(기본), `ReliableUnordered`(HOL 회피), `Unreliable`, `UnreliableSequenced`. 두 신뢰 채널은 한 seq
  공간을 공유하고, Ordered 는 수신 워터마크(`m_recvNext`)로 순서를 판정한다.
- 연결마다 사용자 `ReliableOrdered` 의 전송로를 **한 번만 확정**한다(`EOrderedRoute`: UDP 준비되면 UDP, 일정 시간 못 오면 WS).
  두 전송로를 섞으면 순서가 깨지기 때문이다. 확정 전 메시지는 백로그에 쌓는다.
- 부트스트랩은 WS 로 한다(hello, UDP 토큰 전달). 토큰은 양방향 인증이고, 서버는 첫 인바운드 데이터그램의 출처로 클라 엔드포인트를
  학습한다. `punch`(msgId 0, payload 0)로 NAT 를 유지한다.
- UDP 서버 포트는 TCP 와 같은 번호다. 방화벽은 둘 다 열어야 한다. UDP 는 암호화하지 않는다(wss 는 신뢰 채널만).
- 품질 지표: `GetRoundTripMs`(keepalive ping/pong), `GetUdpLossRate`(수신 seq 갭 기반).

### 1.3 없었던 것

- **오브젝트 복제·트랜스폼 동기화 계층이 없다.** `replicat` / `snapshot` / `interpolat` / `NetworkTransform` 으로 네트워크 쪽을
  찾아도 아무것도 없다. 위치 동기화는 게임이 `PlayerMove{ x, y, tick }` 같은 POD 를 `Unreliable` 로 보내고 손으로 적용했다.
- 웹 호스트가 없다. 브라우저는 소켓을 Listen 할 수 없으므로 WS 방식으로는 불가능했고, WebRTC 는 가지 않았다.
  배포 가이드에는 WebTransport 만 "후속 검토" 로 적혀 있다.
- POSIX 소켓은 커밋 메시지부터 "(unverified)" 다. wss 는 Windows(SChannel) 만이다.

### 1.4 그쪽이 아팠던 것 (리뷰 노트 `EngineReview-2026-07.md` A-5)

처음에는 네이티브가 raw TCP + 4 바이트 길이 헤더, 웹이 raw WebSocket 이라 **웹 클라와 윈 서버가 아예 연결되지 않았다.**
와이어를 WS 하나로 통일한 커밋 `5650f846` 이 그 수정이다. 새 엔진은 처음부터 WS 기준선으로 시작하므로 이 실패는 반복하지 않는다.

## 2. 설계

### 2.1 계층 (새 엔진)

```
[JBroPlatform]        IPlatform 에 소켓 원시 연산. TCP 논블로킹 / UDP 데이터그램 / 주소 해석 /
                      (Web) 브라우저 WebSocket·RTCPeerConnection 브릿지. 기본은 전부 "없음"(거짓)
       ↑ (IPlatform 에 소켓이 생기는 것은 부착 단계다)
[JBroNetwork]         별도 프로젝트. 아래 전부를 담고, 소켓은 §2.3 의 ISocketProvider 로 주입받는다
                        Transport   WS 코덱(RFC6455) · 세션(hello·버전·keepalive·RTT) · UdpChannel · ReliableEndpoint ·
                                    채널 라우팅 · 연결 표 · 메시지 프레이밍 · 꺼내 가기 큐
                        Replication 풀 스냅숏 델타 · 기준 스냅숏 ACK · 네트워크 오브젝트 식별자 표 · 보간 입력
                        Types       연결 ID · 역할 · 채널 · 끊김 사유 · 프로토콜 버전 · 와이어 POD
       ↑ (부착 단계에 생긴다. 엔진 트리가 처음으로 바뀌는 지점)
[JBroNetworkSystem]   엔진 어댑터. GameSystem 둘(수신 적용·송신), 호스트가 소유하는 트랜스포트 펌프,
                      NetworkSystemContext(Internal) / NetworkServiceContext 블록, 값 서비스 둘
```

### 2.2 별도 프로젝트와 엔진 트리의 불변

- `[제안]` 위치는 `source/JBroNetwork/` 다(`source/JBroEngine`·`source/JBroLauncher` 와 형제). 자기 `.slnx` 와
  `JBroNetwork.vcxproj`(정적 라이브러리) + `JBroNetwork.Tests.vcxproj` 를 갖는다. 공개 헤더는 `<JBro/Network/...>` 다.
- **제약은 한 방향이다: 부착 단계 전까지 `source/JBroEngine/` 아래 파일은 바뀌지 않는다.** 엔진은 협업 중이고, 네트워크 작업이
  그쪽 diff 에 섞이면 안 된다. 네트워크가 엔진 모듈을 `ProjectReference` 로 참조하는 것은 엔진 파일을 바꾸지 않으므로 허용이다.
  `JBroCore` 의 컨테이너는 당연히 쓰고, 복제(§2.6)가 `JBroCanvas` 의 풀 타입을 직접 보는 편이 가짜 풀보다 정확하면 그렇게 한다.
  `JBroEngine.slnx` 에 네트워크 프로젝트를 넣는 것도 부착 단계다 - 그때까지는 자기 `.slnx` 로만 연다.
- 부착 단계에 엔진에 생기는 것은 `IPlatform` 소켓 가상 함수, `JBroNetworkSystem` 모듈, `EngineInstance` 의 블록 병합, 프렐류드
  한 줄이다(§2.8·§3-5). 그 밖의 엔진 변경이 필요해지면 계획서에 먼저 적고 확인을 받는다.
- 소켓은 네트워크가 직접 열지 않는다. `ISocketProvider`(§2.3)를 생성자에서 받는다. 독립 개발 단계에서는 테스트가 Winsock 을
  직접 감싼 provider 와 인메모리 provider 를 넘기고, 부착 단계에서 `IPlatform` 을 감싼 provider 가 들어온다.
- 시계도 주입한다(기존 `CReliableEndpoint` 와 같다). 테스트가 시간을 손으로 돌려 RTO 만료를 결정적으로 재현한다.

### 2.3 소켓 경계

```cpp
namespace JBro::Network
{
    // 불투명 엔드포인트. sockaddr_in / in6 를 담는 POD. 기존 NetUdpEndpoint 와 같다.
    struct Endpoint { std::uint8_t data[28]; std::uint32_t length; };

    enum class SocketIo : std::uint8_t { Ok, WouldBlock, Closed, Error };

    class IStreamSocket { /* Connect · Listen · Accept · Send · Recv · Close - 논블로킹 */ };
    class IDatagramSocket { /* Open · Bind · Resolve · SendTo · RecvFrom · Close */ };
    class ISignalChannel { /* WebRTC 시그널 메시지 송수신. §2.7 */ };

    class ISocketProvider
    {
    public:
        virtual OwnerPtr<IStreamSocket>   CreateStreamSocket()   = 0;   // 없으면 null
        virtual OwnerPtr<IDatagramSocket> CreateDatagramSocket() = 0;   // Web 은 null
        virtual OwnerPtr<IPeerConnection> CreatePeerConnection(const PeerConnectionDesc&) = 0; // WebRTC. 없으면 null
    };
}
```

- `null` 이 "이 플랫폼에는 없다" 다. `#if JBRO_PLATFORM_WEB` 로 계층을 통째로 빼던 기존 방식은 쓰지 않는다. UDP 채널 코드는
  항상 빌드되고, 데이터그램 소켓이 `null` 이면 비활성일 뿐이다.
- 부착 단계에서 `IPlatform` 에 같은 모양의 가상 함수가 생기고(기본은 `null`/거짓), `JBroNetworkSystem` 이 그것을 감싼
  provider 를 만든다. Windows 는 `WindowsSocket.cpp`, Web 은 `WebPlatform.cpp` 에 브라우저 브릿지가 들어간다.

### 2.4 트랜스포트와 꺼내 가기

- **연결 방식이 둘이다.** (1) 접속형: `Connect(host, port)` / `Listen(port)`. WS 와 그 위 UDP. (2) 피어형: 시그널 채널로 SDP·ICE
  후보를 교환해 `IPeerConnection` 을 잇는다. 트랜스포트 인터페이스는 처음부터 둘을 품는다. 연결이 성립한 뒤에는 상위 계층이
  둘을 구분하지 않는다 - 연결 ID 하나와 채널 넷이다.
- **콜백을 두지 않는다.** 기존 `std::function` 콜백(`OnConnected`·`OnMessage<T>`)은 DLL 경계 POD 규칙과 매 프레임 할당 금지에
  걸린다. 대신 이벤트와 메시지를 POD 큐에 쌓고, 상위가 `TakeEvents(buffer, capacity)` / `TakeMessages(buffer, capacity)` 로
  꺼내 간다. `IPlatform::TakeFileEvents` 와 같은 모양이다. 큐가 차면 오래된 비신뢰 메시지부터 버리고 `Overflow` 이벤트 하나를
  남긴다. 신뢰 메시지는 버리지 않고 소켓에서 읽기를 멈춘다(역압).
- 워커로 옮기는 날에는 이 큐의 채우는 쪽만 워커가 되고 꺼내는 쪽은 그대로다. 그때 `SafePtr`·할당은 워커에 두지 않는다(D-121 과
  같은 규율).
- 세션·프레이밍·채널 라우팅·전송로 확정(`OrderedRoute`)·지연 teardown 은 기존 `CNetworkManager` 의 규칙을 그대로 잇는다.
  프로토콜 버전 상수는 와이어가 바뀔 때마다 올린다.

### 2.5 Reliable UDP 이식 규칙

- 알고리즘은 바꾸지 않는다. ACK 누적 + 비트필드, RTO = f(SRTT), 지수 백오프, dedup, Ordered 재정렬 버퍼, Unordered 즉시 전달,
  프래그먼트 분할·재조립, AIMD cwnd, 지연 ACK 피기백. 데이터그램 헤더 v2 레이아웃도 그대로다.
- 컨테이너만 바꾼다. `std::map` / `std::set` 재전송·재정렬 버퍼는 seq 창 크기가 고정이므로 **고정 크기 원형 배열**(seq 를 창 크기로
  나눈 나머지가 슬롯)로 간다. 송신 큐는 초기화 때 잡은 원형 버퍼다. 매 틱 경로에 할당이 없어야 한다.
- 메시지 ID → 타입 표는 호스트 쪽에 하나만 둔다(부착 단계, D-44 와 같은 바인딩). 와이어 ID 는 기존처럼 게임이 숫자로 명시한다.
- **완료 조건은 기존 `CLossyUdpSocket` 테스트가 같은 시드로 같은 결과를 내는 것**이다. 유실·중복·재정렬을 시드 기반 PRNG 로
  주입하는 데코레이터를 `IDatagramSocket` 위에 그대로 옮긴다. 루프백은 무손실이라 이것 없이는 신뢰 엔진이 검증되지 않는다.

### 2.6 복제 (A) - 컴포넌트 풀 스냅숏 델타

- **대상은 컴포넌트 타입 단위로 등록한다.** "이 타입의 풀을 복제한다" 가 단위다. Canvas 가 타입별 풀을 직접 소유하므로
  (ProjectRule), 풀 메모리를 연속으로 읽어 스냅숏을 찍을 수 있다. `Transform2D` 자체가 대상이 되므로 `NetworkTransform2D` 는 없다.
  차원 모듈에 네트워크 코드가 한 줄도 생기지 않는다.
- **서버가 권위다.** 고정 스텝마다 등록된 풀을 바이너리로 찍고(기존 바이너리 직렬화 재사용), 클라이언트가 마지막으로 ACK 한
  기준 스냅숏과의 차이만 `UnreliableSequenced` 로 보낸다. 클라이언트는 기준 스냅숏의 tick 을 ACK 로 되돌린다.
  ACK 가 오래 안 오면 전체 스냅숏을 다시 보낸다. Quake 3 계열이다.
- **스폰·소멸은 신뢰 채널이다.** 델타는 상태만 나른다. 오브젝트가 생기고 죽는 사건은 `ReliableOrdered` 로 따로 가고, 프리팹은
  에셋 `Uuid` 로 가리킨다. 클라이언트는 그 사건을 받아 오브젝트를 만들고 식별자 표에 등록한 뒤에야 델타를 적용한다.
- **식별자는 컴포넌트가 아니라 표다.** `GameObjectHandle` 은 프로세스 지역 값이라 와이어에 못 쓴다. 복제 시스템이
  `InstanceId ↔ NetworkObjectId` 표를 자기 안에 갖는다. 사용자 눈에 보이는 컴포넌트 추가는 0 개다. 소유 클라이언트와 권한도 이 표의
  열이다.
- **보간은 클라이언트가 한다.** 최근 두 스냅숏 사이를 tick 기준으로 보간한다. 예측·되감기는 이 계획의 범위 밖이다(`[열림]`).
- **실행 순서**: 수신 시스템(가장 앞, 델타·사건 적용) → 물리 등 시뮬레이션 → 송신 시스템(가장 뒤, 스냅숏 생성·송신).
  `GameSystem::GetExecutionOrder` 양 끝이다. 하나로 묶으면 물리보다 앞이면서 뒤일 수 없다. 둘 다 `FixedUpdate` 다.
- `[열림]` 매 스텝 풀 전체 바이트 비교 비용. 풀이 연속 메모리라 캐시 친화적이지만 실측이 없다. 오브젝트 단위 dirty 비트(컴포넌트가
  바뀔 때 세우고 송신 뒤 내림)는 측정 뒤 넣는다. 부착 단계 전(독립 단계)에는 가짜 풀로 처리량을 잰다.

### 2.7 웹 호스트 - WebRTC DataChannel

- 브라우저는 소켓을 Listen 할 수 없다. 그러나 `RTCPeerConnection` + `RTCDataChannel` 로 브라우저끼리 직접 이어질 수 있고,
  `ordered: false, maxRetransmits: 0` 로 열면 비신뢰·비순서 전달도 브라우저 안에서 된다. 웹 클라이언트 하나가 리슨 서버 역할을
  맡는 구조가 성립한다. 기존 규약은 **"웹은 WebSocket 을 받을 수 없다. 웹 호스트는 WebRTC 로만 가능하고 시그널링 서버가 필요하다"**
  로 고쳐 적는다.
- **시그널링 서버**가 필요하다. SDP 제안·응답과 ICE 후보를 중계하는 작은 WebSocket 서버다. 방 코드로 두 피어를 만나게 하는 것까지가
  역할이고 게임 데이터는 지나지 않는다. `[제안]` 이 서버는 네이티브 게임 호스트의 기능으로 만든다(같은 트랜스포트 코드로 WS 를
  받으니 새 코드가 거의 없다). 별도 배포 서버는 뒤로 미룬다.
- **STUN/TURN**. NAT 뒤 두 브라우저를 잇는 데 STUN 으로 안 되고 TURN 릴레이가 필요한 비율이 실제로 있다. ICE 서버 목록은
  `PeerConnectionDesc` 로 게임이 넘긴다. TURN 서버 운영은 이 계획의 범위 밖이다(`[열림]`).
- **채널 매핑**: 피어 연결 하나에 DataChannel 을 넷 연다 - 채널 열거형과 1:1 이다. WebRTC 가 SCTP 위에서 신뢰·순서를 스스로
  처리하므로 **피어형 연결에서는 `ReliableEndpoint` 를 쓰지 않는다.** 채널 라우팅이 연결 종류를 보고 갈린다.
- **네이티브 ↔ 웹 P2P** 는 `[열림]` 이다. 네이티브가 WebRTC 피어가 되려면 WebRTC 스택(libdatachannel 같은 서드파티)이 들어가야
  한다. 1 차는 웹끼리 P2P 와 네이티브 호스트 ↔ 웹 클라(WS) 두 가지다. `ISocketProvider::CreatePeerConnection` 은 네이티브에서
  `null` 을 돌려주고, 자리만 남긴다.
- Web 의 `IPeerConnection` 구현은 emscripten JS 접착이다. 이벤트는 JS 콜백에서 POD 큐로 옮겨 놓고 메인 스레드 폴링이 꺼낸다 -
  §2.4 의 꺼내 가기가 여기서도 같은 모양이다.

### 2.8 스크립트 경계 (부착 단계)

- `JBroNetwork` 의 타입은 `JBroCore` 만 쓰므로 스크립트 DLL 이 그대로 include 할 수 있다.
- `NetworkSystemContext`(Internal, POD, 인터페이스 포인터) 와 `NetworkServiceContext`(값 서비스 묶음) 를 `ScriptContextBlock`
  확장 블록으로 넘긴다(D-37). `JBroRuntime::ServiceContext` 에는 넣지 않는다 - 거기 넣으면 네트워크를 쓰지 않는 게임 바이너리까지
  의존을 끌고 간다(D-43 이 2D 슬롯을 뺀 이유와 같다).
- 트랜스포트는 호스트가 소유하고 캔버스보다 오래 산다(로비 → 전장 캔버스 전환에도 연결 유지). 그래서 블록은 프레임워크가 아니라
  **호스트가** 만든다. `EngineInstance` 가 프레임워크 블록 뒤에 자기 블록을 이어 붙여 로더에 넘기도록 고친다
  (`EngineInstance.cpp` 의 `GetScriptContextBlocks` 호출 지점).
- 값 서비스 둘: `NetworkSessionService`(연결·역할·RTT·손실률, 트랜스포트를 봄), `NetworkService`(권한·스폰·소멸·메시지 송신·
  꺼내 가기, 복제 시스템을 봄). `Physics2DService` 규약을 따른다 - 상태 없음, 시스템이 없으면 출력을 비우고 거짓, 예외 없음.
- 프렐류드 `<JBro/ScriptAPI.h>` 에 `<JBro/Network/ServiceContext.h>` 한 줄이 늘고, 2D·3D 프렐류드가 같은 줄을 공유한다.

## 3. 단계

각 단계의 완료 조건은 테스트다. 빌드 성공만으로 끝났다고 보지 않는다.

1. `[완료]` **프로젝트 뼈대와 경계.** (2026-09-20) `source/JBroNetwork/` 에 `JBroNetwork`(정적 lib) + `JBroNetwork.Tests`,
   자기 `.slnx`, 출력은 `source/JBroNetwork/Build/`. `Types.h`·`Socket.h`(`IStreamSocket`·`IDatagramSocket`·`IPeerConnection`·
   `ISocketProvider`·`IClock`)·`Internal/ByteRing.h`·`Transport`(길이 접두 프레이밍, 연결 표, 꺼내 가기 큐, 역압, 지연 teardown)·
   `Testing/MemorySocketProvider`(파이프 스트림 + 포트 표 데이터그램, 접속은 Accept 까지 `Connecting`)·`Testing/ManualClock`.
   테스트 11 개: 서버·클라이언트 왕복, 좁은 파이프 위 20000 바이트, 닫기 양쪽 관측, 받는 이 없는 접속, 이벤트 넘침 → `Overflow`,
   꺼내지 않은 메시지의 생존(압축), 저장소 역압 무손실, 한계 초과 프레임 → `Error` 끊김, 데이터그램 왕복·무음 폐기·불가 플랫폼.
   `git diff --stat -- source/JBroEngine` 은 비어 있다.
2. `[완료]` **WS 기준선.** (2026-09-20) `Internal/WebSocketProtocol`(SHA-1·Base64·핸드셰이크·프레임 헤더·마스크, 고정 버퍼),
   `Transport` 를 WS 위로 옮김 - 단계 `TcpConnecting → WebSocketHandshaking → SessionHandshaking → Ready`, 메시지는
   `[uint16 LE 메시지 ID][페이로드]`, 채널은 와이어에 없음(WS 로 온 것은 전부 `ReliableOrdered`), 클라이언트 → 서버 마스크,
   조각 조립 버퍼(연결마다 최대 메시지 크기), Close·Ping·Pong 처리. 세션: hello·버전 검증(불일치는 Bye 를 보내고 **닫지 않음**,
   기존 엔진의 RST 교훈)·keepalive ping/pong·RTT·무응답 타임아웃. `Connected` 는 hello 뒤에만, 서버가 받아들였다가 hello 전에
   떨어진 소켓은 이벤트 없음. `Native/WinsockSocketProvider`(스트림·데이터그램, WSAStartup 은 인스턴스 짝), `SteadyClock`.
   테스트 +14: 코덱(RFC 3174·4648·6455 예제, hashlib 로 구한 패딩 경계 5 개, 헤더 세 길이 형식×마스크, 구간 마스크 연속성),
   세션(hello 전 무이벤트, 버전 불일치, 15ms RTT, 5s 타임아웃, 스캐너 무음 폐기, 5 바이트 조각 조립, Close 프레임),
   실제 Winsock 루프백(연결·왕복·40000 바이트·닫기, 거부된 접속 → `Error`).
3. `[완료]` **UDP 채널 + Reliable UDP.** (2026-09-20) `Internal/UdpDatagram.h`(v2 코덕), `Internal/ReliableEndpoint`(인플라이트는
   `seq % 32` 고정 슬롯, 앞서 받은 순번은 32 비트 하나, 송신 큐는 바이트 고리, 재조립 슬롯·순서 대기 항목은 고정 배열),
   `Internal/UdpPeer`(토큰·엔드포인트·손실 표본·전송로·백로그), `Transport` 통합(서버는 WS 와 같은 포트에 UDP, 토큰은 WS 로,
   punch 로 엔드포인트 학습, 채널 라우팅과 WS 폴백, 사용자 `ReliableOrdered` 는 연결당 한 번 전송로 확정, 확정 전 백로그),
   `Testing/LossyDatagramSocket`(시드 기반 유실·중복·재정렬, 송신 쪽만).
   **기존 엔진과 달라진 것 둘.** (1) RTT 표본은 ack 가 새로 덮은 것 가운데 **순번이 가장 큰 첫 송신 패킷**에서만 뜬다. 앞선 패킷의
   ack 가 유실된 뒤 뒤따르는 누적 ack 가 그것까지 덮으면 그 "왕복" 은 유실된 ack 를 기다린 시간이고, 그것을 표본으로 삼으면 RTO 가
   커지고 커진 RTO 가 더 큰 표본을 허용해 상한(2 s × 백오프 64)까지 달린다 - 30% 유실에서 실제로 그렇게 멈췼다. 받는 쪽도 빈 곳이
   남아 있는 동안 standalone ack 를 25ms 마다 되풀이한다(TCP 의 중복 ack 역할). (2) 클라이언트는 서버가 답하기 전까지 punch 를
   100ms 마다 되풀이하고, 서버는 punch 에 punch 로 답한다. 기존 엔진은 한 번 두드리고 다음 keepalive(1 s)까지 기다렸다 -
   유실 아래에서 첫 punch 가 사라지면 UDP 가 1 초 늦었다.
   **기존 엔진의 검증 테스트는 저장소에 없다.** `CLossyUdpSocket` 과 주입 이음새만 있고 그것을 쓰는 테스트 파일이 없다(전 리포 grep).
   그래서 "같은 시드로 같은 판정" 은 원본이 없고, 새 테스트가 결정론을 스스로 세운다. 테스트 +9: 코덕, UDP 가 양쪽 전송로가 됨,
   30% 유실·10% 중복·깊이 4 재정렬에서 순서 보장 300 개가 순서대로 정확히 한 번(같은 시드 = 같은 라운드 수, 다른 시드 = 다른 결과),
   순서 무관 200 개 정확히 한 번(일부는 역순), 20000 바이트 조각 메시지 셋이 유실 아래에서 순서대로, 인플라이트 ≤ cwnd ≤ 32 창,
   비신뢰 도착률 ≈ 70% 와 손실률 측정, Sequenced 단조, UDP 없는 클라이언트는 즉시 WS 확정·서버는 2 초 뒤 WS 확정과 백로그 순서,
   UDP 끈 서버. 실제 Winsock 루프백에서도 UDP 경로로 통과.
   신뢰 엔진의 연결당 고정 예산: 인플라이트 32 KB + 송신 큐 128 KB + 재조립 4 × 64 KB + 순서 대기 36 KB ≈ 450 KB(설정으로 바꾼다).
4. `[완료]` **복제 (A) 독립 단계.** (2026-09-20) `Replication/ReplicationTypes.h`(`NetworkObjectId`·`SpawnDesc` 32 B POD·메시지 대역
   0xFE00~)·`IReplicatedPool`/`IReplicationHost`(핵심이 엔진을 보는 유일한 창: 오브젝트 식별자 + 고정 크기 바이트 블록)·
   `Snapshot`/`SnapshotHistory`(키 (오브젝트,타입) 정렬, 고리)·`DeltaCodec`(두 포인터 병합 인코드, 기준 ∪ changed − removed 삼중 병합
   디코드)·`ReplicationServer`(스텝: 풀 방문 → 새 오브젝트 스폰(신뢰) → 사라진 오브젝트 소멸(신뢰) → 클라이언트마다 ACK 한 기준
   대비 델타(`UnreliableSequenced`, 1 KB 넘으면 트랜스포트가 WS 로) → 늦게 온 클라이언트는 전부 스폰)·`ReplicationClient`(델타를 기준에
   얹어 스냅숏 되살림 → ACK, 스냅숏이 진실이고 풀 적용은 투영이라 스폰이 델타보다 늦어도 그때 입힘, `Apply(alpha)` 는 앞 스냅숏을
   `from` 으로 함께 넘겨 보간은 타입을 아는 어댑터가)·`Testing/FakeReplication`. 기준이 이력(32 틱)에서 밀리면 전체 스냅숏.
   **실측(§2.6 열림 항목의 답)**: 2000 오브젝트 × 8 B, 매 스텝 200 개 변경, Debug 빌드, 인메모리 소켓: 스텝당 **1.86 ms**
   (서버 스냅숏·정렬·델타 + 양쪽 트랜스포트 Update + 클라이언트 적용 전부 포함), 전체 스냅숏 16000 B 대비 델타 평균 **2612 B**.
   dirty 비트 없이도 예산 안이다 - 넣지 않는다. 테스트 +7: 깨끗한 와이어 수렴, 30% 유실·중복·재정렬 수렴, 소멸 전파, 늦게 온 클라이언트,
   호스트가 제외한 오브젝트, 보간 입력(`from`·`alpha`), 처리량.
   **이 단계에서 신뢰 엔진에 더한 것**: 유실 테스트가 서버 → 클라이언트 방향의 스폰 50 개에서 멈췄다 - 잃은 두 순번 뒤의 30 개가 순서
   대기에 쌓여 32 창이 막히고, 그 둘은 백오프(×64)로만 재전송되는데 그 ack 도 30% 유실. 그래서 (1) 선택 ack 가 뒤 순번을 확인했는데
   앞 순번이 남아 있으면 RTO 를 기다리지 않는 **빠른 재전송**(TCP 중복 ack 판정), (2) 새로 받은 뒤 standalone ack 를 **두 번 더 되풀이**,
   (3) 백오프 상한을 ×64 에서 **×8** 로. 죽은 링크는 세션 타임아웃이 끊으므로 긴 백오프가 필요 없다.
   `[열림]` 델타 한 틱이 `maxDeltaBytes`(60 KB)를 넘으면 그 틱은 보내지 않고 센다(`oversizedTicks`). 전체 스냅숏이 그보다 크면
   영원히 못 보낸다 - 그때는 전체를 여러 메시지로 나누는 일이 필요하다. 지금 예산에서는 약 4600 개 항목까지다.
5. `[완료]` **부착.** (2026-09-20) 이 단계 전의 네 커밋에는 `source/JBroEngine/` 변경이 없다(`git log --stat` 로 확인).
   **네트워크 프로젝트 쪽**(엔진 트리 아님): `System/INetworkSystem.h`(값 서비스가 보는 창), `Internal/SystemContext.h`·
   `ServiceContext.h`(D-37 블록의 내용), `Internal/ScriptModuleContext.h`(`Make*Block`·`Find*Context`, TypeId 는
   `MakeStableTypeId("JBro.Network.SystemContext")`/`ServiceContext`), `Service/NetworkSessionService`·`Service/NetworkService`
   (상태 없는 값 타입, 시스템이 없으면 거짓·0·-1 - `Physics2DService` 규약). 그래서 JBroNetwork 가 JBroRuntime 도 참조한다.
   **엔진 쪽에 생긴 것**: (1) `IPlatform::CreateSocketProvider()`(기본 null, Windows 는 `WindowsSockets.cpp` 에서
   `Native::WinsockSocketProvider`). `Platform.h` 는 `Network::ISocketProvider` 를 **전방 선언**만 한다 - 플랫폼 헤더를 쓰는 모든
   모듈이 네트워크 헤더를 보지 않게. (2) 새 모듈 `JBroNetworkSystem`: `NetworkHost`(호스트 소유, `INetworkSystem` + `IReplicationHost`,
   트랜스포트 펌프·복제 메시지 가르기·게임 메시지 큐, 캔버스 묶기/풀기 때 복제 서버·클라이언트가 새로 섬), `CanvasPoolAdapter<Component, Wire,
   Traits>`(캔버스 풀 → 복제 풀, 타입을 아는 것은 Traits 만), `NetworkReceiveSystem`(순서 50)·`NetworkSendSystem`(순서 500).
   (3) `Framework2DSystem/Network/Transform2DReplication.h`: `Transform2DWire`(저작 값 셋, 20 B)와 보간 Traits. `Framework2D` 는
   네트워크가 있으면 캔버스를 묶고 풀을 등록하고 시스템 둘을 세운다 - 헤더는 `Framework2DNetworkBinding` 을 불투명 소유로만 들어
   에디터가 네트워크 헤더를 보지 않는다. (4) `FrameworkContext::network`(전방 선언 포인터), `EngineConfig::networkEnabled`,
   `EngineInstance`: 플랫폼의 provider 와 `SteadyClock` 위에 `NetworkHost` 를 세우고(불투명 소유), 호스트 쪽 컨텍스트를 바인딩하고,
   프레임워크 블록 뒤에 네트워크 블록 둘을 이어 DLL 로더에 넘기고, `PumpEvents` 뒤 프레임 밖에서 `NetworkHost::Update`,
   `GetNetwork()`. (5) 두 프렐류드에 `<JBro/Network/ServiceContext.h>` 한 줄. (6) 빌드: `JBro.Common.props` 에 `JBroNetworkDir` 과
   스크립트 include, vcxproj 아홉 개의 include·참조(Platform·Host·Framework2D·3D·2DSystem 은 링크 없이, Tests·EditorHost·GameHost 는 링크),
   `.slnx` 에 두 프로젝트, 헤더 자립성 번역 단위 재생성(+3).
   **검증**: `Tests/NetworkHostTests.cpp` - 한 프로세스에서 호스트 둘(인메모리 소켓, 손 시계)이 이어져 서버 캔버스의 `Transform2D` 20 개가
   클라이언트 캔버스에 같은 오브젝트 수·같은 값으로 서고, 값 변경이 따라오고, 소멸이 전파되고, 게임 메시지는 복제와 섞이지 않으며
   복제 대역으로의 송신은 거절된다. 소켓 없는 플랫폼은 전부 조용히 거짓이다. `JBroTests` 전체(`source/JBroEngine` 에서 실행) 통과,
   `JBroEditorHost` 링크 통과, 네트워크 테스트 전체 통과.
   `[열림]` 스크립트 DLL 이 네트워크 블록을 바인딩하는 일은 각 DLL 의 Load 가 `FindNetwork*Context` → `BindNetwork*Context` 로 한다
   (프로브 DLL 은 아직 하지 않는다 - 네트워크를 쓰지 않으므로). 프리팹으로 스폰하는 길(`SpawnDesc::prefab`)은 비어 있고 지금은
   빈 오브젝트에 어댑터가 컴포넌트를 붙인다.
6. `[진행 예정]` **WebRTC.** `IPeerConnection` 계약, Web 의 emscripten 접착, 네이티브 호스트 안의 시그널링 서버, 피어형 연결의
   채널 매핑. 완료: 브라우저 둘이 시그널링을 거쳐 이어지고 채널 넷이 각자 규약대로 전달한다(웹 빌드가 서야 하므로 순서는 뒤다).
7. `[열림]` wss(SChannel), POSIX 소켓(기존도 미검증), 네이티브 WebRTC 피어, 예측·되감기, DTLS, TURN 운영, 워커 I/O.

## 4. 규칙과 부딪히는 지점

| 기존 엔진 | 걸리는 규칙 | 새 엔진 |
|---|---|---|
| `std::function` 콜백 | DLL 경계 POD, 매 프레임 할당 금지 | POD 큐 꺼내 가기(§2.4) |
| `SafePtr<INetworkManager>` 를 ScriptCore 로 | 서비스는 값 타입, 컨텍스트 블록 배선 | 값 서비스 둘 + 확장 블록(§2.8) |
| `std::unordered_map` / `std::vector` / `std::string` | 전용 컨테이너 | `Table` / `Array` / `String` |
| `OrderedBacklog` 의 `std::vector<uint8_t>` 항목 | 매 메시지 할당 | 원형 버퍼 |
| `CReliableEndpoint` 의 `std::map` / `std::set` | 매 틱 할당 | 고정 크기 원형 배열(§2.5) |
| 타입 태그 주소 → ID 표가 매니저 안 | 호스트와 DLL 이 각자 Runtime 사본(D-44) | 표는 호스트에 하나, DLL 이 바인딩 |
| `#if !JBRO_PLATFORM_WEB` 로 UDP 계층 배제 | 플랫폼 차이는 "없음" 기본값으로 | provider 가 `null`(§2.3) |
| 접두어 `E`·`C`·`F` | 타입 접두사 금지(`I`·`m_` 만) | `NetChannel`·`ReliableEndpoint`·함수 포인터 별칭 |
| 소켓 계층이 네트워크 안에 플랫폼 폴더로 | 엔진 모듈은 OS 를 `IPlatform` 로만 | 부착 단계에 `IPlatform` 로 이동(§2.3) |

## 5. 열린 것과 가정 모음

- `[제안]` §2.2 위치 `source/JBroNetwork/`. §2.7 시그널링 서버를 네이티브 호스트 기능으로.
- `[열림]` §2.6 풀 비교 비용과 dirty 비트. §2.7 네이티브 WebRTC 피어·TURN. §3-7 전부.
- 기존 엔진의 알려진 한계를 그대로 물려받는다: UDP 미암호, wss Windows 전용, POSIX 미검증, TLS 재협상 미지원.

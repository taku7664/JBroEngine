# JBroEngine 네트워크

전 플랫폼(윈/웹, 추후 안드) **WebSocket(RFC6455) 단일 와이어** 기반. 윈도우 서버 ↔ 윈/웹 클라 크로스플레이.
이 문서 = 현재 구조 + Reliable-over-UDP 설계안(온디맨드).

---

## 1. 왜 이 구조인가 (하이브리드 TCP+UDP)

브라우저는 **raw UDP 불가**. 전 플랫폼 교집합은 WebSocket(TCP 위) 하나뿐.
→ 신뢰 = TCP(WebSocket), 비신뢰 = UDP(네이티브만). **웹 지원의 유일한 구조.**
올-UDP 통일(ENet식)은 웹 크로스플레이를 죽이므로 채택 불가.

## 2. 계층

```
게임 코드                Script.Network / Engine.Network (INetworkManager)
  Host/Join · Send<T>(peer, msg, channel) · OnMessage<T> · OnConnected/Disconnected · RTT/Loss
────────────────────────────────────────────────────────────────────────
NetworkManager           세션(hello/keepalive/RTT/이유) + 메시지 프레이밍([msgId][payload])
                         + 채널 라우팅(신뢰→WS, 비신뢰→UDP or 폴백)
────────────────────────────────────────────────────────────────────────
INetworkTransport(신뢰)   메시지 단위 WS. 네이티브=ISocket+WS코덱(+TLS=wss), 웹=emscripten
UdpChannel(비신뢰)        토큰 부트스트랩 + 데이터그램([token][ch][seq][msgId][payload]) + 시퀀싱
────────────────────────────────────────────────────────────────────────
ISocket / IUdpSocket      논블로킹 소켓. Windows(WinSock) · Posix(안드/iOS/리눅스, 미검증)
                          + WinTlsSocket(SChannel wss 데코레이터)
```

## 3. 채널 (전달 보장)

```cpp
enum ENetChannel { ReliableOrdered, Unreliable, UnreliableSequenced };
Script.Network->Send(peer, PlayerMove{...});                      // ReliableOrdered(기본)
Script.Network->Send(peer, PlayerMove{...}, ENetChannel::Unreliable);
```
- **ReliableOrdered** → WS(TCP). 순서보장 신뢰. 스폰/점수/이벤트.
- **Unreliable** → UDP(준비 시), 아니면 WS 폴백. 위치 등 최신만 중요.
- **UnreliableSequenced** → UDP + (conn,msgId)별 역전/중복 폐기.
- 채널은 **소켓 선택**이지 와이어 페이로드가 아님. 웹/미준비/대용량(>1024)은 자동 신뢰 폴백.

## 4. 핵심 규약

- **웹은 서버 불가**(브라우저 Listen 불가). 서버는 항상 네이티브.
- 메시지는 **POD**(static_assert). 직렬화 = memcpy(LE 고정). DLL 경계 규약과 일치.
- 시스템 메시지 예약 `0xFF00~`(세션 hello/ping/pong/bye/udp-token). 유저 `1..0xFEFF`.
- 재진입: 콜백 중 요청된 teardown 은 Update 말미 지연 적용(UAF 방지).
- UDP 서버 포트 = TCP 와 동번호(포트 네임스페이스 독립). 방화벽 둘 다 개방.
- wss = SChannel TLS 데코레이터(윈). 손실 처리 = 비신뢰는 복구 안 함(견딤), 중요건 TCP.

## 5. 배포/보안
`Docs/WorkNotes/NetworkDeployGuide.md`(로컬) 참조 — https=wss 강제, 인증서 3옵션(CA/self-signed dev/TLS 프록시), UDP 포워딩. 지표: `GetRoundTripMs`, `GetUdpLossRate`.

---

# Reliable-over-UDP (설계안 — 온디맨드)

**목적**: 네이티브 신뢰 트래픽도 TCP head-of-line blocking 없이 저지연. 고속 경쟁 액션(격겜/FPS)용.
**TCP 제거 아님** — 웹이 TCP 를 강제하므로 서버는 WS/TCP 를 유지하고, 그 위에 네이티브 전용 UDP 신뢰 경로를 *덧댄다*.

## 채택 방향

1. **하이브리드 유지, 네이티브 신뢰만 UDP 로 승격.**
   - 웹 클라 ↔ 서버: WS/TCP (변화 0).
   - 네이티브 클라 ↔ 서버: 핸드셰이크 후 신뢰 채널을 UDP-신뢰로 전환(폴백은 WS).
   - 서버 = 멀티 프로토콜(피어별 전송 선택). 브로드캐스트는 피어별 전송으로 팬아웃.

2. **UdpChannel 에 신뢰 엔진 추가(별도 신뢰 소켓 아님, 같은 UDP 소켓 위 채널).**
   데이터그램 헤더 v2: `[token][flags][channelSeq][ackField][fragInfo][msgId][payload]`
   - flags: reliable / fragment / ack-piggyback.
   - reliable-seq + 재전송 버퍼(ACK 시 해제, RTO=f(RTT) 타임아웃 재전송).
   - ACK: 누적 + 선택적(bitfield) — 재전송 최소화.
   - ReliableOrdered = 수신 재정렬 버퍼(갭 채울 때까지 대기). ReliableUnordered = 즉시 전달 + dedup(HOL 회피).
   - 프래그먼트: >MTU 신뢰 메시지 분할·재조립(폴백 불가한 all-UDP 경로라 필수).
   - 혼잡: 최소 송신 윈도우/페이싱(없으면 홍수). ENet 급이면 AIMD 유사.

3. **부트스트랩 분리.** 세션 hello 는 WS 로(닭-달걀 회피), UDP-신뢰 준비 완료 후 앱 신뢰를 UDP 로 스위치.

4. **채널 확장(게임 편의 유지).**
   ```cpp
   enum ENetChannel { ReliableOrdered, ReliableUnordered/*추가*/, Unreliable, UnreliableSequenced };
   ```
   게임 코드는 채널만 고르면 됨 — 전송 선택/HOL 회피는 엔진이 알아서. **무변경 승격.**

## 사용자 편의 원칙
- 게임 개발자는 **소켓/재전송/순서를 모른다.** 채널 의도만 표현.
- `ReliableUnordered` = "반드시 도착, 순서 무관"(히트판정 등) — TCP HOL 없이 최저지연.
- 환경 제약(웹/방화벽/UDP 차단)은 엔진이 신뢰 WS 로 **투명 폴백** — 게임은 항상 동작, 품질만 변동.
- `GetRoundTripMs`/`GetUdpLossRate` 로 게임이 품질 적응(송신빈도/보간).

## 바뀌는 것 / 안 바뀌는 것
| | 영향 |
|---|---|
| 게임 공개 API | 무변경 (+enum 1) |
| INetworkManager 인터페이스 | 무변경 |
| WS/TCP 경로(웹·부트스트랩) | **무변경** |
| UdpChannel | 대규모 확장(신뢰 엔진) |
| SendFramed 라우팅 | 피어별 전송 선택 |
| 세션 | 부트스트랩 vs 앱-신뢰 분리·협상 |

## 작업량 / 리스크
- **~1500~2500줄 + 무거운 검증.** 사실상 미니 TCP.
- 버그 밭: RTO 튜닝, seq 랩, 재정렬/중복 엣지, 혼잡 붕괴.
- **검증 가능(윈)**: lossy IUdpSocket 래퍼로 유실/재정렬/중복 주입 → 신뢰 보장·순서·재조립 테스트. (POSIX 와 달리 이 머신서 가능.)

## 언제
구체적 고속 경쟁 액션 게임 요구가 나올 때. 그 전엔 현 하이브리드로 충분(대부분 2D/캐주얼).
DTLS(암호화 UDP)는 직교 축 — 비신뢰 데이터 비밀 필요 시 별도.

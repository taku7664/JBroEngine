# 시간·난수·디버그 드로 계획

> 결정은 [todo.md](./todo.md) 의 D-231(시간과 난수)·D-232(디버그 드로), 계약은 [ProjectRule.md](../docs/ProjectRule.md) §7 과 §7.2 다.
> 계기(2026-09-27): 셋 다 코드에도 계획에도 없었고, 스크립트가 `OnUpdate(float deltaTime)` 인자로 델타를 받고 있었다.
> 그것은 §7 의 "서비스 접근을 위해 매 호출마다 delta time 이나 서비스 참조를 전달하는 구조를 기본 방식으로 삼지 않는다 (MUST)" 를
> 어긴 모양이다. 기존 엔진의 훅은 `OnUpdate()` 였고 시간은 서비스에서 읽었다.

## 1. 기존 엔진

### 1.1 시간 - `Engine/Core/Time/Time.h`·`Time.cpp`, `GameFramework/Canvas/CanvasManager.cpp`

- `CTime` 이 `steady_clock` 으로 프레임 델타를 재고 0.25 초에서 자른다(첫 프레임·창 끌기·중단점 재개의 수 초짜리 델타가 스크립트 이동을
  폭주시켰다는 주석이 있다). 스케일 델타·언스케일 델타·누적 시간 둘·타임스케일·고정 델타(기본 50 Hz, 0.001~1 초로 자름)·프레임 수를 든다.
- 고정 스텝 누산기는 `CTime` 이 아니라 `CCanvasManager` 에 있고, 스케일 델타를 쌓아 8 스텝에서 자른다. 재생 중에만 돈다.
- 스크립트는 `ScriptCore.Time`(`SafePtr<CTime>`)을 받았다. 훅은 `OnUpdate()`·`OnFixedUpdate()` 로 인자가 없다.

아팠던 것:

- **T1. 스크립트가 시계 객체를 통째로 받았다.** `SetFixedDeltaSeconds`·`Reset` 까지 보여서 스크립트가 엔진의 시간을 되감을 수 있었다.
- **T2. 누적 시간이 `float` 다.** 한 시간 뒤에는 한 단계가 0.25 ms, 하루 뒤에는 8 ms 라 `Time` 으로 만든 흔들림·주기가 계단이 된다.
- **T3. 고정 스텝 안의 `GetDeltaSeconds()` 가 프레임 델타다.** `OnFixedUpdate` 에서 델타를 읽으면 고정 델타가 아니라 그 프레임의 델타가 나온다.
- **T4. 시간의 자리가 셋이다.** 시계는 `CTime`, 누산기는 캔버스 관리자, 멈춤은 캔버스 관리자의 재생 상태다. 새 엔진은 이것이 더 나빠져서
  두 프레임워크가 누산기를 **각자** 들고, 3D 는 재생을 멈춰도 고정 스텝과 델타를 그대로 돌린다(2D 는 멈춘다).
- **T5. 한 프레임 진행이 없다.** 멈춘 게임을 한 스텝씩 볼 길이 없다.

### 1.2 난수 - `Engine/Core/Random/RandomService.h`·`.cpp`

- `std::mt19937` 하나를 `std::mutex` 로 감싸고 `std::uniform_int_distribution`·`uniform_real_distribution` 으로 뽑는다. 씨앗은
  `std::random_device`, `SetSeed` 로 바꾼다. `RangeInt`·`RangeFloat`·`UnitVector2` 가 전부다.

아팠던 것:

- **R1. 표준 분포는 구현마다 값이 다르다.** 같은 씨앗이라도 MSVC 와 libc++ 가 다른 수를 낸다 - 두 번째 컴파일러(D-195)·웹 빌드·네트워크
  동기화에서 같은 씨앗이 같은 게임을 만들지 않는다.
- **R2. 뽑을 때마다 뮤텍스.** 매 프레임 경로에서 잠근다. 워커가 쓰지 않는데도 그렇다.
- **R3. 흐름이 하나다.** 새 파티클 효과가 난수를 하나 더 뽑으면 적의 행동이 바뀐다 - 게임 쪽이 제 흐름을 따로 가질 길이 없다.
- **R4. 씨앗을 모른다.** 매번 다른 씨앗이라 재생에서 한 번 본 버그를 다시 볼 수 없다. 씨앗이 로그에 남지 않는다.

### 1.3 디버그 드로 - `Engine/Core/Debug/DebugDraw2D.*`·`DebugRenderer2D.*`, `GameFramework/Debug/CanvasDebugDrawSystem.*`

- `CDebugDraw2D` 가 선·원을 `std::vector` 에 쌓고 `BeginFrame` 에 비운다. 선은 픽셀 두께이고 제출한 오브젝트 주소(`void*`)를 든다.
- `CDebugRenderer2D` 가 선을 두께만큼 사각형으로 펴서 그린다(D3D11 의 선 목록은 1 픽셀뿐이라). 에디터(`ImEditor.cpp`)만 부른다.
- `CanvasDebugDraw::Submit` 은 카메라 절두체만 그린다(콜라이더는 에디터의 ImGui 경로로 옮겨 갔다).

아팠던 것:

- **D1. 그릴 때마다 GPU 버퍼를 만든다.** 정점 버퍼와 상수 버퍼를 `Render` 마다 `CreateBuffer` 한다. 선 목록도 매 프레임 `std::vector` 로 자란다.
- **D2. 게임 화면에는 나오지 않는다.** 에디터 오버레이에서만 그린다.
- **D3. 스크립트가 버퍼를 비울 수 있다.** `ScriptCore.DebugDraw2D` 가 `Clear()`·`GetLines()` 까지 보인다.
- **D4. 한 프레임짜리뿐이다.** 남겨 둘 시간(duration)이 없다. `OnFixedUpdate` 에서 그린 선은 고정 스텝이 없는 프레임에 사라져 깜빡인다.
- **D5. 2D·직교 카메라만.** 두께를 직교 카메라 크기로 바꾸므로 3D 에서 쓸 수 없다.

## 2. 설계

### 2.1 자리

| | 엔진 쪽(Tier E, `JBroHost`) | 스크립트 쪽(Tier S) |
|---|---|---|
| 시간 | `System::TimeSystem` | `System::ITimeSystem`·`FrameTime`·`Service::TimeService` (`JBroRuntime`) |
| 난수 | `System::RandomSystem` | `System::IRandomSystem`·`Service::RandomService` (`JBroRuntime`), `RandomStream` (`JBroCore`) |
| 디버그 드로 | `System::DebugDrawSystem` | `System::IDebugDrawSystem`·`DebugLine` (`JBroRuntime`), `DebugDraw2DService`·`DebugDraw3DService` (각 Framework) |

- 셋 다 차원과 무관하고 다른 모듈을 몰라도 되므로, 텍스트(`ITextSystem`·`TextServiceBase`)가 남긴 길을 따라 인터페이스는 `JBroRuntime` 에 둔다.
  새 Tier S 모듈을 만들지 않는다. **공통 `SystemContext` 와 `ServiceContext` 가 처음으로 슬롯을 갖는다** - §10.3 의 예시
  (`struct ServiceContext { Service::TimeService Time; }`)가 이 자리를 그렸다. 공통 블록은 `BindScriptModuleContexts` 가 이미 묶으므로
  확장 블록을 더하지 않는다.
- 디버그 드로의 **표면만** 차원별이다(`Vec2`·`Vec3`). 저장소는 3D 좌표 하나로 두고 2D 는 z 를 0 으로 쓴다. 서비스는 도형을 선으로 펴서
  `AddLines` 한 번으로 넘긴다.
- 구현 셋은 `EngineInstance` 가 소유한다(§7 "Time, Input 같은 핵심 서비스의 수명은 엔진이 소유한다"). 스크립트가
  `<JBro/Host/TimeSystem.h>` 를 include 하면 C1083 이다(`/p:JBroTierProbe=Time`).

### 2.2 시간

- **훅에서 인자를 뺀다.** `GameScriptBase::OnUpdate()`·`OnFixedUpdate()` 이고 델타는 `GetServiceContext().Time.DeltaTime()` 으로 읽는다.
  가상 함수 표가 바뀌므로 공통·2D·3D 서비스 컨텍스트의 판번호를 올린다(D-28). 엔진 시스템(`GameSystem::OnUpdate(Canvas&, float)`)은
  엔진 레이어라 스케줄러가 주는 델타를 그대로 받는다 - 규칙이 겨눈 것은 스크립트의 서비스 접근이다.
- **시간은 한 자리에 있다(T4).** `TimeSystem` 이 프레임 델타·타임스케일·멈춤·한 프레임 진행·고정 스텝 누산을 모두 든다. 두 프레임워크의
  누산기를 지우고, 프레임워크는 `GetFixedStepCount()` 만큼 `BeginFixedStep` → 시스템 `FixedUpdate` 를 돈다. `IFramework::Update` 는
  인자가 없다 - 프레임워크가 `FrameworkContext::time` 을 읽는다(없으면 `Initialize` 가 거절한다).
- **프레임 하나.** 호스트가 `BeginFrame(날 델타)` 를 부른다. 날 델타가 무효(NaN·음수)면 지금처럼 프레임을 거절한다.
  - 언스케일 델타 = min(날 델타, `maxDeltaTime`(기본 0.25 초, 기존과 같다)). 넘친 만큼 게임 시간이 느리게 흐른 것으로 친다.
  - 게임 델타 = 언스케일 × 타임스케일. **멈춰 있으면 0** 이고 누적 게임 시간이 서지만 언스케일 시간은 흐른다.
  - 고정 스텝 수 = 누산기(게임 델타를 쌓는다) 안의 온 스텝, 상한 `maxFixedSteps`(기본 4). 넘친 온 스텝은 버리고 조각은 둔다(지금과 같다).
    타임스케일이 0.5 면 고정 델타는 그대로이고 스텝이 절반으로 준다 - 물리의 안정성은 고정 델타가 정한다.
  - `FixedStepAlpha()` = 남은 누산 / 고정 델타. 렌더 보간을 할 게임이 쓴다.
- **고정 스텝 안에서는(T3)** `DeltaTime()` 이 고정 델타이고 `Time()` 이 고정 시간이다. `IsInFixedStep()` 이 참이다.
- **누적 시간은 `double`(T2)** 이다. 델타는 `float`.
- **스크립트가 바꿀 수 있는 것은 타임스케일뿐(T1)** 이다(0 이상 100 이하, NaN 은 거절하고 거짓). 고정 델타·상한은 프로젝트 설정이다.
- **한 프레임 진행(T5).** 멈춘 동안 `RequestStep()` 을 부르면 다음 프레임 하나가 게임 델타 = 고정 델타로 돈다(고정 스텝 정확히 하나와
  `OnUpdate` 하나). 그 프레임만 프레임워크가 스크립트·물리 시스템을 켰다가 끝에 다시 끈다. 오디오·네트워크는 건드리지 않는다 - 다시 켜면
  `playOnStart` 가 울리는 정책(오디오)을 한 스텝마다 밟지 않는다.
- **재생의 처음.** 에디터가 재생을 시작하고 멈출 때 `ResetGameTime()` 이 누적 시간·누산기·타임스케일(1)을 되돌린다. 프레임 수는 엔진 수명이다.
- **프로젝트 설정.** `.jproject` 최상위 `FixedDeltaTime`(기본 1/60)·`MaxFixedSteps`(기본 4)·`MaxDeltaTime`(기본 0.25)·`RandomSeed`(기본 0).
  손대지 않은 파일은 적지 않는다. 설정 창에 "시간" 줄이 있고 `SetProjectFile` 이 다음 프레임부터 건다.

### 2.3 난수

- **`RandomStream`(`JBroCore`, 헤더만, 16 B 값)** 은 PCG32(XSH-RR)다. 정수 구간은 Lemire 의 치우침 없는 곱셈 거절, 실수는 위 24 비트를
  2^-24 로 곱한 [0, 1) 이다. 매핑까지 우리 코드라 **같은 씨앗이면 컴파일러·플랫폼과 무관하게 같은 수열(R1)** 이고, 시험이 첫 수들을 못박는다.
  게임이 제 흐름을 들 수 있다(R3) - 컴포넌트 필드로 두어도 되고 `GetState`/`SetState` 로 세이브에 적을 수 있다.
- **엔진 흐름.** `RandomSystem` 이 `RandomStream` 하나를 든다. 서비스(`Range`·`Value`·`Chance`·`UInt32`·`SetSeed`·`GetSeed`·`MakeStream`)가
  인터페이스로 뽑는다. `MakeStream()` 은 엔진 흐름에서 씨앗과 흐름 번호를 뽑아 새 흐름을 준다 - 씨앗이 정해지면 그것도 정해진다.
- **뮤텍스가 없다(R2).** 메인 스레드 전용이고 워커는 제 `RandomStream` 을 든다(`SafePtr` 과 같은 규칙).
- **씨앗(R4).** 프로젝트 `RandomSeed` 가 0 이 아니면 그것, 0 이면 재생을 시작할 때마다 새로 뽑고 `random seed: <n>` 을 로그에 남긴다.
  같은 버그를 다시 보려면 그 수를 `RandomSeed` 에 적는다. 에디터는 재생마다, 게임은 켤 때 한 번 씨앗을 건다.
- 벡터 도우미(단위 원 위·안, 단위 구 위, 회전)는 차원별 헤더(`Random2D.h`·`Random3D.h`)의 자유 함수이고 컨텍스트 슬롯을 차지하지 않는다.

### 2.4 디버그 드로

- **저장소.** `DebugDrawSystem` 이 `DebugLine`(양 끝 `float[3]`·RGBA8·픽셀 두께·남은 시간, 36 B)을 **엔진 초기화 때 잡은 고정 용량**
  (`EngineConfig::maxDebugLines`, 기본 16384)에 쌓는다. 넘치면 버리고 센다. 매 프레임 할당하지 않는다(D1).
- **수명(D4).** `duration` 이 0 이면 한 프레임이다: 그린 프레임에 그려지고 다음 프레임 첫머리에 지워진다. 0 보다 크면 **게임 시간**으로
  줄어든다 - 멈춘 동안은 남는다. `OnFixedUpdate` 에서 그린 0 초짜리 선은 **다음 고정 스텝이 돌 때까지** 남는다(깜빡이지 않는다).
  에디터가 재생을 시작·멈출 때 모두 지운다.
- **스크립트 표면(D3).** 그리기만 있다. 비우기·읽기는 엔진 쪽이다.
  - 2D: `Line`·`Ray`·`Arrow`·`Rect`(가운데·크기·각)·`Circle`·`Polygon`·`Cross`.
  - 3D: `Line`·`Ray`·`Arrow`·`Box`(가운데·반 크기·회전)·`Sphere`(세 대원)·`Circle`(법선)·`Axes`·`Cross`.
- **그리기(D1·D2·D5).** 새 셰이더·파이프라인을 만들지 않는다. 2D 는 선마다 흰 스프라이트 사각형(`SpriteSubmit`, 텍스처 없음 = 흰색)을
  스프라이트 뒤에 내고, 3D 는 선마다 월드 텍스트 사각형(`WorldTextSubmit`)을 낸다 - 메시에 가려지고(깊이를 보고 쓰지 않는다) 세 백엔드가
  이미 그린다. **두께는 뷰마다 픽셀로 맞춘다**: 2D 는 `2 × orthographicSize / 뷰 높이`, 3D 원근은 카메라에서 선 가운데까지의 거리로 잰
  픽셀 크기다. 3D 사각형은 선 방향과 시선에 수직인 쪽으로 편다.
- **어디에 보이나.** 캔버스 뷰(에디터)는 기본으로 보이고 게임 뷰는 에디터의 토글, 게임 실행은 프로젝트의 `DebugModeEnabled` 를 따른다
  (지금까지 읽기만 하고 쓰는 곳이 없던 키다). 캔버스 뷰에서 감춘 오브젝트(D-163)와 무관하다 - 선은 오브젝트가 아니다.

## 3. 단계와 완료 조건

1. **시간과 난수의 뼈대.** `RandomStream`·`FrameTime`·`ITimeSystem`·`IRandomSystem`·두 서비스·`TimeSystem`·`RandomSystem`, 공통 컨텍스트
   슬롯. 시험: 수열 못박기(씨앗 42 의 첫 수), 구간의 끝과 치우침(10 만 번 막대의 카이제곱), 상태 되살리기, 델타 자르기·타임스케일·멈춤·
   고정 스텝 수와 상한·조각 보존·스텝 안의 델타·알파·한 프레임 진행, 묶지 않은 서비스의 기본값.
2. **훅과 프레임워크.** `OnUpdate()`·`OnFixedUpdate()`, `IFramework::Update()`, 두 프레임워크의 누산기 제거와 3D 의 멈춤, 호스트 배선,
   프로젝트 설정 네 키. 시험: 스크립트가 서비스로 읽은 델타와 고정 델타, 3D 가 멈추면 고정 스텝이 없음, 한 프레임 진행이 스크립트와 물리를
   한 번씩 돌림, 스크립트 DLL(탐침)이 공통 컨텍스트로 시간을 읽음.
3. **디버그 드로.** 저장소·두 서비스·두 브리지·호스트 배선. 시험: 수명 셋(한 프레임·게임 시간·고정 스텝), 용량과 버린 수, 픽셀로 잰 선
   (렌더러를 세워 2D·3D 한 줄씩 읽어 두께와 자리), 캔버스 뷰와 게임 뷰 토글, 할당 0.
4. **에디터.** 한 프레임 진행(메뉴·단축키), 게임 뷰의 디버그 드로 토글, 설정 창의 시간 줄, 통계 창의 시간·선 수.
5. **문서.** `ProjectRule.md` §7·§7.2, 위키, 도면.

### 진행

- `[완료]` 1 단계(`0773d6d`). PCG32 는 공개 데모의 기준값(씨앗 42·흐름 54 의 첫 여섯 수)과 같고, 0..9 를 10 만 번 뽑은 카이제곱은 9.06 이다(자유도 9).
- `[완료]` 2 단계. 실측: 상한을 넘은 스텝만큼 게임 델타를 줄이므로 **고정 스텝이 1/60 이면 15 fps 아래에서 게임이 느려진다**(0.25 초 프레임의
  게임 델타는 0.083 초). 전에는 물리만 느려지고 `OnUpdate` 는 제 델타를 받아 둘이 어긋났다. 탐침 DLL 이 공통 컨텍스트로 호스트의 시계와 난수 흐름을
  읽고, 음성 탐침(`/p:JBroTierProbe=Time`)은 C1083 이다. 뮤테이션 22 개(시계 7·서비스 2·매핑과 PCG 4·프레임워크 3·엔진 3·프로젝트 파일 3)가 모두
  제 단언에서 잡혔다 - `ScriptDLLLoaderTests` 의 `Check` 는 문구를 찍지 않아 처음에 `exit 3` 으로만 보였고, 문구를 찍게 해서 다시 쟀다. 시험의 가짜 프레임워크(`ScriptDLLLoaderTests`)가 공통 컨텍스트를 빈 값으로 다시 묶던
  것을 지웠다 - 공통 컨텍스트는 이제 호스트의 것이다. 시험은 `Tests/TestClock.h` 의 공용 시계로 프레임워크를 돌린다.
- `[완료]` 3 단계. `DebugDrawTests`: 수명 셋·멈춘 프레임·용량과 틀린 값·서비스의 도형 모양(64 개 묶음을 넘는 폴리곤 포함)·120 프레임 할당 0.
  픽셀(세 백엔드 모두): 2D 의 3 픽셀 선은 게임 뷰(8 픽셀/유닛)와 두 배 당긴 캔버스 뷰(16 픽셀/유닛)에서 똑같이 3 행, 3D 의 4 픽셀 선은 파란 상자 옆에서
  4 행이고 상자 뒤에서는 가려진다. 게임 뷰를 끄거나 `EditorViewDesc::debugDraw` 를 끄면 선이 없다. 뮤테이션 17 개가 모두 잡혔다 - 처음에는 엔진이
  프레임마다 선을 거두는 줄(`EngineInstance` 의 `m_debugDraw->BeginFrame()`)을 지워도 살았다. 저장소 시험이 `BeginFrame` 을 손으로 불렀기 때문이다.
  탐침 DLL 이 그린 선이 호스트 저장소에 들고 다음 틱에 거둬지는 시험(`ScriptDLLLoaderTests`)을 더해 잡았다.

## 4. 남은 것

- `[열림]` 3D 선을 가림 없이(맨 위에) 그리기. 월드 텍스트 경로는 깊이를 본다.
- `[열림]` 렌더 보간(`FixedStepAlpha` 로 트랜스폼을 섞어 그리기) - 게임이 알파를 읽을 수는 있지만 엔진이 섞지는 않는다.
- `[열림]` **시험의 순서 의존(이 작업 전부터 있던 것).** GPU 픽셀 시험(`MeshPixelTests` 도 같다)이 `InputTouchTests::TestWindowsPointerMessages` 보다
  먼저 돌면 "a lift is reported even without its place" 가 실패한다. 따로 돌리면 통과한다. 디버그 드로 시험을 처음에 맨 앞에 두었다가 이것을 보았고,
  다른 픽셀 시험 옆으로 옮겼다. 원인(장치를 만든 뒤 가짜 포인터 번호의 `GetPointerInfo` 가 무엇을 돌려주는지)은 재지 않았다.
- `[열림]` 위키(모듈 구조·스크립트 API 쪽)를 고치지 않았다. 로컬 사본이 없고 공개 저장소에 올리는 일이라 사용자 확인이 먼저다.
- `[열림]` 디버그 글자(`DebugDraw.Text`) - 텍스트 경로가 레이아웃을 요구한다.

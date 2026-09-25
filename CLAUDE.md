# CLAUDE.md

> 글로벌 지침(`~/.claude/CLAUDE.md`) 상속: 사고규율(Before/After Acting), 행동규율(Break/Cross/Ground), 환경식별, 호칭, Python/uv 규칙 등.

## Project Rules

**현재 계약은 [docs/ProjectRule.md](./docs/ProjectRule.md), 변경 근거는
[tasks/todo.md](./tasks/todo.md)의 Decisions 절에 있다. 작업 전에 둘 다 읽는다.**
둘이 충돌하면 임의로 하나를 선택하지 않는다. 해당 Decision의 `Updates` / `Obsoletes` 관계와
현재 코드를 확인하고, 방향에 영향을 주는 충돌은 사용자에게 확인한 뒤 두 문서를 함께 고친다.

핵심만 요약하면 다음과 같다. 세부 조건과 예외는 원문을 본다.

- 판단 순서: 정확성 → 단순성 → 영향 범위 최소화 → 검증 가능성 → 유지보수성 → 작업 속도
- **작업마다 관련 문서를 갱신한다.** 결정·실측 결과·진행 현황·남은 일을 계획서(`tasks/*.md`)·Decisions·`ProjectRule.md` 에
  적고 커밋한 뒤 끝났다고 보고한다. 대화에만 나오고 문서에 없는 결정·실측은 없는 것으로 본다
- 모듈은 각자 빌드 단위를 가지고, 공개 헤더는 `<JBro/<이름>/...>` 로만 참조한다.
  의존 선언하지 않은 모듈의 헤더를 include하면 컴파일이 실패해야 한다. 역방향·순환 의존 금지
- DLL 경계는 교체·재로드가 필요한 곳에만 만든다. 현재는 게임 스크립트 하나뿐이고 나머지는 정적 링크
- 2D/3D 배타성은 엔진 빌드가 아니라 사용자 스크립트 프로젝트와 게임 익스포트에만 적용한다.
  엔진과 에디터는 둘 다 포함한다
- 최상위 실행 단위는 `Canvas` 다. Canvas가 오브젝트 풀과 타입별 컴포넌트 풀을 직접 소유한다.
  `World` / ECS / `Scene` / `SceneManager` 같은 중복 수명 계층은 이름을 바꿔서도 만들지 않는다
- 스크립트 참조는 GameObject 전용 16B `GameObjectHandle`과 그 외 타입용 24B `Ref<T>`로 나눈다.
  GameObjectHandle의 안전 멤버는 무효 접근을 로그로 남기고 무시한다
- 차원과 무관한 공개 값 타입은 JBroCore에 한 번만 정의한다. Framework가 같은 공개 타입을
  재정의하지 않으며, 이식 완료는 기존 임시 정의 제거와 결합 공개 헤더 컴파일까지 포함한다
- 호스트와 게임 DLL 경계를 넘는 데이터는 POD 여야 한다
- 매 프레임 도는 경로에 `dynamic_cast` · 힙 할당 · 문자열 생성/비교를 두지 않는다
- 타입 접두사는 쓰지 않는다. 인터페이스 `I`와 private 멤버 `m_`만 예외다.
  직렬화는 YAML 또는 바이너리 우선
- `std::vector` / `std::unordered_map` / `std::string` 대신 JBro 값 타입을 쓰고,
  `std::make_unique` 대신 `MakeOwnerPtr`를 쓴다
- 제어문 본문을 같은 줄에 쓰지 않는다. `if (x) return;` 같은 한 줄 제어문을 만들지 않는다
- `SafePtr`는 메인 스레드 전용이다. 워커 태스크에 캡처하거나 워커에서 참조 카운트를 바꾸지 않는다
- 한글 주석이 들어간 신규 헤더·소스는 UTF-8 BOM으로 저장한다
- **기존 엔진 소스는 `C:\Users\박주형\source\repos\JBroEngine` 에 있다.** 에디터를 손대기 전에
  거기서 같은 화면(`Application/Editor/...`)을 먼저 찾아 읽는다. 경로를 몰라 못 읽은 것을
  "기존에 없다" 로 적지 않는다
- 에디터 화면은 공용 위젯 계층(`JBro::Widget`)을 거친다. 패널이 ImGui 를 직접 부르지 않는다.
  화면에 나오는 글자는 로컬라이징 키로 쓰고, 라벨은 위젯에 넘기지 않으며, 한 값은 한 줄이다.
  한국어 문구는 번역체로 쓰지 않는다 - 키가 어디에 쓰이는지 확인하고 한국어 소프트웨어가 그 자리에 쓰는 말로 쓴다
  (`컴포넌트 추가`, `사용`, `선택한 오브젝트가 없습니다`, `실행 취소`). 컴포넌트 이름은 번역하지 않고 타입 이름
  (`Transform2D`)으로, 필드는 필드 이름으로 보인다
- 에디터의 편집은 커맨드로만 한다. **되살릴 값을 먼저 뜨지 못했으면 지우지도 떼지도 않는다.**
  대상은 포인터가 아니라 번호로 가리키고, 한 손짓이 여럿을 바꾸면 커맨드도 하나다
- 방향을 바꾸는 설계 판단과 새 컴포넌트·서비스 추가는 사용자 확인 뒤 진행한다
- 사용자 보고는 한국어, 커밋 메시지는 영어로 작성한다.
  작업 단위마다 커밋하고 `feat` / `fix` / `refactor` / `test` / `chore` / `docs` 타입 접두어를 붙인다.
  커밋 전에 빌드·테스트 통과와 스테이징 diff를 확인한다. 히스토리 재작성과 강제 푸시는 사전 확인을 받는다
- `new` / `delete`를 직접 쓰지 않는다. 고유 소유는 `MakeOwnerPtr`, 비소유 참조는 `SafePtr`다
- 빌드 성공만으로 검증 완료로 보지 않는다. 경계 규칙은 어길 때 실제로 컴파일이 실패하는지
  음성 테스트로 확인한다

### 관련 문서

- [README.md](./README.md) — 저장소 구성과 빌드 방법. 처음 온 사람이 먼저 보는 문서
- [GitHub Wiki](https://github.com/taku7664/JBroEngine/wiki) — 모듈 구조·오브젝트 모델·참조·스크립트 경계와 API·리플렉션·렌더링·에디터를 설명한다.
  **잘 바뀌지 않는 것만 적는다** — 진행 현황과 남은 일은 `tasks/todo.md` 가 갖고, 위키에 옮겨 적지 않는다.
  미구현인 JBroScript 는 위키에 두지 않는다. 규칙의 원문이 아니므로 `ProjectRule.md`·Decisions 와 어긋나면 원문이 맞고, 계약이 바뀌면 위키도 같이 고친다
- [docs/ProjectRule.md](./docs/ProjectRule.md) — 확정 규칙
- [docs/JBroEngine.drawio.xml](./docs/JBroEngine.drawio.xml) — 구조 다이어그램 6장(계층 개관·모듈 의존·런타임과 프레임 루프·
  스크립트 경계·렌더링 경로·에디터). 규칙의 원문이 아니므로 `ProjectRule.md`·Decisions 와 어긋나면 원문이 맞고, 계약이 바뀌면 도면도 같이 고친다
- [docs/Jbro_Engine_Architecture_Draft_v2.md](./docs/Jbro_Engine_Architecture_Draft_v2.md) — 폐기된 역사 초안
- [docs/Jbro_CPP_Script_Object_Safety.md](./docs/Jbro_CPP_Script_Object_Safety.md) — 폐기된 스크립트 객체 안전성 초안
- [tasks/todo.md](./tasks/todo.md) — 공용 남은 일과 확정 결정(Decisions) 기록. **남은 일은 공용·2D·3D 로 나눈다**(D-116):
  [tasks/todo-2d.md](./tasks/todo-2d.md)(2D 프레임워크·에디터 화면·스프라이트, `[논의]` 항목 포함),
  [tasks/todo-3d.md](./tasks/todo-3d.md)(재질·물리 3D·3D 기즈모, 2D 뒤의 순서)
- [tasks/jbroscript-plan.md](./tasks/jbroscript-plan.md) — JBroScript(`.jscript`) 언어와 리플렉션 계획(D-56).
  언어는 **미구현**이고 리플렉션(`PropertyInfo`·`JBRO_FIELD`·컨테이너 조작)은 섰다.
  기존 엔진 JPROP 의 실패 원인과 MSVC 실측 결과를 담고 있다
- [tasks/jbroscript-syntax.md](./tasks/jbroscript-syntax.md) — JBroScript 사용자 문법 정리본. 확정·제안·열림을 항목마다 표시했다
- [tasks/jbroc-rules.md](./tasks/jbroc-rules.md) — 컴파일러 `jbroc` 규칙 정리본(타입체커·이미터·`ref` 변환·빌드·테스트).
  `Modules/JBroScriptCompiler` 에 렉서와 파서가, `Modules/JBroc` 에 명령줄 실행 파일이 섰다(§11, D-104·D-105)
- [tasks/launcher-plan.md](./tasks/launcher-plan.md) — 런처 JBro Launcher(C# / WinUI 3) 계획(D-97·D-100).
  에디터 실행 인자 규약(D-97)과 앱 뼈대(`source/JBroLauncher`)가 섰다.
  런처와 에디터는 프로세스 경계로만 만나고, 같은 리포에 둔다.
  엔진 버전은 `JBro.Common.props` 한 곳에 있고 실행 파일의 버전 리소스가 말한다(D-101)
- [tasks/framework3d-plan.md](./tasks/framework3d-plan.md) — 3D 프레임워크·D3D11/Vulkan 백엔드·트랜스폼 기즈모(D-106~D-109).
  네 단계가 모두 섰다. 실측·가정·열린 것이 §2.6~§2.9·§3 에 있다
- [tasks/asset-plan.md](./tasks/asset-plan.md) — 에셋 시스템 계획(D-111~D-113). 1 단계(`Uuid`·레지스트리·`.jmeta`·스캔),
  2 단계(타입별 풀·텍스처와 스프라이트 로드·해석 패스), 3 단계(텍스처 있는 스프라이트·UV 사각형·`SpriteLibrary`)가 섰다.
  4 단계(에디터: 검색 드롭다운·에셋 필드·에셋 크기와 샘플러·에셋 브라우저와 옵션 편집·파일 감시, D-118~D-121)까지 섰다.
  기존 엔진 에셋 시스템의 분석과 그쪽이 겪은 문제는 §1,
  설계는 §2, 남긴 것은 §3 각 단계 끝과 §4
- [tasks/network-plan.md](./tasks/network-plan.md) — 네트워크 계획(D-122). 트랜스포트·WS·Reliable UDP·복제 핵심은 별도 프로젝트
  `source/JBroNetwork/`(자기 `.slnx`, 엔진의 Core·Runtime 만 참조)에 있고, 엔진 쪽 어댑터는 `Modules/JBroNetworkSystem`(`NetworkHost`·
  `CanvasPoolAdapter`·수신/송신 시스템)이다. 소켓은 `IPlatform::CreateSocketProvider` 로만 온다. 기존 엔진의 WS + Reliable UDP
  하이브리드 분석이 §1, 설계(소켓 주입·꺼내 가기 큐·풀 스냅숏 델타 복제·WebRTC 웹 호스트)가 §2, 단계와 완료 조건·실측이 §3 에 있다
- [tasks/audio-plan.md](./tasks/audio-plan.md) — 오디오 계획(D-197·D-198). **아직 코드는 없다.** `ma_engine` 을 안에 둔 `AudioMixer`,
  보이스·버스는 핸들로만 나가고 믹서 API 는 메인 스레드 전용이며 재생 중에는 원자 값만 쓴다. 소스는 차원 무관 `AudioSource`(새 Tier S 모듈), 리스너는
  `AudioListener2D`/`3D`. 기존 엔진 오디오의 구조와 겪은 문제가 §1, 설계가 §2, 단계와 완료 조건이 §3 에 있다
- [tasks/physics-plan.md](./tasks/physics-plan.md) — 2D 물리 계획(D-199). 결정은 섰고 코드는 1 단계부터 진행한다.
  기존 엔진 물리의 구조와 **오목 폴리곤이 틀렸던 여섯 원인**(도형 중심으로 법선 뒤집기·통짜 오목 도형 클리핑 등)이 §1,
  캔버스를 모르는 커널 모듈 `JBroPhysics2D` 와 볼록 조각을 자식 도형으로 다루는 설계가 §3, 단계와 완료 조건이 §4 에 있다
- [tasks/text-plan.md](./tasks/text-plan.md) — 2D 텍스트 계획(D-200). 1 단계(커널 `Modules/JBroText`: stb_truetype 의 `FontFace`, UTF-8·커닝·줄바꿈·정렬의
  `TextLayout`)가 섰고 다음은 2 단계(아틀라스와 비트맵 렌더)다. 기존 엔진 `Text2D` 를 깨트려 본 결과
  (HarfBuzz 를 글자 묶음마다 불러 커닝이 없었다·텍스트마다 GPU 버퍼·외곽선 상한)가 §1, D-51 과의 충돌과 갈림길이 §3,
  글자마다 스프라이트 인스턴스로 제출하는 설계가 §4, 단계와 실측이 §5, 결정이 §6 에 있다. 시험 폰트와 기대값은 `source/JBroEngine/Tests/Data/Fonts/README.md`
- [tasks/ide-plan.md](./tasks/ide-plan.md) — 스크립트 편집기 JBro Script Editor(Code-OSS 포크) 계획(D-87).
  편집기 리포는 `F:\Project\JBroScriptEditor`(원격 없음)다. 새 문법의 강조 확장과 코어 패치 0001~0003 이 섰고
  upstream 을 패치해 개발 실행으로 띄울 수 있다. 설치본(포크 빌드)은 **아직 없다**(D-102).
  `.jscript` 전용이고 기능은 내장 확장으로, 언어 지식은 `jbroc --lsp` 에 둔다

두 초안은 일부 절만 골라 현재 계약으로 사용할 수 있는 문서가 아니다. `[대체됨]` 표시가 없는
본문도 폐기된 ECS·World·전 모듈 DLL·단일 `Ref<T>` 모델을 포함한다. 설계 변천을 확인할 때만 읽고,
구현 계약은 `ProjectRule.md`와 `tasks/todo.md` Decisions에서 확인한다.

<!-- 대화 중 발견된 프로젝트 규칙이 여기에 추가됩니다. -->

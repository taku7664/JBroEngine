# 신규 리포를 기존 엔진 구조에 맞추기 — TODO

폐기된 World/ECS 단계 기록은 [canvas-world-foundation.md](./canvas-world-foundation.md)에 남아 있다.
이 문서는 현재 설계나 작업 지시가 아니다.
현재 계약은 [docs/ProjectRule.md](../docs/ProjectRule.md), 변경 근거는 아래 Decisions다.

**현재 작업은 `main` 단일 브랜치·단일 워크트리에서 순차 진행한다.** 아래 문서는 과거 워크트리
분할 당시의 담당 범위와 설계 근거를 보존한 기록이며, 현재 브랜치·병합 지시로 사용하지 않는다:

- [worktree-plan.md](./worktree-plan.md) — 5개 워크트리의 분기·병합 규칙
- [worktree-build.md](./worktree-build.md) — **W-build** · 빌드 구성과 2D/3D 배타 (Stage E)
- [worktree-platform.md](./worktree-platform.md) — **W-platform** · Platform/RHI/Graphics/Asset (D2·D3·C6·H4 API)
- [worktree-framework.md](./worktree-framework.md) — **W-framework** · Framework2D/3D + 시스템 (B5·B11·C4·C5-system·D1·D5)
- [worktree-ref.md](./worktree-ref.md) — **W-ref** · SafePtr 이식 + `Ref<T>` + `GameObjectHandle` + Canvas 구현 (F·G)
- [worktree-host.md](./worktree-host.md) — **W-host** · 컨텍스트 + 스크립트 로더 + 프렐류드 (D4·H)

이 파일의 Decisions는 결정 이력이고, 체크리스트와 진행 상태는 현재 코드·테스트와 대조해야 한다.
과거 작업 문서의 본문과 Decisions가 충돌하면 Decisions를 따르되, `ProjectRule.md`도 같은 변경에서
동기화한다.

**현재 진행 중인 작업 계획은 [structural-refactor-plan.md](./structural-refactor-plan.md)다.**
2026-09-12 전체 구조 검토에서 확정한 방향(D-42~D-55)의 근거·성능 계약·단계 순서·완료 조건이 거기 있다.

## 남은 일 - 공용

> 차원별 남은 일은 [todo-2d.md](./todo-2d.md) 와 [todo-3d.md](./todo-3d.md) 에 있다(D-116). 여기는 두 차원이 함께 쓰는
> 엔진·에디터·빌드의 것만 적는다.

- `[진행 예정]` **RHI 에 상수·구조화 버퍼 바인딩과 컴퓨트 파이프라인**. 재질(3D todo)과 후처리가 이것 없이 서지 않는다.
  지금은 푸시 상수와 텍스처·샘플러 슬롯뿐이다.
- `[진행 예정]` **렌더 패스 그래프**. `Renderer::RecordViews` 의 고정 패스(색 첨부 하나 + 필요할 때 깊이 하나)를 패스
  목록과 핑퐁 타깃 두 장을 관리하는 선형 구조로 바꾼다. 후처리 프로필(D-33)과 2D 라이팅이 이 위에 선다. 완전한 DAG 는
  이 단계에서 과하다.
- `[진행 예정]` **`.jpak` 패키지와 게임 익스포트의 에셋**. 기존 엔진의 레코드 모양(아이디·타입·페이로드 종류·오프셋·크기·
  해시, 참조 기반 포함 집합)을 잇는다. 게임 실행은 디코드된 픽셀을 패키지에서 읽어 런타임에 디코더가 필요 없게 한다.
  `IPlatform` 의 파일 시스템(D-112) 뒤에 패키지 읽기가 들어간다.
- `[열림]` **네트워크의 남은 것**(D-122, [network-plan.md](./network-plan.md)). 여섯 단계(뼈대 → WS → Reliable UDP →
  복제 → 부착 → WebRTC)는 2026-09-20 에 섰고 테스트가 통과한다. 남은 것은 계획서 §3-9 이다: wss·POSIX 소켓·네이티브
  WebRTC 스택·예측과 소유 위임·프리팹 스폰·시그널링 서버를 게임 호스트 옵션으로 켜는 일, 그리고 웹 빌드가 서는 날
  브라우저 접착(§3-6)의 검증이다.
- `[열림]` **미리 읽기(prefetch) 워커.** 뜻: 다음 캔버스가 쓸 에셋 집합을 워커 스레드가 미리 디스크에서 읽고 디코드해 두어,
  캔버스를 여는 순간 메인 스레드가 IO·디코드로 멈추지 않게 하는 것이다. 지금은 로드가 동기·메인 스레드라 캔버스를 열 때
  그 캔버스의 에셋만큼 멈춘다(작은 2D 게임에서는 문제가 아니고, 큰 시트가 많아지면 보인다). 기존 엔진의 P4·P6 이 이 자리의
  실패담이다(워커가 매 프레임 조회와 경쟁, 전부 선로드). 넣는다면 워커는 `TextureData` 같은 POD 자료만 만들고 풀에 넣는 것은
  메인 스레드가 한다(`SafePtr` 는 메인 스레드 전용).
- `[열림]` RHI 를 DLL 로 승격한다는 규칙(ProjectRule §3, SHOULD)이 아직 실행되지 않았다. 백엔드 선택이 명령줄·프로젝트
  파일이 아니라 `EngineConfig::graphicsApi` 코드 설정이다.
- `[열림]` Vulkan: 버퍼가 전부 호스트 가시 메모리, 잡힌 제시 모드를 관측할 길 없음, 미룬 파기가 테스트로 관측되지 않음,
  리뷰에서 남긴 항목(스왑체인 파기 시 제시 엔진 세마포어, 디스크립터 풀 4096 상한, 세 RHI 모듈의 `new`/`delete`).
  framework3d-plan §3.
- `[열림]` 파일 IO 예외 둘(`EditorTheme` 아이콘 글꼴 - 런처 인자가 ANSI, 컴파일러 도구의 진단 파일)과 Web·Android 플랫폼의
  파일 시스템(거짓만 돌려주는 자리). D-112.
- `[열림]` **두 번째 컴파일러로도 서게 한다**(D-195). 지금 막는 것은 `Reflection/Field.h` 의 `__FUNCSIG__` 하나다 -
  그 하나 때문에 리플렉션을 쓰는 모든 번역 단위가 MSVC 밖에서 서지 못한다. 비-MSVC 갈래는 서명 형식이 달라
  (`__PRETTY_FUNCTION__` 은 `>(` 가 아니라 `]` 로 끝난다) `DeriveFieldName` 을 갈래마다 따로 써야 한다.
  값어치는 실측되어 있다: 이 훑기가 `SafePtr.h` 의 빠진 `<utility>` 를 잡았고, MSVC 하나로 재던 자기포함 검사
  174 개는 그것을 못 봤다. Web·Android 플랫폼(D-112)이 설 때 어차피 필요해진다.
- `[열림]` 이전부터 열려 있던 것: 스크립트 DLL 없는 프로젝트가 열리지 않는 문제, 엔진 설치가 자기 버전을 말하는 방법,
  컴포넌트 이름·카테고리의 한국어 표시(B2), 픽셀 테스트가 반투명을 그리지 않는 항목 - 아래 절들의 `[열림]` 표시.

## Goal

`source/JBroEngine` 신규 트리를 **기존 엔진(`source/repos/JBroEngine/Engine`)의
오브젝트-컴포넌트 모델**에 맞추고, 스크립트 노출 표면만 핸들로 바꾼다.

## Editor Snapshot — 2026-09-15

이 절은 에디터 작업의 현재 상태다. **바로 아래의 2026-09-12 스냅샷은 그 시점 기록이고
에디터를 담지 않는다** - 아래가 더 오래된 것이니 순서를 읽는 방향으로 착각하지 않는다.
근거는 D-60, D-63~D-86, D-88, D-89.

**화면이 뜬다.** `Modules/JBroEditorHost` 가 실행 파일이다 — 인자로 프레임 수를 주면 그만큼
돌고 끝난다(사람 없이 띄워 캡처하는 용도). 1280x720 창에 패널 넷이 도킹되어 나오고,
창 크기를 바꾸면 따라가며 게임 뷰는 비율을 지킨다.

```
EditorApplication::Tick
  ├ BuildEditorUi      입력 펌프 → PushInput → BeginFrame → 메뉴바 → dockspace → 패널들 → EndFrame
  │                    (텍스처·정점 버퍼 업로드가 RHI 프레임 밖이어야 한다)
  └ engine->Tick
      └ Renderer::EndFrame
          ├ RecordViews   게임 → 게임 뷰 텍스처(FrameTarget)
          └ overlay       EditorUI::Draw(commands, frameSlot) → 백버퍼
```

서 있는 것:

- **ImGui 백엔드**가 JBroRHI 위에 있다(`imgui_impl_dx12` 를 쓰지 않는다, D-60).
  정점·인덱스 버퍼는 프레임 슬롯마다 나뉜다(D-66).
- **입력**은 플랫폼이 이벤트로 모으고(D-62) 에디터가 ImGui 로 넘긴다.
- **게임 뷰**는 렌더 타깃 하나 차이다(D-63). 카메라는 창이 아니라 `GetFrameExtent()` 를 본다.
- **패널 넷**: Game / Hierarchy / Inspector / Stats. 레지스트리에 등록하고 스스로 자리를
  말한다(D-70).
- **인스펙터**는 컴포넌트 타입을 하나도 모른다. 리플렉션(D-56)을 타고 내려가 잎사귀에서
  코덱을 만난다. `ReadOnly` / `Range` / `Tooltip` 을 존중한다.
- **되돌리기**(D-71): 드래그 하나가 되돌리기 하나. 커맨드는 일곱 - `SetProperty`,
  `CreateObject`, `DeleteObject`, `AddComponent`, `RemoveComponent`,
  `MoveInHierarchy`(D-84), 그리고 여럿을 묶는 `Compound`(D-83).
  오브젝트는 포인터가 아니라 에디터 번호로 가리키고(D-72), 컴포넌트는
  (오브젝트 번호, 타입, 같은 타입 중 몇 번째)로 가리킨다 - 프로퍼티 편집도 그렇다(D-85).
  되돌리기는 자리까지 되살린다: 떼었다 되돌린 컴포넌트는 원래 슬롯으로 간다(D-85).
- **고르기**는 여럿이다. 주된 하나를 따로 들고, 조상이 함께 골라진 것은 대상에서
  뺀다. 편집은 고른 전부에 미치며 숫자는 델타로 간다(D-83).
- **UI 계층**: 공용 위젯(`JBro::Widget`, D-79), 로컬라이징(D-80), 2열 줄 배치와
  한 값 한 줄(D-81). 규칙은 `ProjectRule.md` §11.
- **컨테이너**: `ArrayOps`/`TableOps` 가 서 있고 배열은 목록 위젯으로 그린다(D-82).
  원소 값·추가·삭제·옮기기는 고른 전부에 미치는 한 되돌리기이고, 되돌리기 값은
  컨테이너 전체의 글자다(D-86). 배열과 표는 스냅샷과 캔버스 파일에 들어가며, 셋이
  값을 쓰고 읽는 걸음은 `ReflectedYaml` 하나다.
- **생김새**는 기존 엔진 테마 그대로(D-73), 좁은 리터럴은 UTF-8(D-74).
- **검증**: D3D12 디버그 레이어 + GPU 기반 검증이 테스트 프로세스에서 켜져 있고,
  테스트가 "조용했는가" 를 묻는다(D-64). 단언은 대화상자 대신 중단된다(D-69).

**뮤테이션을 돌렸다(2026-09-15).** 71개 중 67개를 잡는다. 1회차 36개에서 13개만
잡혀서, 그 자리를 메우며 진짜 버그 둘이 나왔다 - 지우면 안 돌아오는 자식(D-76)과
글자 하나 늘 때 죽는 디바이스(D-75). 남은 넷은 동치 뮤턴트이고 근거는 D-77 에 있다.
메운 테스트는 `EditorObjectCommandTests`(새 파일)와 `EditorApplicationTests` 의
패널·인스펙터·글꼴 아틀라스 절이다.

**UI 계층을 이식했다(2026-09-15).** 공용 위젯·로컬라이징·줄 배치가 서 있다
(D-79~D-82, 규칙은 `ProjectRule.md` §11). 여럿 고르기와 컴포넌트 붙이기·떼기,
**고른 전부에 미치는 편집**(D-83), **계층 끌어 옮기기**(D-84)가 들어갔다.

**뮤테이션으로 잰다.** 도구는 `tools/mutate.py`(저장소 안에 있다). 에디터 구간에 지금까지 150개 남짓 돌렸고, 살아남은 것은
테스트를 채우거나(대부분) 동치라고 판단해 근거를 적었다(D-77). 잴 수 없는 줄은 동치로
적기보다 지운다(D-85·D-86). 진짜 결함 넷이 그 과정에서 나왔다 - 지우면 안 돌아오는
자식(D-76), 글자 하나 늘 때 죽는 디바이스(D-75), 빈 로케일 표를 성공이라 깔던 것(D-80),
읽기 전용 목록의 단언. 컨테이너 작업에서 결함 일곱이 더 나왔다(D-85 에 둘, D-86 에 다섯).
테스트가 `JBRO_EDITOR_SHOT` 을 주면 화면을 파일로 찍는다 - 사람이 창을 띄워 보는 것은
반복되지 않기 때문이다(§11.4).

다음에 할 만한 것 — 한 일에서 곧바로 이어지는 것들이다:
- 커맨드가 일곱이다. 기존에는 서른 개 가까이 있다 — 컴포넌트 순서, 레이어,
  복사/붙여넣기, 폴리곤 버텍스. 묶는 쪽(`CompoundCommand`)이 생겼으므로
  "한 동작이 여러 값을 바꾼다" 는 자리는 이제 새 커맨드 없이 선다.
- **뿌리끼리의 차례**를 못 바꾼다(D-84). 캔버스에 오브젝트 순서가 없다 —
  데이터 모델을 건드리는 일이라 확인이 필요하다.
- **구조체 원소의 목록**이 섰다(D-89 ①~⑤): 접기 마디, 원소 안 필드 편집이 고른 전부에 한
  되돌리기, 표를 끊고 줄 전체. 끌어 옮기기와 bool·int·enum 원소 목록도 마우스로 재었다.
  잔여 결함 둘도 고쳤다 - 펼침 상태가 원소를 따라가고, 행 배경이 그린 만큼 덮는다(D-89 끝).
  남은 것은 인스펙터의 `ImGui::` 직접 호출(§11.1)이다.
- `Vec2`·`Color` 필드 편집이 커맨드를 거치지 않던 결함을 고쳤다(D-89) - 한 줄 숫자 묶음은 이제
  `SetPropertyCommand` 의 잎사귀다.
- 게임 뷰 **매 프레임 opt-in** 이 들어갔다(2026-09-15, D-63 끝). 패널이 그려지지 않은 프레임에는
  뷰를 기록하지 않고 텍스처는 그대로 둔다.
- **팝업 큐**가 섰다(D-92). 핸들·같은 Id 중복 방지·한 번에 하나만 뜨는 모달이고, 콜백은 패널처럼
  가상 함수다. 저장 실패는 `MessagePopup` 으로 알린다.
- **캔버스 저장**이 메뉴와 Ctrl+S 로 된다(D-93). 경로를 모르면 플랫폼의 파일 대화상자로 한 번 묻고,
  그 뒤로는 같은 파일에 쓴다. 네이티브 대화상자가 실제로 뜨는지는 **사람이 아직 확인하지 않았다** -
  테스트는 대화상자를 대신하는 함수로 잰다.
- **커맨드가 열 개다.** 컴포넌트 자리 옮기기(D-94)와 오브젝트 나무 붙여넣기(D-95)가 들어갔다.
  기존 서른 개 가까이 중 남은 것: **레이어**(에디터에 레이어 화면 자체가 없다 - 패널을 새로 두는
  일이라 확인이 필요하다), **폴리곤 버텍스**(`Collider2D` 에 버텍스 필드가 없다 - 컴포넌트 필드
  추가라 확인이 필요하다), 뷰포트·캔버스 배경색(그 개념이 새 엔진에 아직 없다).
- **FontAwesome 아이콘**이 들어갔다(D-96). 목록의 손잡이·삭제 표시가 글리프다.
- 표는 인스펙터에서 개수만 보인다. 키 칸을 어떻게 받을지가 정해지지 않았다.
- 못 옮긴 위젯: 에셋 필드, 오디오 셋, 경로 필드, 레이어 머리(D-79 에 이유).
- 인스펙터의 **값 잎사귀**는 공용 위젯을 거친다(D-96). 남은 직접 호출은 구조(접기 머리, 우클릭 메뉴,
  구분선, 회색 글자, 목록 원소 마디 - D-89 가 `Widget::Tree` 를 쓰지 않기로 한 자리)다.
- 방향 확인이 필요한 것(`[열림]`): A5·B2(카테고리 묶음), A7(`지우기` 문구), 뿌리끼리의 차례.

## Divergence Findings — 기존 엔진 대비 대조 (2026-09-15)

기존 엔진(`C:\Users\박주형\source\repos\JBroEngine`)을 읽기 전용 기준으로 놓고 현재 트리를
항목별로 대조한 결과다. **아래는 조사 기록이고 아직 고친 것이 없다.** 방향을 바꾸는 항목에는
`[열림]` 을 붙였다. 이미 `Editor Snapshot` 에 적힌 누락(FontAwesome, 에셋 필드, 팝업 등)은
여기 다시 적지 않는다.

### A. 계약과 코드가 어긋난 것

- **A1. 스크립트 실행 목록을 매 프레임 다시 세웠다.** **고쳤다(2026-09-15, A3 와 함께).**
  `ProjectRule.md` §8 과 D-45 는 "목록은 더티 플래그로 지연 재구축하며 트리거는 스크립트
  부착/분리·`SetParent`·레이어 생성/파괴/이동" 인데, `ScriptSystem::OnUpdate` 가 조건 없이
  `Rebuild` 를 불렀다. 그 안에서 매 프레임 돌던 것은 전체 수집, `std::sort`, 항목마다의 부모
  사슬 거슬러 오르기, 그리고 **선형 탐색이 겹친 이중 순회 두 벌**이었다.

  **고친 방법.** `Canvas` 가 `GetScriptOrderRevision()` 을 들고, D-45 가 이름을 댄 자리에서
  그 값을 올린다 - 스크립트 부착(`AttachComponent<T>` 는 **T 가 스크립트일 때만**, `AttachScript`),
  스크립트 분리와 파괴, `SetParent`, `SetComponentIndex`, `SetChildIndex`, `SetObjectLayer`,
  그리고 레이어 생성·파괴·이동이 전부 거치는 `ReindexLayers`. `GameObject` 는 Tier S 라
  `Canvas` 를 알 수 없으므로 파괴와 같은 방식의 함수 포인터로 건너간다.
  `ScriptSystem` 은 마지막으로 본 값과 다를 때만 다시 세운다.

  **스크립트를 껐다 켜는 것은 트리거가 아니다.** 활성 판정을 목록 만들 때가 아니라 훅을
  부를 때 하도록 옮겼다 - 그러지 않으면 `SetEnabled` 가 트리거 목록에 들어와 D-45 가 댄
  이름들보다 넓어진다.

  **스크립트가 하나도 없으면 트리를 걷지 않는다.** 그러지 않으면 스크립트를 쓰지 않는
  캔버스도 오브젝트 수만큼 배열을 채우고, 그 첫 채움이 정상 프레임의 힙 할당이 된다(§9).
  실제로 `RendererContractTests` 의 할당 계약이 그것을 잡았다. 순회도 재귀가 아니라 멤버
  배열 스택으로 돌아 재구축이 힙을 건드리지 않는다.

  재는 것은 `ScriptSystem::GetRebuildCount()` 이고,
  `ScriptSchedulingTests::TestTheOrderIsRebuiltOnlyWhenSomethingChangedIt` 이 아무것도 건드리지
  않은 프레임에서 그 값이 그대로인지, 트리거마다 오르는지, 스크립트가 아닌 컴포넌트를
  붙였을 때는 오르지 않는지를 확인한다.

- **A2. 스크립트 훅을 부르는 구간에 순회 가드가 없었다.** **재현하고 고쳤다(2026-09-15).**
  `ProjectRule.md` §8 은 "`Canvas` 가 순회 깊이 가드를 소유하고 `ForEach<T>` 와 **스크립트
  실행 목록 순회**에 적용한다" 이다. 가드가 서던 자리를 전부 세어 보니 `ForEachObject`·
  `ForEach<T>`·`CollectScripts` 셋이었고, 조항이 지목한 **스크립트 실행 목록 순회만 빠져
  있었다.** 빠뜨린 정도가 아니라 채울 수 없는 상태였다 - `IterationGuard` 가 `Canvas` 의
  private 이라 다른 모듈의 `ScriptSystem` 은 그것을 들 수조차 없었다.

  **재현.** `ScriptSchedulingTests::TestDestroyingAnObjectFromAScriptHookIsDeferred` 다.
  크래시로 재지 않았다 - 풀 슬롯 메모리는 파괴 뒤에도 남아 있어서 죽은 객체 위의 가상
  호출이 그냥 통과하고, 그러면 "안 터졌으니 괜찮다" 는 반대 결론이 난다. 대신 파수병 값을
  두고 파괴자가 지나간 뒤에 훅이 불렸는지를 직접 보았다. 고치기 전 결과는 다음과 같다.

  ```
  test failure: a script must never receive OnUpdate after its own destructor ran
  ```

  즉 **크래시가 아니라 조용히 도는 쪽**이었다. 기존 테스트는 파괴를 루프 바깥에서만 해서
  이 자리를 재지 않았다.

  **고친 방법.** `Canvas::IterationGuard` 를 공개로 옮기고(Canvas 는 Tier E 라 스크립트
  타깃 include 경로에 없어 사용자에게는 여전히 안 보인다) `ScriptSystem` 의 훅 디스패치
  루프 셋(`OnUpdate`·`OnFixedUpdate`·`OnShutdown`)을 그것으로 감쌌다. 흐름 정리 지점은
  손대지 않았다 - `Framework2D` 가 고정 스텝마다와 시스템 갱신 뒤에 `FlushPendingDestroy`
  를 이미 D-45 가 말한 자리에서 부르고 있었다. **빠진 것은 가드 하나뿐이었다.**
  Debug·Release 양쪽 전체 테스트 통과.

- **A3. 실행 순서 정렬 기준이 구 엔진과 달랐다.** **구 엔진 그대로 되돌렸다(2026-09-15, 빡대리 결정).**
  갈리는 자리가 둘이었고 둘 다 실제로 닿는 길이었다.

  **(가) 형제 서브트리가 섞였다.** 구 엔진은 루트를 (레이어, 생성 순서)로 줄 세운 뒤 루트마다
  서브트리를 깊이 우선으로 내려간다(`AppendObjectScriptsInHierarchyOrder`). 새 코드는
  `(layerOrder, depth, instanceId)` 평면 정렬이라 A(자식 A1)와 B(자식 B1)가 A, **B**, **A1**, B1
  순으로 돌았다.

  **(나) 한 오브젝트 안의 차례가 갈렸다.** 구 엔진은 컴포넌트 배열 자리를 따르고
  `ProjectRule.md` §8 문구도 "컴포넌트 **부착 순서**" 인데 새 코드는 `InstanceId` 로 정렬했다.
  평소에는 같지만 **에디터에서 컴포넌트를 떼었다 되돌리면 갈린다** - D-85 가 원래 자리로
  보내는데(`ComponentCommands.cpp`) `Attach` 가 새로 만든 것이라 `InstanceId` 는 가장 크다.
  배열에서 첫째인 것이 실행은 꼴찌가 됐다.

  **고친 방법.** 깊이 우선 순회로 되돌렸다. 루트는 (레이어 합성 순서, `InstanceId`) 로 줄
  세우고 - `InstanceId` 가 생성 시각을 담으므로 그것이 구 엔진의 `GetCreationOrder` 다(D-9) -
  루트마다 서브트리를 통째로 내려간다. 오브젝트 안에서는 컴포넌트 배열 자리를 그대로 따른다.

  **자식은 정렬하지 않고 배열 자리를 따른다 - 구 엔진과 한 군데 다르다.** 구 엔진은 자식을
  `GetCreationOrder` 로 정렬했는데, 그 엔진에는 형제 자리를 바꾸는 길이 없어 배열 자리가 곧
  생성 순서였다. 이 엔진에는 `GameObject::SetChildIndex` 가 있고 계층에서 끌어 옮기면 그것이
  움직인다(D-84). 정렬해 버리면 사용자가 옮긴 자리를 실행 순서가 무시한다. 아무도 옮기지
  않았으면 배열 자리가 생성 순서이므로 구 엔진과 같은 결과다.

  재는 것은 `ScriptSchedulingTests` 의 `TestSiblingSubtreesDoNotInterleave` 와
  `TestScriptsInOneObjectFollowTheComponentSlotOrder` 다. 뒤의 것은 고치기 전 코드에서
  `[0]=1 [1]=2` 로 실패하는 것을 확인하고 넣었다.

- **A4. 부모의 `Transform2D` 를 끄면 자식 서브트리가 원점으로 튀었다.** **고쳤다(2026-09-15, 빡대리 결정).**
  `Transform2DSystem` 의 루트 판정이 "부모 Transform 이 없거나 **비활성이면** 내가 루트" 였고
  루트는 단위행렬에서 전파했다. 그래서 부모 오브젝트는 켜 둔 채 부모의 `Transform2D` 만
  끄면 자식 서브트리가 부모 월드를 잃고 원점 기준으로 옮겨 갔다. 구 엔진은 Transform 이
  `GameObject` 의 멤버라 끌 수 있는 물건이 아니었고 부모 유무만 봤다.

  **A4b — 갱신이 멈춘 Transform 이 `worldValid = true` 를 그대로 들고 있었다.** 시스템은
  비활성 Transform 을 건너뛸 뿐 캐시를 무효로 만들지 않았다. 그런데 계층 끌어 옮기기는
  `HierarchyCommands` 에서 "한 프레임도 돌지 않았거나 **꺼져 있는 오브젝트다**" 라고 적어 두고
  `worldValid` 로 그것을 가르려 한다. 그 기대가 지켜지지 않아, 꺼 둔 사이에 부모가 움직였으면
  철 지난 월드로 자리를 보존했다. 코드에 적힌 의도가 분명해 방향을 묻지 않고 함께 고쳤다.

  **고친 방법(선택: 서브트리를 통째로 건너뛴다).** 루트는 "부모가 없거나 **부모에 Transform2D 가
  아예 없을 때**" 다. 부모에 Transform 이 있는데 꺼져 있으면 이 노드는 루트가 아니고 어느
  루트에서도 닿지 않으므로 갱신되지 않는다. 갱신 전에 모든 Transform 의 `worldValid` 를 한 번
  내리고, 전파가 닿은 것만 다시 참이 된다. 그리기와 카메라가 `worldValid` 를 보므로 서브트리는
  화면에서 사라질 뿐 **아무것도 엉뚱한 자리에 나타나지 않는다.** `SetActive(false)` 가 서브트리를
  같이 누르는 것과 같은 모양이고, §8 의 "활성 판정은 `IsActiveComponent()` 단일 게이트" 와도 맞는다.

  **부모에 Transform 이 아예 없는 것은 꺼진 것과 다르게 둔다.** 물려받을 자리가 없으므로 그
  아래는 자기 로컬이 곧 월드다. 이쪽 동작은 바뀌지 않았다.

  재는 것은 `Framework2DSystemTests` 의
  `TestDisablingAParentTransformSkipsTheSubtreeInsteadOfMovingIt` 과
  `TestAParentWithNoTransformLeavesTheChildAtItsOwnLocal` 이고, 기존
  `TestTransformHierarchyPropagation` 에 캐시 무효 단언을 덧붙였다. 셋 다 고치기 전 코드에서
  하나씩 터지는 것을 확인하고 넣었다.

  ```
  test failure: a transform that stopped being updated must not stay marked valid
  test failure: a subtree under a disabled transform must not be marked valid
  test failure: the subtree must not be moved to the origin
  ```

- **A5. `Category` 어트리뷰트가 저장만 되고 아무도 읽지 않는다.** (확신 높음)
  `Reflection/Field.h` 의 `Attribute::Category`, `FieldAttributes::category`,
  `PropertyInfo::category` 가 다 있고 `HasEditInfo()` 도 그것을 센다. 그런데
  `InspectorPanel.cpp` 는 `category` 를 한 번도 읽지 않는다 — 붙여도 화면이 그대로다.
  구 엔진은 `EditorReflectionLabels::GetCategoryLabel` 로 `editor.category.*` 키를 찾아
  인스펙터를 묶어 그렸고 카테고리가 일곱 개 있었다. `Field.h` 주석이 "기존 엔진은 알 수
  없는 어트리뷰트를 로그 경고로 넘겨서 오타 난 `Range` 하나가 조용히 사라졌다" 고
  비판한 바로 그 모양이다.

- **A6. `Layer2D` 의 상태를 렌더가 읽지 않는다.** (확신 높음)
  `Layer2D` 는 블렌드·공간·불투명도·패럴랙스·별도 텍스처 다섯 값을 들고 있지만,
  `Framework2D.cpp` 의 생성·조회 말고는 어느 렌더 경로도 그것을 읽지 않는다. 구 엔진은
  레이어 블렌드가 실제로 돌고 테스트 프로젝트에 `LayerBlendTest.jcanvas` 가 있다.
  덧붙여 `ProjectRule.md` §7 은 `ScaleMode`·`AnchorToSafeArea` 도 `Layer2D` 소유라고
  적었는데 타입에는 그 둘이 아예 없다 — 문서가 코드보다 앞서 있다.

- **A7. `common.clear: 지우기` 가 D-91 금지 목록에 있는 말이다.** (확신 높음)
  `ProjectRule.md` §11.2 는 `붙이기`·`떼기`·`지우기`·`만들기`·`끝내기` 를 쓰지 말라고
  못박았다. `Localization/ko-KR.yaml` 의 `common.clear` 값이 `지우기` 이고, 쓰이는
  자리는 검색 칸의 지움 단추 툴팁(`Widget/Fields.cpp:91`)이다. 구 엔진 값은 `비우기`
  이므로 §11.2 의 "기존 엔진 한국어 표를 따른다" 와도 어긋난다. `[열림]` — 이 자리에
  둘 말을 정해야 한다.

- **A8. `ProjectFile.h` 머리말이 코드보다 엄격하다.** (확신 높음, 영향 작음)
  머리말은 "모르는 것을 만나면 추측하지 않고 줄 번호와 함께 실패한다" 고 적었는데,
  실제 파서는 모르는 블록과 모르는 키를 **조용히 건너뛴다**(`ProjectFile.cpp` 의
  `skipDeeperThan`). 실제 `.jproject` 를 열려면 건너뛰는 편이 맞으므로 고칠 것은
  머리말 쪽이다. **머리말을 고쳤다(2026-09-15).** 모르는 키는 건너뛰고, 형식이 틀린 것(탭·홀수
  들여쓰기·키 없는 항목·닫히지 않은 따옴표·`key: value` 아닌 줄·타입 불일치)만 줄 번호와 함께
  실패한다고 적었다. 코드는 그대로다.

- **A9. 쓰이지 않는 로케일 키 둘.** `list.empty`, `common.none` 은 어느 코드도 부르지 않는다.
  **지웠다(2026-09-15).** 키 상수와 두 로케일 표에서 함께 뺐다.

- **A10. 에디터 테스트가 한동안 불안정했다.** (실측, 2026-09-15) `[지켜볼 것]`
  같은 실행 파일을 여덟 번 돌렸더니 세 번 실패했고, **실패한 자리가 매번 달랐다.**

  ```
  test failure: the second element must be drawn as a node that can be opened
  test failure: the File menu must be named in the loaded locale
  test failure: the second row must have a handle to drag
  ```

  실패가 한 자리에 머물지 않고 옮겨 다니는 것은 테스트끼리 전역 상태(ImGui 컨텍스트·IO,
  또는 이름으로 찾는 창)를 나눠 쓰고 앞의 것이 남긴 것을 뒤의 것이 밟는다는 뜻이다.

  **잰 것을 그대로 적는다. 원인은 단정하지 않는다.**

  | 커밋 | 기계 상태 | 결과 |
  |---|---|---|
  | `5538a01` 그대로 | 옆에서 빌드가 돌던 중 | 8번 중 3번 실패 |
  | `5538a01` + A3·A1 변경 | 옆에서 빌드가 돌던 중 | 8번 중 6번 실패 |
  | `5538a01` + A3·A1 변경 | 한가함 | Debug·Release 각 5번, 전부 통과 |
  | `e304e08`(현재 HEAD) | 한가함 | 8번 전부 통과 |

  **다른 세션에서 같은 날 잰 것을 덧붙인다(2026-09-15).** 에디터 작업 쪽은 스크래치 워크트리에서
  따로 돌렸고, 실패는 **옆에서 다른 `JBroTests.exe` 가 돌고 있을 때만** 났다 - 빌드만 돌 때는
  나지 않았다. `b47a464` 의 자기 프로세스 창 찾기를 넣은 **뒤에도** 다른 테스트 프로세스가 돌면
  실패했으므로(둘째 줄), 창 이름 충돌은 원인 중 하나였을 수는 있어도 전부는 아니다.

  | 조건 | 결과 |
  |---|---|
  | 다른 `JBroTests.exe` 없이, 옆에서 MSBuild 만 돌던 중 | 연속 4번 통과 |
  | 다른 `JBroTests.exe` 가 도는 중, 창을 프로세스로 찾는 헬퍼 포함 | 5번 중 5번 실패, 자리는 매번 다름(끌기·클릭 자리) |
  | 다른 `JBroTests.exe` 가 끝나길 기다린 뒤 | 이후 회차 전부 통과 |

  **포커스는 아니다.** `EditorUI::PushInput` 에 FocusGained/FocusLost 를 찍는 프로브를 넣고 다른
  프로세스가 도는 중에 돌렸는데 포커스 이벤트가 한 번도 오지 않았다(테스트 창은 숨김이다).
  마우스는 `PostMessage` 로만 넣고 플랫폼은 메시지로만 읽으므로 남는 공유 자원은 GPU 다 - D3D12
  디버그 레이어와 GPU 기반 검증이 켜진 프로세스 둘이 한 GPU 를 나눠 쓴다. 단정하지 않고 적어 둔다.
  당장의 대응: 에디터 테스트를 돌리는 스크립트가 다른 `JBroTests.exe` 가 끝날 때까지 기다린다.
  **덧붙여, 헤더 레이아웃이 바뀐 커밋(`e304e08`)을 증분 빌드로 받았을 때 `Run-Time Check Failure #2 -
  Stack around the variable 'canvas' was corrupted` 로 첫 테스트에서 죽었고, `/t:Rebuild` 로 사라졌다.**
  다른 세션의 커밋을 받은 뒤에는 전체 다시 빌드한다.

  실패를 본 회차는 **전부 `b47a464`(이 프로세스의 창을 이름이 아니라 프로세스로 찾는다) 이전**
  이고 **전부 기계가 바쁠 때**다. 두 조건이 겹쳐 있어 어느 쪽이 고친 것인지 이 기록만으로는
  가를 수 없다. 그러니 닫힌 것으로 표시하지 않는다 - 바쁜 기계에서 다시 여덟 번 돌려 보면
  갈린다. 이것을 적어 두는 이유는 따로 있다: 조사 중에 **"인스펙터 x 필드"·"File 메뉴" 실패를
  작업 변경 탓으로 알고 두 번 쫓았다.** 통과 여부가 회차를 타면 `ProjectRule.md` §13 의
  "커밋 전 테스트 통과 확인" 이 아무것도 보장하지 않는다.

### B. 어긋남은 아니지만 구 엔진보다 못한 것

- **B1. 루트를 고르는 데 매 프레임 선형 탐색이 붙는다.**
  `Transform2DSystem::OnUpdate` 는 transform 마다
  `Canvas::FindComponentRaw<Transform2D>(parent)` 를 부른다 — 부모의 컴포넌트 배열을
  훑는 선형 탐색이다. 구 엔진은 `object.GetParent().IsValid()` 한 줄이었다.
  `ProjectRule.md` §9 의 "조회는 초기화 시점 캐시로 해결한다" 와 반대 방향이다.

- **B2. 컴포넌트 이름·카테고리의 한국어 표시가 통째로 사라진다.** `[열림]`
  구 엔진은 컴포넌트 17종의 한국어 이름과 카테고리 7종을 `ko-KR.yaml` 에 갖고 있었다.
  D-91 이 컴포넌트 이름을 타입 이름으로 고정한 것은 결정이지만, **카테고리까지 같이
  없어진 것은 그 결정이 다룬 범위가 아니다**(A5 와 짝이다). 카테고리 묶음을 되살릴지
  확인이 필요하다.

## Audit Snapshot — 2026-09-12

**그 시점의 기록이다.** 2026-09-12 에 코드와 테스트를 직접 대조해 적었고, 그 뒤의 에디터
작업(D-60, D-63~D-86)은 담지 않는다 - 현재 상태는 위의 `Editor Snapshot` 이다.
아래의 Stage·Review 체크리스트도 당시 스냅샷이라 지금을 증명하지 않는다.

- 현재 브랜치와 워크트리는 `main` 하나다.
- Renderer의 프레임 패킷 API, Canvas·참조·풀의 기본 구현과 버전형 스크립트 DLL 진입점은 코드와
  테스트가 존재한다.
- D-38에 따라 `JBro::Color`는 Core의 단일 정의를 사용한다. Framework2D의 기존 기본 흰색은
  소비자별 명시 초기화로 보존했고, `ScriptAPI.h`와 Framework2D 공개 헤더 결합 테스트가 통과한다.
- `String::Split`은 STL 컨테이너를 공개하지 않고 `Array<String>`을 반환한다. 기본으로 빈 필드를
  보존하고 `skipEmpty=true`일 때만 제외하는 계약을 공개 헤더 결합 테스트로 고정한다.
- `SafePtr.h`는 구 엔진 원본 477줄과 줄 단위로 대조했다. 이식본의 차이는 UTF-8 BOM과
  `namespace JBro` 여닫는 두 줄뿐이며 포인터·수명·캐스트 로직 차이는 0건이다.
- `TObjectPool<T>`의 32슬롯 청크는 생성자에서 받은 `JAllocator`로 할당·반환한다. 200개 생성 시
  7개 청크 할당, 풀 파괴 시 7개 반환, 첫 객체 주소 불변을 Debug/Release 테스트로 확인했다.
- 구 엔진의 `Utillity/Math`는 아직 이식되지 않았다. 현재 `Vec2`/`Rect`/`Matrix3x2`는 Framework2D,
  `Vec3`는 Framework3D, `Matrix4x4`는 Graphics에 분산돼 있다. 공통 수학 타입의 이름과 Core 입주
  범위를 확정하기 전에는 임시 타입을 정식 계약으로 간주하지 않는다.
- Framework3D의 기존 5개 타입은 `ComponentBase` 파생 컴포넌트가 되었고 Runtime Canvas를 사용한다.
  3D 전용 시스템과 렌더 추출은 아직 골격이므로 Framework3D 전체를 완료로 표시하지 않는다.
- D-40에 따라 차원 독립 `Canvas`와 공통 `Layer`를 JBroRuntime으로 옮겼다. D-41에 따라 블렌드·
  불투명도·공간·패럴랙스·별도 합성 텍스처 상태는 Framework2D의 `Layer2D`로 분리했다. Debug_Game3D
  링크에는 JBroRuntime과 JBroFramework3D만 포함되며 JBroFramework2D는 포함되지 않는다.
- 레이어 검색 실패를 `Array::Size()`와 비교하던 기존 오류를 `Array::InvalidIndex` 비교로 바로잡았다.
  파괴된 레이어의 조회·이동·재파괴·오브젝트 배정이 범위 밖 접근 없이 실패하는 테스트를 추가했다.
- D-5에 따라 `Ref<GameObject>`는 템플릿 정적 단언으로 컴파일을 거부한다. 일반 `Ref<T>`의 24B
  레이아웃 검증은 GameObject가 아닌 표본 타입으로 유지하며 음성 컴파일 프로브를 통과한다.
- Framework2D의 `ScriptSystem`은 아직 실행 구현이 없는 스텁이다.
- InstanceId 테스트는 4096 시퀀스를 검사하지만 서로 다른 생성기 세션의 난수 비반복은 검사하지 않는다.
- SDK/Dist 미러와 프로젝트 Templates는 현재 트리에 없다. 존재하기 전에는 관련 완료 조건을 충족한
  것으로 표시하지 않는다.
- draw.io XML 9페이지는 파싱과 연결 무결성을 확인했다. 설치된 draw.io 뷰어가 없어 렌더링 화면
  검수는 아직 증명되지 않았다.

## Open Decisions — 구현 전 빡대리 확인 필요

아래 항목은 구현자가 임의로 좁은 임시 구조를 선택하면 최종 방향을 바꾸므로, 빡대리가 계약을
확정한 뒤 Decisions와 `ProjectRule.md`에 함께 반영한다.

2026-09-12 구조 검토로 3·4·6·7·8이, 2026-09-14 에 1·2·5가 확정되어 Decisions로 옮겼다
(각 항목 끝의 `→ 확정` 참조). **열린 항목은 없다.**

1. **InstanceId 세션 10비트의 범위**
   현재 문구는 프로세스 시작 시 한 번 생성하는 난수를 뜻하지만 구현은 생성기 객체마다 다시 뽑는다.
   프로세스당 값 하나를 공유할지, 생성기마다 0~1023 안에서 비반복 값을 보장할지 결정해야 한다.
   프로세스 재실행 간 절대 비반복은 10비트 난수만으로 보장할 수 없어 비트 구성 또는 영속 상태 변경이
   필요하다.
   **→ 확정: D-59.** 프로세스당 값 하나를 공유하고, 한 실행 안에서의 비충돌만 보장한다.
2. **공통 수학 타입의 이름과 이식 순서**
   차원 독립 공개 타입을 JBroCore에 한 번만 정의한다는 소유권은 D-38로 이미 확정됐다. 여기서 정할
   것은 Core로 옮길지 여부가 아니라 정식 이름·필드 계약과 한 번에 옮길 범위다.
   구 엔진의 `Utillity/Math`에는 `Vector2T`/`Vector2`, `RectT`/`Rect`, `Size`, `Matrix3x2`,
   `Layout2D`가 있다. 신규 트리의 `Vec2`, `Vec3`, `Rect`, `Matrix3x2`, `Matrix4x4`를 한 번에 정식
   타입으로 통합할지, 구 엔진의 2D 타입부터 순차 이식할지 결정해야 한다. 순차 이식을 선택해도 남은
   차원 독립 임시 타입의 Core 통합 의무는 없어지지 않는다. `Layout2D`는 차원 의미가 있으므로
   Framework2D에 남기는 안이 기본 제안이다.
   **→ 확정: D-57.** 수학 타입은 Core 로 올리지 않고 2D·3D 모듈에 나눠 둔다.
3. **ScriptSystem 실행 순서와 변이 경계**
   구 엔진은 레이어 → 하이라키 → 오브젝트의 컴포넌트 부착 순서로 실행 목록을 만들고, 순회 중
   스크립트 생성·파괴가 목록을 무효화하지 않도록 재빌드와 파괴를 안전 지점까지 미룬다. 신규 Canvas는
   이 목록과 지연 변이 경계가 없다. 이 순서를 그대로 유지할지와 FixedUpdate/Update 뒤 flush 시점을
   확정해야 한다. H5의 DLL 리플렉션 생성·파괴 함수 없이 정적 부착 스크립트만 지원하는 구현은 완료로
   간주하지 않는다.
   **→ 확정: D-45.** 구 엔진 계약을 그대로 이식한다.
4. **ScriptAPI 한 줄 include의 실제 소유 위치**
   D-18은 `<JBro/ScriptAPI.h>` 하나만 허용하지만 현재 파일은 Core 경로에서 Runtime을 역참조하고
   Framework2D 공개 타입은 포함하지 않는다. 프로젝트 차원 선택이 같은 경로의 완성된 프렐류드를
   제공할지, 별도 ScriptAPI 빌드 단위를 둘지 결정해야 한다. 사용자가 Framework 헤더를 추가로
   include하게 하는 방식은 D-18을 수정하지 않는 한 채택할 수 없다.
   **→ 확정: D-42.** 각 Framework 모듈의 `Include/JBro/ScriptAPI.h`(같은 경로)로 이동한다.
5. **PixelPerfect 투영 계약**
   기준 해상도, pixels-per-unit, 정수 배율, 남는 영역의 letterbox/crop 정책을 정해야 한다. 현재
   `CameraProjection2D::PixelPerfect`는 존재하지만 시스템이 명시적으로 실패시키며 구현 완료가 아니다.
   **→ 확정: D-58.** 동작해야 한다. 세부 계약은 기존 엔진의 것을 읽고 따른다.
6. **프로젝트 수명의 에셋 소유자 이름과 역할**
   D-12는 `Manager` 명칭을 폐기했지만 현재 코드에는 `AssetManager`, `GetAssetManager` 및 관련 Context
   필드가 남아 있다. 메타데이터를 보관하는 `AssetRegistry`는 별도로 존재하고, 문서에는 사용자 호출
   표면인 `Service::AssetService`도 예고되어 있다. 에셋 로드·캐시를 소유하는 프로젝트 수명 객체를
   `AssetSystem`으로 개명하고 값형 `AssetService`를 앞에 두는 안을 기본으로 제안한다. `AssetRegistry`가
   로드 소유까지 합칠지는 수명과 공개 API를 바꾸므로 빡대리가 확정한다.
   **→ 확정: D-50.** `AssetSystem`(로드·캐시)과 `AssetRegistry`(메타데이터)를 분리 유지, 값형 `AssetService`.
7. **스크립트 리플렉션의 컨테이너 메모리 경계**
   `Allocator.h`는 호스트 할당기를 DLL에 바인딩한다고 설명하지만, 현재 `ScriptModuleLoadContext`에는 할당
   함수가 없고 `BindHeapAllocator` 호출도 0건이다. 또한 `String`은 `HeapAllocator`를 쓰지 않으므로 할당기
   함수만 ABI에 추가해도 스크립트 필드 전체가 안전해지지 않는다. D-37을 유지하려면 호스트는 DLL
   메모리의 C++ 컨테이너를 직접 조작하지 않고, DLL이 제공하는 필드 복사·편집·직렬화 연산을 통하는
   안을 기본으로 제안한다. 반대로 호스트가 직접 편집해야 한다면 `String`까지 포함한 공유 할당기 ABI를
   새로 설계해야 하므로 빡대리가 확정한다.
   **→ 확정: D-51.** 기본 제안 채택 — 호스트는 DLL 컨테이너를 직접 조작하지 않고 DLL이 제공하는 연산을 통한다.
8. **ScriptAPI의 실체 타입 노출 범위**
   `ProjectRule.md` §5는 `Canvas` 같은 구현 타입이 스크립트 헤더에 나타나지 않아야 한다고 규정하지만,
   §6은 `Ref<Canvas>`를 스크립트 참조 종류로 열어 두고 있다. 현재 `ScriptAPI.h`는 실체
   `GameObject.h`를 include하며 그 공개 표면에서 `Canvas* GetCanvas()`까지 보인다. 스크립트가
   `GameObjectHandle`과 서비스만 보게 할지, 실체 타입 선언은 보되 획득 경로만 막을지 확정해야 한다.
   이 결정 전에는 프렐류드 공개 표면을 완료로 표시하지 않는다.
   **→ 확정: D-42.** 스크립트는 `Canvas`·`GameObject` 선언 자체를 받지 않는다. 모듈 계층 분리로 강제한다.

## Decisions

문서 초안과 다르게 확정한 사항이다. 초안보다 이 절을 우선한다.

### 모델

- **D-1. ECS 를 쓰지 않는다. 오브젝트-컴포넌트 모델이 확정이다.**
  `Entity` 정수 ID 없음, 컴포넌트는 다형성, 다중 타입 `Query` 없음.
  시스템은 `ForEach<T>` 로 타입별 컴포넌트 풀을 순회한다.
  **시스템은 `Ref<T>` 를 거치지 않는다** — 순회가 실체 참조를 그대로 주므로 해석 비용이 0 이다.
  순회 중 생성·파괴는 금지한다.
- **D-2. `World` 를 두지 않는다.**
  `Canvas` 가 오브젝트 풀 · 타입별 컴포넌트 풀 · Layer · 시스템을 직접 소유한다.
  수명 계층은 `Canvas` → `GameObject` 하나뿐. `Scene` / `SceneManager` 도 두지 않는다.
- **D-3. 부모·자식·레이어는 `GameObject` 의 멤버. Transform 은 컴포넌트로 유지.**
  기존 엔진 (2D-only) 은 Transform 을 멤버로 뒀지만, 신규 리포는 2D+3D 지원이라 다르다.
  `Component::Transform2D` (Framework2D), `Component::Transform3D` (Framework3D) 각각.
  GameObject 는 dimension 을 모른다. 어느 프레임워크가 로드됐냐에 따라 어느 Transform 을
  attach 할지 결정된다. 부모/자식/레이어는 dimension 무관이라 그대로 멤버로 둔다.
- **D-4. 엔진 내부 참조는 `SafePtr` 를 그대로 쓴다.**
  핸들로 바꾸지 않는다. `Utillity/SafePtr` 는 불가침이다.
  → **W-ref 가 SafePtr 를 이식하고 GameObject 리트로핏.**

### 참조와 식별자

- **D-5. 스크립트에 노출하는 참조는 두 가지뿐이다.**
  1. **`GameObjectHandle`** (16B) — GameObject 만 예외. `IF` 없이 안전 멤버 호출
     (`.Destroy()` / `.SetActive()` / `.GetComponent<T>()` 등). `operator->` 없음.
  2. **`Ref<T>`** (24B) — 그 외 모든 참조 (컴포넌트/스크립트/에셋/캔버스). 저장 · 직렬화용.
- **D-6. 스크립트별 핸들 타입을 코드 생성으로 만들지 않는다.**
  생성 전까지 사용자 코드가 컴파일되지 않아 스크립트를 막 작성한 시점에 편집기가 깨진다.
- **D-7. `Ref<T>` 의 접근자는 하나다.**

  ```cpp
  if (T* p = ref.Get()) p->Foo();   // 확인하고 쓴다
  ref->Foo();                        // 확인 안 하고 쓴다 — 무효면 크래시
  ```

  `operator->` 는 내부적으로 `Get()` 을 부르며 Debug assert 만 차이다.
- **D-8. `GameObjectHandle` 의 무효 접근은 로그 후 무시한다.**
  크래시도 예외도 절반 실행도 없다. 값을 돌려주는 접근만 실패가 드러나게 한다(`TryGetPosition`).
  무검사 경로(`Ref<T>::operator->`)는 이 보장을 하지 않는다. 선택은 사용자가 한다.
- **D-9. 영속 식별자는 `InstanceId`(`uint64` 하나)다.**
  `[42비트 ms][10비트 세션난수][12비트 시퀀스]`. 시간은 프레임당 1회 캐시.
- **D-10. `Ref<T>` 저장부는 24B POD.**
  `InstanceId ObjectId + InstanceId ComponentId + InstanceHandle Cached`.
  로드 직후 일괄 패치업해서 프레임 루프의 식별자 조회를 0 회로 만든다.

### 이름과 경계

- **D-11. 네임스페이스로 구분한다. 타입 접두사를 쓰지 않는다.**
  `JBro::Component` / `Asset` / `System` / `Service` / `Internal`, 나머지는 `JBro` 직속.
  `JBro::Game` 은 두지 않는다.
  예외 둘: 인터페이스 `I` 접두, private 멤버 `m_`.
- **D-12. `Manager` 명칭을 폐기하고 `System` / `Service` 로 나눈다.**
- **D-13. 컨텍스트는 셋이다.**
  `EngineContext`(호스트 전부) → `SystemContext`(DLL 은 받되 사용자엔 비공개) /
  `ServiceContext`(사용자 공개). 서비스 헤더는 시스템을 전방 선언만 하고
  실제 호출은 비인라인 `.cpp` 에 둔다.
- **D-14. DLL 경계는 게임 스크립트 하나뿐이다.**
  RHI 는 정적으로 시작. 두 번째 Windows 백엔드가 생기면 승격.
- **D-15. 2D/3D 배타성은 엔진 빌드가 아니라 사용자 스크립트 프로젝트와 게임 익스포트에만 적용한다.**
  엔진·에디터는 둘 다 포함. 2D 게임 실행 파일에 3D 코드는 안 들어간다.
- **D-16. 디바이스 로스트는 치명적 오류로 처리하고 종료한다.**
  대신 RHI 디바이스 포인터를 그래픽스 계층 밖으로 내보내지 않는다.
- **D-17. `GraphicsSystem` 을 폐기하고 `Renderer` 가 디바이스 부착을 흡수한다.**
  → **W-platform 담당 (JBroGraphics 안에서 신설).**

### 스크립트 표면

- **D-18. `ScriptAPI.h` 는 스크립트 DLL 이 include 하는 유일한 헤더다.**
  `using namespace JBro` 포함. 1-뎁스 네임스페이스 (`Component::Transform2D` 등) 는 유지.
  `SystemContext` 헤더는 include 하지 않는다 (사용자에게 시스템 노출 금지).
  → **W-host 담당.**
- **D-19. 게임 스크립트 클래스는 `JBRO_SCRIPT` 매크로로 선언한다.**
  `class` 로 적으면 컴파일은 되지만 에디터 코드 생성기가 grep 못 해서 목록에 안 뜬다.
  → **W-host 담당.**

### 이번 세션 종료 시점의 상태 재정의 (컨텍스트 압축 후 잊혔던 것들)

- **D-20. `F2. GameInstance 식별자 → InstanceId` 는 폐기.**
  신규 리포엔 `GameInstance` 클래스 자체가 없다. 대체: **GameObject 와 컴포넌트가 생성 시
  `InstanceIdGenerator::Generate()` 로 아이디 발급.**
- **D-21. `G7. using GameObject = CGameObject 별칭 제거` 는 폐기.**
  신규 리포엔 `CGameObject` 자체가 없다.
- **D-22. `D2. GraphicsApi/SurfaceHandle 을 Core 로 이동` 은 철회.**
  대체: `GraphicsApi` 는 RHI, `SurfaceHandle` 은 Platform. 의존은 `RHI → Platform` 방향.
- **D-23. `F5. 프레임 루프 식별자 조회 0회 확인` 은 스코프 축소.**
  프레임 루프 자체가 아직 없다. 대체: `Ref<T>::Get()` 캐시 히트 시 InstanceId 안 읽는지 유닛테스트.
- **D-24. `C5. Physics2D System/Service 분리` 는 크로스-워크트리.**
  `System::Physics2DSystem` (시뮬레이션) 은 W-framework, `Service::Physics2DService` (스크립트 노출)
  는 W-host.
- **D-25. `H4. LoadLibrary/HMODULE 은 코드에 없음` 은 크로스-워크트리.**
  `IPlatform::LoadDynamicLibrary` / `GetSymbol` / `UnloadDynamicLibrary` API 는 W-platform,
  스크립트 로더 소비는 W-host.

### 검토 3차에서 확정한 것

- **D-26. 활성 캔버스 가정. `GameObjectHandle` / `Ref<T>` 는 프로세스-전역 레지스트리로 해석한다.**
  Handle 은 16B 유지. `Canvas::CreateObject` 시 `InstanceId → GameObject*` 를 전역 레지스트리에 등록,
  `DestroyObject` 시 제거. 다중 캔버스 (에디터 미리보기) 는 명시적 캔버스 파라미터 API 로 예외 처리.
  → Unity 의 (자산 GUID = 영구) + (InstanceID = 활성 씬 런타임) 이원 구조와 동일.
- **D-27. `Context.h` 를 3개 파일로 분리한다.**
  - `Runtime/EngineContext.h` — 호스트 전용 (`EngineContext` 정의).
  - `Runtime/SystemContext.h` — DLL 은 include 하지만 `ScriptAPI.h` 프렐류드는 include 하지 않음.
  - `Runtime/ServiceContext.h` — `ScriptAPI.h` 프렐류드가 include. 사용자 노출.
  현재 B0 에서 한 파일에 셋을 모아뒀는데, `ScriptAPI.h` 가 include 하면 `SystemContext` 정의도 사용자
  TU 로 딸려온다. 파일을 나눠서 include 트리로 노출 범위를 강제.
- **D-28. `SystemContext` ABI 는 (c) 재빌드 강제 + (a) 버전 스탬프 안전망 조합.**
  게임 DLL 은 어차피 사용자 프로젝트마다 호스트와 함께 빌드되므로 재빌드가 자연스러운 규약.
  `SystemContext` 첫 필드는 `uint32 AbiVersion`. 호스트/DLL 이 다른 버전이면 로드 거부.
  런타임 함수 테이블(b) 은 매 프레임 호출되는 시스템에 함수 오버헤드가 곤란해서 채택 안 함.
- **D-29. Renderer 는 저수준 API 만 노출한다.**
  `BeginFrame` / `SetCamera` / `SubmitSprite` / `SubmitMesh` / `EndFrame`. `RenderWorld2D` /
  `RenderWorld3D` 같은 프레임 타입은 소비자 (Framework2D/3D 시스템) 가 자기 안에서 만들고
  Renderer 의 저수준 API 를 반복 호출한다. Renderer 가 `RenderWorld2D` 를 인자로 받으면 `JBroGraphics`
  가 `JBroFramework2D` 를 참조해야 하는 역방향 의존이 생겨서 안 됨.
- **D-30. `GetComponent<T>` 는 `GameObject::m_components` 선형 순회.**
  기존 엔진도 같은 구조 (`FindComponentRaw` 가 `m_components` 순회). n=5 기준 캐시-핫 배열 순회가
  해시맵 조회보다 빠르다. 시스템의 매 프레임 순회는 `Canvas::ForEach<T>` 를 쓰므로 GetComponent
  는 사용자 스크립트의 초기 캐싱 용도. 매 프레임 부담이 없다.
- **D-31. `GetComponent<T>` 는 첫 번째만 반환.** 여러 개 필요하면 `GetComponents<T>` (복수형).
  같은 타입 다중 컴포넌트 (B11) 를 지원하지만 첫 매칭을 조용히 반환하는 게 문서 명시된 계약.
- **D-32. D-29를 보정한다 — Renderer 저수준 API는 즉시 드로우가 아니라 프레임 패킷 수집이다.**
  `BeginFrame` 안에서 `BeginView / EndView` 로 카메라·출력 경계를 명시하고, Framework2D/3D 는
  자기 프레임 타입을 Graphics 에 노출하지 않은 채 `ArrayView` 로 스프라이트·메시 패킷을 일괄
  제출한다. Renderer 는 `EndFrame` 에서 컬링·정렬·에셋 해석·배칭·렌더 그래프 구성·커맨드 기록을
  수행한다. `SubmitSprite / SubmitMesh` 단건 API 는 같은 수집 경로의 편의 함수이며 RHI 를 즉시
  호출하지 않는다. 정상 프레임 경로에서 일반 힙 할당·문자열 생성/비교·`WaitIdle` 을 하지 않는다.
- **D-33. 사용자 커스텀 포스트프로세스는 Shader Graph 에셋으로 제공한다.**
  사용자용 Shader Graph 는 컴파일되어 Shader/Material 에셋과 바인딩 메타데이터가 되고,
  `PostProcessProfile` 이 실행 순서를 보관한다. Graphics 내부 Render Graph 는 패스 순서·렌더 타깃
  수명·배리어를 관리한다. 게임 스크립트에는 Renderer/RHI 또는 임의 GPU 콜백을 노출하지 않는다.
  단일 그래프는 기본적으로 입력 하나에서 출력 하나를 만들고, 여러 단계 효과는 Profile 이 연결한다.
  Bloom 처럼 내부 다중 패스가 필요한 그래프는 Shader Graph 컴파일 결과가 그 패스 구성을 가진다.
- **D-34. `ServiceContext`는 서비스 객체를 값으로, `SystemContext`는 좁은 시스템 인터페이스 포인터를 보관한다.**
  서비스 포인터는 사용자 코드에 null 검사와 수명 혼동을 퍼뜨리므로 채택하지 않는다. 구체 시스템 포인터는
  프렐류드 경계를 오염시키므로 채택하지 않는다. 두 Context는 첫 필드의 `AbiVersion`과 POD 경계를 유지한다.
- **D-35. GameHost의 `Skipped` 프레임은 플랫폼 이벤트 인지형 최대 16ms 대기로 제한한다.**
  정상 `Ready` 프레임은 기다리지 않는다. 무조건 `Sleep`하는 방식은 입력·창 종료 반응을 늦추고,
  아무 대기도 하지 않는 방식은 최소화·표면 일시 불가 상태에서 CPU를 소모하므로 채택하지 않는다.
- **D-36. 차원별 서비스는 선택된 Framework의 별도 값 Context로 제공한다.**
  Updates: D-13, D-27, D-34. Runtime의 세 Context는 공통 경계로 유지하고, `Physics2DService`는
  `Framework2DServiceContext`가 값으로 보유한다. 스크립트는 `GetFramework2DServices().Physics2D`로 접근한다.
  활성 프로젝트를 여는 호스트만 Framework의 스크립트 Context를 바인딩한다. 독립 미리보기 초기화는
  전역 바인딩을 바꾸지 않으며, 프로젝트 종료 시 시스템을 파괴하기 전에 바인딩을 해제한다.
  3D 전용 스크립트 타깃에서는 Framework2D include 경로가 없어 이 접근점 자체가 컴파일되지 않는다.
- **D-37. 스크립트 DLL은 단일 버전형 C 진입점과 확장 Context 블록으로 바인딩한다.**
  Updates: D-14, D-28, D-36과 H4의 기존 `JBroScriptModule_Register` 다중 심볼 설명.
  DLL은 `JBroScriptModule_GetApi` 하나를 내보내고, `ScriptModuleApi`의 ABI 버전·구조체 크기·필수
  Context 요구 목록을 호스트가 먼저 검사한다. 공통 `SystemContext`/`ServiceContext`는 고정 필드로,
  차원별 Context는 `TypeId + AbiVersion + Size + Data` POD 블록으로 로드 시 한 번 전달한다.
  Framework2D가 자기 TypeId·요구 버전·블록 생성과 해석을 내부 ABI 어댑터로 소유하므로 Runtime은
  Framework2D를 참조하지 않고 일반 Framework2D 서비스 공개 헤더도 Runtime 내부 계약을 노출하지 않는다.
  `Load`/`Unload` 함수 포인터는 DLL 수명 전환에만 쓰며 매 프레임 호출하지 않는다. `Load` 실패 시에도
  `Unload` 롤백 훅을 먼저 부른 뒤 DLL을 해제한다. 최초 성공 로드와 명시적 언로드, 이전 모듈을 내린
  모든 재로드 시도는 세대 변경으로 외부 캐시 무효화 신호를 만든다.
  DLL 경계로 C++ 가상 객체나 메모리 소유권을 넘기지 않는다. 구 엔진의 C++ 가상 모듈 객체는 컴파일러·
  CRT·할당자 ABI를 경계에 고정하므로 채택하지 않았고, Context별 내보내기 심볼 방식은 Framework가
  늘 때마다 Runtime 로더 수정과 부분 바인딩 실패를 만들므로 채택하지 않았다.
- **D-38. 차원 독립 공개 값 타입은 JBroCore에 한 번만 정의한다.**
  Framework는 같은 이름의 공개 타입을 다시 선언하지 않고 Core의 정식 타입을 include한다.
  임시 타입이 있던 상태에서 정식 타입을 이식할 때는 소비자 마이그레이션과 임시 정의 제거를 같은
  작업의 완료 조건으로 삼는다. `ScriptAPI.h`와 선택 Framework의 공개 헤더를 함께 컴파일하는 검증과
  공개 타입 중복 검사를 통과하기 전에는 이식을 완료로 표시하지 않는다. `Color`가 이 규칙의 첫 교정
  대상이며, 필드명과 기본값처럼 동명이지만 서로 다른 계약도 소비자별로 명시적으로 보존한다.
- **D-39. Windows의 스크립트 DLL은 원본과 같은 디렉터리의 일회성 섭도 복사본으로 로드한다.**
  Updates: D-25, D-37과 H6. Windows가 로드한 파일을 잠그는 동안에도 빌드 출력 경로를
  삭제·교체할 수 있게, `WindowsPlatform` 내부 핸들이 `원본경로.jbro.PID.순번.dll`을 소유한다.
  의존 DLL 탐색 위치를 바꾸지 않도록 원본과 같은 디렉터리를 쓰며, 로드 실패·재로드·언로드에서
  섭도 파일과 네이티브 핸들 래퍼를 함께 정리한다. 로드 중 원본 파일 삭제·재배치와 섭도 복사본
  개수를 실제 DLL 테스트로 검증한다. 별도로 빌드한 V1·V2 DLL을 교체해 내보낸 revision이
  1→2로 바뀌는 것까지 실측했다. `ScriptSystem` 실행과 연결한 `OnUpdate` 로그 변화는 H6에 남는다.
- **D-40. 차원 독립 `Canvas` 본체와 `Layer` 정체성은 JBroRuntime의 단일 정의로 둔다.**
  Updates: D-2, D-3, D-15와 D-38의 단일 정의 원칙. Framework별 Canvas 복제본을 만들지 않는다.
  오브젝트·컴포넌트 풀과 시스템 스케줄러는 공통 Canvas가 소유하고, Framework2D/3D는 선택된 차원의
  컴포넌트·시스템·렌더 추출만 연결한다. Runtime `Layer`에는 차원 독립 정체성과 소속 계약만 두며,
  블렌드·불투명도·패럴랙스·별도 합성 텍스처 같은 2D 상태는 Framework2D가 별도로 소유한다.
- **D-41. Framework2D의 레이어 합성 상태 타입은 `Layer2D`다.**
  Runtime `Layer`에는 식별자·이름·순서·표시 여부만 남긴다. `Layer2D`는 블렌드·불투명도·공간·
  패럴랙스·별도 합성 텍스처 상태를 보관하며 Framework2D가 소유한다. Runtime `Layer`와의 연결 저장
  방식은 Framework2D 내부 구현으로 두며 Runtime 공개 계약이나 직렬화 형식을 늘리지 않는다.
  공통 Layer에 2D 상태를 남기거나 Framework별 Canvas를 복제하는 방식은 채택하지 않는다.

### 2026-09-12 구조 검토에서 확정한 것

근거·대안·검증은 [structural-refactor-plan.md](./structural-refactor-plan.md) §2·§3에 있다.
판단 기준은 "나중에 바꾸는 비용이 큰 계약은 지금 확정한다"이다.

- **D-42. 모듈을 스크립트가 보는 층(Tier S)과 엔진만 보는 층(Tier E)으로 물리 분리한다.**
  Updates: D-18, D-27. Closes: Open Decision 4·8.
  Tier S = `JBroCore`, `JBroRuntime`(Component·Ref·GameObjectHandle·GameScriptBase·System/ServiceContext·ScriptModule·
  `Internal/InstanceRegistry`), `JBroFramework2D`(컴포넌트·서비스·`GameScript2D`·`Layer2D` 값 타입·
  `Internal/ScriptModuleContext`·`ScriptAPI.h`), `JBroAssetTypes`.
  Tier E = `JBroCanvas`(Canvas·GameObject·Layer·GameSystem·SystemScheduler),
  `JBroHost`(EngineInstance·IFramework·ScriptDLLLoader), `JBroFramework2DSystem`(시스템·렌더 추출·`Framework2D` 클래스),
  Graphics·RHI·Platform·Asset. 의존은 Tier E → Tier S 방향만 허용한다.
  `ScriptAPI.h`는 각 Framework 모듈의 `Include/JBro/ScriptAPI.h`에 두어 경로는 하나, 내용은 차원별이다.
  Tier S의 `ComponentBase::GetOwner()`·`GameScriptBase::GetGameObject()`는 `GameObjectHandle`을 반환하며,
  `GameObject*`를 돌려주는 접근은 `JBroCanvas`의 내부 접근 클래스(구 엔진 `CCanvasRuntimeAccess` 패턴)에만 둔다.
  `Canvas::GetComponent<T>(owner)`(`T*` 반환)는 같은 내부 접근 클래스로 옮기고 `FindComponentRaw`로 개명한다.
  기각: 프렐류드 음성 테스트만 늘리는 안(직접 include를 막지 못함), 한 모듈에 include 루트 둘(§3 빌드 단위 원칙과 충돌).
  `Internal/InstanceRegistry`는 Tier S다. `Ref<T>::Get()`과 `GameObjectHandle::Resolve()`가 레지스트리를 호출하고
  이 둘은 스크립트 DLL이 링크해야 하므로(D-44), 레지스트리가 Tier E에 있으면 DLL이 링크되지 않는다.
  `Canvas`는 레지스트리에 등록·해제하는 쪽이고 Tier E → Tier S 방향이라 문제가 없다.
- **D-43. 차원별 시스템 인터페이스는 확장 블록으로 전달하고 공통 `SystemContext`에는 차원 무관 시스템만 둔다.**
  Updates: D-34, D-36, D-37. `SystemContext::Physics2D`를 제거한다(`SystemContextAbiVersion` 3).
  Framework2D는 `Framework2DSystemContext`(Tier S `Internal/`)를 D-37 확장 블록으로 전달하고
  `Physics2DService.cpp`는 `GetFramework2DSystems().Physics2D`를 읽는다.
- **D-44. `InstanceRegistry`는 프로세스 전역·캔버스 무관이며, 스크립트 DLL에 로드 시 1회 바인딩한다.**
  Updates: D-26. `ScriptModuleLoadContext`에 `Internal::InstanceRegistry* Registry`를 추가한다(ABI 2).
  Runtime의 `InstanceRegistry::Get()`은 바인딩된 포인터를 반환한다. 호스트는 프로세스 시작 시 자기 인스턴스를,
  DLL은 `Load`에서 호스트 것을 바인딩한다. 포인터 1회 바인딩이므로 §6.2의 "매 프레임 함수 테이블 우회 금지"와 충돌하지 않는다.
  레지스트리는 캔버스를 모른다. 슬롯은 프로세스 전역이고 `InstanceId`는 프로세스 유일이므로 캔버스 두 벌(에디터 편집본+Play 사본)이
  공존해도 해석이 모호하지 않다. D-26의 "다중 캔버스 명시 파라미터 예외"와 보류 절의 "Canvas 두 벌이면 핸들 재검토"는
  **재검토 없이 성립**한다. `GameObjectHandle` 16B·`Ref<T>` 24B는 영구 고정이다.
  "어느 캔버스에 생성하는가"는 해석이 아니라 서비스 문제이며, 생성 서비스는 호출 스크립트의 소유 오브젝트에서 캔버스를 얻는다.
- **D-45. 스크립트 실행 순서와 변이 경계는 구 엔진 계약을 그대로 이식한다.**
  Closes: Open Decision 3. 실행 목록은 레이어 합성 순서 → 오브젝트 계층(부모 먼저) → 컴포넌트 부착 순서다.
  목록은 더티 플래그로 지연 재구축하며, 트리거는 스크립트 부착/분리·`SetParent`·레이어 생성/파괴/이동이다.
  순회 중 생성은 즉시 수행하되 목록에는 다음 프레임 반영, 순회 중 파괴는 지연 큐에 넣고 `FixedUpdate` 묶음 뒤와 `Update` 뒤
  두 지점에서 flush한다. 순회 깊이 가드(`ScriptIterationGuard`)는 `Canvas`가 소유하고 `Canvas::ForEach<T>`에도 적용한다.
- **D-46. `Layer`는 식별자(`LayerId`)와 합성 순서 캐시(`m_order`)를 분리해 갖고, 렌더가 순서와 가시성을 사용한다.**
  Updates: D-41(유지·보강). `LayerIndex`를 `LayerId`로 개명한다(단조 증가·재사용 없음·직렬화 값).
  `Canvas`는 Create/Destroy/Move에서 `ReindexLayers()`로 `m_order`를 갱신한다(구 엔진 `CGameLayer::m_index`와 같은 역할).
  렌더 정렬 키는 `(layerOrder, renderOrder, sourceId)`를 `std::uint64_t` 하나로 패킹하고, 비가시 레이어는 추출 단계에서 건너뛴다.
  `Layer2D`는 지연 생성한다 — `GetLayer2D(id)`는 살아 있는 런타임 레이어에 상태가 없으면 기본값으로 만들고,
  죽은 레이어면 스테일 엔트리를 지우고 `nullptr`을 반환한다. Runtime 공개 계약 변경 없음.
  구 엔진 `CGameLayer`의 잔여 필드 귀속: `ScaleMode`·`AnchorToSafeArea` → `Layer2D`,
  `SourceAssetGuid`·`KeepOnCanvasChange` → Runtime `Layer`.
  기각: Runtime `Canvas`에 수명 콜백 추가(D-41 위반, 등록 누락 시 재발), 가변 `Canvas` 접근 차단(D-42로 이미 해소).
- **D-47. 월드 변환 캐시는 `Component::Transform2D` 안에 둔다. `WorldTransform2D`는 폐기한다.**
  Updates: D-3(Transform은 컴포넌트 — 유지). `Transform2D`에 `world`·`worldRotation`·`worldScale`·`worldValid`를 두고
  시스템만 쓴다. `Transform2DSystem`은 `Canvas::GetHierarchyVersion()`이 바뀔 때만 부모 먼저 순서의 `Transform2D*` 배열을
  재구축하고, 매 프레임은 그 배열을 한 번 선형 순회한다(조회 0회, 재귀 없음).
  기각: 시스템이 `WorldTransform2D`를 자동 부착(사용자가 붙이지 않은 컴포넌트가 인스펙터·프리팹 diff에 나타남).
- **D-48. `ComponentBase`의 가상 함수 집합을 확정한다.**
  `~ComponentBase()`, `GetTypeId()`, `OnAttached()`, `OnDetached()`, `OnEnabled()`, `OnDisabled()`.
  스크립트 DLL이 파생하는 타입의 vtable은 ABI이므로 이후 추가는 D-28 재빌드 규약 위에서만 허용한다.
  `GameScriptBase`의 `OnCreate`는 `OnAttached` 뒤, `OnDestroy`는 `OnDetached` 앞에 온다.
  형제 컴포넌트 캐시는 `OnAttached`에서 잡고 `InstanceHandle`과 함께 저장해 프레임 시작에 세대 비교 1회로 검증한다.
- **D-49. `IFramework::Render()`는 `RenderResult { Submitted, NothingToSubmit, Failed }`를 반환한다.**
  호스트는 `Failed`만 치명으로 본다. `Framework3D`는 렌더 시스템이 생길 때까지 `NothingToSubmit`을 반환한다.
- **D-50. 에셋은 값 타입 모듈과 시스템 모듈로 나누고 `AssetManager`는 `AssetSystem`으로 바꾼다.**
  Closes: Open Decision 6. `JBroAssetTypes`(Tier S: `AssetId`·`AssetHandle`·`AssetMetadata`·`Asset::*`)와
  `JBroAsset`(Tier E: `AssetSystem` 로드·캐시 소유, 프로젝트 수명; `AssetRegistry` 메타데이터). 스크립트 표면은 값형 `Service::AssetService`.
  `AssetRegistry`는 로드 소유를 합치지 않는다.
- **D-51. `String`은 `std::string` 래퍼로 영구 확정하고 경계·핫 데이터에서는 금지한다.**
  `GameObject::m_tag` 이식 완료(2026-09-12). 원문은 `NameTable`이 보관하고 `NameId`는
  `MakeNameId(text)`로 표 없이 구할 수 있다. `NameTable`도 `InstanceRegistry`와 같은
  `Local`/`Get`/`Bind`를 가지며 `ScriptModuleLoadContext.Names`로 DLL에 바인딩한다(ABI 3).
  Closes: Open Decision 7(기본 제안 채택). POD Context·패킷·`Ref`·핸들·컴포넌트 공개 필드에 `String`을 두지 않는다.
  이름·태그는 인턴된 정수(`NameId = MakeStableTypeId(text)`)로 두고 문자열은 에디터·직렬화 계층이 보관한다.
  `GameObject::m_tag`가 첫 교정 대상이다. 스크립트 리플렉션 필드의 컨테이너 편집은 DLL이 제공하는 연산을 통한다.
- **D-52. 컨테이너 할당기 정책은 인스턴스를 가질 수 있어야 하며, 프레임 임시 배열은 `JMemoryContext.frame`을 쓴다.**
  구현됨(2026-09-12). 되감기 주체는 `Canvas::BeginFrame`이 아니라 `EngineInstance::Tick`이다 —
  프레임을 여는 쪽이 프레임 메모리를 소유하며, Canvas는 이 메모리를 소유하지 않는다.
  복사는 원본 정책을 물려받지 않고 대입은 받는 쪽 정책을 지킨다. 재해싱은 정책을 유지한다.
  아레나를 넘긴 요청은 기본 힙에서 받아 오고 그 블록도 되감기가 회수한다.
  `Array<T, Allocator>`·`Table<..., Allocator>`의 정책 타입에 `[[no_unique_address]]` 멤버로 상태를 허용한다.
  기본 `HeapAllocator`는 빈 타입으로 유지(크기 증가 0), `JAllocatorRef` 정책을 추가한다.
  `frame`은 `Canvas::BeginFrame`에서 리셋되는 선형 할당기다. 도입 시점은 단계 3의 첫 항목이다 — 프레임 임시 배열을 처음 쓰기 직전.
- **H5 리플렉션 — 생성·파괴 경로만 구현됨(2026-09-13).**
  `ScriptRegistry`(Runtime, `Local`/`Get`/`Bind`)에 DLL이 `{name, typeId, size, alignment, Construct, Destruct}`를
  등록하고 `Canvas::AttachScript(owner, name)`이 `ScriptPool`로 만든다. 로드 컨텍스트 ABI 4(`Scripts`).
  기존 엔진의 `CreateScriptFunc`가 캔버스를 받던 모양은 쓸 수 없다 — D-42의 Tier 분리 때문이다.
  **프로퍼티·인스펙터 메타데이터·직렬화는 아직 없다.** Open Decision 3은 생성 경로를 지목했고 그것은 섰다.
- **프로젝트 파일은 `.jproject`(YAML), 기존 엔진과 같은 키를 쓴다(2026-09-13).**
  `LoadProjectFile` / `EngineInstance::OpenProjectFile`. 읽는 범위는 기존 파일이 실제로 쓰는 부분집합이고,
  모르는 구조는 줄 번호와 함께 거절한다. 아직 읽지 않는 키 아래 블록은 들여쓰기로 건너뛴다.
- **D-53. 죽은 계약을 삭제하고 골격은 "미완"으로 명시한다.**
  삭제: `EngineContext`(`EngineInstance`가 그 역할), `RuntimeModule`/`Runtime.h`, `RefCategory::Canvas`·`Asset`,
  스켈레톤 스모크 3개, `TObjectPool::Slot::generation`, `GameObject::m_destroyContext`/`m_destroyCallback`.
  Audit Snapshot에 명시: `PrefabSpawner`·`AssetRegistry`·`AssetSystem::Load`·`ScriptSystem`은 선언만 있는 골격.
- **D-54. 성능 계약을 측정 가능한 형태로 고정한다.**
  Updates: D-32(패킷 필드 보강). 정상 프레임(스폰 포함)에서 힙 할당 0회, `InstanceRegistry` 영속 조회 0회를 카운팅 할당기·카운터로 단언한다.
  `TObjectPool`: ControlBlock을 구 엔진처럼 `m_freeBlocks`로 재활용하고 `Reserve`에서 미리 확보, `Destroy`는 청크 베이스
  정렬 배열 이진 탐색으로 슬롯을 찾는다. `GameObject`는 `m_activeInHierarchy`를 캐시하고 `SetActive`·`SetParent`가 전파한다.
  `Ref<T>::Get()`은 캐시 슬롯이 살아 있고 세대만 다르면 확정 사망으로 단락한다. `Table<InstanceId, …>`는 항등 해시를 쓴다.
  `SpriteSubmit`·`GpuSpriteInstance`의 `world`는 `Matrix4x4`가 아니라 아핀 6 + 깊이 1이다.
  구현형은 `SpriteTransform2D { float linear[4]; float translation[2]; float depth; }`(28B)이고 인스턴스 스트라이드는 44B다.
  셰이더는 `float4x4`를 조립하지 않고 두 내적으로 위치를 만든다. `MeshSubmit`은 `Matrix4x4`를 유지한다.
  `SpriteSubmit::renderOrder`는 렌더러가 읽지 않아 제거했다(D-53) — 정렬은 `RenderWorld2D`가 제출 전에 끝낸다.
  `depth`는 깊이 버퍼가 붙기 전까지 항상 0이며, 자리를 비워 둔 것은 그때 ABI를 다시 깨지 않기 위해서다.
  `RenderWorld2D`는 `(uint64 key, uint32 index)`를 정렬하고
  아이템은 제자리에 둔다. `Ref<T>`·`GameObjectHandle`은 `SafePtr`와 같이 메인 스레드 전용이다.
- **D-55. `ComponentBase::m_owner`는 `SafePtr<GameObject>`로 유지한다.**
  raw 포인터로 줄이면 8B와 역참조 하나를 아끼지만 §6의 명시 규칙을 바꾸는 일이다. private 멤버라 나중에 바꿔도 공개 계약이
  깨지지 않으므로(D-28 재빌드 규약) 지금 열지 않는다.

- **D-56. 게임 스크립트 언어를 자작하고 C++ 로 트랜스파일한다. 이름 JBroScript, 확장자 `.jscript`.**
  **아직 아무것도 구현하지 않았다.** 방향·근거·실측은 [tasks/jbroscript-plan.md](./jbroscript-plan.md) 에 있다.
  요지: 백엔드는 VM 이 아니라 C++ 소스이며 `ScriptDLLLoader`·`ScriptRegistry`·`ScriptPool`·Tier 분리·POD ABI 가 전부 그대로 쓰인다.
  리플렉션은 **형식 하나(`PropertyInfo`), 생산자 둘** 로 푼다 — 빌트인 컴포넌트는 C++ 매크로, 사용자 스크립트는 트랜스파일러.
  언어는 대체가 아니라 **추가 프론트엔드**다. C++ 스크립트 경로를 죽이지 않으므로 언어가 막혀도 엔진은 멀쩡하다.
  `PropertyInfo` 모양은 2026-09-14 에 확정했다(계획서 §8) — 타입의 사실은 `TypeDescriptor` 에,
  필드의 사실은 `PropertyInfo` 에 두고, 접근은 오프셋이 아니라 접근자 하나로 한다.
  **쪽지 보관함은 둘이다** — 빌트인 컴포넌트는 엔진 수명, 스크립트는 DLL 수명이라 한 그릇에 섞지 않는다.
  Open Decision 3 / H5 의 남은 절반(프로퍼티·인스펙터 메타데이터·직렬화)이 이 결정의 대상이다.
  **기존 엔진의 `JPROP` 스크립트 59건은 옮기지 않는다(2026-09-15).** 변환기도 만들지 않는다.
  남은 언어 결정(구현 언어·타입 범위·표현식·반환 타입·오류 처리·타입체커·API 투영)의 **제안**은
  계획서 §20 에 있고 아직 확정이 아니다.
  **2026-09-15 에 논의를 정리본 둘로 모았다.** 사용자 문법은 [jbroscript-syntax.md](./jbroscript-syntax.md),
  컴파일러 규칙은 [jbroc-rules.md](./jbroc-rules.md). 확정·제안·열림을 항목마다 표시했다.
  **빌트인 쪽 생산자는 2026-09-14 에 붙였다**(`JBRO_FIELD`, `7be6602`) — 계획서 §14.
  **보관함 둘과 2D 빌트인 부착도 2026-09-14 에 끝냈다**(`c64b8c6`, `ff94c69`, `b7354f2`) — 계획서 §15.
  `TypeDescriptor` 에 `fields` 가 생겨 구조체가 자기 필드로 말한다(88 → 96 바이트).
  **3D 빌트인 다섯도 붙였다**(`926491b`, 계획서 §16). 남은 것은 `ArrayOps`/`TableOps` 구현과
  직렬화(`.jcanvas`)다. 등록 함수의 반환값은 소비자가 없어 실패 경로가 변이로 잡히지 않는다(§16.3).
  에셋 참조는 `AssetId`(저장) + `AssetHandle`(해석된 런타임 값, 비저장)로 나눴다(`ff6b41f`, 계획서 §15.4).
  `AssetSystem` 이 스텁이라 **해석 패스는 아직 없다** — 로드가 실제로 생길 때 붙인다.
  **직렬화의 아래층(YAML 부분집합 읽기·쓰기)은 붙였다**(`e5cf75f`, `JBro/Host/Yaml.h`).
  기존 엔진의 `.jcanvas` 다섯 개를 실제로 읽고, 쓰는 모양이 그 파일들과 같다는 것을 테스트가 고정한다.
  **캔버스 저장도 붙였다**(`87d73d6`, 계획서 §17). 구조체는 이름 없이 나열하고
  (`TypeDescriptor::writeFieldsAsSequence`), 기존 엔진의 `.jcanvas` 는 읽지 않는다 —
  거기 있는 컴포넌트가 이 엔진에 하나도 없어서 읽어 봐야 거의 다 버려진다.
  **읽는 쪽도 붙였다**(`05ac54b`, 계획서 §17.5). `ComponentRegistry` 가 이름으로 컴포넌트를
  붙이고, 저장→로드→저장이 같은 글자를 낸다. 읽기는 모르는 필드·타입을 조용히 넘기지 않고
  어디서 멈췄는지 말한다. 3D 컴포넌트 다섯도 이름으로 붙는다(`8930c98`).
  **에디터가 `.jproject` 와 `.jcanvas` 를 연다**(`e076f96`) — 프로젝트를 열고, 씬을 읽고,
  돌리고, 저장하는 길이 UI 없이 한 번 관통한다. 차원(2D/3D)은 `.jproject` 에 적는 자리가
  없어서 호출자가 넘긴다. 그 과정에서 결함 둘을 고쳤다 — 여는 데 실패했을 때 빈 오류를
  돌려주던 것과, 프로젝트 파서가 `Key: ""` 를 `Key:` 로 착각해 그 키와 뒤따르는 줄을
  건너뛰던 것이다.
  **`#line` 디버그 매핑도 확인했다**(계획서 §13.3.1) — 주소에서 `.jscript` 의 줄을 정확히
  되찾는다. 다만 **그것은 절반이다**(§18): 그 줄에 서서 스크립트가 보이려면 이미터가
  ① 문장마다 `#line` 을 찍고 ② 이름을 1:1 로 두고 ③ 임시변수를 만들지 않아야 한다.
  셋을 안 지키면 지역 변수 창에 `jbro_tmp_0..8` 이 뜨고 중단점이 옆 줄에 선다(실측).
  그리고 이 실측은 **Visual Studio 기준**이다 — 자작 Code-OSS 포크에서는 MS C/C++ 확장이
  막혀 있고 오픈 대안은 PDB 지원이 약하다. 그쪽은 아직 열린 항목이다(§18.7).

- **D-57. 수학 타입은 차원별 모듈에 둔다. Core 로 올리지 않는다.**
  Closes: Open Decision 2. Narrows: D-38.
  `Vec2`·`Rect`·`Matrix3x2` 는 `JBroFramework2D/Math2D.h`, `Vec3` 는 `JBroFramework3D/Math3D.h`,
  `Matrix4x4` 는 `JBroGraphics/Renderer.h` 에 두는 현재 배치를 유지한다.
  D-38 의 "차원 독립 공개 값 타입"은 `Color` 처럼 **차원 의미가 없는 것**에만 적용된다.
  벡터·행렬은 차원이 곧 의미이므로 그 대상이 아니며, 2D 프로젝트가 3D 수학을 링크하지 않는다.
  대가: 2D 와 3D 를 모두 보는 코드(에디터·Graphics)는 양쪽 타입을 각각 받는다.
  변환이 필요하면 그 지점에 명시적으로 둔다.

- **D-58. `CameraProjection2D::PixelPerfect` 는 실제로 동작해야 한다.**
  Closes: Open Decision 5. 지금 `RenderBridge2D` 가 명시적으로 실패시키는 자리를 구현으로 바꾼다.
  세부 계약(기준 해상도, pixels-per-unit, 정수 배율, 남는 영역 처리)은 착수 시 기존 엔진의 것을
  먼저 읽고 따른다 — `.jproject` 때와 같은 이유로 두 번째 계약을 새로 만들지 않는다.

- **D-59. `InstanceId` 의 세션 비트는 한 실행 안에서의 비충돌만 보장한다.**
  Closes: Open Decision 1. 프로세스당 값 하나를 만들어 모든 생성기가 공유하고, 그 안에서 인덱스가 겹치지 않게 한다.
  실행 간 절대 비반복은 보장하지 않는다 — 10비트는 1024회 실행에 한 바퀴 돌며, 그것을 없애려면
  비트 폭을 늘리거나 영속 상태를 두어야 한다. 저장 파일이 세션을 넘어 ID 를 신뢰해야 할 때 다시 연다.

- **D-60. 에디터 UI 는 ImGui 로 하고, 외부 라이브러리는 소스째로 `ThirdParty/` 에 둔다.**
  Closes: 서드파티 정책이 없던 상태. Narrows: `<JBro/...>` include 규칙(외부 라이브러리는 예외).
  **왜 ImGui 인가**: 인스펙터는 매 프레임 프로퍼티 표에서 다시 그려지는 것이고, 이미모드가 그
  모양이다. 리테인드 UI 를 쓰면 뷰모델을 따로 들고 동기화해야 하며 그 둘이 어긋나는 버그가 생긴다.
  RHI 에 필요한 것(`SetScissor`·`SetViewport`·정점/인덱스 버퍼·텍스처·`DrawIndexedInstanced`)이
  전부 이미 있어 새로 뚫을 것이 없다.
  **Code-OSS 와 겹치지 않는다**: 씬 에디터는 네이티브, 코드 에디터는 Code-OSS 다. 씬 뷰를
  웹뷰에 넣으면 매 프레임 렌더 결과를 복사해 넘기고 입력을 되돌려받아야 하는데 얻는 것이 없다.
  둘은 파일(`.jproject`·`.jcanvas`·`.jscript`)로 대화한다.
  **소스째로 넣는 이유**: 빌드 재현성과 버전 고정. 대가는 리포 크기(ImGui 4MB).
  기존 엔진도 같은 방식이었다.
  **성능 규칙의 범위**: "매 프레임 도는 경로에 힙 할당·문자열 생성 금지" 는 **게임 프레임 경로**
  얘기다. ImGui 는 매 프레임 할당과 `std::string` 을 쓰며, 에디터 UI 는 그 규칙의 대상이 아니다.
  가져온 것은 코어뿐이고 **백엔드는 `JBroRHI` 위에 직접 쓴다** — `imgui_impl_dx12` 를 쓰려면
  RHI 가 감춘 D3D12 핸들을 도로 꺼내야 하고 그러면 추상화에 구멍이 생긴다.

- **D-61. RHI 는 텍스처와 샘플러를 슬롯에 직접 묶는다.** 바인드 그룹도 바인들리스도 아니다.
  ImGui 백엔드(D-60)를 쓰려다 렌더러에 **텍스처링이 아예 없다는 것**이 드러나 정한 것이다 —
  스프라이트 셰이더는 틴트를 돌려주고, RHI 에는 `TextureUsage::Sampled` 라는 enum 값만 있었지
  픽셀을 올리는 길도, 샘플러도, 묶는 길도 없었다. 스프라이트도 결국 필요한 기능이다.
  **모양**: `SetTexture(slot, texture)` / `SetSampler(slot, sampler)`. 슬롯 번호가 곧 셰이더의
  `t`/`s` 레지스터다. 기각: 바인드 그룹(지금 필요보다 크다), 바인들리스(`ImTextureID` 와 잘 맞지만
  초기 설정이 크다 — 나중에 옮기더라도 바뀌는 것은 백엔드와 셰이더이고 호출부는 한 줄이다).
  **파이프라인이 개수를 미리 선언한다**(`sampledTextureCount`, `samplerCount`). 루트 시그니처가
  파이프라인과 함께 만들어져 바뀌지 않으므로 그리기 직전에 알 수 있는 값이 아니다.
  선언한 자리를 비운 채 그리면 거절한다 — 통과시키면 셰이더가 남의 디스크립터를 읽는다.
  **디스크립터 링은 프레임 슬롯마다 갈라 둔다.** 겹쳐 도는 프레임이 아직 읽는 자리를 덮지 않게
  해야 한다. 샘플러 쪽은 프레임당 512 가 상한에 가깝다 — D3D12 가 셰이더 가시 샘플러 힙을
  2048개로 제한하고 그것을 프레임 수가 나눠 갖기 때문이다(`static_assert` 가 둘을 묶는다).
  **업로드는 GPU 를 기다리고 프레임 안에서는 거절한다**(`ReadTexture` 와 같은 계약).
  검증은 2x2 텍스처를 화면에 그리고 픽셀을 되읽어 네 텍셀이 제 사분면에 앉는지 본다 —
  디스크립터가 엉뚱한 자리에 가도 D3D12 는 아무 말도 하지 않는다.

- **D-62. 입력은 플랫폼이 이벤트로 모아 준다.** 매 프레임 읽어 가는 키 상태 배열이 아니다.
  ImGui 를 붙이려다 **플랫폼이 입력을 아예 안 만진다는 것**이 드러나 정한 것이다 —
  `WindowProcedure` 가 `WM_CLOSE` 하나만 보고 나머지는 `DefWindowProcW` 로 넘기고 있었다.
  **모양**: `IPlatform::GetInputEvents()` 가 지난 `PumpEvents` 가 모은 `JArrayView<InputEvent>`
  를 돌려준다. 다음 `PumpEvents` 가 그 목록을 비운다. `InputEvent` 는 20바이트 POD 다 —
  게임 DLL 경계를 넘는다. 기각: 폴링식 상태(한 프레임 안에 눌렀다 뗀 키와 글자 입력 순서가
  사라진다. 에디터의 텍스트 필드가 바로 그것을 필요로 한다), ImGui 가 `WndProc` 을 직접 훅
  (`imgui_impl_win32` 방식. 지금은 제일 짧지만 게임 입력을 나중에 또 만들게 된다).
  **키는 물리 키다.** `Key::A` 는 QWERTY 의 A 자리이지 그 키가 내는 글자가 아니다.
  글자는 `InputEventKind::Text` 로 따로 온다 — 같은 키가 배열에 따라 다른 글자를 내기 때문이다.
  **Win32 가 숨긴 것 셋을 넘어야 했다.** 좌우 Shift·Control 과 Enter·키패드 Enter 는 같은
  가상 키이고 스캔코드와 확장 비트로만 갈린다. 글자는 `TranslateMessage` 가 만드는 `WM_CHAR`
  로 온다. BMP 밖 글자는 UTF-16 반쪽 둘로 나눠 오므로 앞쪽을 들고 있다가 합친다.
  플랫폼 포인터는 창 클래스의 여분 슬롯(`cbWndExtra`)에 둔다 — `GWLP_USERDATA` 는 이미
  닫기 플래그가 쓰고 있다. 한 프레임 상한은 4096개이고 넘치면 버린다.
  검증은 창에 실제 메시지를 `PostMessageW` 로 넣고 펌프를 돌려서 본다 —
  `SendMessageW` 로 `WndProc` 을 직접 부르면 `PeekMessage` 와 `TranslateMessage` 를 건너뛰어
  실제로 도는 경로가 아닌 다른 경로를 재게 된다. 뮤테이션 15/15.

- **D-63. 에디터의 게임 뷰는 렌더 타깃 하나 차이다.** 렌더 경로를 따로 만들지 않는다.
  빡대리가 못박은 선이다 — *"렌더러가 최종 게임화면을 메인 렌더타겟에 전해주냐,
  에디터 뷰포트에 전해주냐 차이"*. 경로가 갈리면 에디터에서 보는 것과 실행했을 때
  보는 것이 달라지고, 그 어긋남은 한참 뒤에야 드러난다.
  기존 엔진도 같은 모양이다(`Render2DFrameDesc::Target`, null 이면 백버퍼).
  **모양**: `Renderer::BeginFrame(const FrameTarget&)`. 렌더러의 모드가 아니라 **인자**다 —
  같은 렌더러를 게임 실행에서는 백버퍼로, 에디터에서는 텍스처로 부른다.
  크기가 함께 간다: 뷰포트가 타깃 안에 드는지 재는 기준이 그것이고, 창 크기로 재면
  게임 해상도가 창보다 클 때 멀쩡한 뷰포트가 "화면 밖" 으로 거절당한다.
  **UI 는 같은 프레임 안에서 백버퍼에 얹는다.** `Renderer` 가 뷰를 다 기록한 뒤,
  제시하기 전에 `FrameOverlay` 를 부른다. 프레임과 커맨드 컨텍스트를 밖으로 꺼내지 않고
  불러들이는 이유는, 꺼내 주면 프레임을 여닫는 주체가 둘로 갈리기 때문이다.
  오버레이가 false 를 돌려주면 프레임을 버린다 — 반쯤 그려진 UI 를 내보내지 않는다.
  **그릴 것이 없는 프레임을 버리는 규칙(F-7)에 예외가 생긴다.** 에디터에서는 게임 화면이
  텍스처로 가서 백버퍼에 낼 것이 없는 것이 정상이고, 그 프레임을 버리면 UI 까지 사라져
  화면이 통째로 멈춘 것처럼 보인다. 오버레이가 걸려 있으면 버리지 않는다.
  `Renderer::GetDevice()` 를 연다. 에디터가 게임 뷰 텍스처와 자기 UI 파이프라인을
  만들어야 하는데 디바이스를 쥔 것이 렌더러뿐이었다. **리소스 생성·파기 전용**이다.
  기존 엔진에서 가져올 것 둘: 게임 뷰 RT 는 **프로젝트 해상도**로 만들고 패널에는
  레터박스로 붙인다(패널 크기로 만들면 창을 끌 때마다 재생성되고, 무엇보다 게임이 보는
  화면 크기가 에디터 창에 따라 달라져 `ScreenToWorld` 가 어긋난다). 그리고 게임 뷰 렌더는
  **매 프레임 opt-in** 이다 — 패널이 실제로 그려질 때만 다음 프레임용으로 요청을 다시
  켠다. 탭이 닫히거나 가리면 렌더가 멈추고, RT 는 파기하지 않아 다시 보일 때 이어진다.
  기각: 게임 화면 위에 UI 를 덮는 오버레이만 두는 안(게임 뷰를 패널 안에 넣을 수 없다),
  에디터가 프레임을 직접 여닫는 안(게임 실행과 에디터에서 루프가 둘로 갈린다).
  검증은 같은 스프라이트를 창(64x64)보다 큰 96x48 텍스처에 그려 백버퍼에 그렸을 때와
  같은 그림이 나오는지 보고, 다음 프레임을 타깃 없이 돌려 백버퍼로 돌아오는지 본다.
  호스트 쪽은 가짜 하니스로 네 상태를 밟는다 — 타깃 없음/있음, 빈 프레임을 오버레이
  없이/있게. 뮤테이션 6/6(타깃) + 7/7(오버레이).
  **매 프레임 opt-in 을 넣었다(2026-09-15).** `FrameTarget::recordViews` 가 거짓이면 렌더러는 그 프레임의
  뷰를 기록하지 않고 `RendererFrameStats::skippedViewCount` 로 센다(버린 것과 구분한다). UI 는 엔진 Tick
  보다 먼저 만들어지므로 게임 뷰 패널이 `ImGui::Image` 를 붙인 프레임에 `EditorApplication::RequestGameView()`
  를 부르고, `Tick` 이 엔진 Tick 직전에 그 플래그로 타깃을 다시 준다 - "다음 프레임용" 이 아니라 같은
  프레임이다. 닫힌 패널·다른 탭에 가린 패널은 `Begin` 이 거짓이라 `OnDraw` 가 불리지 않는다. 텍스처는 파기하지
  않는다. 호스트 계약 테스트 ⑤(렌더 패스가 열리지 않고 프레임은 제시됨)와 에디터 테스트(패널을 닫으면
  `skippedViewCount == 1`, 다시 열면 `viewCount == 1`)로 잰다. 뮤테이션 4/4(늘 기록, 요청 플래그 안 내림, 패널이 요청 안 함, 렌더러가 플래그 무시).

- **D-64. 그래픽 테스트는 검증 레이어가 조용한지까지 본다.** 픽셀만 보지 않는다.
  가드를 지워도 그림이 같아 뮤테이션이 죽지 않는 일이 두 번 나와서 정한 것이다 —
  하드웨어가 잘못된 호출을 조용히 주워 담고, 그 기계에서만 맞게 나온다.
  **모양**: `IRHIDevice::GetValidationErrorCount()`. D3D12 는 info queue 를 읽는다.
  세 가지를 지켜야 한다. **디버그 레이어는 프로세스 단위이고 첫 디바이스 전에 켜야 한다**
  (`EnableD3D12ValidationForProcess()`, `main` 맨 앞). 늦게 켜면 D3D12 가 아무 말 없이
  무시하고, 그러면 아무것도 안 세는 0 을 증거로 믿게 된다. **심각도는 꺼내는 쪽에서**
  거른다(쌓는 쪽에 걸면 저장되지 않아 나중에 볼 수 없다). **WARNING 까지 센다** —
  D3D12 는 인덱스 버퍼 초과를 WARNING 으로 낸다. 최적 클리어 값 권고(ID 820) 하나는
  제외한다: 패스마다 지우는 색이 달라 만들 때 정할 수 없고, 매 실행 섞이는 잡소리
  하나가 나머지 경고를 보지 않게 만든다.
  **GPU 기반 검증도 켠다.** 기본 레이어는 그릴 때 디스크립터 테이블의 리소스 상태를
  보지 않아서, 렌더 타깃 상태로 샘플링하는 것을 잡지 못한다. 이 스위트에서 약 5초를
  더 쓰고, 배리어 뮤테이션을 1/4 에서 4/4 로 올렸다.
  **손잡이가 도는지 자체를 재는 테스트를 둔다** — 인덱스 6개짜리 버퍼에서 12개를 그리고
  숫자가 올라가야 한다. 그것이 없으면 다른 테스트의 "조용했다" 가 전부 공허해진다.
  세는 김에 메시지도 찍는다: 숫자만으로는 다시 재현해서 디버거를 붙여야 한다.
  여기서 알게 된 것 하나 — **음수 시저는 D3D12 오류가 아니다**(뷰포트 경계 하한 -32768).
  `EditorUI` 의 clamp 는 D3D12 에서 아무것도 하지 않고, Vulkan 규격 때문에 남긴다.

- **D-65. 에디터는 실행 파일을 따로 갖는다(`JBroEditorHost`).** 게임 실행(`JBroGameHost`)과 나란히 둔다.
  화면에 띄울 길이 없어서 정한 것이다 — 그때까지의 확인은 전부 숨긴 창의 백버퍼를
  되읽는 것이었고, "칠해졌다" 는 알아도 "제대로 보인다" 는 몰랐다.
  인자로 프레임 수를 받으면 그만큼 돌고 끝난다. 사람 없이 띄워 보고 창을 캡처하려면
  그 손잡이가 있어야 한다.
  **켠 상태로 실제 화면을 확인했다**: 1280x720 창, `Game` 패널, 그 안에 640x360 게임 뷰가
  레터박스로. 창 크기를 700x900 으로 바꿔도 패널이 따라가고 비율이 유지된다.

- **D-66. `EditorUI` 의 정점·인덱스 버퍼는 프레임 슬롯마다 나눈다.**
  하나로 두고 매 프레임 덮어쓰고 있었다. `WriteBuffer` 는 매핑된 메모리에 그냥 memcpy 라
  **아무것도 기다려 주지 않는데**, 프레임은 셋까지 겹쳐 돈다 — 지난 프레임이 아직 읽는
  중에 다음 프레임이 덮어쓴다. 렌더러는 스프라이트 인스턴스 버퍼를 같은 이유로 이미
  슬롯마다 나눠 갖고 있었다.
  **나누는 열쇠는 프레임 슬롯이다.** 그 슬롯의 지난 프레임이 끝났다는 것은 RHI 가 이미
  보장한다. 그래서 오버레이가 슬롯을 함께 받고, `Draw(commands, frameSlot)` 이 그 슬롯의
  버퍼에 채운 뒤 그린다. **버퍼를 만드는 일은 프레임 밖에 남는다**(`CreateBuffer` 가
  프레임 안에서 거절한다) — 어느 슬롯이 쓰일지 알기 전이므로 `EndFrame` 이 전부 잡아 둔다.
  슬롯 수는 `IRHIDevice::GetFramesInFlight()` 에게 묻는다. 짐작하는 것은 백버퍼 포맷을
  짐작하는 것과 같은 실수다.
  **이 결함은 그림으로 잡히지 않는다.** 테스트가 느려서 겹치지 않기 때문이다. 대신
  두 가지를 붙잡았다: 프로브를 두 프레임 돌려 **두 번째**(슬롯 0 이 아닌) 프레임의 그림을
  보고, 렌더러 계약 쪽에서는 **연속 두 프레임의 슬롯이 달라야 한다**고 못 박았다.
  후자가 없으면 늘 0 을 넘기는 코드가 통과한다 — 0 에 쓰고 0 을 읽으니 그림은 맞다.

- **D-67. 프로젝트가 없어도 에디터는 그린다.** 호스트의 "그릴 것이 없으면 프레임을
  버린다"(F-7)에 두 번째 예외가 생겼다.
  첫 예외는 게임이 텍스처로 가서 백버퍼가 빌 때였고(D-63), 이번은 프레임워크가 아예
  없을 때다. 게임에게는 프로젝트가 없으면 그릴 것이 없는 게 맞지만, **에디터는 그때가
  메뉴와 프로젝트 브라우저가 필요한 때다.** 오버레이가 걸려 있으면 프레임을 연다.
  최소화와 프로젝트 정리 중은 그대로 건너뛴다 — 그릴 표면이 없거나 지금 내려가는 중이다.

- **D-68. 빌려 쓰는 디바이스는 먼저 죽을 수 있다.** `EditorUI::AbandonDevice()` 가 그 경우다.
  호스트는 렌더가 실패하면 **그 프레임 안에서 스스로 정리하고 디바이스까지 놓는다**.
  에디터 UI 가 그 디바이스로 만든 파이프라인과 텍스처를 뒤늦게 해제하려 들면 그 자리에서
  터진다 — 실제로 터졌고, 창을 닫는 **보통 경로**가 바로 그 길이다.
  `AbandonDevice` 는 아무것도 해제하지 않고 핸들만 버린다. 디바이스가 죽을 때 그 위의
  것들도 함께 죽었기 때문이다. 에디터는 `Tick` 이 거짓을 돌려주는 즉시 그것을 부른다.

- **D-69. 테스트 프로세스에서 단언은 대화상자를 띄우면 안 된다.**
  뮤테이션 한 번이 7분을 멈춰 있었고 교착인 줄 알았는데, ImGui 단언이 CRT 의
  중단/재시도/무시 창을 띄우고 사람을 기다리고 있었다. **사람 없이 도는 자리에서는
  멈춘 것과 실패한 것을 구분할 수 없다** — 이쪽이 터지는 것보다 나쁘다.
  `TestMain` 이 `_CrtSetReportMode` 로 단언을 stderr 로 보내고 중단시킨다.
  뮤테이션 도구도 함께 고쳤다: 테스트에 시간 제한을 두고, 원본을 기억해 두었다가
  되돌리는 대신 **`git checkout` 으로** 되돌린다. 기억해 두는 방식은 그 사이에 사람이
  고친 것을 지워 버린다(실제로 지웠다).

- **D-70. 에디터 패널은 레지스트리에 등록한다. 생명주기 훅은 다섯이다.**
  `EditorApplication::BuildEditorUi` 안에 `ImGui::Begin("Game")` 이 박혀 있어서 패널
  두 개째부터 안 되던 것을 고치면서 정했다.
  **모양**: `EditorPanel` — `OnCreate` / `OnDestroy` / `OnUpdate` / `OnDraw` / `OnMenuBar`.
  `AddPanel` / `FindPanel` / `GetPanelCount`. 같은 제목은 거절한다(ImGui 가 제목으로
  창을 식별해서 둘이 한 창을 나눠 쓴다).
  **훅이 다섯인 것은 재 봤기 때문이다.** 기존 엔진의 `IImWindow` 는 스물한 개를 두었고,
  그 위의 패널 열세 개가 override 하는 것을 세어 보니 `OnCreate` 16, `OnRenderStay` 16,
  `OnUpdate` 7, `OnDestroy` 6, `OnMenuBar` 5 였다. 나머지 아홉(포커스·렌더·클립의
  Enter/Stay/Exit)은 **패널 override 가 0건**이다. 나중에 넣는 것이 지우는 것보다 쉽다.
  **`OnUpdate` 는 닫혀 있어도 돈다.** 안 보인다고 멈출지는 프레임워크가 아니라 패널이
  정할 일이고, 열어 볼 때만 세는 통계 패널은 열어 보는 행위가 측정을 바꾼다.
  **도킹**: 창 전체를 덮는 dockspace. 처음에 전부 한 노드에 붙였더니 탭으로 겹쳐
  맨 위 하나만 보였다 — 그래서 패널이 `GetPreferredDock()` 으로 자기 자리를 말하고
  (Center/Left/Right/Bottom) 에디터는 여전히 어느 패널이 무엇인지 모른다.
  기존 엔진의 `InitializeDockLayout(dir)` 이 그 자리다. 기본 자리는 첫 프레임에 한 번만
  잡고 그 뒤로는 사용자가 옮긴 자리를 따른다.
  기각: 기존 21개 그대로(안 쓰는 훅 아홉을 지금 만들고 유지해야 한다),
  3개로 더 줄이기(기존 패널 6개가 `OnUpdate` 를, 4개가 `OnMenuBar` 를 쓴다).

- **D-71. 편집은 커맨드로만 한다. 드래그 하나가 되돌리기 하나다.**
  기존 엔진 `IEditorCommand` 를 그대로 따른다 — `GetName` / `Execute` / `Undo` / `Redo` /
  `TryMerge`. **`Execute` 가 성공해야 쌓인다**: 실패한 편집이 스택에 남으면 다음 Ctrl+Z 가
  일어나지도 않은 일을 되돌린다.
  **드래그 병합이 기존에서 가져온 가장 값진 부분이다.** 슬라이더를 끄는 동안 프레임마다
  커맨드가 생기는데 그것을 다 쌓으면 되돌리기를 백 번 눌러야 한다. `Execute` 는 값이 바뀐
  프레임에만 불려서 "손 뗀 순간" 을 볼 수 없는데, 마우스 왼쪽 버튼의 **누른 시간이 누르는
  동안 단조 증가하고 새로 누르면 0 으로 돌아간다** — 그 값이 줄었으면 새 덩어리다.
  인스펙터도 기즈모도 배선할 것이 없다.
  **저장 여부는 불리언이 아니라 판번호다**(`revision != savedRevision`). 고쳤다 되돌려도
  상태가 어긋나지 않는다. 기존은 문서마다 두었고(스프라이트·이펙트 편집기가 따로 있어서)
  여기는 캔버스 하나뿐이라 하나다.
  **인스펙터는 값을 직접 쓰지 않는다**: 스냅샷 → 위젯이 바꿈 → 도로 되돌림 → 커맨드로
  다시 적용. 쓰는 길이 하나로 남아야 되돌리기가 무엇을 되돌리는지 갈리지 않는다.
  Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z(기존과 같은 배치). **텍스트 필드에 타자 치는 중이면
  건너뛴다** — 글자를 되돌려야지 씬을 되돌리면 안 된다.
  기존과 다른 점 둘. **커맨드가 하나다**: 기존은 바이트 복사용과 직렬화 스냅샷용을
  나눴는데(메모리를 소유하는 타입은 얕은 복사가 안 되므로) `ValueCodec` 이 그 구분을
  흡수했다. **스택에 상한(256)을 두었다**: 기존에는 없어서 오래 켜 두면 계속 자란다.
  검증은 유닛 6 + 마우스가 있는 것 1. 드래그 병합은 ImGui 컨텍스트가 있어야만 도는
  코드라 나머지가 못 미친다 — 렌더러 없이 컨텍스트만 세우고 버튼을 손으로 눌러
  네 가지를 봤다(드래그 3프레임=1항목, 뗐다 다시=새 항목, 드래그 중 다른 필드=안 합침,
  되돌린 직후=안 합침).

- **D-72. 커맨드는 오브젝트를 포인터가 아니라 에디터 번호로 가리킨다.**
  **삭제를 되돌리면 오브젝트가 새로 만들어진다.** 그 전에 쌓인 커맨드가 들고 있던
  포인터는 죽는다 — 옮기고, 지우고, 되살린 뒤에 그 옮김을 되돌리면 아무 일도 안 난다.
  사용자에게는 Ctrl+Z 가 고장 난 것으로 보인다.
  기존 엔진은 파일 직렬화용 GUID 가 이미 있어서 커맨드마다 그것으로 다시 찾았다.
  우리는 없으므로 **에디터가 자기 번호를 매긴다**(`EditorObjectRegistry`). 저장 파일에는
  나가지 않고 편집하는 동안만 산다. 되살릴 때 `Rebind` 로 **같은 번호에 다시 건다**.
  모르는 번호에는 걸지 않는다 — 지어내면 그 번호를 들고 있던 커맨드가 엉뚱한 것을 찾는다.
  **삭제 undo 는 스냅샷이다**(기존과 같다). 컴포넌트는 풀이 소유하고 되살리면 주소가
  달라지므로 바이트를 들고 있을 수 없다. 값은 코덱 글자로 뜬다.
  **나무는 평평하게 + ParentIndex** 로 뜬다 — `Array` 가 자기 타입을 품을 수 없기도 하고,
  캔버스 파일이 이미 같은 방식으로 계층을 적는다. 앞에서부터 만들면 부모가 늘 먼저 있다.
  프로퍼티를 등록하지 않은 컴포넌트 타입이 있으면 **삭제를 거절한다** — 되살려도 값이
  비므로, 조용히 잃는 것보다 낫다.
  아직 없는 것: 다중 선택, 복사/붙여넣기, 컴포넌트 추가·제거·순서 커맨드
  (기존에는 서른 개 가까이 있다).

- **D-73. 에디터 생김새는 기존 엔진에서 그대로 옮긴다.** 비슷하게 새로 고르지 않는다.
  색 일흔 개와 치수(모서리 3.0, 탭만 각지게 + 위 선 2.5, 트리선, 도킹 분리선 1.0,
  `WindowMenuButtonPosition = None`, `WindowMinSize (60,30)`)는 눈으로 맞춰 가며 깎은
  값이다. 바꿀 이유가 생기면 그 이유를 `EditorTheme.cpp` 에 적고 바꾼다.
  글꼴도 malgun 15px, 오버샘플 3×3, `PixelSnapH` 로 같다. **다른 점 하나**: 기존은 한글
  자모·음절 범위를 손으로 넘겼는데 그때 ImGui 는 아틀라스를 미리 구워야 했기 때문이다.
  우리 백엔드는 `RendererHasTextures` 를 켜서 글자가 필요할 때 올라가므로, 범위를 적으면
  쓰지도 않을 글리프를 굽고 적지 않은 글자는 못 쓴다. 글꼴이 없는 기계는 기본 글꼴로
  떨어지고 그렇게 말한다 — 거기서 멈추면 글꼴 하나 때문에 아무것도 못 본다.
  아직 안 가져온 것: FontAwesome 아이콘 폰트(파일도 없고 아이콘 쓸 자리도 아직 없다).

- **D-74. 좁은 문자열 리터럴은 UTF-8 이다(`/utf-8`).**
  한글 이름을 넣어 보다 알았다. `/utf-8` 이 없으면 MSVC 가 좁은 리터럴을 **시스템 코드
  페이지**(한국어 윈도우에서 949)로 인코딩하는데, 우리가 글자를 넘기는 곳은 전부 UTF-8 을
  기대한다 — ImGui 도, `.jcanvas` 도, `NameTable` 도. 한글이 섞인 UI 문자열이 화면에서
  깨지고 컴파일러는 경고(C4566)만 하고 지나간다.
  기존 엔진은 모든 구성에 `/utf-8` 을 켜 두었다. `JBro.Common.props` 에 넣어 모든 모듈에
  적용한다.

- **D-75. 프레임 밖에서 명령 할당자를 되감기 전에, 그것을 쓰던 프레임을 기다린다.**
  에디터 뮤테이션을 돌리다 인스펙터를 훑는 테스트에서 디바이스가 날아가 찾았다.
  `DXGI_ERROR_INVALID_CALL`, **검증 레이어는 한 마디도 하지 않았고**, 매번 죽지도
  않았다 - API 호출만 보면 틀린 곳이 없는 GPU 타임라인의 사고라서 그렇다.
  `D3D12Device::WriteTexture` 는 프레임 밖에서 돌면서
  `m_commandAllocators[m_nextFenceValue % MaxFramesInFlight]` 를 그냥 되감았다.
  그 할당자를 GPU 가 아직 읽고 있으면 디바이스가 통째로 죽는다.
  `BeginFrame` 은 자기 슬롯의 펜스를 기다리고 `ReadTexture` 는 `WaitIdle` 로 흐름을
  비운다 - **여기만 빠져 있었다.**
  **이 길은 글꼴 아틀라스에 글자가 하나 늘 때마다 돈다**(ImGui 1.92 는 글리프를
  필요할 때 굽는다). 툴팁이 처음 뜰 때, 메뉴를 처음 열 때, 처음 보는 이름이
  인스펙터에 뜰 때다. 한가하면 할당자가 마침 비어 있어 멀쩡하고, 프레임이 밀려
  있을 때만 죽는다.
  고침은 되감기 전 `WaitIdle()`. 슬롯 셈이 `BeginFrame` 과 달라
  (`m_nextFenceValue` 대 `m_frameSerial`) 어느 슬롯이 걸릴지 모르고, 이 함수는
  어차피 아래에서 제 복사가 끝날 때까지 막는다.
  검증은 프레임마다 **아직 안 그린 한글 음절**을 그리는 패널로 300프레임이다.
  ASCII 는 이미 구워져 있어 이 길을 열지 못하고, 몇 프레임으로는 재현되지 않는다.

- **D-76. 삭제 커맨드는 반쪽 스냅샷을 거절한다.** 배열이 비었는지만 보아서는 모자란다.
  `Capture` 는 프로퍼티를 등록하지 않은 컴포넌트를 만나면 거짓을 돌려주는데
  (D-72), **부르는 쪽이 그 대답을 버리고 있었다.** 뿌리가 걸리면 배열이 비어
  걸러지지만, **자식**이 걸리면 뿌리 스냅샷은 이미 들어가 있어 안 비었다 -
  캔버스는 나무를 통째로 지우고 되돌리기는 뿌리만 되살린다. 자식은 영영 사라지는데
  커맨드는 성공했다고 말한다.
  닿을 수 있는 길이다: `Canvas::AttachScript` 가 붙인 스크립트도 `GetComponents()`
  에 들어오고 리플렉션 표는 없다.
  뜬 결과를 `m_captured` 로 들고 있다가 `Execute` 에서 함께 본다.

- **D-77. 뮤테이션으로 재고 나서 남는 것은 "죽일 수 없는 것" 과 "안 잰 것" 을 갈라 적는다.**
  에디터 구간에 71개를 돌렸다. 1회차 36개 중 13개만 잡혔고, 그 자리를 메우며
  진짜 버그 둘(D-75, D-76)이 나왔다. 최종으로 남은 넷은 **동치 뮤턴트**라고
  판단했고 근거는 이렇다 - 테스트를 지어내지 않고 여기 적는다.
  - `prop:an-empty-path-resolves`: `path.depth == 0` 을 빼도 루프가 돌지 않아
    `found` 가 널로 남고 그 아래 검사가 잡는다. 이른 탈출은 뜻을 적은 것이다.
  - `cmd:duration-equal-breaks-drag`: `<` 를 `<=` 로. 누른 시간은 프레임마다
    늘어나므로 같아지려면 `dt == 0` 이어야 한다.
  - `insp:widget-write-is-not-reverted`: 위젯이 쓴 값과 커맨드가 쓰는 값이 같다.
    되돌려 놓는 줄은 **쓰는 길을 하나로 남기는 뜻**이지(D-71) 값을 바꾸지 않는다.
  - `insp:readonly-edit-still-commits`: 읽기 전용 값은 `BeginDisabled` 안이라
    위젯이 "바뀌었다" 고 답할 수 없다. 저 조건의 `editable` 은 겹으로 두른 것이다
    (`insp:readonly-is-editable` 이 잡히므로 `BeginDisabled` 자체는 재고 있다).

- **D-78. 에디터 UI 계층(공용 위젯·로컬라이징·레이아웃)을 이식하지 않은 것은 누락이다.**
  화면을 띄워 놓고 사용자가 짚었다. 기능은 도는데 **기존 엔진이 깎아 놓은 UI 계층을
  통째로 건너뛰고** ImGui 원시 호출로 패널을 그리고 있었다. 규칙은 `ProjectRule.md` §11
  로 옮겼고, 여기에는 무엇이 있었는지와 실측을 적는다.
  **① 공용 위젯**: `Application/Editor/ImItem/` 에 20여 종 4,700줄이 있다.
  `ImListVirtual` 은 저장소를 모르는 목록이다 - 원소 접근을 콜백으로 받아 타입이 지워진
  리플렉션 `Array` 도 같은 UI 로 그리고, 추가·삭제·드래그 재정렬·읽기 전용이 그 안에 있다.
  **우리 `ArrayOps`/`TableOps` 가 비어 있는 것과 같은 자리다** - 목록 UI 를 새로 짤 이유가
  없었다. `ImTreeBegin` 은 행 사각형과 내용 사각형을 나눠 주어 행에 썸네일·배지를 얹게 한다.
  그 밖에 `ImAssetField`·`ImSearchBox`·`ImSectionHeader`·`ImStatusBadge`·`ImSplitter`·
  `ImValidationMessage`·`ImEnumCombo`·`ImDragScalar` 등이 있다.
  **② 로컬라이징**: `Loc::Text(key)` / `Loc::TextOr(key, fallback)`, 키는
  `EditorLocalizationKeys.h` 에 669줄. 기본 `ko-KR`, 폴백 `en-US`. 우리는 영어 리터럴을
  소스에 박아 두었다.
  **③ 레이아웃**: `ImGui::Utillity::FormLayout` 이 2열 표를 만들고 왼쪽 라벨, 오른쪽
  `SetNextItemWidth(-FLT_MIN)` 위젯을 둔다. **기존 인스펙터의 모든 위젯은 라벨을 `""` 로
  넘긴다** - 라벨은 표의 왼쪽 칸이 그린다. 우리는 위젯에 라벨을 넘겨 ImGui 가 오른쪽에
  붙이게 두었고, 좁은 패널에서 `orthographicSi…` 로 잘렸다.
  **한 값 한 줄**도 여기서 갈렸다. 기존은 잎사귀를 **타입으로 분기**해
  `Vec2`→`DragFloat2`, `Rect`→`DragFloat4`, `Color`→`ColorEdit4` 로 **한 줄**에 그린다.
  우리는 필드가 있으면 무조건 타고 내려가 색 하나가 네 줄을 먹었다. 우리 리플렉션에도
  이미 표시가 있다 - `TypeDescriptor::writeFieldsAsSequence`(`MakeVectorTypeDescriptor`)
  가 "같은 종류 값을 늘어놓은 구조체" 를 뜻한다.
  **닫기 단추는 확인해 보니 기억과 달랐다.** 기존 `CImWindow::HandleBegin` 은
  `IMWINDOW_FLAG_NO_CLOSE_BUTTON` 이 없으면 `&isVisible` 을 `ImGui::Begin` 에 넘긴다 -
  즉 **기본값은 X 가 있는 쪽**이고, 그 플래그를 세우는 곳은 `MainDockWindow` 하나뿐이다.
  그러니 기존에서도 도구 창에는 X 가 있었을 것이다. 다만 **창마다 고를 수 있어야 한다는
  것**은 맞고, 우리는 모든 패널에 일률적으로 달고 있었다. 사용자 확인이 필요한 자리다.

- **D-79. 에디터 공용 위젯은 `JBro::Widget` 네임스페이스에 옮긴다.** 이름의 `Im` 접두어는 뗀다.
  기존 엔진 `Application/Editor/ImItem/` 과 `Engine/Editor/ImGuiUtillity.h` 를 옮겼다.
  접두어를 떼는 것은 이 코드베이스의 규칙이고(`I` 와 `m_` 만 예외), 네임스페이스가
  그 일을 대신한다 - `Widget::List` 가 `ImList` 만큼 읽힌다.
  옮긴 것: `StyleScope`·`DisableScope`·`InvalidScope`·`IdScope`(스스로 되감는 스코프),
  `FormLayout`, `FieldLabel`·`SectionHeader`·`ValidationMessage`, `Tree`, `List`,
  `TextButton`·`IconButton`·`ActionButton`, `SearchBox`·`TextField`·`StatusBadge`·
  `Splitter`·`EnumCombo`·`DragInt`·`DragFloat`·`LoadingSpinner`·`CheckMark`.
  **그리는 규칙은 손대지 않았다.** 특히 `Tree` 는 기존이 `TreeNodeBehavior` 를 통째로
  다시 쓴 550줄이고, 눈으로 맞춰 깎은 값이라 이름만 바꿔 옮겼다.
  다른 점 셋. **`EnumCombo` 는 `magic_enum` 대신 리플렉션의 `enumNames` 를 쓴다** -
  같은 사실의 출처를 둘로 만들지 않는다. **아이콘 글꼴이 없어 글리프가 글자다**
  (FontAwesome 은 파일도 없다) - 글꼴이 생기면 넘기는 값만 바뀐다.
  **`Table` 을 다루는 `List` 덮개는 아직 없다** - 키를 받는 칸을 어떻게 그릴지가
  정해지지 않았고, 목록 위젯의 `drawAddRow` 자리가 그것을 위해 열려 있다.
  옮기지 못한 것: `ImAssetField`(에셋 시스템이 껍데기), `ImAudioBusField`·
  `ImAudioVisualizer`·`ImSpectrumVisualizer`(오디오가 없다), `ImPathField`·
  `BrowseFileButton`(파일 대화상자가 없다), `ImLayerHeader`(레이어 UI 가 아직 없다).

- **D-80. 화면에 나오는 글자는 키로 쓰고, 창의 정체는 번역하지 않는다.**
  `Loc::Text(key)` / `Loc::TextOr(key, fallback)`, 키는 `LocalizationKeys.h`,
  로케일은 `Localization/<로케일>.yaml`. 기본 `ko-KR`, 폴백 `en-US`(기존과 같다).
  **`Text` 는 못 찾으면 키를 내놓는다** - 빠진 자리가 화면에서 보여야 한다.
  `TextOr` 는 부른 쪽이 들고 있던 영어를 내놓고, 코드에 있는 것이 그것이라
  이쪽이 기본 쓰임새다.
  **패널 제목은 둘로 나뉜다.** `GetTitle` 은 번역하지 않는 안정된 이름이고
  `GetDisplayTitle` 이 보이는 이름이다. ImGui 는 창을 이름으로 식별하므로 제목을
  번역하면 언어를 바꾸는 순간 모든 창이 처음 보는 창이 되어 도킹 배치가 날아간다.
  창은 `보이는이름###안정된이름` 으로 연다 - `ImHashStr` 이 `###` 에서 해시를 다시
  세므로 앞쪽은 마음대로 바뀌어도 된다(기존 `CImWindow::GetImGuiLabel` 과 같은 수).
  **읽기에 실패하면 있던 표를 지우지 않는다.** 기존 엔진은 로케일과 폴백이 같은
  이름이고 그 파일이 없을 때 "폴백은 필요 없었으니 성공" 이 되어 빈 표를 깔았다 -
  화면의 모든 글자가 키로 바뀌는데 부르는 쪽은 참을 받는다. 테스트가 잡았다.

- **D-81. 인스펙터는 한 값을 한 줄에 그리고, 라벨은 위젯에 넘기지 않는다.**
  줄은 `Widget::FormLayout` 의 2열이다 - 왼쪽 라벨, 오른쪽 `SetNextItemWidth(-FLT_MIN)`.
  위젯에 라벨을 넘기면 ImGui 가 오른쪽에 붙이고 좁은 패널에서 **잘린다**
  (`orthographicSi…` 로 실제로 났다). 기존 인스펙터의 모든 위젯이 라벨을 `""` 로
  넘기는 것이 이 때문이다.
  **잎사귀가 전부 실수이고 넷 이하인 구조는 한 줄이다.** `Vec2`→`DragFloat2`,
  `Rect`→`DragFloat4`, `Color`→`ColorEdit4`(견본과 알파 막대). 필드가 있다고
  무조건 타고 내려가면 색 하나가 네 줄을 먹고, 사용자는 색을 고르는 대신 숫자를
  맞추게 된다. **주소를 모아서 넘긴다** - 실수 넷이 붙어 있다고 믿지 않는다.
  **한 줄에 안 담기는 구조는 같은 표 안에서 이어 그린다.** 값 칸에 표를 하나 더
  열면 안쪽 칸 폭이 바깥과 따로 놀고 이름이 두 번 나온다(그렇게 났다). 트리 마디를
  줄 전체에 걸치고 자식을 같은 표의 다음 줄로 낸다.
  보이는 컴포넌트 이름에서 `Component::` 접두어를 뗀다.

- **D-82. 컨테이너의 타입소거 조작을 채웠다.** 인터페이스만 있고 구현이 없었다.
  `ArrayOps` 는 `Array<T>`, `TableOps` 는 `Table<K,V>` 다. 표의 커서는 **원시 슬롯**을
  그대로 쓴다 - `Table` 에 이미 `IsSlotOccupied`/`KeyAt`/`ValueAt` 이 있었고 주석이
  "리플렉션의 타입소거 순회용" 이라고 적어 두었다. 순회 서수로 흉내내면 훑기가
  제곱이 된다.
  `RemoveAt` 은 뒤를 당긴다. 마지막 것을 끌어다 덮으면 순서가 깨지고, 순서가 있어서
  배열이다.
  `String` 에 설명자를 붙였다 - `Table<String, ...>` 의 키에 필요했고, 인스펙터의
  글자 칸에 실제로 닿는 타입이 그 전에는 하나도 없었다. 코덱의 `Assign` 이
  복사하는 것이 `ValueCodec::Assign` 이 존재하는 이유 그대로다.
  **배열 원소 편집은 아직 커맨드가 아니다.** 프로퍼티 길은 필드 번호의 나열이고
  원소 번호를 담으려면 길이 "필드인가 원소인가" 를 함께 들어야 한다 - 되살리기와
  직렬화가 같이 바뀐다. 늘리고 줄이는 것만 즉시 반영한다(D-71 의 예외).

- **D-83. 고른 것 전부에 편집이 미치고, 숫자는 델타로 간다.**
  기존 엔진 `CSetObjectTransformCommand` 를 따랐다 - 대상마다 시작값을 잡고
  델타만 누적하며, 되돌리기는 시작값으로 돌아간다.
  **숫자를 절대값으로 옮기면 안 된다.** 위치가 저마다 다른 셋을 골라 x 를 끌었을 때
  셋이 한 자리로 모이면 옮긴 것이 아니라 뭉갠 것이다. 델타가 없는 것(bool·enum·
  문자열)만 고른 값을 그대로 준다.
  델타는 **위젯의 쓰기를 되돌린 뒤에 잰다.** 되돌리기 전에 재면 위젯이 쓴 값
  자체가 델타가 된다.
  **묶는 쪽을 따로 두었다**(`CompoundCommand`). 기존은 대상 목록을 든 전용 커맨드였는데,
  기존에도 "단추 하나가 두 값을 바꾼다" 를 위한 커맨드가 따로 있었으므로 묶는 것을
  일반화하면 둘 다 덮는다. **전부 되거나 하나도 안 된다** - 중간에 실패하면 앞서
  적용된 것을 되돌린다(D-76 과 같은 이유).
  **`CanMerge` 를 커맨드 인터페이스에 더했다.** `TryMerge` 만으로는 묶음을 합칠 수
  없다 - 셋을 합친 뒤 넷째가 거절하면 절반만 합쳐진 상태이고 되돌릴 방법이 없다.
  먼저 전부 물어보고 전부 합친다.
  대상은 **(같은 타입, 같은 번째)** 로 고른다. 그 컴포넌트가 없는 오브젝트는 빠지고,
  조상이 함께 골라진 오브젝트도 빠진다(부모를 옮기면 자식은 따라 움직인다).
  뮤테이션 10개 전부 잡는다. 처음에 둘이 살아남았다 - **컴포넌트 번째를 무시해도**
  (대상마다 컴포넌트가 하나뿐인 테스트만 있었다), **되돌리기를 정순으로 해도**
  (서로 독립인 대상만 묶는 테스트뿐이었다) 아무 테스트도 울지 않았다. 둘 다 메웠다.

- **D-84. 계층에서 끌어 옮기면 부모·형제 자리·월드 위치가 한 커맨드로 함께 움직인다.**
  기존 `CMoveGameObjectInHierarchyCommand` 와 같은 자리다 - 셋은 드롭 한 번에 함께
  바뀌므로 커맨드를 나누면 되돌리기가 쪼개진다.
  **월드 자리를 지킨다**(기존의 WorldStay). 부모가 바뀌면 같은 로컬 값이 다른 월드
  자리를 뜻하게 되어, 안 고치면 놓는 순간 화면에서 튄다. 기존은 부모 월드 행렬을
  뒤집고 분해했는데, 우리 트랜스폼은 월드를 **이미 분해해서 들고 있어**(D-47)
  역행렬이 필요 없다 - 부모 월드 위치를 빼고, 역회전하고, 부모 크기로 나눈다.
  **런타임을 두 군데 손봤다.**
  `GameObject::SetParent` 가 옛 부모에서 뺄 때 `RemoveAllSwap` 이 아니라 `RemoveAll`
  을 쓴다. 마지막 것을 끌어다 덮으면 **부모를 바꾸는 것만으로 남은 형제들의 차례가
  흐트러진다** - 계층에 보이는 순서이고, 끌어 옮긴 것을 되돌려도 제자리로 오지 않는다.
  `SetChildIndex` / `FindChildIndex` 를 더했다. 기존 배열을 다시 늘어놓을 뿐 새
  상태를 만들지 않는다.
  **안 하는 것들**: 자기 밑으로 넣기는 아예 뜨지 않는다(거절당한 뒤 순서만 바뀌면
  반쯤 적용된 상태가 된다). 제자리 옮기기는 편집이 아니다. 월드 값이 아직 안 선
  트랜스폼과 크기 0 인 부모 아래에서는 짐작하지 않고 로컬을 그대로 둔다.
  **옮기는 것은 프레임이 끝난 뒤다.** 그리는 도중에 부모를 바꾸면 지금 순회 중인
  자식 배열이 그 자리에서 달라진다.
  **뿌리끼리는 차례를 못 바꾼다.** 뿌리의 순서는 캔버스 풀의 순회 순서이고 캔버스에
  순서라는 것이 없다. 주는 것은 UI 결정이 아니라 데이터 모델 결정이다 - 저장 파일의
  차례도 그것을 따라야 하므로, 사용자 확인 뒤에 한다.
  뮤테이션 10개 전부 잡는다. 처음에 둘이 살아남았고 **둘 다 테스트가 약해서였다.**
  형제가 셋일 때는 가운데를 빼면 밀어낸 것과 마지막을 끌어다 덮은 것의 결과가 같다 -
  넷이라야 갈린다. 그리고 옮길 자리를 늘 0 으로만 시험하면 "언제나 맨 앞에 꽂는"
  구현과 구분되지 않는다. 둘 다 고쳤다.

- **D-85. 되돌리기는 자리까지 되살린다. 프로퍼티 편집도 주소로 가리킨다.**
  배열 편집 설계(D-86)를 준비하다 결함 둘을 테스트로 재현했다.
  **① 프로퍼티 편집이 `SafePtr` 로 대상을 들고 있었다.** D-72 가 번호로 가리키게
  정했는데 `SetPropertyCommand` 만 남아 있었다. 고치고 → 지우고 → 삭제를 되돌리고 →
  고침을 되돌리면 옛 컴포넌트가 죽어 있어 **아무 일도 일어나지 않았다.** 이제
  `ComponentAddress`(오브젝트 번호, 타입, 같은 타입 중 몇 번째)를 들고 쓸 때마다 찾는다.
  주소는 프로퍼티 커맨드도 쓰므로 `ComponentAddress.h` 로 떼었다(컴포넌트 커맨드 →
  스냅샷 → 프로퍼티 커맨드로 include 가 돈다).
  **② 떼기를 되돌리면 컴포넌트가 맨 뒤에 붙었다.** 같은 타입이 둘이면 차례가 뒤바뀌고,
  그 전에 쌓인 편집의 "몇 번째" 가 **형제를 가리켜 값을 엉뚱한 컴포넌트에 썼다** -
  아무 일도 안 일어나는 것보다 나쁘다. 기존 엔진도 맨 뒤에 붙였지만 GUID 로 가리켜서
  드러나지 않았다. `GameObject::SetComponentIndex` / `FindComponentIndex` 를 더했고
  (D-84 의 `SetChildIndex` 와 같은 모양 - 밀어서 끼우고, 끝을 넘으면 끝), 떼기 커맨드가
  슬롯 번호를 적어 두었다가 되돌릴 때 그 자리로 보낸다. 스크립트 실행 순서는 슬롯이
  아니라 `InstanceId` 로 정렬하므로 바뀌지 않는다.
  기각: **`InstanceId` 를 스냅샷에 넣어 되살리고 그것으로 가리키기**(기존 엔진과 같은
  구조, 되돌린 오브젝트를 가리키던 `Ref<T>` 도 이어진다) - 캔버스에 "id 를 정해 붙이기"
  가 필요하고 D-72 의 번호 체계를 다시 정하는 일이라 이 결함 하나로 열지 않는다.
  **형제까지 떠서 전부 다시 붙이기** - 멀쩡한 형제가 새로 만들어져 `InstanceId`·주소가
  바뀌고 `OnDetached`/`OnAttached` 가 다시 돈다.
  뮤테이션 16개 중 14개를 잡았고, 남은 둘은 잴 수 없는 줄이라 지웠다. 커맨드 병합의
  레지스트리 비교(에디터 하나에 스택·레지스트리가 하나씩이다)와, 되돌린 뒤 "몇 번째" 를
  다시 세는 줄(되돌리기는 차례대로 오므로 같은 슬롯이면 같은 값이다). 처음에는 떼는
  것이 0번 슬롯인 테스트뿐이라 "언제나 0번에 꽂는" 구현을 가려내지 못해서, 앞에 다른
  타입을 두고 슬롯을 직접 확인하게 고쳤다.

- **D-86. 컨테이너 편집은 컨테이너 전체를 글자로 떠서 되돌린다.** D-82 의 "원소 번호를
  길에 담는다" 방향을 `Updates` 한다. **구현을 마쳤다(2026-09-15).**
  **길은 처음 만나는 컨테이너에서 멈추고, 그 아래는 컨테이너의 글자가 담는다.**
  `SetPropertyCommand` 의 잎사귀는 코덱을 가진 값이거나 컨테이너이고, 컨테이너의 글자는
  캔버스 파일과 같은 YAML 이다. 원소 값·추가·삭제·옮기기가 모두 "컨테이너 전체의 전과 후"
  다(기존 `CSetComponentSerializedPropertyCommand` 와 같다). 컨테이너 쓰기는 전부 되거나
  하나도 안 된다 - 못 읽으면 쓰기 전 글자로 돌려놓는다.
  **목록은 편집을 값이 아니라 연산으로 적는다**(`ListEdit`). 위젯이 원소에 쓴 값은 그 자리에서
  되돌리고 델타(실수·실수 묶음)나 고른 글자(bool·int·enum·문자열)로 적으며, 추가·삭제·
  옮기기는 적기만 한다. 다 그린 뒤 `MakeListEditCommand` 가 고른 대상마다 연산을 다시 적용해
  전후를 뜨고 한 묶음으로 실행한다(D-83). **연산이 다 맞지 않는 대상은 통째로 빠진다** -
  원소가 모자라 뒤 연산이 막혔는데 앞 연산만 남기면, 사용자가 한 적 없는 편집이 된다.
  **값을 YAML 로 쓰고 읽는 걸음은 한 곳이다**(`JBro/Reflection/ReflectedYaml.h`). 캔버스 파일에만
  있던 것을 JBroCore 로 옮겨 스냅샷·커맨드가 함께 쓴다. 배열은 시퀀스로, 표는 `Key`/`Value`
  맵의 시퀀스로 **키 글자 순으로** 적는다 - 슬롯 순서는 넣은 내력에 따라 달라서, 그대로 적으면
  내용이 같은 표가 다른 글자가 되고 되돌리기가 바뀌지 않은 것을 바뀌었다고 본다. 같은 키가
  두 번 나오면 실패다. 나열로 적는 원소(`Color`)와 배열 안의 배열은 기존 엔진 `.jcanvas` 가
  `Array<Vector2>` 를 적는 모양 그대로 대시만 있는 줄 아래에 적는다.
  기각: **길에 원소 번호 담기** - 길이 "필드인가 원소인가" 를 들어야 하고 깊이 4 가 모자라며,
  원소 추가·삭제·옮기기 커맨드 셋과 원소 스냅샷이 따로 필요하다. **주된 대상 하나에만 적용**
  (기존 엔진) - 여럿 고른 편집이 전부에 미친다는 D-83 과 어긋난다. 대가는 되돌리기 기록에
  배열 전체가 들어가고, 드래그 중 프레임마다 전체를 글자로 바꾸는 것이다.
  **고치며 드러난 결함 다섯**, 모두 테스트로 먼저 재현했다.
  (1) 스냅샷이 컨테이너를 **조용히 건너뛰어** 지웠다 되살린 배열이 비어 돌아왔다(D-76 위반).
  (2) 스냅샷이 잎사귀를 512바이트 고정 버퍼로 읽고 읽지 못한 값을 조용히 뺐다. 이제 크기
  제한이 없고, 뜨지 못한 값이 하나라도 있으면(구조체 안이라도) 캡처가 실패해 삭제·떼기가 거절된다.
  (3) `TableOpsOf` 가 키를 만드는 함수를 `CreateValue` 자리에 넣고 `CreateKey` 를 비워 두었다 -
  값을 만들라고 하면 키 크기의 객체가 나왔고, 테스트도 그 이름으로 키를 만들어 드러나지 않았다.
  (4) `YamlWriter::BeginSequence(nullptr)` 가 아무것도 적지 않고 돌아와 뒤따르는 닫기가 부모
  블록을 닫았다. (5) 인스펙터의 옮기기가 원소 주소를 받은 **뒤에** 배열을 늘려, 저장소가 옮겨
  가면 해제된 메모리에 썼다. 이제 늘린 뒤에 주소를 받고, 테스트는 용량을 원소 수에 딱 맞춘
  배열로 그 길을 밟는다.
  뮤테이션 30개(값 걸음 8·스냅샷 7·목록 편집 10·인스펙터 5)를 전부 잡는다. 처음에 일곱이
  살아남았다. 여섯은 테스트가 약해서 메웠고 - 같은 키가 두 번일 때 **이유**, 항목에 낀 다른 키,
  구조체 안에서 막힌 캡처, 반쯤 적용된 대상, 실수 묶음 원소 끌기, 도달한 값을 델타로 삼은 것
  (둘이 같은 만큼 틀리게 움직여도 통과했다) - 하나(삭제의 범위 검사)는 `RemoveAt` 이 이미
  보는 것이라 동치로 적지 않고 줄을 지웠다.
  **남은 것**: bool·int·enum 원소의 글자 옮기기와 목록 끌어 옮기기는 단위 테스트만 있고 마우스로
  재지 않았다. 필드를 가진 구조체 원소는 여전히 그리지도 옮기지도 못한다(코덱이 없어 `Assign`
  이 없다). 표 편집 UI 는 개수만 보인다. 스냅샷은 깊이 4 를 넘는 필드를 아직 조용히 건너뛴다
  (지금 닿는 컴포넌트는 없다).
  **(2026-09-15 추가) 깊이 4 를 넘는 필드는 이제 캡처를 실패시킨다.** 건너뛰면 떼기가 성공하고
  되돌린 컴포넌트에서 그 값만 기본값이 되는데, 캔버스 파일은 깊이 제한 없이 쓰므로 파일에 있는
  값이 되돌리기에서만 사라진다 - §11.5 "되살릴 수 없는 것은 하지 않는다" 그대로다. 넷째 칸
  잎사귀는 떼었다 되돌리면 돌아오고, 다섯째 칸이 있는 컴포넌트는 떼기가 거절된다
  (`TestRemovingIsRefusedWhenAValueIsTooDeepToAddress`). 뮤테이션 2/2(건너뛰기로 되돌림, 경계를
  한 칸 당김).
  **사용자 확인이 필요했던 자리**: `TableOpsOf` 의 키·값 만들기가 `new`/`delete` 를 직접 썼다.
  D-88 에서 풀었다.

- **D-87. 스크립트 편집기는 Code-OSS 포크 "JBro Script Editor" 이고 `.jscript` 만 다룬다.**
  계획·근거·단계는 [tasks/ide-plan.md](./ide-plan.md) 에 있다. **2026-09-15 에 P0 스파이크를 닫았고 P1(문법 강조
  확장)이 편집기 리포 `F:\Project\JBroScriptEditor` 에 섰다.** 포크 빌드는 아직 없다.
  D-60 의 "코드 에디터는 Code-OSS" 를 구체화한다. C++(빌트인 컴포넌트와 C++ 스크립트 경로)은
  Visual Studio 에서 편집하므로 C++ 언어 서비스는 넣지 않는다. C++ 스크립트 경로는 D-56 대로 남는다.
  **JBro 기능은 전부 내장 확장으로 만든다.** 코어 패치는 하되 제품 모양(기본 UI·배치·메뉴)에만
  쓰고 패치 하나에 바꾸는 것 하나를 이유와 함께 둔다. **무엇을 바꿀지는 D-102 에서 정했다(2026-09-16).**
  **확장 마켓플레이스는 연결하지 않는다.** 포크는 약관상 MS 마켓에 접속할 수 없고, 필요한 확장은
  편집기가 싣는다. ~~`product.json` 한 곳이라 되돌릴 수 있다.~~ **Code-OSS 는 `extensionsGallery`
  키를 아예 갖고 있지 않아 비울 것이 없다(D-102).** 열려면 그 키를 새로 적는다.
  **화면은 기본 한국어이고 로컬라이징을 전제로 한다**(D-80 과 같이 폴백은 영어). 원문은 영어로 쓰고
  한국어는 번역 파일이다. 본체는 내장 언어 팩, JBro 확장은 `package.nls`·`l10n` 번들, `jbroc` 진단은
  LSP 가 넘기는 `locale` 로 고른다. 마지막 것은 `jbroc` 설계에 들어가야 하는 요구 사항이다.
  **화면 언어 설정은 씬 에디터와 공유한다.** 둘 다 읽는 파일로 하며(D-60), 그 파일은 씬 에디터에
  사용자 설정 파일이 생길 때 정한다.
  **포크 빌드는 배포할 것이 생길 때 한다.** 그 전에는 확장을 일반 VS Code 에서 개발한다.
  편집기 리포는 엔진과 분리하고(`JBroScriptEditor`, CLI `jbro-script-editor`), 언어 지식은
  엔진 리포의 `jbroc --lsp` 에 둔다. 그래서 문법 강조 외의 모든 기능이 `jbroc` 을 기다린다.
  기각: 전체 브랜치 포크(매달 병합 충돌), 포크 없이 VSCodium + 확장팩(제품 정체가 없다),
  clangd 로 C++ 편집 지원(범위 밖).
  **스파이크에서 확인한 결함 하나(2026-09-15)**: `Field.h` 의 `__FUNCSIG__` 파싱이 clang 형식을 읽지 못해
  clang-cl 23.1.1 로는 `JBRO_FIELD` 를 쓰는 파일이 컴파일되지 않는다(MSVC 대조군은 통과).
  jbroscript-plan §18.7 의 `clang-cl -gdwarf` 디버깅 경로를 쓰기 전에 고쳐야 한다. 실측은 ide-plan §4.1.

- **D-88. 타입을 모르는 저장소는 부르는 쪽이 준 자리에 만들고 지운다.** D-86 에 남긴 확인 자리를
  푼다(`Updates` D-86).
  `TableOps` 의 키·값 만들기가 `new` 로 만든 객체의 소유를 `void*` 로 넘겼다. §14 는 `new`/`delete` 를
  막고 `MakeOwnerPtr` 를 가리키는데, **`OwnerPtr` 는 이 계층에 과하다** - 객체마다 제어 블록을 힙에
  따로 두고, 참조 수가 원자적이지 않아 메인 스레드 전용이며, JBroCore 의 컨테이너·리플렉션 같은
  바닥이 소유 도구를 끌어오면 의존 방향이 거꾸로 선다. 게다가 소유를 놓는 길이 없어 `void*` 로 넘길
  수 없고, POD 가 아니라 스크립트 DLL 과 나누는 함수 표에 담을 수도 없다.
  **`Array`·`Table` 이 이미 답이었다.** 둘은 `SafePtr.h` 를 끌어오지 않고, 할당기에서 받은 메모리에
  `std::construct_at`/`std::destroy_at` 으로 원소를 만들고 지운다. 조작 함수를 `ConstructKey(자리)`·
  `DestructKey`·`ConstructValue`·`DestructValue` 로 바꿨고, 자리는 부르는 쪽이 설명자의 `size`·
  `alignment` 로 할당기에서 받는다. 만들고 지우는 코드는 표를 등록한 모듈 안에 남으므로 경계를
  넘는 것은 여전히 함수 포인터와 `void*` 뿐이다. §14 에 이 경우를 규칙으로 적었다 - `new`/`delete`
  금지와 `MakeOwnerPtr` 는 객체의 소유권 이야기이고, 타입을 모르는 저장소는 할당기와 제자리 생성이다.
  기각: **예외로 두고 `new`/`delete` 유지** - 규칙에 구멍이 생기고 표를 읽을 때 항목마다 소유가
  날 포인터로 돈다. **`OwnerPtr` 를 돌려주는 조작 함수** - POD 경계에 어긋나고 `OwnerPtr<void>`
  를 만들 수 없다(`T& operator*` 가 `void&` 가 된다).

- **D-89. 필드를 가진 구조체 원소의 목록을 그리고 고치고 옮긴다.** (①~⑤ 완료, `Updates` D-86)
  D-86 이 남긴 "구조체 원소는 그리지도 옮기지도 못한다" 를 푼다. 기존 엔진에는 옮겨 올 화면이
  없다 - 필드 타입이 18값 닫힌 enum 이라 사용자 구조체 원소가 아예 없었다(§11.4).
  사용자가 고른 방향(2026-09-15): **옮기기는 원소 타입을 아는 `ArrayOps::Move`**, **원소 행은
  접기 마디이고 기본은 접힘**, **원소 안의 배열·표는 이번에 개수만 보여 주고 고치지 않는다.**
  단계: ① `ArrayOps::Move` ② `ListEdit` 에 원소 안의 필드 길 ③ 목록 위젯 행이 내용 높이를 따른다
  ④ 인스펙터가 그리기와 커밋을 나눠 원소 안에서도 같은 잎사귀 규칙을 쓴다 ⑤ 마우스 테스트·
  스크린샷·뮤테이션.
  **① 에서 진짜 결함이 나왔다.** 옮기기가 원소 코덱의 `Assign` 을 빌렸는데, 필드로 말하는
  타입(`Vec2`·`Color`)에는 코덱이 없다 - **`Vec2`·`Color` 목록을 끌어 놓으면 대상이 전부 빠지고
  아무 일도 없었다.** 옮기기 테스트가 `Array<float>` 뿐이라 드러나지 않았다. `ArrayOpsOf<T>` 가
  `std::rotate` 로 제자리에서 돌리게 했고, 끝에 임시 자리를 늘리던 수(늘기 전 주소가 죽던 위험)도
  함께 없어졌다. `ArrayOps` 가 48 에서 56 바이트가 되어 경계 단언의 잰 값을 고쳤다 - 지금은
  스크립트 DLL 이 리플렉션 표를 받지 않으므로 양쪽 레이아웃이 어긋날 곳은 없다.
  테스트는 수정 전에 `Color` 옮기기에서 실패했다. 조작 함수는 내용이 밖에 있는 `String` 원소로
  잰다(얕게 옮기면 두 자리가 한 버퍼를 든다). 뮤테이션 6/6 - 뒤로 옮길 때 한 칸 모자람, 앞으로
  옮기기가 아무것도 안 함, 끝을 넘는 목적지·출발지 받아들이기, 옮기지 않고 성공이라 말하기,
  출발지와 목적지 뒤바꾸기. 끌어 옮기기를 마우스로 재는 것은 ⑤ 에 남았다.
  **② 원소 안의 필드 길.** `ListEdit` 에 `fieldPath[4]`·`fieldDepth` 를 더했다. 적용은 원소의 필드를
  따라 잎사귀까지 내려가 숫자면 델타를, 아니면 글자를 쓴다. 안쪽 배열·표와 필드를 더 가진 구조체는
  코덱이 없어 거절되고, 원소나 길이 맞지 않는 대상은 지금처럼 커맨드에서 빠진다. 테스트는 종류가
  섞인 원소(실수·bool·색·안쪽 구조체·안쪽 배열)로 재고, 컴포넌트 두 개에 한 되돌리기로 닿는지와
  고치지 않은 필드가 목록 전체의 글자를 타고 온전히 돌아오는지를 본다. 수정 전에는 첫 델타에서
  실패했다. 뮤테이션 7/8 - 길 무시, 없는 필드 받아들이기, 잎사귀를 지나 내려가기, 타입·주소가
  원소에 머물기, 원소의 코덱·숫자를 읽기. **살아남은 하나는 길이 상한 검사다.** `fieldPath` 밖을
  읽지 않게 막는 줄인데, 이 레이아웃에서는 배열 바로 뒤가 `fieldDepth` 자체라 검사를 빼도 결과가
  같다 - 관찰할 수 없지만 메모리 안전 때문에 지우지 않는다.
  **④ 에서 정할 것.** 목록을 글자에서 읽으면 원소를 기본값으로 다시 만들고 글자에 있는 필드만
  채운다(`ReadArray`). 그래서 원소 안의 `NoSerialize` 필드는 고쳐도 글자가 같아 편집이 빠지고,
  목록을 편집하거나 되돌릴 때마다 기본값으로 돌아간다. 원소 안에서는 읽기 전용으로 그리는 것이
  맞아 보인다 - 저장하지 않는 값은 되살리지 않는다는 스냅샷의 전제와 같다.
  **③ 목록 행이 내용 높이를 따른다.** 목록 위젯이 행마다 그린 뒤 커서를 한 줄 높이의 배경 자리
  끝으로 되돌려, 여러 줄을 그리는 행 위에 다음 행이 겹쳤다. 되돌리는 줄을 지웠다 - 손잡이 줄이
  이미 한 줄 높이를 차지하므로 한 줄짜리 행은 전과 같은 자리에 온다. **에디터 스크린샷
  (`editor_list`·`editor_inspector`)을 고치기 전후로 찍어 한 바이트도 다르지 않음을 확인했고**, 그때
  잰 한 줄 행 사이 틈(4)을 테스트에 박았다 - 모든 행에 같은 만큼 틈이 늘면 "키 큰 행 뒤의 틈이
  짧은 행 뒤와 같다" 는 비교로는 드러나지 않는다. 배경과 끌기 자리는 여전히 첫 줄만 덮는다.
  뮤테이션 2/2(한 줄 높이로 되돌리기, 모든 행 1픽셀 키우기).
  **④ 를 시작하다 두 번째 진짜 결함이 나왔다: `Vec2`·`Color` 필드 편집이 커맨드를 거치지 않았다.**
  인스펙터는 커밋 전에 코덱으로 전 글자를 떴는데, 한 줄 숫자 묶음에는 코덱이 없어 뜨지 못했고
  뜨지 못하면 커밋을 건너뛰었다 - 위젯이 쓴 값이 그대로 남았다. `Transform2D.position`·`scale`,
  `SpriteRenderer2D.tint`·`pivot`·`size`, 콜라이더 `offset`·`size` 를 끌면 **되돌릴 수 없었고 여럿
  골라도 주된 것만 움직였다**(D-71·D-83 위반). 여럿 고른 편집 테스트가 실수(회전)만 끌어서
  드러나지 않았다. `SetPropertyCommand` 는 "가지는 잎사귀가 아니다" 로 `position` 을 거절하도록
  테스트로 박혀 있었다.
  사용자가 고른 방향(2026-09-15): **한 줄 숫자 묶음을 잎사귀로 받는다.** 글자는 컨테이너처럼 값
  전체의 YAML 이고(D-86 과 같은 걸음), 대상마다 커맨드 하나이며 길 깊이를 더 쓰지 않는다. 칸
  (`position.x`)으로 내려가는 길도 그대로 잎사귀다 - 스냅샷이 칸마다 뜬다. 가지의 예는 한 줄에
  담기지 않는 `world`(`Matrix3x2`, 실수 여섯)로 바꿨다. 인스펙터는 코덱 대신 커맨드의
  `ReadValue`/`ApplyValue` 로 뜨고 되돌린다. 기각: **실수 칸마다 커맨드** - 규칙은 그대로지만
  대상마다 커맨드가 2~4개이고, 드래그 병합이 칸 수에 묶이며, 구조체 안의 `Rect` 처럼 깊은 곳은
  길 깊이 4 를 넘는다.
  UI 테스트(`TestAVectorFieldEditsThroughACommand`)는 수정 전 "and leave one thing to undo" 에서
  실패했다. 뮤테이션 6/6 - 숫자 묶음을 잎사귀에서 빼기, 아무 구조체나 잎사귀로 받기, 통째 쓰기의
  되돌림 빼기, 커밋·스냅이 코덱으로 뜨기, 위젯이 쓴 값을 되돌리지 않기.
  **스크립트 타입과의 관계(사용자 메모, 2026-09-15)**: 스크립트는 엔진 타입만 쓰고 `Int`(64비트)·
  `Float`·`Vector2` 만 있다(jbroscript-syntax §7.1). 인스펙터는 위젯을 타입 이름(`"float"`·`"int32"`·
  `"JBro.Color"`)으로 고르므로, 스크립트 타입이 리플렉션으로 들어오면 그 이름 비교를 고쳐야 한다.
  엔진 `Vec2` 는 `Vector2` 로 바꾸기로 확정됐지만(jbroscript-syntax §12 의 2번) 이름 변경 작업은 따로
  남아 있어 여기서 바꾸지 않았다. 바꿀 때 인스펙터의 `"JBro.Color"` 같은 타입 이름 비교도 함께 본다.
  **④ 구조체 원소를 그린다(2026-09-15).** 목록 원소 하나를 필드와 같은 잎사귀 규칙(`DrawValue`)으로
  그리고, 필드를 가진 구조체면 접기 마디(기본은 접힘) 안에 필드마다 한 줄씩 그린다. 원소 안의 편집은
  컴포넌트 길 대신 원소 번호와 원소 안의 필드 길을 든 `ListEdit` 로 적혀(`Context::element`), 목록이 다
  그려진 뒤 고른 대상마다 한 되돌리기로 간다. 원소 안의 `NoSerialize` 필드는 잠그고(사용자 확인, 목록을
  글자로 되돌리므로), 원소 안의 배열·표는 개수만 보인다. enum 원소는 이제 글자 칸이 아니라 콤보다.
  **화면에서 고친 셋.** 처음 모양(값 칸 안의 목록 → 원소 안의 두 칸 표)을 1024 창에서 찍어 보니
  ① 값 칸이 몇 픽셀이라 `100` 이 `1` 로 보였고 ② 펼친 줄의 삭제 표시가 필드 표에 밀려 반쯤 잘렸고
  ③ 계층 패널용 `Widget::Tree` 가 줄 왼쪽부터 배경을 칠해 손잡이와 번호를 덮었다.
  ① 은 사용자가 고른 방향(2026-09-15)으로 풀었다: **구조체 원소의 목록은 컴포넌트 표를 끊고 줄 전체를
  쓰며, 라벨은 한 줄 위에 선다**(`FormLayout::Break`). ImGui 표에는 칸 합치기가 없어서 `FullRow` 로는
  첫 칸에 갇혔다(해 보고 확인했다). 다시 연 표는 같은 이름이라 ImGui 가 한 표의 인스턴스로 보고 칸 폭을
  함께 쓴다 - 처음에는 라벨 폭을 프레임 너머로 넘기는 저장소 코드를 썼는데, 끄고도 테스트가 통과해
  잴 수 없는 줄로 지웠다. 끊긴 뒤 항목의 Id 사슬에는 "##Instances" 와 인스턴스 번호가 낀다. 트리 마디가
  열린 자리(구조체 필드 안의 목록)에서는 끊으면 표가 마디의 Id 를 빼므로 값 칸에 둔다. 마디 안에서는
  들여쓰기를 필드 표에서 돌려받는다. ② 는 필드 표 폭을 행 내용 폭으로 주고(`FormLayout` 의 `width`),
  목록 위젯이 삭제 표시를 줄마다 같은 칸(내용 폭 끝)에 두게 해 풀었다. ③ 은 중첩 구조체 필드와 같은
  `ImGui::TreeNodeEx` 로 바꿔 풀었다. 기존 `editor_list`·`editor_inspector` 스크린샷은 고치기 전후로
  한 바이트도 다르지 않다.
  UI 테스트(`TestAStructElementOpensAndEditsEveryChosenList`)는 수정 전 "마디로 그려져야 한다" 에서
  실패했다. 원소 안 실수 끌기가 두 목록에 같은 델타 한 되돌리기, bool 이 두 목록에, 저장하지 않는 필드
  잠금, 원소 안 배열이 목록으로 그려지지 않음, 값 폭이 목록 폭의 4분의 1 이상(값 칸 안에서는 7%),
  삭제 표시가 같은 칸이고 값이 그 앞에서 멈춤, 끊긴 표 앞뒤 값 칸이 같은 x, 구조체 필드 안의 목록이
  단언 없이 그려짐을 본다. 뮤테이션 12/12 - 원소 범위 안 넘기기, 위젯이 쓴 값 남기기(숫자 묶음·잎사귀),
  저장하지 않는 필드 풀기, 안쪽 목록 그리기, 끊지 않기, 열린 마디 아래서 끊기, 표가 폭 다 쓰기,
  들여쓰기 남기기, 표 다시 열지 않기, 삭제 표시가 내용에 붙기, 타입만 보는 숫자 묶음 판정이 늘 거짓.
  **⑤ 마우스로 재었다(2026-09-15).** 테스트 둘을 더했다(`TestDraggingAStructElementReordersEveryChosenList`,
  `TestFlagCountAndToneElementsEditByMouseOnEveryChosenList`). 행의 손잡이(`##row_body`)를 잡아 행 사이의
  놓는 자리(`##slot`)에 두 축으로 끌면(`DragTo`) 고른 목록 전부에서 같은 원소가 같은 자리로 가고 한
  되돌리기다. 위로 한 번, 맨 아래로 한 번 끌어 **뒤로 갈 때 목표를 한 칸 당기는 보정**까지 재고, 바로
  아래 자리에 놓으면 되돌리기가 늘지 않는 것도 본다. 켜기 칸 누르기·정수 끌기·enum 콤보 고르기는 델타
  없는 값이라 주된 목록에서 고른 값이 다른 목록의 같은 자리에 그대로 간다(D-83) - 정수도 그렇다.
  `CollectNumbers` 가 실수만 숫자로 보므로 여럿 고른 정수 필드 편집도 같은 길이다. enum 은 콤보 칸
  번호가 아니라 이름 글자로 가며(값이 2·5·9 로 연속이 아닌 `Tone` 으로 재었다), 고르면 팝업이 닫힌다.
  컴포넌트 하나에 목록이 셋이라 몸통을 화면 위에서 아래로 세는 헬퍼(`FindListBodyAt`)와 한 칸짜리
  위젯을 여러 x 에서 찾는 헬퍼(`FindItemAnywhereInWindow`)를 더했다 - 처음에 한 x 로 짐작해 켜기 칸을
  빗맞혔고, 콤보 항목 Id 에 칸 번호가 끼는 것도 실측으로 알았다. 스크린샷 `editor_toggled_list` 를 찍어
  목록 셋이 손잡이·번호·값·삭제 표시 순으로 그려지는 것을 봤다.
  **위젯 무대에서도 끈다.** 인스펙터 테스트는 커맨드가 생기는지로 재므로, 제 바로 아래 자리에 놓는
  손짓에 위젯이 "바뀌었다" 고 답해도 커맨드가 값을 비교해 걸러 보이지 않았다(뮤테이션에서 살아남았다).
  그 답은 위젯의 계약이라 렌더러 없는 무대(`EditorWidgetTests`)에서 ImGui 입력 이벤트로 끌어 재는
  테스트(`TestDroppingARowMovesItOnceAndDroppingBelowItselfChangesNothing`)를 더했다 - 위로 놓기,
  바로 아래 놓기(콜백도 반환값도 없어야 한다), 맨 끝에 놓기(보정된 번호 2)를 본다.
  뮤테이션 8/8 - 놓는 자리 한 칸 어긋남, 뒤로 갈 때의 보정 빼기, 꾸러미에 다음 행 번호 담기, 바로 아래
  놓기 guard 빼기(위젯 테스트가 잡는다), 옮기기 편집의 출발·목적 뒤바꾸기, bool·enum 글자 안 뜨기,
  위젯이 쓴 값 안 되돌리기, enum 원소 커밋 빼기.
  **다른 세션과 같은 트리에서 재지 않는다.** 이번에 엔진 쪽 작업이 같은 트리에서 진행 중이라 그 미완성
  수정이 빌드를 깨거나 앞 절의 테스트를 실패시켰고, 뮤테이션 둘이 "build" 로 나와 결론이 나지 않았다.
  커밋된 HEAD 로 스크래치 워크트리를 만들어 거기서 빌드·테스트·뮤테이션을 돌렸다.
  **잔여 결함 둘을 고쳤다(2026-09-15).** ① **행 배경과 끌기 자리가 행이 그린 만큼 덮는다.** 행 높이는
  그려 봐야 알므로, 지난 프레임에 잰 내용 높이를 창 상태 저장소에 행 번호 아래 `##row_height` 로 두고 다음
  프레임의 `Selectable` 크기로 쓴다. 마디를 펼친 첫 프레임 한 번은 첫 줄만 덮이고 다음부터 맞는다. 내용
  끝으로 재고 배경으로 재지 않는다 - 배경으로 재면 한 번 늘어난 높이가 접은 뒤에도 줄지 않는다(테스트가
  먼저 잡았다: 처음 검사는 바깥 `GetFrameHeight()` 와 비교해 틀렸고, 목록은 줄 간격을 좁힌 제 스타일로
  그리므로 한 줄이 된 행이 실제로 그린 높이와 비교한다). 배경이 넘치는 것은 겹침 규칙 때문에 뒤 항목이
  가려 마우스로는 보이지 않아, 위젯 테스트가 저장값을 직접 본다. 뮤테이션 3/3(기억 안 함, 줄지 않음, 배경에
  안 씀). ② **펼침 상태가 원소를 따라간다.** 마디의 펼침과 행 높이는 ImGui 창 상태 저장소에 행 번호 아래의
  Id 로 쌓이므로, 옮기기·지우기 때 그 값을 함께 옮긴다(`Widget::CarryRowInt/Float`, `DropRowInt/Float`).
  위젯은 제 행 높이를 스스로 옮기고, 인스펙터는 마디 이름(타입 표시 이름)으로 펼침을 옮긴다 - 콜백이 목록
  몸통 안에서 불리므로 같은 창의 저장소다. 되돌리기는 UI 상태를 되살리지 않는다 - 옮긴 뒤 되돌리면 펼침은
  새 자리에 남는다. 테스트는 펼친 둘째를 첫째 위에 놓고 첫째의 마디가 열려 필드가 보이는지, 펼친 둘째 위의
  첫째를 지우면 새 첫째가 열려 있는지를 본다. 뮤테이션 4/4(옮길 때 안 옮김, 지울 때 안 옮김, 옮긴 값 잃음, 마지막 행 안 지움).
  **남은 것(그 뒤)**: 원소 안 `NoSerialize` 필드의 값은 목록을 편집하거나 되돌릴 때 기본값이 된다
  (스크린샷에서 `echo` 5 → 0). 인스펙터가 아직 `ImGui::` 를 직접
  부르는 자리가 많다(§11.1) - 이번에 더한 마디도 기존 중첩 구조체와 같은 직접 호출이다.
  기각: **원소 안은 라벨 위·값 아래로 쌓기** - 표를 끊지 않아 단순하지만 §11.3 두 칸 규칙의 예외가
  생기고 필드마다 두 줄이다. **두 칸 그대로** - 기본 폭에서 값이 몇 픽셀이다.
  기각: **구조체 설명자에 복사 함수** - "fields 와 codec 은 함께 있지 않다" 옆에 예외가 생긴다.
  **원소를 YAML 글자로 떠서 옮기기** - 새 함수는 없지만 느리고 실패할 길이 는다.

- **D-90. 작업마다 관련 문서를 갱신한다.** `ProjectRule.md` §1 에 MUST 로 넣었다.
  빡대리의 지시다(2026-09-15). 계기는 스크립트 편집기 작업에서 **대화로만 오간 것이 문서에 빠진 일**이다.
  엔진 API 시그니처를 스크립트 표기로 자동 변환하는 프로브 결과와, 편집기의 진행 현황·남은 일·막힌 곳이
  대화에서 보고만 되고 계획서에는 적히지 않았다. 둘은 이 결정과 함께 `jbroc-rules.md` §7.1 과 `ide-plan.md` §8 에 옮겼다.
  **규칙**: 코드·설계·조사·논의 어느 작업이든 끝났다고 보고하기 전에, 정하거나 새로 알게 된 것(결정, 실측·프로브 결과,
  진행 현황, 남은 일, 막힌 곳)을 계획서·Decisions·`ProjectRule.md` 중 맞는 곳에 적고 커밋한다. 사용자 확인을 기다리는 것도
  `[열림]`·`[대기]` 처럼 상태를 붙여 적는다. 대화에만 나오고 문서에 없는 결정·실측은 없는 것으로 본다.

- **D-91. 한국어 문구는 번역체로 쓰지 않는다.** `ProjectRule.md` §11.2 에 MUST 로 넣었다(`Updates` D-80).
  빡대리의 지시다(2026-09-15): `컴포넌트 붙이기`·`켜짐` 같은 말은 남에게 보이는 제품에 맞지 않고, 문구는
  **어디에 쓰이는지 확인하고** 옮겨야 한다. D-80 에서 키를 채울 때 영어를 낱말마다 옮겨 번역체가 됐다.
  `ko-KR.yaml` 44개 키를 코드에서 쓰는 자리마다 확인하고 다시 썼다. 바뀐 것과 그 자리:
  | 자리 | 전 | 후 |
  |---|---|---|
  | 창 제목·창 메뉴 | 게임 / 계층 | 게임 뷰 / 계층 구조 (→ D-161 에서 기존 이름 `레이어`) |
  | 파일 메뉴 항목 | 끝내기 | 종료 |
  | 편집 메뉴 항목 | 되돌리기 / 다시하기 | 실행 취소 / 다시 실행 |
  | 메뉴바 오른쪽 끝 표시 | 저장 안 됨 | 저장되지 않음 |
  | 계층 빈 상태 | 캔버스가 비어 있습니다 | 캔버스에 오브젝트가 없습니다 |
  | 계층 우클릭 메뉴 | 오브젝트 만들기 / 자식 만들기 / 지우기 | 오브젝트 추가 / 자식 오브젝트 추가 / 삭제 |
  | 계층 검색 칸 힌트, 검색 칸 기본 힌트 | 찾기 | 검색 |
  | 인스펙터 빈 상태 | 고른 것이 없습니다 | 선택한 오브젝트가 없습니다 |
  | 인스펙터 여럿 선택 표시 | %d개를 골랐습니다 | 오브젝트 %d개 선택됨 |
  | 컴포넌트 켜기 칸 라벨 | 켜짐 | 사용 |
  | 컴포넌트 추가 단추 / 머리 우클릭 메뉴 | 컴포넌트 붙이기 / 떼기 | 컴포넌트 추가 / 제거 |
  | 추가 팝업 빈 상태 | 등록된 컴포넌트 타입이 없습니다 | 추가할 수 있는 컴포넌트가 없습니다 |
  | 컴포넌트 본문 안내 | 이 타입은 프로퍼티를 등록하지 않았습니다 | 속성 정보가 등록되지 않은 컴포넌트입니다 |
  | 이름 모를 컴포넌트 머리 | (이름 없는 컴포넌트) | (알 수 없는 컴포넌트) |
  | 값 칸 대신 | (그릴 방법이 없는 타입) / (너무 깊어 고칠 수 없음) / (너무 길어 보일 수 없음) | (표시할 수 없는 타입) / (중첩이 깊어 편집할 수 없음) / (값이 길어 표시할 수 없음) |
  | 통계 줄 | 프레임 / 초당 %.0f 프레임 / 누적 프레임 / 버린 뷰 | 프레임 시간 / FPS %.0f / 총 프레임 수 / 그리지 못한 뷰 |
  | 목록 맨 아래 줄 / 삭제 표시 툴팁 | 원소 추가 / 원소 삭제 | 항목 추가 / 항목 삭제 |
  그대로 둔 것: 인스펙터, 통계, 파일·편집·창, 캔버스 저장, (이름 없음), 활성, 뷰·스프라이트 %u개, %d개,
  지우기(검색 칸 비우기 단추의 툴팁이라 맞다), 없음. 형식 지정자는 차례까지 그대로다(`TestTheShippedLocalesAgreeOnFormats`).
  화면에서 확인했다(1024 창 스크린샷: 계층 구조·검색·게임 뷰·오브젝트 2개 선택됨·활성·사용·항목 추가·저장되지 않음).
  기각: **기존 엔진 한국어 표를 그대로 가져오기** - 기존 표에도 `원소 추가`·`요소 추가` 가 섞여 있고 `다시 실행`
  옆에 `되돌리기` 를 쓴다. 용어의 출처로는 따르되 같은 번역체를 옮기지 않는다.
  **컴포넌트 이름과 필드 이름은 번역하지 않는다(사용자 결정, 2026-09-15).** 인스펙터 머리는 타입 이름
  (`Transform2D`), 필드 라벨은 필드 이름(`position`) 그대로다. 기존 엔진은 `editor.component.<타입>` 키
  (`트랜스폼 2D`)로 한국어화했지만 **새 엔진은 따르지 않는다** - 컴포넌트는 어디서나 타입 이름으로 일관되게
  보인다. 확인해 보니 이미 그렇다: 인스펙터 머리·컴포넌트 추가 팝업·목록 원소 마디 셋이 모두
  `DisplayTypeName`(접두어를 뗀 타입 이름)을 쓰고, 새 엔진에는 컴포넌트 이름 키가 하나도 없다. 그래서 코드는
  그대로 두고 규칙만 적었다(§11.2). 기각: **이름으로 키 짓기**(`editor.component.*`)와 **`Name()` 어트리뷰트에
  키 적기** - 코드·문서·화면의 이름이 갈린다.

- **D-92. 모달 팝업은 큐를 거치고, 콜백은 가상 함수다.** 기존 엔진 `ImPopupDesc` + `CImPopupWindow` 를
  옮겼다(2026-09-15). 남긴 것: 핸들(`PopupHandle`, 포인터 대신 번호 - D-72 와 같은 이유), 같은 Id 가 살아
  있으면 `OpenPopup` 이 그 핸들을 돌려주는 중복 방지, 한 번에 하나만 뜨고 앞이 닫힌 다음 프레임에 뒤가
  뜨는 FIFO(ImGui 모달은 스택이라 한 프레임에 하나만 정상이다). 바꾼 것: `std::function` 셋을
  `EditorPopup::OnEnter/OnDraw/OnExit` 가상 함수로(패널 D-70 과 같은 모양, 에디터에 `std::function` 을 쓰는
  자리가 없고 팝업이 들고 갈 상태는 파생 클래스 멤버가 자연스럽다). 창 이름은 `제목###popup_<핸들>` 이라
  제목이 바뀌어도 같은 창이다(D-80 과 같은 수). 뜨지 않은 채 닫힌 것은 훅을 받지 않고, UI 가 꺼지면
  큐를 비운다. `MessagePopup`(제목·글·확인 단추)이 첫 사용자다. 테스트는 두 팝업을 열어 하나만 뜨고,
  핸들로 닫으면 나가는 훅이 한 번 오고 다음 것이 뜨며, 닫기 단추가 없는 팝업은 X 를 그리지 않는 것을
  본다(`window->HasCloseButton`). 뮤테이션 6/7 - 살아남은 "스스로 닫은 팝업을 `CloseCurrentPopup` 으로
  ImGui 에 알리기" 는 다음 프레임에 `BeginPopupModal` 이 안 불리면 ImGui 가 스스로 닫아 결과가 같으므로
  **지웠다**(§12).

- **D-93. 캔버스 저장은 경로를 한 번 묻고, 그 뒤로는 같은 파일에 쓴다.** (2026-09-15)
  `IPlatform::ShowFileDialog(owner, FileDialogDesc, String&)` 를 더했다. 기본은 "없다"(거짓)이고 Windows 는
  기존 엔진 `ShowFileDialogEx` 를 옮긴 `IFileDialog`(COM) 구현이다 - COM 은 부르는 자리에서 켜고 끈다
  (게임 실행이 COM 을 들지 않게). `EngineInstance::GetMainWindow()` 가 주인 창이다. 에디터 쪽: 저장
  메뉴와 Ctrl+S(`ImGui::Shortcut`, 전역 라우트)가 `RequestSaveCanvas()` 로 플래그만 세우고, `Tick` 이
  UI 프레임을 닫은 뒤 엔진 프레임을 열기 전에 `PerformSaveRequest()` 로 처리한다 - **대화상자는 막히는
  호출이라 어느 프레임도 열려 있지 않은 자리여야 한다.** `LoadCanvas`·`SaveCanvas` 가 경로를 기억하고
  (`GetCanvasPath`), 프로젝트를 닫으면 잊는다. 저장이 되면 `MarkSaved`, 실패하면 `MessagePopup`(Id
  `save_failed` 라 연달아 실패해도 하나). 취소는 아무것도 남기지 않는다.
  **테스트 자리**: `EditorApplicationConfig::fileDialog` 함수 포인터가 널이 아니면 플랫폼 대신 그것을
  부른다 - 네이티브 대화상자는 사람 없이 닫히지 않는다. 그래서 **대화상자가 실제로 뜨고 경로를 돌려주는지는
  사람이 확인해야 한다**(§11.4) - 아직 하지 않았다. Ctrl+S 도 같다: `PostMessage` 로 넣은 키는
  `GetKeyState` 의 조합키 상태를 바꾸지 못해 테스트로 재지 못했다.
  뮤테이션 8/8(저장 뒤 경로 안 기억, 저장됨 표시 안 함, 요청 플래그 안 내림 - 처음엔 살아남아 요청 뒤
  프레임을 더 돌리는 단언을 더했다, 취소해도 저장, 실패를 조용히, 프로젝트 닫아도 경로 유지, 읽어도
  경로 안 기억, 열기 대화상자로 뜸).

- **D-94. 컴포넌트 슬롯 옮기기는 커맨드다.** (2026-09-15) `MoveComponentCommand(registry, objectId,
  from, to)` - 기존 `CReorderComponentCommand`. 슬롯 순서가 스크립트 실행 순서라(D-45, A3) 되돌릴 수
  있어야 한다. 슬롯 번호로 가리킨다 - 되돌리기는 차례대로만 오므로 `to` 자리의 것이 옮긴 그것이다.
  같은 자리·끝을 넘는 번호·모르는 오브젝트 번호는 거절한다. 인스펙터 컴포넌트 머리의 우클릭 메뉴에
  `위로 이동`·`아래로 이동` 이 붙었고 양 끝에서는 그쪽이 잠긴다. 여럿 골랐을 때는 주된 오브젝트의
  것만 옮긴다 - 컴포넌트 제거와 같다(머리는 주된 오브젝트의 슬롯을 그린다). 테스트는 커맨드
  단위(옮기기·되돌리기·다시하기·거절 셋)와 화면 단위(우클릭 → 메뉴 항목 → 순서 → 되돌리기, 첫 슬롯의
  `위로 이동` 잠김)다. 뮤테이션 5/5(되돌리기가 도로 안 옮김, 같은 자리 받아들임, 끝 넘는 번호 받아들임, 메뉴가 반대로 옮김, 첫 슬롯의 `위로 이동` 안 잠김).

- **D-95. 복사·붙여넣기는 에디터 안의 스냅샷 클립보드다.** (2026-09-15) 지우기 되돌리기(D-76)의
  평평한 나무 스냅샷을 `ObjectTreeSnapshot` 으로 꺼내 `DeleteObjectCommand` 와 새
  `PasteObjectsCommand` 가 같이 쓴다 - 다른 점은 옛 번호에 다시 거는지(`rebind`) 새 번호를 받는지뿐이다.
  붙여넣기의 첫 실행은 새 번호를 받아 항목에 적고, 다시 하기는 그 번호에 다시 건다 - 붙인 것을 골라
  고친 커맨드가 그 뒤에 쌓이기 때문이다. `CopySelection` 은 고른 것 중 맨 위 것들만 뜬다(자식은 그 안에
  있다). 하나라도 뜨지 못하면 클립보드를 건드리지 않는다. `PasteClipboard` 는 주된 선택의 **형제**로
  붙이고(고른 것이 없으면 뿌리), 붙인 뿌리들을 고른다. 부모가 지워졌으면 거절한다 - 뿌리에 슬쩍 붙이지
  않는다. 반쯤 붙다 막히면 붙은 것을 도로 지운다. Ctrl+C/Ctrl+V(글자 칸이 입력을 먹고 있을 때는 그 칸의
  것)와 계층 메뉴의 `복사`·`붙여넣기`(클립보드가 비면 잠김, 빈 자리의 붙여넣기는 뿌리)다. 프로젝트를
  닫으면 클립보드를 비운다 - 번호는 그 프로젝트의 것이다.
  기존 엔진과 다른 점: 기존은 YAML 글자를 **시스템 클립보드**에 두어 프로세스 사이에서도 붙었다. 지금
  캔버스 파일 쓰기는 캔버스 전체 단위라 부분 나무의 글자 왕복이 없어 에디터 안에서만 붙는다. 그 왕복이
  생기면(프리팹이든 부분 저장이든) 그때 글자 클립보드로 바꾼다. 기존의 "붙인 그룹 중심을 마우스 자리로"
  도 넣지 않았다 - 씬 뷰 안의 마우스 좌표가 아직 없다.
  뮤테이션 12/12 - 처음 11/12 였고 살아남은 "뜨지 못한 뿌리가 있어도 반쪽 클립보드를 채움" 은 프로퍼티를 등록하지 않은 컴포넌트를 든 오브젝트를 함께 골라 복사하는 단계를 더해 잡았다(새 번호 안 적음, 자식 안 뜸, 다시하기가 새 번호 받음, 사라진 부모를 뿌리로 대체, 원본이 그대로 선택, 늘 뿌리에 붙음 등).

- **D-96. 아이콘은 Font Awesome 글리프고, 인스펙터 잎사귀는 공용 위젯으로 그린다.** (2026-09-15)
  **아이콘.** 기존 엔진의 `Font Awesome 7 Free-Solid-900.otf` 를 `ThirdParty/FontAwesome/` 에 그대로 두었다
  (OFL 1.1, README 에 출처·라이선스). `EditorTheme::ApplyFont` 가 본문 글꼴(malgun, 없으면 기본 글꼴)에
  `MergeMode` 로 U+F000..F8FF 만 합친다. **파일이 있는지 `fopen` 으로 먼저 본다** - ImGui 는 없는 글꼴
  파일에 단언으로 죽는다(워크트리에 글꼴을 안 복사했을 때 테스트 전체가 abort 로 죽어 알았다). 없으면
  아이콘은 네모지만 에디터는 뜬다. 경로는 `EditorApplicationConfig::iconFontPath`(실행 폴더 기준, 로컬라이징
  표와 같다). 쓰는 글리프는 `EditorIcons.h` 에 실제로 쓰는 것만 둔다. 목록 위젯의 손잡이(`=`)와 삭제
  표시(`x`)를 `GripLines`·`Xmark` 로 바꿨고, 스크린샷 `editor_list` 에서 ≡ 와 ✕ 로 그려지는 것을 봤다.
  테스트는 글꼴이 합쳐졌는지(`HasIconFont`)와 두 글리프가 글꼴 안에 있는지(`IsGlyphInFont`)를 본다.
  삭제 표시의 Id 가 글리프 글자가 되어 테스트의 `"x"` 를 `Icons::Xmark` 로 바꿨다.
  **위젯 계층(§11.1).** 인스펙터의 값 잎사귀가 ImGui 원시 호출 뭉치였다. `Widget::Checkbox`·`ColorField`·
  `ScalarRunField`·`SliderFloat`·`SliderInt` 를 더하고, 실수·정수·bool·enum·글자·색·숫자 묶음을 전부
  `Widget::DragFloat/DragInt/EnumCombo/TextField` 와 그것들로 그린다. `DragFloat/DragInt` 는 **단추가 없으면
  Id 를 쌓지 않는다** - 칸 하나뿐이면 `id` 가 곧 그 칸이라 필드 Id(`##value`)를 세는 테스트가 그대로다.
  실수 형식은 위젯 기본 `%.2f` 가 되어 소수 자리가 하나 줄었다(기존 엔진 값이다, D-73).
  **잠재 결함 하나가 드러났다.** `ImGuiSliderFlags_AlwaysClamp` 는 `ClampZeroRange` 를 품어 min == max == 0 인
  끌기를 0 에 묶는다 - 범위 없는 필드를 위젯으로 그리자 회전 끌기 테스트가 "값이 움직이지 않는다" 로
  잡았다. 위젯이 범위가 있을 때만 붙잡게 고쳤다(`ClampFlags`). 위젯 테스트가 범위 없는 끌기를 재지 않아
  숨어 있던 것이다.
  **남은 직접 호출**: 접기 머리(`CollapsingHeader`), 우클릭 메뉴(`BeginPopupContextItem`·`MenuItem`), 구분선·
  회색 글자·들여쓰기, 목록 원소의 마디(`TreeNodeEx` - D-89 가 `Widget::Tree` 의 배경 칠 때문에 고른 것).
  §11.1 의 MUST 는 이것들을 뭉치로 보지 않는 해석이며, 확인이 필요하면 `[열림]` 으로 올린다.
  뮤테이션 5/5(글꼴 안 합침, 합쳐도 안 알림, 단추 없는 끌기가 Id 를 쌓음, 켜기 칸이 Id 무시, 슬라이더가 범위를 버림).
- **D-97. 에디터는 실행 인자로 프로젝트를 받고, 런처는 그 위에 선다.** (2026-09-16)
  **왜.** 유니티 허브처럼 프로젝트를 관리하고 골라서 여는 런처(C# / WinUI 3)를 만들기로 했다.
  런처와 에디터는 항상 같이 배포하므로 같은 리포에 두고, 둘은 **프로세스 경계로만 만난다** -
  런처가 `JBroEditorHost.exe` 를 인자와 함께 실행하고 종료 코드와 표준 출력으로 결과를 받는다.
  엔진을 런처 안에 싣지 않는다. 엔진이 죽어도 런처는 살아 있어야 한다. 계획은
  [launcher-plan.md](./launcher-plan.md) 에 있다.
  **인자 규약.** `--project <경로>` · `--framework 2d|3d` · `--content-root <경로>` ·
  `--frames <수>` · `-h`/`--help` 이고, 이름 없는 인자 하나는 프로젝트 경로다.
  **예전에 첫 인자를 프레임 수로 읽던 것을 버렸다** - 경로를 주는 것이 사람이 손으로 여는
  기본 동작이고, 숫자와 경로를 한 자리에서 구분하면 규약이 지저분해진다. 프레임 수는
  `--frames` 전용이다(사람 없이 돌리는 확인용).
  **종료 코드.** `0` 정상, `1` 초기화 실패, `2` 프로젝트 열기 실패, `3` 에디터 UI 실패,
  `64` 인자 오류. 런처가 이 값으로 사용자에게 무엇이 틀어졌는지 구분해 보여 준다.
  **차원은 인자로 받는다.** `.jproject` 에 2D/3D 를 적는 자리가 없기 때문이고, 이것은
  `OpenProjectFile` 이 이미 정한 것이다. 런처가 프로젝트마다 기억한다.
  **`--content-root`.** 로컬라이징 표와 아이콘 글꼴은 작업 폴더 기준이었다(D-96). 런처가
  작업 폴더를 옮기는 대신 이 값을 넘긴다. 주지 않으면 예전처럼 작업 폴더가 기준이다.
  **경로 인코딩 결함을 하나 잡았다.** 한국어 윈도우에서 `argv` 는 UTF-8 이 아니라 ANSI
  코드페이지(949)로 들어온다. 엔진이 `fopen_s` 로 여는 자리는 이것과 맞아떨어져 한글 경로도
  열리는데, **ImGui 는 받은 경로를 UTF-8 로 보고 넓은 문자로 바꾼다** - `--content-root` 에
  `C:\Users\박주형\...` 를 주자 아이콘 글꼴을 못 열고 단언으로 죽었다. `MergeIconFont` 가
  파일을 직접 읽어 `AddFontFromMemoryTTF` 로 넘기게 고쳤다(버퍼는 아틀라스가 물려받으므로
  `ImGui::MemAlloc` 으로 잡는다). D-96 의 `fopen` 미리보기는 그 읽기에 흡수됐다.
  **확인한 것.** `--help`·값 없는 옵션·모르는 차원·프로젝트 두 번 주기가 각각 종료 코드
  64 와 사유를 낸다. 작업 폴더를 `%TEMP%` 로 옮긴 채 한글이 섞인 프로젝트 경로와 한글이
  섞인 `--content-root` 로 45 프레임 띄우고 종료 코드 0 을 봤다. 없는 프로젝트 파일은
  `(line 0): cannot open the project file` 과 종료 코드 2 다. `JBroTests` Debug 전체 통과.
  **막힌 것(`[열림]`).** 스크립트 DLL 이 없는 프로젝트는 열리지 않는다
  (`EngineInstance::OpenProjectFile` 이 DLL 로드 실패를 프로젝트 열기 실패로 본다).
  런처의 "새 프로젝트 만들기" 가 이것에 막히므로 정책을 정해야 한다(launcher-plan §4 Q1).
  **→ D-98 이 정했다.**
- **D-98. 스크립트 DLL 을 싣지 못해도 프로젝트는 열린다.** (2026-09-16) (`Updates` D-97)
  **왜.** 아직 한 번도 빌드하지 않은 프로젝트에는 스크립트 DLL 이 없다. 그것을 빌드하는 곳이
  에디터인데 그 에디터가 열리지 않으면 새 프로젝트를 시작할 길이 없다. 런처의 "새 프로젝트
  만들기" 도 여기에 막혀 있었다.
  **무엇을 바꿨나.** `EngineInstance::OpenProject(framework, scriptModulePath)` 가 DLL 로드
  실패를 프로젝트 열기 실패로 보지 않는다. 대신 못 실었다는 사실이 남는다 -
  `IsScriptModuleLoaded()` 와 `GetScriptModuleError()` 이고, `EditorApplication` 이 그대로
  내보낸다. **조용히 열지는 않는다** - 호스트가 `note:` 와 `warning:` 두 줄을 내므로
  런처가 그대로 사용자에게 보여 줄 수 있다. 프로젝트를 닫으면 사유도 지워진다.
  스크립트를 애초에 가리키지 않는 프로젝트는 예전과 같이 열리고 사유도 비어 있다.
  **확인한 것.** 스크립트 DLL 이 없는 `.jproject` 를 30 프레임 띄우고 종료 코드 0 과 경고
  두 줄을 봤다. 테스트 두 개를 새 계약으로 고쳤다 - `TestAFailedOpenSaysWhy` 는
  `TestAProjectOpensWithoutItsScriptModule` 이 되어 열림·안 실림·사유·모듈 이름을 재고,
  `ScriptDLLLoaderTests` 의 호스트 배선 테스트는 실패한 모듈 뒤에도 프레임워크가 남아
  있는지와 닫으면 사유가 지워지는지를 잰다. Debug / Release 빌드와 테스트 전체 통과.
  **남은 일.** 에디터 화면에 이 경고를 띄우는 것은 아직 없다. 지금은 표준 출력뿐이다.
  로컬라이징 키가 필요하므로(§11.2) 런처 작업(L1)과 같이 한다.
- **D-99. `.jproject` 가 엔진 버전과 차원을 적고, 둘 다 없으면 프로젝트가 아니다.** (2026-09-16)
  (`Updates` D-97 · `Obsoletes` 기존 엔진 프로젝트 호환)
  **왜.** 런처가 다중 엔진 버전을 다루려면 "이 프로젝트를 어느 엔진으로 여는가" 를 알아야 하고,
  그 값이 런처 목록에만 있으면 프로젝트 폴더를 다른 기계로 옮기는 순간 사라진다. 유니티도
  같은 이유로 `ProjectSettings/ProjectVersion.txt` 를 프로젝트 안에 두고 허브가 그것을 읽는다.
  **우리는 파일을 나누지 않고 `.jproject` 안에 넣는다** - 런처는 이름과 해상도를 보여 주려고
  어차피 이 파일을 읽고, 파일을 둘로 두면 어긋날 자리만 하나 더 생긴다.
  **무엇을 바꿨나.** `EngineVersion`(문자열)과 `Framework`(`2D`/`3D`) 를 더했고 **둘 다 필수**다.
  없으면 줄 0 과 사유로 거절한다. 기본값을 두지 않는 이유는 3D 프로젝트가 조용히 2D 로 열리면
  화면이 비어 있는 채로 원인을 찾게 되기 때문이다. `FrameworkKind` 는 `JBro/Editor/
  EditorApplication.h` 에서 `JBro/Host/ProjectFile.h` 로 옮겼다 - 파일이 그 값을 적는 자리다.
  `EditorApplication::OpenProjectFile` 은 차원 인자를 버리고 파일에서 읽으며(열기 한 번에
  파일을 한 번 더 읽는다, 프레임 경로가 아니다), 호스트의 `--framework` 인자도 없앴다.
  **기존 엔진 프로젝트 호환을 접었다.** 옮겨 올 프로젝트가 없다는 것이 근거다(2026-09-16 확인).
  `ProjectFile.h` 의 "그 쪽 프로젝트를 그대로 열 수 있어야 한다" 문단을 고쳤다. `.jcanvas` 는
  그대로 기존 파일을 읽는다 - 이 결정은 `.jproject` 에만 적용된다.
  **확인한 것.** 이 기계에 있는 **기존 엔진의 실제 `.jproject`** 가 이제 거절되고, 그 사유가
  `EngineVersion` 을 가리키는 것을 봤다(형식 오류로 거절되는 것이 아니다). 키 하나씩 빠진
  경우·빈 엔진 버전·`Framework: 4D` 를 각각 거절하고 줄 번호를 낸다. `Framework: 3D` 프로젝트를
  에디터로 열어 `GetCanvas()` 가 3D 프레임워크에서 나오는 것까지 봤다 - `GetCanvas` 가
  `m_frameworkKind` 를 믿고 `static_cast` 하므로 여기가 어긋나면 조용히 틀리는 자리다.
  한글 경로 프로젝트를 40 프레임 띄우고 `(engine 0.1.0, 2D)` 를 봤다. Debug / Release 통과.

- **D-100. 런처는 `source/JBroLauncher`, 비패키지 자체 포함, 순수 로직은 따로 잰다.** (2026-09-16)
  **자리와 배포.** 엔진과 항상 같이 배포하므로 같은 리포에 둔다. .NET 10,
  `net10.0-windows10.0.19041.0`, Windows App SDK `1.8.260804001` **고정**(엔진 툴셋 고정과
  같은 이유다 — "최신" 으로 두면 기계마다 다른 것으로 빌드된다). 비패키지에 자체 포함이라
  사용자가 런타임을 따로 깔지 않는다.
  **엔진 설치 폴더의 모양.** `JBroEditorHost.vcxproj` 가 빌드 뒤 `Localization/` 과 아이콘
  글꼴을 실행 파일 옆으로 복사한다. 그전에는 리포의 `source/JBroEngine` 을 작업 폴더로 두고
  실행해야만 글자와 아이콘이 나왔다. 런처는 이 폴더를 등록하고 작업 폴더로 준다.
  **테스트.** `source/JBroLauncher.Tests` 는 `Model` 아래 소스를 **링크해서** 컴파일한다 -
  WinUI 를 참조하면 로직을 재는 데 창이 필요해진다. 엔진의 `JBroTests` 처럼 실행 파일 하나다.
  **확인한 것.** 빌드 경고 0 건, 창이 실제로 떴다(제목 `JBro Launcher`). 런처 테스트 통과.
  **뮤테이션 2/2.** ① `Build` 블록 확인을 빼면 다른 블록의 `ProductName` 이 제품 이름이 된다 -
  **처음에는 살아남았다.** 테스트가 그 블록을 파일 뒤에 두어서 나중 값이 이겨 우연히 같은
  답이 나왔다. `Build` 를 앞에 두도록 고쳐서 잡았다. ② 빈 엔진 버전을 `null` 검사로만 보면
  빈 문자열이 통과한다.
  **막힌 것(`[열림]`).** 엔진 설치가 자기 버전을 말하는 방법이 없다(launcher-plan §4 Q5).
  지금은 실행 파일의 제품 버전을 보고 없으면 폴더 이름을 쓰는데, 버전 리소스가 없어서
  폴더 이름(`Debug`)이 버전이 된다. **그래서 프로젝트와 매칭되지 않고, 여는 것이 막힌다.**

- **D-101. 엔진 버전은 `JBro.Common.props` 한 곳에 있고, 실행 파일의 버전 리소스가 그것을 말한다.** (2026-09-16)
  **왜.** `.jproject` 가 `EngineVersion` 을 적는데(D-99) 엔진 설치 쪽에는 그 값에 대응하는
  것이 없었다. 런처가 폴더 이름으로 짐작하던 동안에는 등록한 엔진이 `Debug` 버전이 되어
  **프로젝트를 여는 것 자체가 막혀 있었다.**
  **무엇을 했나.** `JBro.Common.props` 에 `JBroVersionMajor/Minor/Patch` 와 합친 `JBroVersion`
  을 두고(현재 `0.1.0`), `JBroEditorHost` 가 `Source/EditorHost.rc` 로 버전 리소스를 굽는다.
  숫자는 `ResourceCompile` 의 정의로 넘어가므로 **리소스 파일에는 버전이 적혀 있지 않다.**
  올릴 때는 props 의 세 줄만 바꾼다.
  **런처는 짐작하지 않는다.** `EngineVersionReader` 가 제품 버전을 읽고, 비어 있거나
  `0.0.0` 이면 등록을 거절한다. 짐작한 값이 프로젝트와 우연히 맞으면 엉뚱한 엔진으로 열게
  되고 그때 무엇이 잘못됐는지 알 길이 없다.
  **확인한 것.** 빌드한 `JBroEditorHost.exe` 가 `ProductVersion=0.1.0`·`ProductName=JBro Engine`
  을 말하고, 런처 테스트가 그 실행 파일에서 `0.1.0` 을 읽는 것을 봤다(리포에 빌드가 없으면
  건너뛰고 그 사실을 남긴다). 버전 리소스가 없는 실행 파일과 없는 경로는 각각 거절한다.
  **재지 못한 것.** `0.0.0` 을 버전으로 보지 않는 분기는 테스트가 없다 - 그 값을 가진
  실행 파일을 만들 방법이 마땅치 않다. rc 의 기본값이 `0,0,0` 이라 실제로 생길 수 있는 값이다.

- **D-102. 편집기 코어 패치는 넷이다 - 첫 실행 한국어, 채팅·AI 제거, `.vsix` 설치 차단, 메뉴 구조.** (2026-09-16)
  (`Updates` D-87)
  **왜 이제 정하나.** D-87 이 "코어 패치는 하되 무엇을 바꿀지는 미정" 으로 남겨 둔 질문이다.
  패치 목록이 빌드 스크립트의 입력이라 포크 빌드를 시작하기 전에 답이 있어야 한다. upstream
  `1.137.0` 소스를 `F:\Project\JBroScriptEditor\upstream` 에 받아서 읽고 정했다. 항목과 줄 번호,
  위험은 [ide-plan.md](./ide-plan.md) §5.2 의 패치 목록에 있다.
  **고른 기준은 "설정이나 확장으로는 닿지 않는가" 다.** 닿는 것은 패치하지 않는다. 그래서 셋을 뺐다.
  시작 화면(환영 탭)을 뜨지 않게 하는 일은 확장이 `workbench.startupEditor` 기본값을 기여하면 된다.
  **계정 메뉴는 깃 로그인을 연결하기로 해서 그대로 둔다** - 채팅을 없애도 Git·GitHub 확장이
  저장소에 접근하려면 이 메뉴가 로그인 창구다. **`inlineCompletions` 도 그대로 둔다** - 인라인 제안의
  엔진은 `editor/contrib` 에 있고, 확장이 제안을 내놓는 통로는 평범한 확장 API 라 로그인과 무관하다.
  `workbench/contrib` 쪽에 든 것은 상태 표시줄 항목과 설정 스키마뿐이라 지워서 얻는 것이 없고,
  나중에 `jbroc` 의 LSP 가 제안을 줄 때 그 표시가 쓸모 있다.
  확장 마켓플레이스는 Code-OSS 가 이미 `extensionsGallery` 키를 갖고
  있지 않아서 **비울 것이 없다** - D-87 의 "`product.json` 한 곳" 이라는 서술은 이 점에서 틀렸다.
  ~~**채팅·AI 제거는 설정으로 대신할 수 없다.** `chat.disableAIFeatures` 는 Copilot 확장을 끄는
  것이 중심인데 마켓이 없어 그 확장이 설치되지 않는다.~~ **틀렸다(2026-09-17, 아래).**
  `product.json` 은 기본 설정값을 바꾸지 못한다(`IProductConfiguration` 에 `configurationDefaults` 가 없다).
  `defaultChatAgent` 만 지우면 설정 논리는 죽지만 `chatSetupHidden` 기본값이 `false` 라 **UI 는 남는다.**
  ~~그대로 두면 로그인만 권하고 눌러도 되는 것이 없는 화면이 된다 - 그래서 지운다.~~
  ~~**오프라인으로 동작하는 것을 확인했다.**~~ `product.json` 에 `updateUrl`·`telemetryOptInStatusUrl`·
  `experimentsUrl`·`surveys` 가 없고, 채팅의 자격 확인은 로그인하지 않으면 네트워크 요청을 내지 않는다.
  **그러나 "오프라인으로 동작한다" 는 결론은 근거가 부족했다**(2026-09-17, 아래).
  **메뉴 구조(0004)는 재편하되 기본 레이아웃은 VS Code 와 비슷하게 유지한다.** 세부 항목은
  포크를 띄워 실제 화면을 보고 정한다 - 지금 목록으로 적어도 화면을 모르고 적는 것이 된다.
  **2026-09-17: 0001·0003 을 만들었고 0002 는 전제가 틀려 방식을 바꿨다.** 패치 파일과 적용 스크립트는
  편집기 리포 `patches/`·`scripts/apply-patches.mjs` 에 있고, 항목마다 무엇을 쟀는지는 ide-plan §5.2 에 있다.
  - **틀린 전제.** Copilot Chat(`GitHub.copilot-chat` 0.65.0)은 소스 트리의 **내장 확장**(`extensions/copilot`)이고,
    패치 없는 개발 실행에서 에이전트 호스트가 **로그인 없이 Copilot 클라이언트를 띄웠다.** "마켓이 없어 설치되지
    않는다" 와 "죽은 UI" 는 둘 다 틀렸다. 채팅·AI 를 없애기로 한 판단은 이것으로 약해지지 않는다 - 오히려 시작만 해도
    프로세스가 뜨므로 근거가 강해졌다. "오프라인으로 동작한다" 는 `product.json` 만 보고 내린 결론이었고, 에이전트
    호스트의 통신은 재지 않았다.
  - **0002 의 첫 방식(기여 import 삭제)은 버렸다.** 창은 떴지만 태스크 서비스·디버그 도구 모음·확장 기여·시작 화면
    실행기가 채팅 서비스에 기대고 있어 20개 넘는 기여가 만들어지지 않았고, 에이전트 호스트도 그대로 떴다. 태스크가
    죽으면 `jbroc` → MSBuild 빌드를 태스크로 돌릴 수 없다.
  - **0002 는 `chat.disableAIFeatures` 기본값을 `true` 로 바꾼다.** 에이전트 호스트를 켤지를 이 설정이 정하고, 내장
    Copilot 확장도 이 설정으로 꺼진다. 띄워서 에이전트 호스트가 뜨지 않고, 어떤 로그에도 Copilot 이 없고, 에러가
    없는 것을 봤다. 화면에 채팅 UI 와 Copilot 안내가 뜨지 않는 것은 빡대리가 봤다.
    **"없앤다" 는 "기본으로 끈다" 로 정했다(2026-09-17).** 사용자가 설정에서 다시 켤 수 있고, 그것을 막는 추가 패치는
    하지 않는다. 한 줄이라 upstream 을 올릴 때 부담이 거의 없다는 것이 이유다.
  - **0003 은 결정할 때보다 넓다.** 입구가 여덟이었고(API 명령 `workbench.extensions.installExtension`, Windows 기본
    확장 초기화, "Developer: Install Extension from Location..." 을 새로 찾았다) 모두 node 설치 서비스의 `install()`·
    `installFromLocation()` 에서 만나므로 거기서 거절한다. 폴더 설치도 같은 통로라 포함했다. 형식을 갖춘 `.vsix` 를
    CLI 로 넣어 거절되는 것을 봤고, 형식이 틀린 파일로는 CLI 가 먼저 zip 에러를 내서 잴 수 없었다.

- **D-103. 설치본은 `Launcher/` 와 `Editor/<버전 폴더>/` 이고, 런처가 알아서 찾는다.** (2026-09-16)
  (`Updates` D-100)
  **왜.** 런처가 엔진을 하나도 모른 채 시작해서, 사용자가 폴더를 등록해 줘야만 프로젝트를
  열 수 있었다. 같이 배포하는 물건이 서로를 모르는 것이 이상하다.
  **모양.**
  ```
  JBroEngine/
    Launcher/            런처 실행 파일
    Editor/
      v0.1.0/            JBroEditorHost.exe · Localization/ · ThirdParty/
      v0.1.1/
  ```
  런처는 자기 실행 파일 옆의 `..\Editor` 를 훑어 엔진을 찾는다. **목록을 그릴 때마다 다시
  훑는다** - 폴더를 지우거나 새로 넣은 것이 런처를 다시 켜지 않아도 보여야 한다.
  찾은 것은 저장하지 않으며, 저장하는 것은 다른 경로에 둔 엔진을 직접 등록한 경우뿐이다.
  같은 폴더가 양쪽에 있으면 찾은 쪽만 남긴다 - 같은 엔진이 두 줄이면 어느 것을 고른 것인지
  알 수 없다. 찾은 엔진은 목록에서 뺄 수 없다(빼도 다음에 다시 나타난다).
  **폴더 이름은 버전이 아니다.** 보여 주기용이고, 버전은 실행 파일의 버전 리소스가
  말한다(D-101). 폴더 이름을 믿으면 이름만 바꿔도 다른 엔진이 된다. 버전을 말하지 않는
  폴더는 목록에 올리지 않는다.
  **배포본이 죽던 결함을 잡았다.** `EnableMsixTooling=false` 로 두었더니 `dotnet publish`
  산출물에서 앱의 리소스 색인(`JBroLauncher.pri`)과 컴파일된 XAML(`.xbf`)이 빠지고, 그
  실행 파일이 창을 띄우기도 전에 `0xC000027B` 로 죽었다. **빌드 폴더에서는 그 파일들이 옆에
  있어서 멀쩡히 떴다** - 배포본을 실제로 세워 보지 않았으면 못 봤을 자리다. 비패키지로
  배포하더라도 이 스위치는 켜 둔다.
  **확인한 것.** 스크래치패드에 위 모양대로 설치본을 세우고(런처는 publish 산출물, 엔진은
  `Editor/v0.1.0`), 그 런처가 정상으로 떴다. 테스트는 가짜 설치본을 세워 ① 실행 파일이 있는
  폴더만 찾고 ② 버전이 폴더 이름이 아니라 실행 파일에서 오며 ③ 없는 루트에서 터지지 않는
  것을 잰다. **화면을 눌러 본 확인은 아직 없다** - 시작 메뉴에 없는 앱이라 화면 조작 권한을
  받지 못했다.

- **D-104. `jbroc` 은 라이브러리와 실행 파일로 나누고, 괄호 안의 줄바꿈은 문장을 끝내지 않는다.** (2026-09-17)
  (`Updates` jbroc-rules §2·§3.3, jbroscript-syntax §2)
  렉서·파서를 시작하기 전에 빡대리에게 넷을 물어 정했다.
  **모듈.** `JBroScriptCompiler`(정적 라이브러리, Tier E, `<JBro/ScriptCompiler/...>`)에 렉서·파서와 이후의
  타입체커·이미터를 두고, `JBroc`(실행 파일)은 명령줄만 맡는다. 에디터(`JBroEditor` + `JBroEditorHost`)와 같은
  모양이고, `JBroTests` 가 라이브러리를 직접 테스트할 수 있다. 실행 파일 하나에 모두 넣으면 테스트가 그 코드에
  닿으려고 소스를 따로 끌어와야 한다.
  **줄바꿈.** 문장 끝은 줄바꿈이지만 **`( )` 와 `[ ]` 안의 줄바꿈은 문장을 끝내지 않는다.** 긴 호출
  (`Raycast(from,⏎ down, 1.0, ref hit)`)을 나눠 쓸 수 있게 하기 위해서다. `{ }` 는 블록이라 해당하지 않는다.
  Python·Kotlin 과 같은 규칙이다. 버린 안은 "줄바꿈은 예외 없이 문장 끝" 이다(단순하지만 긴 호출을 나눌 수 없다).
  **진단 번역.** `jbroc` 전용 파일 `Localization/jbroc/<로케일>.yaml` 을 두고 JBroCore 의 `Yaml.h` 로 `jbroc` 이
  직접 읽는다. 파일 모양과 로케일 이름(`ko-KR`·`en-US`)은 에디터의 것과 같다. 에디터의 `LocalizationTable` 은
  `JBroEditor` 모듈 안에 있어 `jbroc` 이 기댈 수 없고, 그것을 공용 모듈로 내리는 리팩터링은 지금 할 이유가 없다.
  **[제안] 문법.** 문법 문서의 [제안] 항목(예약어 목록, `switch`/`case`, 생성자 모양, `is not null`, `for` 의 세 모양)은
  **제안대로 파싱한다.** 문법 강조도 이미 제안대로 칠하고 있어 둘이 같아진다. 제안이 바뀌면 파서를 고친다.
  **보충(2026-09-18, 빡대리 확인): 파서는 한 번에 읽고 선언과 식은 모양으로 가른다.** jbroc-rules §2 가 확정으로 적은
  "두 번 훑는다(타입 이름을 먼저 모은다)" 를 바꾼다. 스크립트 파일에서 모은 타입 이름으로는 엔진 타입(`Collision2D`)을
  알 수 없어 `Collision2D hit` 를 가르지 못하기 때문이다. 이 언어의 식에는 이름 두 개가 나란히 오는 모양이 없으므로
  "타입 + 이름" 으로 시작하는 문장은 선언이다. 모든 파일의 타입 이름을 모으는 일은 타입체커의 이름 해석으로 옮긴다.
  파일 하나만으로 파싱되므로 편집기(`jbroc --lsp`)에도 맞다. 구현과 검증은 jbroc-rules §11.2.

- **D-105. `JBroc` 은 진단을 MSVC 모양으로 내고, 번호는 코드마다 고정한다.** (2026-09-18)
  `jbroc` 의 명령줄 실행 파일(`Modules/JBroc`, D-104)의 계약이다. 편집기의 빌드 태스크와 빌드 스크립트가 이것에 기댄다.
  **출력.** 진단 한 줄은 `<절대 경로>(<줄>,<열>): error JBC<번호>: <메시지>` 이고 표준 출력으로 간다. VS Code 의
  `$msCompile` 매처가 그대로 읽는 모양이다. 그 매처는 경로를 절대 경로로만 받으므로(upstream `problemMatcher.ts` 의
  `fileLocation: Absolute`) 상대 경로를 받아도 절대 경로로 바꿔 낸다. 명령줄과 파일의 문제는 표준 에러로 간다.
  **번호.** 렉서는 1000 번대, 파서는 2000 번대이고 **코드마다 적어 고정한다.** 열거형 순서에서 뽑으면 코드를 하나 끼워
  넣을 때 이미 내보낸 번호가 밀린다.
  **종료 코드.** 0 에러 없음, 1 소스에 에러, 2 명령줄이나 파일 문제(1 보다 우선), 3 `JBroc` 자신의 결함.
  **언어.** 기본 `ko-KR`, 폴백 `en-US`, `--locale` 로 고른다. 번역 표는 실행 파일 옆 `Localization\jbroc` 에서 읽는다.
  표가 없으면 메시지를 키로 내되 종료 코드는 그대로다.
  **사람 없이 부르는 도구다.** 단언은 대화상자가 아니라 표준 에러로 가고, 잡히지 않은 예외도 알리고 끝난다(D-69 와 같은 이유).
  구현 세부와 검증, 만들며 잡은 결함 둘(Debug 단언 대화상자로 멈춤, 한글 경로를 코드 페이지 949 로 읽어 난 예외)은 jbroc-rules §11.3.
  **뮤테이션(2026-09-18).** 변이 16개 중 15개를 테스트가 잡았다. 남은 하나(`lexically_normal` 을 뺌)는 MSVC 의 `absolute` 가
  `GetFullPathNameW` 로 이미 경로를 정리해서 생긴 동치라, 테스트를 지어내지 않고 그 호출을 지웠다(jbroc-rules §11.3).

- **D-106. 3D 는 2D 와 같은 뼈대로 세우고, 월드 캐시는 분해된 값이며, 깊이는 메시가 있는 뷰에만 단다.** (2026-09-18)
  사용자 지시("3D 프레임워크 검토 및 2D 와 맞물리게 병렬 구현")로 시작했고 계획과 상태는
  `tasks/framework3d-plan.md` 에 있다. 이 결정은 1단계에서 정한 것이다.
  - **컴포넌트**: `Transform3D` 에 월드 캐시(`worldPosition`·`worldRotation`·`worldScale`·`worldValid`,
    `NoSerialize | ReadOnly | Category("World cache")`). **행렬이 아니라 분해된 값이다** - `Matrix4x4` 는
    `JBroGraphics` 소유(§10.1)라 컴포넌트 라이브러리가 들 수 없고, 스크립트 프렐류드에 렌더러 타입이 새면
    안 된다(§5). 대가는 비균등 스케일 아래 회전의 전단이 자식에게 안 가는 것(Unity 와 같다). `Camera3D` 는
    2D 와 같은 이름(`projection`·`orthographicSize`·`nearPlane`·`farPlane`·`clearColor`·`primary`) +
    `verticalFieldOfView`. `MeshRenderer3D` 에 `tint`·`visible`.
  - **수학**: `Math3D.h` 는 hot-path inline 만(벡터·사원수). 행렬 함수는 `Framework3DSystem/Math3DMatrix.h`.
    규약은 렌더러와 같다 - 열 벡터, `values[row*4+col]`, 오른손, 카메라는 -Z, 깊이 0..1. 사원수 곱은
    해밀턴이고 `Rotate(Multiply(a, b), v) == Rotate(a, Rotate(b, v))`. 오일러는 Z→X→Y(Unity 순서).
  - **시스템**: `Transform3DSystem`(100)·`Camera3DSystem`(300)·`MeshRender3DSystem`(400), `RenderWorld3D`,
    `RenderBridge3D`. 2D 와 실행 순서 번호까지 같다. 부모 트랜스폼이 꺼져 있으면 자식은 뿌리가 되지 않는다
    (A4 의 함정을 처음부터 피했다).
  - **메시 자원**: `Renderer::RegisterMesh(vertices, indices) -> AssetHandle`, `UnregisterMesh`.
    핸들 모양이 `AssetHandle` 인 것은 `MeshSubmit` 이 그 타입이기 때문이고 발급자가 렌더러라는 것은
    `MeshLibrary`(Framework3DSystem, `AssetId → AssetHandle`)만 안다. 빌트인 정육면체가 `builtin/cube`
    (`MakeStableTypeId` 해시) 로 팔린다. `[가정]` `AssetSystem` 이 실제로 로드하게 되면 그쪽으로 옮긴다.
    `MeshRender3DSystem` 이 빈 핸들을 `meshId` 로 매 프레임 해석해 컴포넌트에 써 둔다.
  - **렌더러**: 메시 파이프라인(인스턴스 = 월드 4x4 행 넷 + tint, 푸시 상수 = 뷰·투영, 램버트 하나 + 앰비언트,
    깊이 `LESS_EQUAL` 쓰기, 뒷면 컬링, 화면에서 시계 방향이 앞면 - 처음 "CCW 가 앞면" 이라 적고 정육면체를 그렇게
    감았던 것은 틀렸고 D-108 의 뮤테이션이 잡아 고쳤다). 셰이더 `BuiltinMesh.hlsl` 을 `Compile.ps1` 에 더했다.
    **깊이 텍스처는 `BeginFrame` 이 타깃 크기로 미리 확보한다**(백버퍼용·프레임 타깃용 둘) - 디바이스가
    프레임 안에서 자원을 만들지 않기 때문이다. **깊이 첨부는 메시가 있는 뷰에만 단다** - 스프라이트만
    있는 2D 프레임은 전과 같은 패스다. `[가정]` 한 뷰에 스프라이트와 메시가 함께 오면 깊이가 붙은 패스에
    깊이 없는 스프라이트 PSO 가 그려진다. 2D·3D 프로젝트가 배타적이라 그 뷰는 지금 없다.
  - **D3D12**: `BeginRenderPass` 가 깊이 첨부를 받는다(`ResolveDepthStencil`, `DEPTH_WRITE` 전이, 네이티브
    렌더 패스의 깊이 설명자, 비네이티브 경로의 `ClearDepthStencilView`). D32Float 라 스텐실은 `NO_ACCESS`.
  - **검증**: framework3d-plan §2.6. 뮤테이션 MUT3D_RESULT.
  - **없는 것**: 물리 3D 시스템, 재질 해석, 3D 스크립트 서비스, 씬 파일의 3D 저장 확인. 뒤의 것은 2D 와 같은
    `ReflectedYaml` 길이라 될 것이지만 재지 않았다 - `[열림]`.

- **D-107. D3D11 백엔드는 즉시 컨텍스트로 같은 RHI 계약을 채우고, 셰이더는 API 마다 다른 바이트코드를 받는다.**
  (2026-09-18, framework3d-plan 2단계) `JBroD3D11RHI` 모듈. 슬롯·세대 핸들은 D3D12 와 같은 모양이고 프레임
  슬롯은 하나, 펜스·디스크립터 힙·은퇴 펜스는 없다(참조 계수가 수명을 지킨다). 백버퍼는 제시 직전에 사본을 떠서
  되읽는다(플립 모델). 푸시 상수는 파이프라인마다 b0 상수 버퍼 하나. `GraphicsApi::D3D11` 은 enum 끝에 더해
  저장된 숫자가 밀리지 않는다. **바이트코드**: 같은 HLSL 을 dxc(DXIL)와 fxc(SM 5.0 DXBC)로 함께 굽고 렌더러와
  에디터 UI 가 `GraphicsApi` 로 고른다(`PickShader`). fxc 헤더의 `BYTE` 는 `JBro::Sm5` 네임스페이스 안에서만
  준다. `.hlsl` 은 ASCII 로 쓴다(fxc 가 BOM 을, dxc 가 BOM 없는 한글을 거절한다 - §14 BOM 규칙의 예외).
  에디터와 게임 호스트는 `graphicsApi` 설정으로 모듈을 고른다. 테스트: 스프라이트·메시 픽셀 테스트를 두
  백엔드에 템플릿으로 돌리고 D3D11 스모크(클리어·제시·크기 바꾸기·되읽기·프레임 안 자원 거절·검증 0)를 더했다.
  **함정 둘을 문서에 남겼다**(framework3d-plan §2.7): 플립 모델의 되읽기, 그리고 `%TEMP%` 아래 워크트리에서
  MSBuild 가 헤더 의존을 기록하지 않아 헤더 뮤테이션이 헛도는 것 - 워크트리를 `F:\AI\wt` 로 옮겼다.
  뮤테이션 10/13(`F:\AI\wt` 로 옮긴 뒤 헤더 변이 둘도 죽었다). 살아남은 것: `Present` 의 VSync 인자(관측할
  손잡이 없음), 알파 블렌드 끄기(픽셀 테스트가 반투명을 그리지 않는다 - `[열림]`), 그리고 "프레임 안 텍스처 생성"
  변이는 패턴이 안 맞아 적용되지 않았다(스모크 테스트가 그 경로를 직접 본다).
- **D-108. Vulkan 백엔드는 1.3 동적 렌더링으로 같은 RHI 계약을 채우고, 셰이더는 SPIR-V 를 세 번째 바이트코드로 받는다.**
  (2026-09-18, framework3d-plan 3단계) `JBroVulkanRHI` 모듈. 슬롯·세대 핸들은 D3D12 와 같은 모양이고 프레임
  슬롯은 스왑체인의 `maxFramesInFlight`(상한 3)다. 슬롯마다 명령 버퍼·펜스·획득 세마포어·디스크립터 풀이 있고,
  파기는 은퇴 목록에 넣어 슬롯 펜스가 끝난 뒤 지운다. `vulkan-1.dll` 은 실행 시간에 열어 실행 파일이 SDK 를 링크하지
  않는다. 좌표는 높이 음수 뷰포트로 D3D 와 맞추고 앞면은 `CLOCKWISE` 다. 스왑체인 이미지는 표면 크기로 만들되
  계약의 크기는 요청값으로 남긴다. 되읽기는 제시 직전 사본(D-107 과 같다). 검증 오류는 `VK_EXT_debug_utils` 의
  ERROR 메시지 수다. **바이트코드**: 세 `Compile.ps1` 이 Vulkan SDK 의 dxc 로 `*_SPV.generated.h` 를 더 굽고
  (`-fvk-t-shift 8 0 -fvk-s-shift 16 0`), 푸시 상수 b0 은 `JBRO_SPIRV` 매크로로 두 표기를 나눈다 - DXIL·DXBC 는
  바이트 하나도 바뀌지 않았다. `PickShader` 와 `EditorUI::Initialize` 가 `GraphicsApi::Vulkan` 에 SPIR-V 를 준다.
  에디터·게임 호스트는 `graphicsApi` 설정으로 세 모듈 중 하나를 고른다. 테스트: 스프라이트·메시 픽셀과 텍스처
  바인딩(고의 실수는 Vulkan 에서 너비 0 뷰포트)과 에디터 화면 테스트를 Vulkan 에도 돌리고 Vulkan 스모크를 더했다.
  실측·가정은 framework3d-plan §2.8 에 있다. Vulkan 헤더는 `ThirdParty/Vulkan-Headers`(SDK 1.4.350.0, Apache-2.0)에
  넣어 클론이 SDK 없이 빌드된다. 뮤테이션 14/15 - 살아남은 것은 미룬 파기를 바로 지우는 변이(관측 불가, `[열림]`).
  살아남았던 앞면 규약 변이가 **1단계의 정육면체 감김 결함**을 드러냈다(바깥 면이 컬링되고 있었다, §2.8) - 시계 방향으로
  고쳤고, 반투명 스프라이트 픽셀 테스트를 더해 알파 블렌드 변이도 세 백엔드에서 죽였다.
- **D-109. 트랜스폼 기즈모는 카메라 행렬만 아는 수학 위에 서고, 끌기 하나는 커맨드 하나다.**
  (2026-09-18, framework3d-plan 4단계) `GizmoModel`(JBroEditor 공개 헤더, ImGui 를 모름)이 뷰-투영과 화면 사각형으로
  손잡이 픽셀 좌표·집기·끌기를 낸다. 이동은 축 직선과 마우스 광선의 최근접점, 자유 이동은 화면 평면 교점, 회전은
  화면 각도(부호는 축을 조금 돌려 본 투영으로 잰다), 크기는 축 위치의 비율. `Widget::Gizmo` 가 ImGui 로 그리고 끌며
  손잡이마다 hovered/active Id 를 둔다. `GameViewPanel` 이 렌더러의 지난 프레임 카메라(`GetLastViewCamera`)로
  게임 그림 위에 얹고, 끌기를 `GizmoEditing` 이 편집으로 옮긴다: 끄는 동안 필드에 직접 쓰고 놓으면 편집 전 값으로
  되돌린 뒤 `SetPropertyCommand` 묶음 하나를 실행한다(인스펙터 드래그와 같은 길, §11.3). 대상은 맨 위 선택들이고
  주된 것의 월드 델타를 각자의 부모 좌표계로 돌린다. 3D(`Transform3D`)와 2D(`Transform2D`, `planar`)가 같은 뼈대다.
  모드는 단추 줄(로컬라이징 키 `gizmo.*`)과 W·E·R. 테스트: 수학 테스트와 에디터 끌기 테스트(이동·되돌리기·회전·크기).
  실측·가정·열린 것은 framework3d-plan §2.9·§3. 뮤테이션 9/11 - 살아남은 둘은 동치 변이(§2.9).
- **D-110. 렌더러 성능은 벤치마크로 재고, 메시는 종류마다 드로우 하나며, 백엔드는 프레임 안에서 디스크립터를 다시 쓴다.**
  (2026-09-18) `JBRO_BENCH=1` 로 `JBroTests.exe` 를 실행하면 테스트 대신 벤치마크가 돈다(`Tests/RendererBenchmark.cpp`,
  세 백엔드 같은 장면, Release 로 잰다). 그것으로 찾아 고친 셋: (1) 업로드가 뷰 안에서 메시를 슬롯별로 모아
  (계수 정렬) 종류마다 드로우 하나(`MeshRun`) - 16000 개 정육면체가 드로우 하나, 프레임 0.95 → 0.43ms(D3D12);
  (2) D3D12 가 드로우마다 디스크립터를 복사해 512 개째에서 나머지 드로우를 조용히 버리던 것 - 프레임 안에서
  스테이징한 테이블 16 개를 기억해 다시 걸고 텍스처 몫을 4096 으로, Vulkan 도 set 16 개를 같은 식으로; (3) 인스턴스
  배열을 항목마다 `Add` 하던 0.3ms - 한 번에 크기를 잡고 자리에 쓴다. 전후 표와 계측·되돌린 시도는
  framework3d-plan §2.10. **렌더러·백엔드의 성능 변경은 이 벤치마크의 전후 숫자를 문서에 남긴다.**
  이어진 그래픽스 리뷰(§2.11)에서 스프라이트가 깊이 패스 위에 있을 때의 파이프라인 포맷 위반, D3D12 되읽기의 펜스
  값 충돌과 `AbortFrame` 상태 어긋남, D3D11 의 느린 상수 버퍼 길과 일부 쓰기의 0 채움 등을 고쳤다. RHI 파이프라인에
  `depthTest`·`depthWrite` 가 생겼다.
- **D-111. 에셋 시스템은 `tasks/asset-plan.md` 의 설계로 세운다 - 이진 UUID, 타입별 풀, GPU 는 프레임워크가 든다.**
  (2026-09-18) 기존 엔진의 에셋 시스템과 그쪽 후속 문서를 읽고 다시 설계했다(asset-plan §1). 사용자가 확정한 것:
  (1) 128 비트 식별자는 JBroCore 의 `Uuid { uint64 high; uint64 low; }` 하나이고 `AssetId` 는 그 별칭이다
  (`using`, 별개 타입 없음 - 아이디 역할에 더 가질 것이 없다). `Generate` 는 버전 4 난수, `FromName` 은 버전 8 이름
  해시라 둘이 겹치지 않는다. 텍스트는 파일에 적을 때만 만든다 - 기존 엔진은 문자열 GUID 위에 `Guid128` 을 덧붙여
  두 벌이 됐다. 캔버스 파일의 `spriteId:` 는 이제 32 자리 16 진수다(이전 파일은 없다). (2) 이미지 파일 하나는 **Texture 와 Sprite 두 에셋**으로
  등록된다. Sprite 는 임포트 때 자동으로 생겨 사용자는 파일 하나만 본다. 3D 재질은 Texture 를 참조한다.
  (3) 에셋 폴더는 `Contents/Assets` 이고 `.jproject` 의 `AssetDirectory` 키(기본값 `Contents/Assets`)가 정한다.
  `AssetIgnorePatterns` 는 스캔과 파일 감시 둘 다에 적용한다 - 기존 엔진은 감시에만 적용해 숨김 폴더의 파일이
  에셋으로 등록됐다. (4) 재질은 D-33 의 Shader Graph 방향을 유지한다. 자료 모델은 `{ Shader 에셋, 파라미터 블록,
  텍스처 슬롯 }` 이고 첫 구현은 빌트인 셰이더만 Shader 에셋으로 등록한다 - 사용자 셰이더 임포트는 나중에 Shader
  에셋을 만드는 길이 하나 더 생기는 것이라 재질과 렌더러가 바뀌지 않는다. 그 외 설계(타입별 풀과 index+generation
  핸들, 에셋은 CPU 자료만이고 GPU 는 `SpriteLibrary`·`MeshLibrary` 가 드는 것, 캔버스 단위 참조 수, 프레임 경로에
  조회 없음)는 asset-plan §2. `[열림]` 이미지 디코더 `stb_image` 도입, `SpriteSubmit` 의 UV 사각형(D-32 ABI).
  **1 단계(레지스트리·메타·스캔·`.jproject` 키)가 섰다**(`ffdddb3`·`ade7e2a`, asset-plan §3-1). 메타 파일은 경로를
  적지 않고(옮겨도 아이디가 산다), 게임 실행은 메타를 만들지 않는다(`createMissingMeta` 는 에디터만 참).
  이미지 디코더는 `stb_image` 로 확정했다(2026-09-18, 임포트 경로만). `SpriteSubmit`·GPU 인스턴스의 UV 사각형도
  확정했다(2026-09-18) - 3 단계에서 D-32 ABI 를 고칠 때 Decision 으로 적는다.
  **2 단계(타입별 풀·텍스처와 스프라이트 로드·해석 패스·호스트와 에디터 배선)가 섰다**(`931998d`, asset-plan §3-2).
  `AssetSystem` 의 `LoadTexture/LoadSprite/LoadMesh/LoadMaterial/LoadShader` 는 없다 - 타입은 레지스트리가 알고 결과
  핸들의 상위 4 비트가 타입을 말한다.
- **D-112. 파일 시스템은 플랫폼이 관리한다.** (2026-09-18) 사용자 결정: 플랫폼마다 읽는 길이 다르다(Windows 는 파일,
  Android 는 APK 에셋, Web 은 가상 파일 시스템). `IPlatform` 에 `ReadWholeFile`·`WriteWholeFile`·`FileExists`·
  `DirectoryExists`·`EnumerateDirectory`(방문자, 폴더에 거짓을 돌려주면 내려가지 않음)가 있고 경로는 UTF-8 이다. 기본
  구현은 "파일 시스템이 없다"(거짓) - 테스트의 가짜 플랫폼과 아직 붙이지 않은 Web·Android 가 그것이다. Windows 는
  `filesystem::path` 의 UTF-8 생성자를 거쳐 와이드로 연다(`fopen_s` 는 ANSI 라 한글 폴더에서 조용히 실패한다).
  **엔진 모듈은 파일을 직접 열지 않고 이것을 거친다** - `AssetRegistry`·`AssetMetaFile` 이 첫 사용자다(JBroAsset →
  JBroPlatform 의존이 생겼다). **이전 완료**(2026-09-18): `YamlDocument::Load`·`YamlWriter::Save` 를 없앴다(Tier S 는
  플랫폼을 볼 수 없어 부르는 쪽이 읽어 `Parse` 한다), `LoadCanvasFile`·`SaveCanvasFile` 을 없애고 에디터가 플랫폼으로
  읽고 써서 `ReadCanvasText`·`WriteCanvasText` 에 넘긴다(Canvas 모듈은 플랫폼을 보지 않는다), `LoadProjectFile` 과
  `LocalizationTable::Load` 는 `IPlatform&` 을 받는다(에디터는 플랫폼을 만든 뒤에 로케일을 읽는다). **남긴 예외 둘**:
  `EditorTheme.cpp` 의 아이콘 글꼴은 런처 인자가 ANSI 라(D-97) C 런타임으로 연다 - 인자 인코딩이 UTF-8 로 정리되면
  옮긴다. `JBroScriptCompiler` 의 진단 메시지 파일은 컴파일러 도구(`jbroc`)의 것이라 엔진 모듈 규칙 밖으로 본다.
  **실측**: 플랫폼의 경로는 UTF-8 이므로 `USERPROFILE` 같은 ANSI 환경 변수는 와이드로 받아 UTF-8 로 바꿔 넘긴다.
  UTF-8 이 아닌 바이트는 변환이 던지는데 Windows 플랫폼은 그것을 잡아 빈 경로(= 거짓)로 만든다.
  테스트 `Tests/PlatformFileTests.cpp`.
- **D-113. 스프라이트는 텍스처와 UV 사각형을 들고 텍스처가 같은 이웃을 드로우 하나로 묶는다.** Updates: D-32, D-54.
  (2026-09-19) `SpriteSubmit` 은 `sprite` 대신 렌더러가 `RegisterTexture` 로 발급한 `texture`(비면 1x1 흰색 = 틴트만),
  `float uvRect[4]`(uMin, vMin, uScale, vScale, 기본 전체), `SpriteFilter filter`(기본 Nearest)를 든다. GPU 인스턴스는
  ATTRIBUTE4 로 UV 사각형을 받아 **60B** 다(4x4 시절 80B 보다 여전히 작다). 셰이더는 `t0`/`s0` 를 샘플링하고
  `uv = (x + 0.5, 0.5 - y) * zw + xy` 다(정점 y 는 위, 텍스처 행은 아래로). 렌더러는 제출 순서를 바꾸지 않고 텍스처·샘플러가
  같은 이웃만 `SpriteRun` 으로 묶어 드로우 하나로 낸다 - 정렬은 프레임워크의 일이다. 죽은 텍스처 핸들은 흰색으로 그리되
  `staleTextureSpriteCount` 로 센다. `Renderer::RegisterTexture/UpdateTexture/UnregisterTexture` 는 `RegisterMesh` 와 같은
  규약(프레임 밖, index+generation)이다. **GPU 텍스처는 `SpriteLibrary`(Framework2DSystem)가 든다**(asset-plan §2.5):
  스프라이트 에셋 핸들의 슬롯으로 찍는 배열이라 프레임 경로에 조회가 없고, 텍스처는 처음 만날 때 한 번 올리며
  `pixelGeneration` 이 바뀌면 같은 핸들에 다시 올린다. 해석은 렌더 추출(Update) 단계에서 한다 - 렌더러 프레임 밖이라
  업로드가 허용되는 자리다. `SpriteRenderer2D` 에 `frameIndex` 가 생겼다(넘치면 마지막 칸). `[열림]` `size`·`pivot` 은
  저작 값 그대로다 - 프레임 픽셀 / PPU 로 크기를 정하는 것(기존 엔진 방식)은 정하지 않았다.
  벤치마크(Release, RTX 2080): **회귀가 있었고 절반을 되찾았다.** 같은 기계 상태에서 3 단계 이전 커밋(A)과 잇달아 재어 소음을 갈랐다(메시 장면은
  둘이 같다). 스프라이트 60000, avg ms(제출/끝):
  A `661366a`: D3D12 0.80(0.31/0.49), D3D11 0.59~0.85(0.21~0.36/0.38~0.49), Vulkan 0.96~1.12(0.21~0.29/0.75~0.83).
  B `3432ed0`(두 번 지나감): D3D12 1.44~1.85(0.47~0.58/0.96~1.27), D3D11 1.34~1.50(0.48~0.54/0.87~0.96), Vulkan 1.89~1.94(0.45/1.43~1.49).
  C 한 번 지나감(이 커밋): D3D12 1.29~1.31(0.54~0.55/0.74~0.77), D3D11 1.20~1.49(0.52~0.67/0.68~0.83), Vulkan 1.57~1.69(0.46~0.51/1.11~1.18);
  텍스처 둘·묶음 600: D3D12 1.29~1.52, D3D11 1.25~1.28, Vulkan 1.74~1.77(묶음 600 개의 값은 0.1ms 안이다).
  남은 차이의 원인은 크기다: 패킷이 60B → 80B(`texture`·`uvRect`·`filter`), 인스턴스가 44B → 60B 라 제출의 memcpy 와
  업로드 힙(write-combined)으로의 memcpy 가 각각 1/3 늘었고, 픽셀 셰이더가 텍스처를 샘플링한다. `[진행 예정]` 인스턴스를
  40B 로 줄인다: 틴트를 `UByte4Norm`(4B), UV 사각형을 16 비트 정규화 넷(8B, 세 백엔드에 `UShort4Norm` 정점 형식 추가).
  그러면 3 단계 이전(44B)보다 작아진다. 패킷 쪽은 `material` 이 아직 읽히지 않으므로 그대로 둔다. → D-114 에서 했다.
  뮤테이션: 6 개 중 5 죽음(묶음을 절대 합치지 않음, 죽은 핸들 안 셈, uMin 안 옮김, 라이브러리 UV 의 x 를 y 에서, 라이브러리가 매번
  다시 올림; 19 분). 살아남은 하나(`RegisterTexture` 의 바이트 수 검사 제거)는 **동치**다 - 백엔드의 `WriteTexture` 가 같은
  크기 오류를 이미 거절해 결과가 같다. 렌더러의 검사는 텍스처를 만들고 버리는 낭비를 막는 것뿐이라 그대로 둔다.
- **D-114. 스프라이트 GPU 인스턴스는 40 바이트다 - 틴트는 바이트 넷, UV 사각형은 16 비트 정규화 넷.** Updates: D-113.
  (2026-09-20, `5caaa94`) RHI 에 `VertexFormat::UShort4Norm`(D3D `R16G16B16A16_UNORM`, Vulkan `R16G16B16A16_UNORM`)이
  생겼다. 셰이더는 정규화 형식을 0..1 의 float4 로 받으므로 HLSL 과 바이트코드는 바뀌지 않았다. 틴트와 UV 는 0..1 로
  잘리고 반올림된다(틴트 0.5 → 128 → 0.502, 픽셀 테스트의 오차 0.02 안). 8K 텍스처에서도 UV 정밀도는 1/8 텍셀이다.
  60000 개의 인스턴스 업로드가 3.6MB → 2.4MB 다. 벤치마크(Release, RTX 2080, 같은 기계 상태의 A/B): 40B(이 커밋, 조용한 기계): D3D12 1.03~1.09(제출 0.33~0.38/끝 0.70~0.72), D3D11 1.26~1.29(0.56~0.57/0.71), Vulkan 1.30~1.49(0.34~0.48/0.96~1.01); 텍스처 둘·묶음 600: D3D12 1.14~1.18, D3D11 1.56~1.63, Vulkan 1.35~1.69. 60B 한 번 지나감(앞 실측, D-113): D3D12 1.29~1.31, D3D11 1.20~1.49, Vulkan 1.57~1.69. 3 단계 이전: D3D12 0.80, D3D11 0.59~0.85, Vulkan 0.96~1.12. 즉 D3D12·Vulkan 은 회귀의 절반을 더 되찾았고 D3D11 은 거의 그대로다. 남은 +0.2ms 는 패킷 80B 의 제출 memcpy 와 픽셀 셰이더의 샘플링이다(같은 세션의 A 측 재측정은 다른 세션의 빌드가 겹쳐 메시 장면까지 두 배로 나와 버렸다).
  뮤테이션: 6/6 죽음(세 백엔드의 `UShort4Norm` 을 바이트 형식으로, UV 를 255 로 접음, 알파 항상 불투명, 틴트 자르기 제거 - 마지막 것은 처음에 살아남아 1.5·-1 틴트를 되읽는 픽셀 검사를 더한 뒤 죽였다). 뮤테이션 러너의 첫 회차는 A/B 스크립트가 워크트리 체크아웃 뒤 `tasks/*.md` 를 LF 로 얹지 않아 33 초 만에 가짜 '전부 죽음' 이 나왔고, 얹은 뒤 다시 돌렸다(전체 스위트는 조용한 기계에서 91 초).
  `[열림]` 틴트가 1 을 넘는 HDR 틴트는 이 형식으로 표현할 수 없다 - 필요해지면 패킷의 `filter` 옆에 가산 모드 같은 것으로
  따로 정한다.
- **D-115. 캔버스의 에셋 해석은 프레임워크가 하고, 게임 호스트는 프로젝트 파일과 시작 캔버스를 받는다.** (2026-09-20, `be2a6c6`)
  `IFramework::BindCanvasAssets()`(기본 no-op) 가 캔버스의 모든 컴포넌트를 `ForEachReflectedComponent`(JBroCanvas, 캔버스
  파일과 같은 걸음)로 돌며 `AssetSystem::BindComponentAssets` 로 `xxxId` → `xxx` 를 채운다. 잡은 핸들은 프레임워크가
  들고 다음 해석과 종료 때 놓는다 - 에디터가 따로 들던 목록은 없앴다. 호스트 계층이 `Canvas` 를 보지 않아야 하므로(D-42)
  이 자리가 프레임워크다. 게임 호스트의 인자는 `--project <경로>`·`--canvas <경로>` 둘이고(에디터 D-97 과 같은 모양),
  `wmain` 으로 받아 UTF-8 로 바꾼다. 프로젝트 없이 실행하면 빈 프로젝트로 뜬다(지금까지의 동작). 프로젝트가 있으면
  `OpenProjectFile` → 시작 캔버스(`--canvas` 가 이기고 없으면 `Build.StartupCanvas`, 상대경로는 프로젝트 폴더 기준)를
  플랫폼으로 읽어 `ReadCanvasText` → `BindCanvasAssets`. 캔버스를 못 읽으면 표준 출력에 알리고 빈 캔버스로 뜬다. 모르는
  인자·값 없는 인자는 종료 코드 5 다. 두 게임 구성(`Debug_Game2D`·`Debug_Game3D`) 모두 빌드했고 인자 오류와 없는 프로젝트의
  종료 코드(5·3)를 실행으로 확인했다. 테스트 `Tests/GameHostArgumentTests.cpp`, `AssetSystemTests` 의 프레임워크 해석 검사.
  뮤테이션: 5/5 죽음(다시 풀 때 앞 것을 안 놓음, 종료 때 안 놓음, 표 없는 컴포넌트를 걸음, 모르는 인자 무시, 프로젝트의 시작 캔버스가 --canvas 를 이김; 10 분). 셋째는 처음 살아남아 표를 등록하지 않은 컴포넌트를 캔버스에 붙인 검사를 더한 뒤 죽였다.
  `[열림]` `MeshRenderer3D::meshId` 는 빌트인 정육면체(`FromName`)라 레지스트리에 없어 해석 패스가 핸들을 비우고,
  `MeshRender3DSystem` 이 매 Update 에 `MeshLibrary` 로 다시 푼다 - 메시 에셋이 레지스트리에 들어오면 정리한다.
- **D-116. 남은 일은 공용·2D·3D 로 나눠 적고, 에셋 필드는 공용 드롭다운 위젯이며, 에셋 브라우저 패널을 둔다.** (2026-09-20)
  사용자 결정: `tasks/todo.md` 는 공용 남은 일과 Decisions, `tasks/todo-2d.md`·`tasks/todo-3d.md` 가 차원별 남은 일이다.
  3D 는 2D 뒤의 순서라 재질(D-33 첫 구현)·`materialId` 해석·물리 3D·3D 스크립트 서비스·3D 기즈모 항목은 3D 쪽으로 옮겼다.
  에셋 4 단계(에디터)의 결정: (1) 인스펙터의 에셋 필드는 **드롭다운**이다 - 기존 엔진의 `ImFilterCombo`(검색 달린 목록)·
  `ImAssetField` 처럼 **공용 위젯**(`Widget::FilterCombo` → `Widget::AssetField`)으로 만들고, ImGui 를 직접 부르는 컴포넌트
  추가 팝업 등 같은 자리에 **일괄 적용**한다. (2) **에셋 브라우저 패널**을 넣는다(폴더 트리·파일 목록·선택하면 인스펙터에
  임포트 옵션). (3) 파일 감시는 `[논의]` - 기존 엔진은 mtime 폴링이었고, 비동기 감시(`ReadDirectoryChangesW`) 를
  `IPlatform` 뒤에 두는 안을 todo-2d 에 적었다. 스프라이트 크기 정책과 샘플러 선택의 자리도 `[논의]` 로 todo-2d 에 있다.
  이번에는 작업하지 않고 문서만 정리했다.
- **D-117. 파일 감시는 OS 감시를 플랫폼 뒤에, 스프라이트 크기는 에셋의 PPU 로, 샘플러는 프로젝트 기본 + 텍스처 옵션.** (2026-09-20)
  사용자 결정 셋(todo-2d 의 `[논의]` 종결). (1) **파일 감시**: `IPlatform::WatchDirectory` 가 OS 감시(Windows 는
  `ReadDirectoryChangesW`)를 워커에서 돌리고, 이벤트는 경로 문자열만 든 POD 로 메인 스레드 큐에 넘긴다. 메인 스레드가 프레임
  밖에서 꺼내 원본 변경은 `ReloadInPlace`, `.jmeta` 변경은 무시, 이동은 경로만, 삭제는 참조 수 0 까지 유지. 게임 실행에는
  감시가 없다. 폴링(기존 엔진)은 쓰지 않는다. (2) **스프라이트 크기**: `.jproject` 의 `PixelsPerUnit` 을 없애고 PPU 는 에셋
  단위(`SpriteImportOptions.pixelsPerUnit`, 기본 100)다. 크기 = 프레임 픽셀 / 에셋 PPU. `SpriteRenderer2D` 에 `sizeMode
  { FromSprite, Custom }` 을 두되 이번엔 `FromSprite` 만 구현하고 `size` 는 `Custom` 용으로 남긴다. 피벗은 프레임의 값이
  기본이고 컴포넌트의 `pivot` 은 덮어쓰기다. 프로젝트 키는 지금 파싱만 되고 아무 데서도 읽지 않으므로 없애도 옛 파일은
  열린다(모르는 키는 건너뛴다). (3) **샘플러**: `.jproject` 에 `TextureFilter: Nearest|Linear`(기본 Nearest) 를 두고 텍스처의
  임포트 옵션이 덮어쓴다. 컴포넌트에는 두지 않는다. (4) 미리 읽기 워커는 공용 todo 의 `[열림]` 그대로다.
- **D-118. 목록에서 하나를 고르는 칸은 `Widget::FilterCombo` 이고, `AssetId` 필드는 그 위의 `AssetField` 다.** (2026-09-20, `be2e7cd`·`00c0b90`)
  D-116 의 구현. 검색 칸 달린 드롭다운 하나(`FilterCombo`)가 몸이고 `EnumCombo`·`AssetField`·인스펙터의 컴포넌트 추가가 그
  위에 선다 - 검색·빈 글·Enter 동작을 한 곳에서 고친다. `AssetField` 는 이름 뷰와 아이디 뷰를 받고 레지스트리를 보지 않는다
  (위젯 계층이 에셋 모듈의 표를 모르게). 인스펙터가 에셋 필드를 알아보는 규칙은 해석 패스(D-115)와 같다: `JBro.Uuid` 이고
  이름이 `Id` 로 끝나면 에셋이고, 앞부분이 타입 이름이면 그 타입만 보인다(모르는 앞부분은 전부). 편집 뒤 해석은 커맨드를
  골라내지 않고 **커맨드 판번호**를 본다 - `EditorApplication::Tick` 이 UI 프레임 뒤에 판이 움직였으면 `BindCanvasAssets` 를
  다시 부른다. 되돌리기·다시 실행·붙여넣기도 아이디를 바꾸므로 커맨드 종류로 가르면 빠진다. 비용은 캔버스의 컴포넌트를 한 번
  걷고 풀을 조회하는 것이라 에디터에서는 문제가 아니고, 게임 실행에는 커맨드가 없다. 원소 안의 `AssetId`(목록 원소 편집 경로)는
  아직 글자 칸이다. 뮤테이션: FilterCombo 6/6 잡힘(처음엔 짧은 enum 에 검색 칸을 늘 그리는 변이가 살아 "활성 입력 칸 없음"
  검사를 더했다), AssetField 8/8 잡힘.
- **D-119. 스프라이트의 크기·피벗은 에셋이 정하고, 샘플러는 프로젝트 기본을 에셋 시스템이 로드 때 적용한다.** (2026-09-20, `805ef60`·`0e71bd8`)
  D-117 의 구현. (1) `SpriteRenderer2D::sizeMode { FromSprite, Custom }`, 기본 `FromSprite`: 크기 = 칸 픽셀 / 에셋 PPU,
  피벗 = 칸의 피벗. `Custom` 과 풀리지 않은 스프라이트만 저작 `size`·`pivot` 이다 - 텍스처 없는 스프라이트(기존 캔버스·
  테스트)가 그대로 그려지도록. 피벗 덮어쓰기는 따로 두지 않고 `Custom` 이 크기와 피벗을 함께 맡는다(D-117 의 "컴포넌트
  피벗은 덮어쓰기" 를 이 스위치로 읽었다). `SpriteImportOptions.pixelsPerUnit` 기본 100, 0 이하는 100. `.jproject` 의
  `PixelsPerUnit` 은 없앴고 옛 파일의 키는 모르는 키로 건너뛴다. (2) `TextureFilter { Default, Nearest, Linear }` 는
  AssetTypes 에 있고 `.jproject` 의 `TextureFilter`(Nearest|Linear, 기본 Nearest)와 메타의 `Texture.ImportOptions.filter`
  (기본 `Default`) 가 쓴다. **프로젝트 기본은 `AssetSystem` 이 로드·재로드 때 한 번 적용한다**(`SetDefaultTextureFilter`,
  `TextureData::filter`) - 그리는 쪽(라이브러리·시스템·브리지)이 프로젝트를 묻지 않고 `Default` 를 보지 않는다. 엔진은
  프로젝트 파일을 읽은 뒤 `Bind` 전에 넘긴다. 컴포넌트에는 두지 않는다. 뮤테이션: 크기 6 개 중 5 잡힘(높이가 너비를 쓰는 변이가 살아 2x1 칸 검사를 ④ 에 더해 잡았다). 샘플러 10/10 잡힘(프로젝트 기본 미적용, 메타 필터 무시, 틀린 옵션 허용, Default 기본 유지, 프로젝트 키 무시, 라이브러리·시스템·브리지가 필터를 버림, 엔진이 기본을 안 넘김 - 마지막 것은 처음 살아 엔진 호스트 검사를 더해 잡았다).
- **D-120. 메타는 임포트 옵션 블록을 왕복하고, 에셋 편집은 메타 글자 전체를 되살릴 값으로 든다.** (2026-09-20, `e6d3e96`)
  D-116 의 구현(에셋 브라우저·옵션 편집). `AssetMetaFile` 이 `Texture.ImportOptions`·`Sprite.ImportOptions` 를 리플렉션
  표로 읽고 쓴다 - 있는 블록만 적고, 있으면 전부 읽혀야 한다(모르는 키·틀린 enum 이름은 파일 전체의 실패). 에셋 시스템의
  옵션 읽기도 이 파서 하나다. 에디터의 옵션 편집은 `SetAssetMetaCommand` 로 가고 **되살릴 값은 메타 파일의 글자 전체**다 -
  옵션 블록만 뜨면 나머지 키를 잃을 길이 생긴다. 실행은 새 글자를 쓰고 로드된 텍스처·스프라이트를 `ReloadInPlace` 하므로
  핸들이 살아 캔버스가 그 자리에서 새 칸을 본다. 에셋 선택은 오브젝트 선택과 배타이고 인스펙터는 하나만 보인다. 브라우저는
  디스크가 아니라 레지스트리를 보이며, 이미지는 한 줄(Texture 레코드)이다. 인스펙터의 에셋 블록은 컴포넌트와 같은 Id 사슬
  (`슬롯 → ##import → 필드 → ##value`)이라 테스트가 같은 길로 찾는다.
- **D-121. 에셋 폴더 감시는 `IPlatform` 뒤의 OS 감시이고, 적용은 엔진이 프레임 밖에서 한다.** (2026-09-20, `0bea257`)
  D-117 (1) 의 구현. `WatchDirectory`/`StopWatching`/`TakeFileEvents` 와 POD `FileEvent`(경로 글자만, 고정 1024 바이트).
  Windows 는 `ReadDirectoryChangesW` + 워커 스레드 + 고정 고리 버퍼(256, 뮤텍스)다 - 워커는 할당도 `SafePtr` 도 만지지
  않는다. **첫 요청은 워커를 띄우기 전에 건다**(안 그러면 감시 직후의 변경을 놓친다 - 처음 테스트가 그렇게 실패했다).
  넘침(OS 가 버림, 고리가 참, 경로가 김)은 쌓인 것을 다 꺼낸 뒤 `Overflow` 하나로 알린다. `EngineInstance::PollAssetChanges`
  가 적용한다(원본 → 재로드, 이름 → `Rename`, 삭제 → `Unregister`, 생성·모르는 삭제·넘침 → 다시 스캔, `.jmeta` 무시).
  `EngineConfig::watchAssetDirectory` 는 에디터만 참이라 게임 실행에는 감시가 없다. 에디터는 `Tick` 첫머리에서 꺼내고
  다시 스캔했으면 `BindCanvasAssets` 를 돌린다. Web·Android 는 기본(거짓)이다.
- **D-122. 네트워크는 별도 프로젝트로 엔진 없이 먼저 세우고, 기존 엔진의 WS + Reliable UDP 하이브리드를 전용 컨테이너로 잇고,
  동기화는 컴포넌트 풀 스냅숏 델타까지 엔진이 맡는다.** (2026-09-20, [network-plan.md](./network-plan.md))
  지금 엔진에는 네트워크 모듈도 서비스도 없다. 기존 엔진(`Engine/Core/Network`)은 전 플랫폼 WebSocket 기준선 위에 네이티브
  UDP 를 덧대고 그 위에 Reliable UDP(ACK·재전송·RTO·재정렬·프래그먼트·AIMD·피기백)를 끝까지 만들었으나, 오브젝트 복제는 없었고
  웹 호스트도 없었다(§1). 정한 것: (1) `source/JBroNetwork/` 별도 VS 프로젝트. 엔진은 협업 중이므로 **부착 단계 전까지
  `source/JBroEngine/` 아래 파일은 바뀌지 않는다.** 네트워크가 무엇을 참조하는지는 제약이 아니다(엔진 모듈 참조는 엔진 파일을
  바꾸지 않는다). 부착 단계에 엔진에 생기는 것은 `IPlatform` 소켓·`JBroNetworkSystem`·`EngineInstance` 블록 병합·프렐류드 한 줄이다.
  소켓과 시계는 주입받는다.
  (2) 메인 스레드 폴링으로 시작하되 트랜스포트 밖 경계는 전부 POD 큐 꺼내 가기(`TakeEvents`/`TakeMessages`)다 - 콜백을 두지
  않는다. 워커로 옮겨도 서비스 API 가 바뀌지 않게 하기 위해서다. (3) STL 컨테이너 대신 `Array`/`Table`/`String`, 매 틱 경로는
  고정 원형 배열. (4) 동기화는 별도 동기화 컴포넌트 없이 **컴포넌트 타입 단위로 풀을 등록해 스냅숏 델타**를 보낸다. 식별자는
  컴포넌트가 아니라 시스템의 `InstanceId ↔ NetworkObjectId` 표다. 스폰·소멸은 신뢰 채널, 상태는 `UnreliableSequenced`.
  (5) WebRTC DataChannel 을 범위에 넣어 웹끼리 P2P 호스트를 허용한다. 기존 규약 "웹은 서버 불가" 는 "웹은 WebSocket 을 받을 수
  없다" 로 고친다. 시그널링 서버가 필요하고, 네이티브 ↔ 웹 P2P 는 열어 둔다. (6) 검증은 테스트 프로젝트 안에서 호스트 둘을 루프백으로
  잇고, 유실은 기존 `CLossyUdpSocket` 데코레이터를 옮겨 주입한다. **2026-09-20 에 1~5 단계가 섰다**(계획서 §3): 트랜스포트·WS·
  Reliable UDP·복제·부착. 부착에서 엔진에 생긴 것은 `IPlatform::CreateSocketProvider`, 모듈 `JBroNetworkSystem`(`NetworkHost`·
  `CanvasPoolAdapter`·수신/송신 시스템), `Transform2DReplication`, `FrameworkContext::network`·`EngineConfig::networkEnabled`·
  `EngineInstance` 의 네트워크 소유와 블록 병합, 프렐류드 한 줄, 빌드 파일이다. 6 단계(WebRTC)는 피어형 연결·시그널링 서버/클라이언트를
  인메모리 피어로 검증했고, 브라우저 접착(`Web/WebSocketProvider`, Emscripten 전용)은 작성만 했다 - 이 저장소에 웹 도구가 없어
  컴파일도 확인하지 못했다(계획서 §3-6·§3-9). **자원을 잡는 때는 D-125 가 고쳤다** - 캔버스를 묶는 것만으로 복제가 서지 않고,
  게임이 켤 때 선다. 스크립트 경계는 D-37 확장 블록이고 `JBroRuntime::ServiceContext` 에는 넣지 않는다(D-43 과 같은 이유).
- **D-123. 에셋 4 단계 전수 검수(2026-09-20, `2de9f0a`): 감시는 소멸자가 정리하고 죽음을 알리며, 변경 적용은 조용해진 뒤에 하고, 메타는 적히지 않는 값을 쓰지 않는다.**
  세 갈래로 나눠 검수했다(감시·엔진 적용 / 에디터 위젯·패널 / 에셋·스프라이트 파이프라인). 고친 것: (1) `FileWatcher` 는
  소멸자가 멈춤 → 합류 → 핸들 닫기를 한다 - `Shutdown` 없이 파괴되면 joinable 스레드로 `std::terminate` 였다. 워커의
  비정상 종료(대기·읽기 실패)는 로그와 함께 `Overflow` 하나로 알리고 `IsWatching` 이 거짓이 된다. 넘침 때 이름 바꾸기의
  앞 조각을 버린다(무관한 다음 이름 바꾸기와 짝지어졌다). 알림 항목의 경계를 검사한다. 경로 버퍼는 512 바이트다.
  (2) 엔진은 이벤트 뒤 **조용한 프레임 셋**이 지나야 재로드·다시 스캔을 돌린다 - 저장 프로그램은 쓰기가 끝나기 전에도
  알리고 여러 번 알리며, 대량 복사는 하나로 묶인다. 실패한 재로드는 다섯 번까지 다시 한다. 다시 스캔은 새 표에 하고
  폴더를 못 읽으면 전 표를 둔다(`rescanFailed`) - 한 프레임의 실패로 모든 참조가 풀리지 않게. `Overflow` 는 로드된 것
  전부를 재로드한다(다시 스캔만으로는 화면이 옛 그림에 머문다). 이름 바꾸기는 `.jmeta` 를 새 자리에 같이 써 아이디가
  다음 스캔에도 산다. 한쪽만 메타인 이름 바꾸기는 다시 스캔한다. 이벤트 버퍼는 멤버다(스택 128 KB 를 프레임마다 비웠다).
  (3) `FormatAssetMetaFile` 은 옵션 블록이 적히지 않거나 이미지에 스프라이트 아이디가 없으면 글자를 내지 않는다 -
  전에는 빈 `Texture: {}` 를 적어 다음 읽기가 기본값으로 대신했고 옵션이 조용히 사라졌다. `Version` 은 1 만 읽는다.
  메타를 먼저 보고 디코드한다. 표에 있는데 자리가 없는 핸들은 새로 싣는다. 풀이 차서 스프라이트를 못 넣으면 잡은
  텍스처를 놓는다. 0 이하·비유한 PPU 는 로드 때 `DefaultPixelsPerUnit`(AssetTypes 한 곳)으로 바로잡는다. 메타 읽기
  실패는 경로와 줄을 로그에 남긴다. `SpriteLibrary` 는 올리기 실패를 픽셀 세대로 기억해 스프라이트마다 프레임마다
  다시 올리려 들지 않는다. 프로젝트의 `TextureFilter` 오류는 무엇이 되는지 말한다. (4) 에디터: 레지스트리에 판번호를
  두고 에셋 브라우저와 인스펙터의 에셋 목록은 판이 움직였을 때만 다시 모은다(전에는 프레임마다 전부 걸었다).
  `FilterCombo` 는 가장 긴 이름을 항목 수가 바뀔 때만 잰다. `ClearSelection` 이 에셋 선택도 비운다.
  **고치지 않고 남긴 것**(아래 2D 남은 일): 이미지 로드마다 메타를 세 번 파싱하는 것, 메타 오류의 줄 번호가 늘 1 인 것,
  메타 저장이 원자적이지 않은 것(임시 파일 + 바꿔치기가 플랫폼에 없다), `recordsAt` 의 "이미지 하나에 스프라이트 하나"
  가정과 O(N) 걷기, `Custom` 이 크기와 피벗을 함께 맡아 "에셋 크기 + 저작 피벗" 조합이 없는 것. 뮤테이션: 6 개 중 5 잡힘(메타 안 옮김, PPU 안 고침, 판 검사 없음, 빈 스프라이트 아이디 씀, 판번호 안 움직임). 살아남은 것: 다시 스캔 실패 때 전 레지스트리를 비우는 변이 - 감시 중에 에셋 폴더가 사라지는 상황을 테스트가 만들지 못했다(감시 핸들이 폴더를 붙들어 지울 수 없다). 다음에 `ScanAssets` 를 시험 가능한 자리로 빼면 잰다.
- **D-124. 검수에서 남겼던 여섯을 닫았다: 메타는 이미지마다 한 번 파싱하고, 오류는 줄을 말하고, 저장은 바꿔치기이며, 피벗은 크기와 따로 고른다.** (2026-09-20, `d9bf243`)
  D-123 의 "고치지 않고 남긴 것" 을 사용자가 전부 고치라 했다. (1) `YamlDocument` 노드가 시작 줄을 들고(`GetLine`),
  메타 오류는 실패한 키·블록의 줄을 말한다. (2) `IPlatform::MoveFileTo`(Windows `MoveFileExW`, 덮어쓰기·write-through)
  를 두고 `SaveAssetMetaFile` 은 `<meta>.tmp` 에 쓴 뒤 바꿔치기한다 - 쓰다 만 메타로 아이디를 잃지 않는다. 이름 바꾸기의
  메타도 복사가 아니라 옮겨 고아가 없다. 감시는 `.jmeta.tmp` 를 메타처럼 무시한다. (3) 레지스트리에 주인 색인
  (`CollectOwned`)을 두어 주인을 빼면 가리키던 것이 다 빠지고, 엔진은 이벤트마다 레지스트리를 걷지도 "이미지 하나에
  Sprite 하나" 를 가정하지도 않는다. (4) 에셋 시스템은 메타를 이미지마다 한 번 파싱해 주인(Texture) 아이디로 캐시한다.
  `ReloadInPlace`(로드 여부와 무관)와 내리기가 캐시를 비워, 편집한 옵션은 다음 로드가 보고 손으로 고친 메타는 내렸다
  올리면 보인다. (5) `SpriteRenderer2D::pivotMode { FromSprite, Custom }` 이 피벗을 크기와 따로 고른다 - D-117 이 말한
  "컴포넌트 피벗은 덮어쓰기" 그대로이고 D-119 의 결합 해석은 이것으로 대체된다(필드 13). (6) `EngineInstance::RescanAssets`
  로 다시 스캔을 시험할 수 있게 했고(감시 중인 폴더는 지울 수 없지만 이름은 바꿀 수 있어 그렇게 실패를 만든다), 에셋
  브라우저가 감시가 꺼져 있으면 경고 배지를 보인다. 뮤테이션: 7/7 잡힘(다시 스캔 실패 때 비움 - D-123 의 생존 변이가 이번에 잡혔다, 내린 뒤 캐시 유지, 재로드 뒤 캐시 유지, 주인 색인 비움, 피벗이 크기 모드를 따름, 오류 줄 잃음, 임시 파일 남김).

- **D-125. 네트워크는 게임이 켤 때 자원을 잡고 끌 때 돌려준다. 엔진이 호스트를 세우는 것은 스크립트가 켤 대상을 두는 일까지다.**
  (2026-09-20, [network-plan.md](./network-plan.md) §2.4·§3-5)
  D-122 의 부착 단계는 `EngineInstance` 가 `NetworkHost` 를, 프레임워크가 캔버스를 묶으며 복제 서버·클라이언트를 **무조건**
  세우게 했다. 그래서 네트워크를 쓰지 않는 에디터와 단일 플레이도 트랜스포트 1250050 B 와 복제 약 40 MB(기본 설정 계산값)를
  물었고, 그 사실은 어느 문서에도 없었다. 정한 것: (1) 엔진은 `NetworkHost` 를 계속 세운다 - 값 서비스
  (`NetworkSessionService::StartServer` / `Connect` / `Disconnect`)가 부를 대상이 있어야 스크립트가 켜고 끌 수 있다.
  (2) 트랜스포트의 공용 버퍼는 역할을 잡을 때(`Listen`·`Connect`·`HostPeers`·`ConnectPeer`) 잡고 `Close` 가 돌려준다.
  이벤트 큐만은 `Close` 가 방금 넣은 `Disconnected` 를 게임이 다음 프레임에 꺼내 가야 하므로 소멸자까지 산다.
  (3) 복제는 역할이 생긴 뒤에, 그 역할이 쓰는 쪽만 선다(서버 아니면 클라이언트). 캔버스를 묶는 일은 풀의 **등록 순서**를
  기억하는 데까지이고, 복제가 설 때 같은 순서로 넘긴다 - 순서가 곧 타입 번호이므로 양쪽이 같아야 한다.
  (4) `EngineConfig::networkEnabled` 는 남기되 뜻이 "호스트 객체와 스크립트 서비스를 두는가" 로 좁아진다. 켜져 있어도
  자원은 잡지 않으므로 기본값 참을 유지한다. 테스트 둘이 이것을 지킨다(계획서 §3-5의 검증 절). **D-122 의 부착 서술 가운데
  "캔버스를 묶으면 복제가 선다" 는 이것으로 대체된다.**

- **D-126. 네트워크 전수 검수: 상대가 보낸 바이트를 믿던 네 자리를 막고, 꺼져 있던 보간을 켜고, 클라이언트 수에 비례하던
  델타 인코드를 기준별 한 번으로 줄였다.** (2026-09-20, 계획서 §3-8)
  D-122 의 여섯 단계가 테스트를 통과한 뒤, 신뢰할 수 없는 입력이 들어오는 자리와 매 프레임 도는 자리를 다시 읽었다.
  고친 것: (1) `UnreliableSequenced` 의 최근 순번 표가 **상대가 고르는 메시지 ID** 로 자랐다 - 고정 슬롯 64 개로 바꿨다.
  (2) 데이터그램의 채널 바이트를 `UdpProto::Decode` 가 검증한다. (3) 마지막이 아닌 조각은 꽉 차 있어야 한다 - 짧은 중간
  조각을 받으면 조립 버퍼에 앞 메시지의 잔여 바이트가 남은 채 올라왔다. (4) 순번 비교는 부호 있는 거리다 - 32 비트 랩을
  건너는 역전 도착이 중복으로 보이지 않는다. (5) WS 제어 프레임의 FIN·125 바이트 상한을 검사하고, 스크래치를 넘는 시스템
  메시지는 잘라 해석하지 않고 끊는다. (6) 보간: 수신 시스템이 `alpha` 를 1 로 고정해 보간이 꺼져 있었고 `m_dirty` 는 읽히지
  않았다 - 이제 복제가 스냅숏 간격을 재어 계수를 정하고, 간격을 모르면 1 이며, 새 스냅숏도 보간할 것도 없으면 일하지 않는다.
  (7) 델타는 기준마다 한 번만 인코드한다 - 클라이언트 수에 비례하던 비용이 서로 다른 기준의 수에 비례한다.
  (8) `Broadcast` 는 연결을 다시 찾지 않는다. (9) `StartServer` / `Connect` 가 예외를 잡아 거짓으로 돌린다 - D-125 로 그 두
  자리가 예산을 잡는 자리가 되었으므로, 예외가 스크립트 DLL 경계를 넘지 않게 한다.
  **실측 조건을 바로잡았다**: D-122·§3-4 의 스텝당 1.86 ms 는 **클라이언트가 하나일 때**의 값이다.
  `[열림]` `Array` 의 기본 할당자는 실패할 때 `nullptr` 을 돌려주고 `Reallocate` 가 그대로 쓴다 - 진짜 OOM 은 예외가 아니라
  널 역참조이고, 이것은 JBroCore 전체의 정책 문제라 네트워크에서 고치지 않았다.

- **D-127. 기존 엔진의 경로는 `C:\Users\박주형\source\repos\JBroEngine` 이고, 규칙에 적어 둔다.**
  (2026-09-21, `ProjectRule.md` §11.0, [editor-parity-plan.md](./editor-parity-plan.md))
  에디터를 띄운 사용자가 짚었다. 규칙은 §11 내내 "기존 엔진의 `Application/Editor/ImItem/`" 처럼 **상대 경로**로만
  가리켰고, 그 트리가 디스크 어디에 있는지는 어디에도 없었다. 그래서 세션마다 찾아 헤매거나 못 찾은 채로
  "기존에 없다" 고 적었다. 경로를 규칙과 `CLAUDE.md` 에 박고, 화면과 맞닿는 디렉터리 목록을 함께 적었다.
  **경로를 몰라 못 읽은 것과 거기에 없는 것은 다르다** - 이 구분이 §11.4 의 "나란히 놓고 다른 점을 적는다" 를
  지킬 수 있게 하는 전제다.

- **D-128. 뿌리 오브젝트에도 보이는 차례가 있고, 계층의 끌어 놓기는 행을 세 구역으로 나눈다.**
  (2026-09-21, 계획서 §3 단계 1·2)
  사용자가 "하이어라키 뷰에서 순서 바꾸고 자식 부모 해제하는 게 제대로 안 되어 있다" 고 짚었다. 읽어 보니 셋이었다.
  **(1) 뿌리끼리 순서를 바꿀 수 없었다.** 자식은 부모가 `m_children` 으로 차례를 들고 있는데 뿌리는 들 자리가 없어
  풀 순회 순서를 그대로 보여 주었다. `Canvas::GetRootObjects`/`FindRootIndex`/`SetRootIndex` 를 두었다 - 목록은
  **물어볼 때 풀과 맞춘다**(부모가 바뀌는 것은 `GameObject` 안에서 일어나고 Canvas 는 그 자리를 보지 못한다).
  `.jcanvas` 저장도 이 순서로 적는다 - 풀 순서로 적으면 끌어 옮긴 차례가 저장에서 사라진다.
  **(2) 부모 해제가 아예 되지 않았다.** 빈자리에 떨어뜨리는 자리를 `ImGui::Dummy(ImVec2(-FLT_MIN, ...))` 로 만들었는데,
  `Dummy` 는 넘긴 크기를 **그대로** 쓴다(위젯의 음수 폭 규칙이 없다) - 사각형이 뒤집혀 받는 자리가 생기지 않았다.
  `InvisibleButton` 으로 바꾸고 남은 높이가 0 이어도 한 줄은 보장한다. 우클릭 메뉴에도 `부모 해제` 를 두었다
  (기존 엔진 `CLayerTool` 과 같은 자리다).
  **(3) 놓을 자리가 둘뿐이었다.** 줄 사이의 4 픽셀 틈과 줄 자체였다. 기존 엔진처럼 **행 위 25 % = 앞에,
  가운데 50 % = 자식으로, 아래 25 % = 뒤에** 로 바꾸고, 선과 테두리로 무엇이 될지 먼저 보인다.
  곁들여 고친 것: 선택은 누를 때가 아니라 **뗄 때** 한다(누르는 순간 고르면 끌기를 시작하자마자 대상이 바뀐다),
  줄에 대한 판단은 이름을 그리기 **전에** 한다(그 뒤에는 ImGui 의 "마지막 항목" 이 글자라 줄의 빈 곳이 안 눌렸다),
  옮긴 뒤에는 그것을 고르고 **조상을 펼쳐 보여 준다**(접힌 부모 안으로 넣으면 사라진 것처럼 보였다).
  `EditorApplicationTests` 가 실제 마우스 메시지로 세 손짓을 모두 재고, 명령 쪽 계약은 `EditorObjectCommandTests` 가 잰다.

- **D-129. 도크 노드가 스스로 그리는 닫기·창 메뉴 단추를 끈다.** (2026-09-21, `ProjectRule.md` §11.3)
  사용자가 "우측 상단 X 표시도 원래 없어야 한다" 고 짚었다. 탭마다 붙는 X 와 별개로, ImGui 는 도크 노드
  오른쪽 위에 **그 칸의 창을 통째로 닫는 X** 를 그린다. 탭 하나를 닫으려다 그 칸을 전부 닫게 되는 단추다.
  기존 엔진은 `CImDockWindow` 의 기본값(`ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton`)
  으로 둘 다 꺼 두었고, 같은 값을 `DockSpace` 와 `DockBuilderAddNode` 에 넘긴다. 창마다 고르는 쪽은
  `EditorPanel::HasCloseButton` 이고 그대로다.

- **D-130. 뷰마다 타깃을 가질 수 있게 하고, 그 위에 캔버스 뷰를 세웠다.**
  (2026-09-21, 계획서 §3 단계 5)
  사용자가 "게임 뷰가 왜 기즈모가 뜨고 에디팅이 되느냐, 게임 뷰는 시뮬레이션 뷰이고 유니티의 씬 뷰에 해당하는
  것은 기존 엔진에서 캔버스 뷰였다" 고 짚었다. 맞다 - 기존 `CGameViewTool`(210줄)은 그림을 붙이고 상태만 얹고,
  편집은 전부 `CCanvasViewTool`(1,645줄)의 일이었다. 우리는 캔버스 뷰가 **아예 없어서** 편집을 게임 화면 위에
  얹고 있었다.
  **막고 있던 것은 "한 프레임에 타깃이 하나" 였다.** `Renderer::BeginFrame(FrameTarget)` 이 프레임 전체의
  타깃을 정해 모든 뷰가 거기로 갔다. `CameraParams` 에 `target`·`targetExtent` 를 두어 **뷰마다** 갈 곳을
  고르게 했다. 지우는 것은 "첫 뷰" 가 아니라 **"그 타깃의 첫 뷰"** 이고, 그 목록은 고정 길이 여덟이다 -
  이 경로는 힙 할당이 금지되어 있고(`RendererContractTests` 가 잰다) 처음엔 `Array` 로 썼다가 그 테스트에
  잡혔다. `GetLastViewCamera` 와 게임 뷰의 매 프레임 opt-in(D-63)은 **자기 타깃을 든 뷰를 건너뛴다** -
  둘 다 "게임이 보는 화면" 에 대한 것이라, 게임 뷰를 닫아 두었다고 편집 화면까지 멈추면 안 된다.
  그 위에 `IFramework::RenderEditorView(EditorViewDesc)` 를 두었다. 2D 는 **같은 프레임에 모아 둔 그릴
  것을 그대로** 편집 카메라로 한 번 더 낸다 - 게임 카메라를 빌리지 않으므로 카메라가 하나도 없는 캔버스도 보인다.
  `CanvasViewPanel` 은 팬(오른쪽·가운데 끌기)·줌(마우스 아래 점을 붙잡는다)·격자(배율에 따라 1·2·5·10 으로
  올라가고 열 칸마다 진하다)·원점 두 축·선택 테두리(주된 것이 더 밝다)·피킹(작은 것이 이긴다)·기즈모·
  맥락 메뉴·`맞춤`(고른 것, 없으면 캔버스 전체)을 가진다. 텍스처는 **패널 크기**로 잡되 64 의 배수로
  올려 잡아 패널을 끌 때마다 다시 만들지 않는다.
  `[열림]` 3D 의 캔버스 뷰는 비어 있다(기본 구현이 아무것도 하지 않는다). 기존 엔진이 2D 전용이었으므로
  이식이 덜 된 것이 아니라 아직 없는 것이다. 상자 선택과 폴리곤 버텍스 편집도 남았다(계획서 §4).

- **D-131. 게임 뷰는 시뮬레이션 화면이고, 재생·정지는 재생 전의 캔버스로 되돌린다.**
  (2026-09-21, 계획서 §3 단계 3·4)
  D-130 으로 편집할 자리가 생겼으므로 게임 뷰에서 기즈모와 편집을 뗐다. 남은 것은 그림·레터박스·상태 표시
  (`실행 중`/`정지됨`/`열린 캔버스가 없습니다`/`카메라가 없습니다`)이고, 기존 `CGameViewTool` 과 같다.
  **시뮬레이션 개념 자체가 없었다.** `EngineInstance::Tick` 이 언제나 게임을 돌렸고, 그래서 에디터 화면이 곧
  돌아가는 게임이었다 - 방금 놓은 값이 다음 프레임에 스크립트에 덮어써지는 자리다. 이제 에디터는 **멈춘 채로
  뜨고** F5 로 재생·정지, F6 으로 일시정지한다(기존 엔진과 같은 키).
  **멈춘다는 것은 `Update` 를 건너뛰는 것이 아니다.** 처음엔 그렇게 했다가 게임 화면이 통째로 빈 화면이 됐다 -
  그릴 목록은 `Update` 안의 추출이 만들기 때문이다. `IFramework::SetSimulationEnabled` 가 **스크립트·물리·
  네트워크 시스템만** 세우고(`GameSystem::SetEnabled`), 트랜스폼·카메라·스프라이트 추출은 그대로 돈다.
  고정 스텝은 돌지 않고 `Update` 의 dt 는 0 이다.
  **되살릴 값을 먼저 뜨지 못하면 재생하지 않는다**(§11.5). 재생은 캔버스를 통째로 글자로 뜨고
  (`WriteCanvasText`), 정지는 캔버스를 비운 뒤 그 글자를 다시 읽는다. 오브젝트가 새로 만들어지므로 선택·
  오브젝트 번호·되돌리기 스택을 함께 비운다 - 그러지 않으면 Ctrl+Z 가 이제 없는 오브젝트를 가리킨다.

- **D-132. 단축키와 오브젝트 동작을 각각 한 벌로 모았다.** (2026-09-21, 계획서 §3 단계 8·9)
  사용자가 "에디터 관련 유틸 함수들 전부 함수화해서 재사용해, 일관성 있게" 라고 했다. 두 자리가 갈려 있었다.
  **(1) 단축키.** 누르는 자리(`EditorApplication` 안에 흩어진 `ImGui::Shortcut`)와 메뉴에 보이는 글자
  (`"Ctrl+Z"` 를 손으로 적었다)와 할 수 있는지 재는 자리가 셋으로 갈려 있었다. `EditorShortcuts` 가 표 하나로
  들고, 메뉴 항목은 그 표에서 이름·조합키 글자·회색 여부를 받는다. 기본 조합은 기존 엔진 그대로다
  (F5·F6 포함). `Describe` 는 키 이름을 ImGui 에게 묻는다 - 우리가 표를 또 만들면 둘이 갈린다.
  **`ShortcutPanel`** 이 같은 표를 무리별로 늘어놓는다(기존 `CShortcutReferenceTool`). 손으로 적은 목록을
  따로 두면 키를 바꿨을 때 그 창만 옛 글자로 남아 없느니만 못하다.
  **(2) 오브젝트 동작.** 오브젝트 추가·복사·붙여넣기·삭제·부모 해제를 계층 패널이 자기 안에 전부 들고
  있었고, 캔버스 뷰를 만들며 한 벌을 더 쓸 뻔했다. `EditorActions`(기존 `EditorGuiActions` 자리)가 그 한 벌이고
  **회색으로 보이는 규칙까지** 거기 있다. 빈자리 메뉴는 계층과 캔버스 뷰가 같은 것을 쓴다.
  곁들여 `Widget::MouseWasDragged`(뗄 때 고르는 자리가 묻는다)와 `Widget::ToolBarSeparator` 를 공용 위젯으로
  올렸다 - 둘 다 두 군데 이상에서 같은 값으로 쓰던 것이다.

- **D-133. 엔진이 하는 말을 고리 버퍼에 남기고 로그 창으로 보인다.** (2026-09-21, 계획서 §3 단계 10)
  지금까지 알림과 경고는 `std::printf` 로 콘솔에 갔다. **창으로 띄운 에디터에는 그 콘솔이 없다** - 에셋을
  못 읽었다거나 감시가 서지 않았다는 말이 그대로 사라졌고, 사용자는 왜 안 되는지를 화면 어디에서도 볼 수 없었다.
  기존 엔진에는 `CLogTool` 과 `CLogger` 가 있었으므로 이식이 덜 된 자리다.
  `JBro/Core/Log.h` 는 **고정 512줄 고리 버퍼 하나**다(오래된 것부터 밀린다). 등급·갈래·일련번호·판번호를 들고,
  콘솔로도 내보낼지는 켜고 끌 수 있다(테스트가 조용히 돌리는 자리다). 잘린 줄은 `...` 로 끝난다 - 조용히
  자르면 뒷말이 없어진 줄을 원문으로 읽게 된다. **메인 스레드 전용**이고 잠금을 두지 않는다.
  `LogPanel` 은 등급 토글·검색·지우기·자동 스크롤을 가지며, 판번호가 그대로면 스크롤을 건드리지 않는다 -
  매 프레임 바닥으로 끌면 위로 올려 읽을 수 없다. 엔진의 `note:`/`warning:` 열네 자리를 이 길로 옮겼다.

- **D-134. 도크는 두 겹이고, 메뉴도 그 결을 따라 나뉜다.** (2026-09-21, 계획서 §3 단계 6·7)
  사용자가 "기존 엔진의 루트독-메인독 구조를 안 띠고, 메뉴도 제대로 이식하지 않았다" 고 짚었다.
  우리는 도크 공간 하나에 모든 패널을 붙이고 메뉴 막대 하나에 전부 늘어놓았다.
  이제 **도크 뿌리**(`CRootDockWindow` 자리)가 창 전체를 덮고 그 안에 **메인 도크** 하나만 붙는다.
  도구 창은 메인 도크의 도크 공간에 붙는다. 뿌리 노드에는 이름표(`ImGuiWindowClass`)를 달아
  도구 창이 거기 붙지 못하게 한다 - 붙으면 메뉴 막대와 나란히 서고 다시 넣을 방법이 화면에 없다.
  **안쪽 도크에는 이름표를 달지 않았다.** 달았더니 배치를 잡는 첫 프레임에 패널들이 전부 떠 있는
  창이 됐다 - 패널의 이름표는 그 패널이 처음 그려질 때에야 정해지는데, 노드는 그 전에 이름표가
  다른 창을 뱉어 낸다. 바깥만 막아도 목적은 같다.
  **배치는 도크 공간을 내기 전에 잡는다.** 내고 나서 잡으면 그 프레임의 노드가 이미 빈 채로 굳어,
  패널이 떠 있는 창으로 서고 다음 프레임부터 그 자리를 자기 자리로 기억한다. 전에는 도크가 한 겹이라
  드러나지 않던 순서 문제다.
  메뉴는 기존 엔진과 같은 결로 나뉜다: **프로젝트에 대한 것**(프로젝트 열기·캔버스 저장·종료,
  오른쪽 끝의 `저장되지 않음`)은 뿌리, **지금 연 캔버스에 대한 것**(시뮬레이션·편집·창)은 메인 도크다.
  `창 > 에디터` 는 기존처럼 한 겹 더 들어간다.
  `프로젝트 열기` 는 저장과 같은 길이다 - **막히는 대화상자라 프레임 밖에서** 하고(D-93), 열기 전에
  고른 것·오브젝트 번호·되돌리기·시뮬레이션을 먼저 놓는다.
  `[열림]` 새 프로젝트·빌드·설정·디버그 메뉴와 임포터 창은 그 기능 자체가 없어 넣지 않았다.
  `[열림]` 메인 도크의 수명은 기존과 다르다 - 기존은 프로젝트를 열어야 생겼고 우리는 늘 있다(계획서 §3-6).

- **D-135. 레이어를 계층 창에 들였다.** (2026-09-21, 계획서 §3 단계 10-1)
  기존 엔진과 나란히 놓다가 찾았다(§11.4). **엔진에는 레이어가 있고**(`Canvas::CreateLayer`·
  `MoveLayer`·`SetObjectLayer`, `Layer::IsVisible`) **`.jcanvas` 도 레이어를 적는데, 에디터에는
  레이어를 다루는 길이 하나도 없었다** - 만들 수도, 이름을 바꿀 수도, 숨길 수도, 오브젝트를
  옮길 수도 없었다. 기존 엔진 `CLayerTool` 이 계층 창에 두었으므로 같은 자리에 둔다.
  계층 창은 이제 **레이어를 머리로 두고 그 아래에 뿌리 오브젝트**를 묶는다. **위가 앞**이다 -
  캔버스가 든 차례는 0 이 맨 뒤이므로 역순으로 그린다(포토샵과 같은 쪽이다). 줄 오른쪽 끝에
  눈 표시가 있고, 레이어 줄을 끌면 합성 차례가, 오브젝트를 레이어 줄에 떨어뜨리면 그 레이어로 간다.
  **오브젝트를 레이어에 놓는 것은 두 가지 일을 한 손짓으로 한다** - 뿌리로 올리고 레이어를 바꾼다.
  자식인 채로 레이어만 바꾸면 부모와 다른 칸에 놓여 화면에서 사라진 것처럼 되므로,
  `CompoundCommand` 로 묶어 되돌리기 한 번이 둘 다 되돌린다. 레이어는 **부분 트리 전체**가
  함께 간다(기존 엔진의 "자식은 부모 레이어" 불변식).
  커맨드는 여섯이다: 만들기·지우기·자리·이름·보임·오브젝트 옮기기. **레이어 아이디는 되살아나지
  않는다** - `CreateLayer` 가 늘 새 번호를 주므로, 지운 레이어를 되돌리면 같은 이름·자리·
  오브젝트를 가진 새 번호의 레이어가 선다. 화면에서 보이는 것은 같고, 번호를 들고 있던
  오브젝트는 그 커맨드가 다시 잇는다. 마지막 한 장은 지우지 못한다.
  곁들여 찾은 것: `Widget::TreeBegin` 이 연 마디는 **열렸으면 늘 닫아야** 한다 - 레이어가
  그 사이에 지워져도 ImGui 의 짝은 맞춰야 하고, 안 맞으면 그 뒤가 한 칸씩 들여써진다.

- **D-136. 캔버스 뷰에 상자 선택을 넣고, 3D 에서는 궤도 카메라로 세웠다.** (2026-09-21, 계획서 §4)
  기존 엔진 `CCanvasViewTool` 에 있는데 우리에게 없던 둘이다.
  **상자 선택**: 빈 곳에서 끌면 상자가 따라오고, 놓으면 그 상자에 **닿은** 것을 모두 고른다
  (완전히 들어온 것만 고르는 쪽보다 2D 편집에서 손에 붙고, 기존도 겹침 기준이다).
  **끌기 임계값을 넘어야 시작한다** - 넘기 전에 시작하면 그냥 클릭한 것도 빈 상자가 되어,
  무언가를 고르려던 손짓이 선택을 푸는 손짓이 된다. 누르기 시작한 자리에 무언가 있었으면
  그것을 끌려던 것이므로 상자를 만들지 않는다.
  **3D 의 캔버스 뷰**: `Framework3D::RenderEditorView` 가 섰다. 평면을 밀고 당기는 것으로는
  3D 의 뒤를 볼 수 없으므로 **바라보는 점 둘레를 도는** 궤도 카메라다 - 오른쪽 끌기가 각을,
  휠이 거리를 바꾸고, 투영은 원근이다(깊이가 보여야 한다). 세로 각은 수직을 넘지 않는다.
  `[열림]` **3D 의 기즈모·격자·선택 윤곽**은 넣지 않았다. 화면 좌표와 월드를 잇는 것이 2D 처럼
  나눗셈 하나가 아니라 이번 프레임의 뷰-투영이 필요한데, 렌더러가 **편집 카메라를 내주는 길이
  없다**. 여기서 같은 행렬을 한 번 더 세우면 엔진이 세우는 것과 둘로 갈려, 한쪽만 고쳐졌을 때
  손잡이가 그림과 다른 자리에 선다 - 그래서 짐작으로 세우지 않고 열어 둔다.

- **D-137. 프로젝트 설정 창을 세우면서 `.jproject` 에 쓰는 길을 만들었다. 쓰기는 원문을 타고 간다.**
  (2026-09-21, 계획서 §3 단계 11-1)
  기존 엔진 `CProjectSettingsWindow` 자리인데 우리에겐 **읽기만 있고 쓰기가 아예 없었다**.
  값을 보여 주기만 하는 창은 없느니만 못하므로 `WriteProjectFileText`·`SaveProjectFile` 을 먼저 두었다.
  **통째로 다시 쓰지 않는다.** 파서는 모르는 키와 그 블록을 조용히 건너뛰도록 되어 있어,
  읽고 다시 쓰는 것만으로 **남의 설정이 사라진다**. 에셋 메타에서 이미 한 번 겪은 자리다(D-123·D-124).
  그래서 원문의 줄을 타고 가며 **아는 키의 값만** 바꾼다 - 주석도, 모르는 키도, 시퀀스도 제자리다.
  아는 키가 원문에 없으면 뒤에 더하고, `Build:` 아래의 키는 그 블록 끝에 넣는다. 시퀀스 키
  (`AssetIgnorePatterns`·`BuildCanvases`)는 손대지 않는다 - 값이 여러 줄이라 한 줄 바꿔치기로 다룰 수 없다.
  **쓴 것을 도로 읽어 본 뒤에 파일로 보낸다.** 읽히지 않는 글자를 남기면 그 프로젝트는 다음에
  열리지 않는다. 파일에 가는 것은 `<파일>.tmp` 에 쓰고 옮기는 바꿔치기다(D-124).
  창은 **저장을 누르기 전까지 파일에 손대지 않는다**. 되돌리기 스택에는 올리지 않는다 - 에디터의
  Ctrl+Z 는 씬 편집을 되돌리는 것이고, 프로젝트 설정은 파일에 적히는 순간 그 파일이 정본이다.
  엔진 판과 차원은 보여만 준다(런처가 고르는 값이고, 차원은 열린 프레임워크와 어긋나게 할 수 없다).
  `[열림]` 저장한 값 중 **지금 적용되는 것은 에셋의 기본 샘플러뿐이다.** 해상도·에셋 폴더·스크립트
  경로는 프로젝트를 열 때 한 번 읽는 값이라 다시 열어야 반영된다. 몰래 다시 적용하면 열려 있는
  캔버스와 파일이 반쯤 다른 프로젝트가 된다.

- **D-138. 프레임을 구간으로 나눠 재고 그것을 창으로 보인다.** (2026-09-21, 계획서 §3 단계 11-3)
  기존 엔진에는 `CCpuProfilerWindow` 와 `Engine.CpuProfiler` 가 있었고 우리에겐 **계측 자체가 없었다**.
  통계 창은 프레임 시간 하나만 말하는데, 느려졌을 때 **어디가** 느린지는 그 숫자로 알 수 없다.
  `JBro/Core/Profiler.h` 는 고정 칸(구간 128, 겹 16)이다. **매 프레임 도는 경로라 힙 할당도
  문자열 비교도 하지 않는다**(§7) - 이름은 리터럴만 받아 주소로 견주고, 같은 이름·같은 겹은
  한 줄로 합쳐 횟수를 센다. 겹이나 칸이 넘치면 그 구간은 세지 않는다(재려다 프레임을 늘리지 않는다).
  **짝은 넘쳐도 맞춘다** - 어긋나면 그 뒤의 모든 구간이 엉뚱한 겹에 들어간다.
  쌓는 자리와 읽는 자리를 나눈다. 창이 반쯤 찬 목록을 그리지 않는다.
  엔진의 프레임을 `Platform`·`Network`·`Update`(안에 `FixedSteps`·`Systems`)·`Submit`·`Render` 로 나눴다.
  `ProfilerPanel` 은 **창이 열려 있을 때만 켠다** - 재는 것 자체가 프레임에 얹히는 일이다.
  화면을 보다 고친 것: 숫자를 누를 때 **첫 값은 누르지 않는다.** 0 에서 눌러 가면 처음 열 프레임 동안
  프레임 시간이 실제보다 작게 나오고, 구간의 비중이 100 을 훌쩍 넘어 보인다(실제로 `Frame 192.9%` 가 나왔다).
  `[열림]` GPU 프로파일러는 RHI 에 타임스탬프 질의가 없어 세우지 않았다.

- **D-139. 에셋 브라우저가 파일을 다룬다. `.jmeta` 는 늘 함께 움직인다.** (2026-09-21, 계획서 §3 단계 12)
  기존 `CAssetBrowserTool`(2,652줄)에 견줘 우리 것(210줄)은 **보여 주기만** 했다 - 폴더를 만들 수도,
  이름을 바꿀 수도, 옮길 수도, 지울 수도 없었다.
  먼저 플랫폼에 없던 것을 세웠다: `CreateDirectoryAt`·`DeleteFileAt`·`DeleteDirectoryAt`·
  `RevealInFileBrowser`. 폴더 만들기는 **이미 있으면 참**이다 - 부르는 쪽이 "있는가" 를 먼저 묻고
  만들면 그 사이에 생긴 경우를 다루지 못한다. 파일 지우기는 없으면 **거짓**이다 - "지웠다" 와
  "없었다" 를 같게 보면 부르는 쪽이 지워졌다고 믿는다.
  그 위에 `EditorApplication` 의 `CreateAssetFolder`·`RenameAsset`·`MoveAsset`·`DeleteAsset`·
  `RevealAsset`·`RescanAssets` 가 선다. **`.jmeta` 가 늘 함께 간다** - 짝을 잃으면 그 에셋의 아이디가
  사라지고 그것을 가리키던 컴포넌트의 참조가 전부 풀린다(D-111). **덮어쓰지 않는다**: 같은 이름이
  이미 있으면 거절한다. 어느 것이든 성공하면 레지스트리를 다시 훑고 캔버스의 참조를 다시 잇는다.
  화면은 **두 칸**이다(기존과 같다): 왼쪽 폴더 나무, 오른쪽 그 폴더의 내용. 한 나무에 전부 펼치면
  폴더가 깊어질수록 파일이 오른쪽으로 밀려 이름이 보이지 않는다. 길잡이 줄, 검색(찾는 중에는
  폴더를 가리지 않는다), 파일을 끌어 폴더에 놓기(자기 안으로는 못 들어간다), 삭제 확인이 있다.
  꾸러미에는 레코드 포인터가 아니라 **상대경로**를 담는다 - 포인터는 다시 훑으면 다른 것을 가리킨다.
  다중 선택은 D-141 에서, 아이콘 보기는 D-147 에서 섰다.
- **D-140. 렌더러가 이번 프레임에 쓴 편집 카메라를 내준다. 3D 의 기즈모는 그것으로 선다.**
  (2026-09-21, 계획서 §4)
  3D 의 캔버스 뷰(D-136)는 그림만 있고 손잡이가 없었다. 화면 좌표와 월드를 잇는 것이 2D 처럼
  나눗셈 하나가 아니라 이번 프레임의 뷰·투영 행렬이라서다. 에디터가 같은 궤도 행렬을 한 번 더
  세우는 길도 있었지만, **같은 값을 두 곳에서 세우면 한쪽만 고쳐졌을 때 손잡이가 그림과 다른
  자리에 선다** - 그래서 그린 쪽이 쓴 것을 그대로 내주기로 했다.
  `Renderer::GetLastEditorViewCamera` 다. `GetLastViewCamera` 가 게임 카메라를 내주듯 이쪽은
  **자기 타깃을 든 뷰**의 카메라를 내준다(D-130 이 나눈 그 기준이다).
  기록 조건은 게임 쪽과 다르다: 게임 뷰가 닫혀 `FrameTarget::recordViews` 가 꺼진 프레임에도
  편집 화면은 그려지므로(`RecordViews` 가 자기 타깃을 든 뷰는 그 깃발과 무관하게 통과시킨다),
  편집 카메라도 그 깃발을 보지 않는다. 보지 않게 하지 않으면 게임 뷰를 닫는 순간 손잡이가 사라진다.
  `CanvasViewPanel::MakeGizmoCamera` 가 2D 와 3D 를 한 자리에서 가른다: 2D 는 우리가 카메라를
  다 아니 직교 행렬을 세우고, 3D 는 렌더러가 내준 것을 쓴다. NDC 가 앉는 사각형은 패널이 아니라
  **편집 화면 텍스처의 크기**다 - 텍스처는 64 의 배수로 잡고 패널 크기만큼 잘라 붙이므로,
  그 왼쪽 위가 그림의 왼쪽 위와 같다.
  고르기와 선택 표시도 이 카메라를 거친다. 다만 3D 는 **화면에서 가까운 것**을 고르고
  점 하나로 표시한다 - 메시의 실제 크기를 에디터가 알 길이 아직 없어서, 짐작한 상자로 두르면
  맞지 않는 테두리가 된다. 맞지 않는 테두리는 없는 것보다 나쁘다.
  같은 카메라로 **바닥 격자**도 깐다(y=0 평면). 카메라의 높이에 맞춰 띄우면 무엇이 바닥인지
  알 수 없어 오브젝트를 놓을 기준이 사라진다. 간격은 2D 와 같은 눈금(1·2·5·10 …)을 쓰되
  배율은 **바라보는 점에서 한 단위가 몇 픽셀로 보이는지**로 잰다 - 원근에서는 거리가 배율이다.
  선은 토막 내어 잇는다: 양 끝만 투영하면 선이 카메라 평면을 가로지를 때 한쪽 끝이 뒤에 있어
  선 전체가 사라진다. 평면은 지평선까지 이어지지만 바라보는 점 둘레의 스무 칸만 그린다 -
  먼 쪽까지 그으면 한 덩어리로 뭉쳐 잡음이 된다.
  `[열림]` 진짜 선택 윤곽은 메시의 경계 상자를 에디터가 얻는 길이 먼저 있어야 한다.
- **D-141. 에셋 브라우저에서 파일 여러 개를 고른다.** (2026-09-21, 계획서 §3 단계 12)
  기존 `CAssetBrowserTool` 은 여러 파일을 골라 한 번에 옮기고 지웠는데 우리 것은 하나씩이었다.
  파일을 백 개 정리하는 일은 하나씩 하는 일이 아니다.
  고른 것은 **상대경로**로 기억한다 - 레코드 포인터는 다시 훑으면 다른 것을 가리킨다(D-139 와 같은 이유).
  Ctrl 은 하나를 더하고 빼며, Shift 는 닻에서 누른 줄까지다. 범위는 **이번 프레임에 그려진 차례**로
  센다 - 레지스트리의 차례로 세면 걸러 낸 줄이 사이에 끼어 사람이 고른 것과 결과가 갈린다.
  닻이 화면에 없으면(폴더를 옮겼거나 걸러졌다) 누른 것 하나만 고른다.
  **오른쪽 누름은 고른 것을 뒤엎지 않는다.** 여럿을 골라 놓고 그중 하나에 대고 메뉴를 열었을 때
  선택이 하나로 줄면, 여럿에 하려던 일이 하나에만 간다. 고르지 않은 줄에 대고 열면 그것만 고른다.
  끌기는 고른 것을 줄바꿈으로 이어 한 꾸러미에 담는다. 한도(8 KiB)를 넘으면 여럿을 담지 않고
  끄는 것 하나만 담는다 - 잘린 묶음을 놓으면 일부만 움직이고 어디까지 갔는지 화면에 없다.
  삭제는 무엇이 사라지는지 한 줄씩 보여 주고 묻는다. 개수만으로는 잘못 고른 것을 알 수 없다.
  지우는 도중에 레지스트리가 다시 훑이므로 고른 것을 먼저 베껴 두고 그 사본으로 돈다.
  폴더는 하나씩만 고른다 - 폴더와 파일을 섞으면 "이 폴더와 그 안의 것" 이 무엇을 뜻하는지 흐려진다.
  인스펙터는 마지막에 누른 하나를 본다. 여럿의 임포트 옵션을 겹쳐 보이면 어느 값이 어느 파일의
  것인지 알 수 없다.
  `[열림]` 아이콘 보기는 남았다. 텍스처 썸네일을 에디터가 얻는 길이 먼저 있어야 한다.
- **D-142. 인스펙터의 머리가 이름을 고치고, 활성과 사용은 커맨드를 거친다.** (2026-09-21, 계획서 §4)
  기존 인스펙터(3,068줄)와 우리 것(1,183줄)의 차이를 재다 세 가지가 드러났다.
  **오브젝트의 이름을 고칠 길이 에디터에 하나도 없었다.** 만든 오브젝트는 `GameObject` 라는 이름으로
  굳었고 계층 창에도 인스펙터에도 고치는 자리가 없었다. 기존 엔진은 인스펙터 머리에 이름 칸을 둔다
  (계층 창에는 없다 - 그쪽도 인스펙터에서만 고친다). 이름은 태그다(D-51).
  **활성과 사용 토글이 커맨드가 아니었다.** 인스펙터가 `SetActive` · `SetEnabled` 를 그대로 불러
  되돌릴 수 없었다 - 에디터의 편집은 커맨드로만 한다(§11.5).
  활성은 **고른 것이 다 따라간다**. 여럿을 골라 놓고 하나만 꺼지면 나머지는 화면에서 그대로라
  무엇이 바뀌었는지 알 수 없다. 되돌릴 값은 오브젝트마다 따로 뜬다 - 여럿을 골랐을 때 하나는
  켜져 있고 하나는 꺼져 있을 수 있고, 되돌리면 각자 제 값으로 가야 한다.
  이름은 **편집이 끝날 때 한 번** 커맨드가 된다. 커맨드 병합은 마우스를 누른 채일 때만 일어나
  타이핑에는 걸리지 않으므로, 글자마다 커맨드를 만들면 되돌리기가 글자 수만큼 필요해진다.
  그 길로 공용 위젯에 `TextField::CommitOnFinish` 가 섰다: 글자는 치는 대로 들어가되 참을
  돌려주는 시점만 편집이 끝나는 프레임으로 미룬다. 칸은 **치는 중이 아니면 늘 오브젝트의
  이름을 다시 든다** - 고른 것이 바뀔 때만 읽으면 되돌린 뒤에도 옛 글자가 남는다.
  `[열림]` 기존 인스펙터에 있고 우리에게 없는 나머지:
  **캔버스·레이어 인스펙터**(우리 `Layer` 는 이름·순서·보임뿐이라 보여 줄 것이 계층 창과 겹친다.
  기존의 합성 속성 - 공간·배율 방식·시차·자기 텍스처 - 은 엔진에 없다),
  **물리·카메라 디버그 표시**(접촉점·강체 상태·콜라이더 모양을 인스펙터에 그린다),
  **애니메이션 클립 편집기**와 오디오·재질·폰트 임포트 옵션(그 에셋 타입이 없다),
  **인스펙터의 필드 편집을 고른 것 전체에 델타로**(기즈모 쪽은 이미 그렇게 한다 - D-109).
- **D-143. 캔버스 뷰가 콜라이더의 모양을 그린다.** (2026-09-21, 계획서 §4)
  기존 엔진의 캔버스 뷰는 캔버스 안의 콜라이더를 모두 그렸다(서클·폴리곤, 선택한 것은 두껍게).
  우리 것은 그리지 않았다. **물리는 눈에 보이지 않아서**, 충돌 칸이 그림과 어긋나 있어도
  화면에 아무 표시가 없고 부딪혀 보고 나서야 안다.
  `Component::Collider2D` 의 `shape` · `offset` · `size` · `radius` 를 월드 트랜스폼에 얹어 그린다.
  상자는 **돌면 기울어진다** - 외접 사각형으로 그리면 돌려 놓은 오브젝트의 충돌 칸이 실제보다
  커 보인다. 원은 한 축으로만 커져도 원으로 남으므로(물리가 그렇게 다룬다) **더 큰 쪽**으로 잰다.
  트리거는 색을 달리한다 - 막는 것이 아니라 알리는 것이라, 같은 색이면 왜 통과하는지 알 수 없다.
  툴바에 **보임 토글**을 두었다(기존은 늘 그렸다). 늘 그리면 그림을 다듬는 동안 녹색 선이
  방해가 된다. 3D 에는 그 단추를 두지 않는다 - 그릴 콜라이더가 없는데 단추가 있으면
  무엇이 되는 것인지 흐려진다.
  테스트가 켠 프레임과 끈 프레임의 백버퍼를 견줘 500 픽셀이 달라지는 것을 확인한다.
- **D-144. 격자의 눈금에 숫자가 붙는다.** (2026-09-21, 계획서 §4)
  기존 캔버스 뷰는 격자 선마다 좌표를 적었다. 우리 것은 선만 있어 몇 번째 칸인지 세어야 했고,
  배율이 바뀌면 그 셈이 다시 시작됐다. X 는 아래, Y 는 왼쪽 - 기존과 같은 자리다.
  숫자끼리 겹치면 건너뛴다. 겹친 숫자는 둘 다 못 읽는다. 닿을 듯 말 듯 붙은 것도 읽기 어려워
  X 는 24 픽셀, Y 는 글자 높이의 두 배쯤 띄운다.
  `[열림]` 기존의 **픽셀 표시 토글**(유닛 대신 픽셀로 적기)은 넣지 않았다. 픽셀로 적으려면
  한 유닛이 몇 픽셀인지 알아야 하는데, 우리는 그 값이 **스프라이트마다** 있고(에셋의
  `pixelsPerUnit`) 프로젝트 전체를 대표하는 값이 없다. 없는 값을 지어내 적으면 그 숫자를
  믿고 그린 그림이 게임에서 다른 크기로 나온다.
- **D-145. 통계 창이 캔버스의 쓰임새를 보인다. 캔버스가 풀의 쓰임새를 내준다.**
  (2026-09-21, 계획서 §4)
  기존 엔진의 CPU 프로파일러는 프레임 시간 말고도 오브젝트 수·풀 사용량·선택 수·되돌리기
  상태를 냈다. 우리 통계 창에는 프레임 시간과 렌더 통계뿐이었다.
  그 숫자를 내려면 캔버스가 먼저 말해 주어야 해서 `Canvas::ForEachComponentPool` 과
  `GetObjectPoolUsage` 를 더했다. **풀이 언제 늘어났는지는 화면에 나오지 않으면 알 수 없고**,
  늘어나는 그 순간이 프레임을 늘어지게 만드는 자리다.
  만들어지지 않은 풀은 들르지 않는다 - 붙인 적 없는 타입을 0 으로 늘어놓으면 목록이
  타입 목록이 되지 쓰임새가 아니다. 풀의 자리는 오브젝트가 떠나도 **줄지 않는다**(풀의 성질이고,
  그래서 "몇 / 몇" 이 뜻이 있다).
  창에는 오브젝트 수와 풀 크기, 레이어 수, 고른 것의 수, 되돌리기·다시 실행의 수,
  저장되지 않은 변경이 있는지가 줄로 선다.
  `[열림]` 기존의 **스크립트 풀·코루틴** 항목은 그 기능이 아직 없어 두지 않았다.
- **D-146. 에디터 세션이 프로젝트 파일에 남는다. 에디터 언어를 고를 수 있다.**
  (2026-09-21, 계획서 §4)
  기존 엔진은 보던 캔버스·편집 카메라·에디터 언어를 프로젝트에 적었고(`EditorSessionPersistence`),
  다시 열면 그 자리에서 이어서 일할 수 있었다. 우리는 `LastOpenedCanvasPath` 키가 **있기만 하고
  아무도 쓰지 않았다** - 프로젝트를 다시 열면 늘 빈 캔버스였고, 언어를 바꿀 길도 없었다.
  `.jproject` 에 `EditorLocale` · `CanvasViewCameraX` · `CanvasViewCameraY` · `CanvasViewCameraSize`
  가 섰다. 값이 사는 곳을 프로젝트로 잡은 것은 기존과 같은 이유다: 사람마다 다른 값이지만
  프로젝트를 옮겨 다니며 쓰는 것이 한 사람이고, 따로 두면 그 파일의 자리를 또 정해야 한다.
  캔버스 경로는 **에셋 폴더 기준 상대경로에 슬래시**로 적는다 - 절대경로는 프로젝트를 옮기면
  가리키는 곳이 없고, 역슬래시는 다른 플랫폼에서 구분자가 아니라 글자다.
  보던 캔버스가 없거나 깨졌으면 **로그만 남기고 빈 캔버스로 연다** - 지워진 파일 하나 때문에
  프로젝트가 열리지 않으면 고칠 방법도 없다.
  카메라는 패널이 만들어질 때 가져간다. 프로젝트를 여는 시점에는 패널이 아직 없을 수 있다.
  `size` 가 0 이면 적힌 적이 없다는 뜻이다(화면 세로 절반이 담는 월드 길이라 0 일 수 없다).
  언어는 설정 창의 목록에서 고르고 **고르는 즉시 바뀐다** - 글자가 바뀌는 것을 보고 고르는
  일이라 저장한 뒤에야 바뀌면 무엇을 고른 것인지 알 수 없다. 목록은 글자 표 폴더의
  `<로케일>.yaml` 이 그대로 된다. 읽지 못하면 지금 언어를 지킨다 - 표가 반쯤 바뀌면
  화면의 절반이 키로 나온다.
  **창 배치**는 `<프로젝트파일>.layout.ini` 에 ImGui 의 형식 그대로 적는다. `.jproject` 안에
  넣으면(기존이 그랬다) 여러 줄짜리 덩어리가 YAML 한가운데 앉아 손으로 고치기 어려워진다.
  읽은 배치가 **기본 배치를 이긴다**: 첫 프레임의 `DockBuilder`(D-134)는 노드를 지우고 다시
  만들므로, 둘 다 돌면 사람이 옮겨 둔 자리가 매 실행 지워진다. 적힌 것을 읽었으면
  그 프레임의 기본 배치는 건너뛴다. 뮤테이션으로 확인했다 - 읽는 줄을 빼면 테스트가 죽는다.
- **D-147. 에디터가 에셋의 작은 그림을 만들어 든다. 아이콘 보기와 인스펙터 미리보기가 선다.**
  (2026-09-21, 계획서 §3 단계 12, §4)
  전에는 "텍스처 썸네일을 에디터가 얻는 길이 없다" 로 미뤄 두었다(D-139). 다시 재어 보니
  길이 없는 것이 아니라 **에디터가 자기 것을 만들지 않았을 뿐**이었다. 에셋은 CPU 픽셀을
  들고 있고(`TextureData::pixels`), 에디터는 RHI 장치를 들고 있고, UI 는 **RHI 프레임 밖**에서
  만들어진다(`EditorApplication::Tick`) - 텍스처를 올릴 수 있는 자리가 이미 있었다.
  스프라이트를 그리는 쪽이 드는 렌더러 핸들은 ImGui 가 받는 것이 아니라 쓸 수 없다.
  `EditorThumbnails` 다. **줄여서 든다**: 긴 변이 128 을 넘으면 정수 배로 건너뛴다.
  가중 평균이 아니라 건너뛰기인 것은 픽셀 아트의 또렷한 가장자리가 평균에 뭉개지면
  그림을 알아보기 어려워지기 때문이다. 4096 짜리 스무 장을 원본으로 들면 목록을 여는 것만으로
  수백 MB 가 올라간다.
  **한 프레임에 네 개까지만 만든다.** 폴더를 열자마자 백 장을 올리면 그 프레임이 눈에 띄게
  멈춘다. 나머지는 다음 프레임에 채워지고, 기다리는 칸은 테두리만 그린다 - 빈 칸이 접히면
  목록이 프레임마다 움직인다. 실패도 기억한다: 기억하지 않으면 같은 파일을 프레임마다 다시 읽는다.
  에셋은 **잠깐만 든다**. 그림을 만들고 나면 픽셀은 GPU 에 있으므로 계속 잡고 있으면
  `CollectUnused` 가 영영 내리지 못한다. 제자리 재로드로 `pixelGeneration` 이 오르면 다시 만든다.
  그 위에 **아이콘 보기**(기존 `CAssetBrowserTool` 의 두 보기)와 **인스펙터 미리보기**
  (기존 `AssetInspectorPreview` 자리)가 섰다. 스프라이트 아이디를 물으면 짝 텍스처의 그림을 준다 -
  그림을 가진 것은 텍스처 쪽이고 목록에서 사람이 가리키는 것은 대개 스프라이트다.
  고르기·끌기·메뉴는 두 보기가 **같은 함수**를 쓴다. 보기를 바꿨다고 고르는 법이 달라지면
  같은 창이 둘로 나뉜다.
- **D-148. 캔버스 뷰가 집는 칸이 에셋이 정한 크기를 따른다.** (2026-09-21, 계획서 §4)
  `SpriteRenderer2D::sizeMode` 가 `FromSprite` 면 실제 크기는 스프라이트 칸의 픽셀을 그 에셋의
  PPU 로 나눈 값이고(D-117), 그리는 쪽도 그렇게 푼다. 그런데 에디터는 **선언된 `size`** 를 썼다 -
  기본값 1x1 이라, 2 픽셀짜리 그림을 100 배로 키운 오브젝트의 집는 칸이 100 유닛이었다.
  그림 밖 빈 곳을 눌러도 잡히고, 그림 가장자리를 눌러도 놓친다.
  전에는 "에디터가 그 결과를 볼 길이 없다" 로 적어 두었는데, 다시 재어 보니 `AssetSystem::GetSprite`
  하나면 되는 일이었다 - 해석된 핸들은 컴포넌트가 이미 들고 있다(D-115). 피벗도 같은 자리에서
  온다. 칸 번호가 넘치면 마지막 칸을 쓴다 - 그리는 쪽(`SpriteLibrary::Resolve`)과 같은 규칙이다.
  뮤테이션으로 확인했다: 에셋 크기를 쓰지 않게 하면 "그림 밖 두 유닛" 을 누른 테스트가 죽는다.
- **D-149. 선택 테두리가 스프라이트의 실제 모양을 두른다.** (2026-09-21, 계획서 §4)
  기존 엔진의 `CCanvasViewContour`(408줄) 자리다. 사각형으로만 두르면 그림이 칸의 한 귀퉁이에만
  있을 때 빈자리까지 테두리가 둘러쳐져, 무엇을 골랐는지 화면에서 알기 어렵다.
  `EditorSpriteContours` 는 **폴리곤을 잇지 않고 선분을 모은다.** 불투명한 픽셀의 네 변 중
  이웃이 투명한 쪽만 남기면 그것이 곧 경계다. 선분을 하나의 고리로 잇는 일은 구멍이 있거나
  그림이 여러 덩어리로 갈린 경우를 따로 다뤄야 하는데, 화면에 그리는 데에는 이을 필요가 없다.
  좌표는 **칸 안의 비율**이다 - 같은 그림을 쓰는 오브젝트가 저마다 다른 크기로 서기 때문에,
  크기·피벗·회전은 그리는 쪽이 얹는다.
  경계를 재는 해상도는 96 으로 막는다. 1024 짜리 그림의 픽셀 경계를 그대로 그리면 선분이
  수천 개가 되고 화면에서는 굵은 띠로 뭉친다. 알파 8 미만은 없는 것으로 본다 - 0 으로 두면
  눈에 보이지 않는 잔여 알파까지 경계가 되어 윤곽이 그림보다 한 겹 크게 나온다.
  칸이 통째로 투명하면 **칸의 네 변**을 준다. 아무것도 그리지 않으면 선택이 사라진 것처럼 보인다.
  캐시의 열쇠는 아이디가 아니라 **로드된 핸들**이다 - 부르는 쪽의 스프라이트가 이미 그 텍스처를
  참조 수로 잡고 있어, 아이디로 받으면 여기서 다시 로드해 참조 수를 올렸다 내려야 한다.

- **D-150. 캔버스 뷰의 겹쳐 그리기는 패널이 아니라 그린 화면의 크기로 센다.**
  (2026-09-21, 계획서 §4)
  **2D 캔버스 뷰의 겹쳐 그리기가 전부 조금씩 어긋나 있었다.** 격자도, 선택 테두리도, 기즈모도,
  집는 칸도.
  편집 화면 텍스처는 패널보다 크게 잡힌다(64 의 배수로 올림, `RequestCanvasView`). 엔진은
  **그 텍스처 전체**를 화면으로 보고 그리므로 월드 원점은 텍스처 한가운데다. 패널은 그 텍스처의
  왼쪽 위만 잘라 붙이므로, 겹쳐 그리는 쪽이 패널 크기로 좌표를 세면 원점을 패널 한가운데로
  잡는다. 그 차이(실측 13 픽셀, 2 유닛짜리 그림의 18%)만큼 전부 어긋난다.
  **그리는 쪽과 재는 쪽이 같은 셈을 쓰기 때문에 코드로는 드러나지 않았다** - 집어 보면 집히고,
  끌어 보면 끌린다. 화면을 읽어 그림과 테두리의 사각형을 견주고 나서야 보였다.
  3D 는 D-140 에서 이미 텍스처 크기를 쓰고 있었다. 2D 만 남아 있었다.
  `ViewRect` 에 `drawWidth` · `drawHeight` 가 생겼고, `WorldToScreen` · `ScreenToWorld` ·
  팬 · 격자 간격 · 2D 기즈모 카메라가 그것을 쓴다.
  회귀는 **화면 픽셀**로 잡는다(`TestPickingFollowsTheSpriteAssetSize`): 그림의 순색 픽셀과
  테두리 색 픽셀의 사각형이 5 픽셀 안에서 겹쳐야 한다. 패널 크기로 되돌리면 이 검사가 죽는다.
- **D-151. 메뉴에 프로젝트 저장·설정·디버그가 선다.** (2026-09-22, 계획서 §3 단계 7)
  기존 엔진의 메뉴(`RootDockWindow`·`MainDockWindow`)를 다시 대 보니 우리에게 기능은 있는데
  메뉴에 없는 것이 셋이었다. **파일 → 프로젝트 저장**: 캔버스를 저장하고, 그것이 되면 세션
  (D-146)도 프로젝트 파일에 적는다. 캔버스를 저장하지 못했으면 세션도 적지 않는다 - 저장되지
  않은 캔버스를 "마지막으로 연 것" 으로 적으면 다음에 열 때 그 파일이 없거나 옛 것이다.
  **설정 → 프로젝트 설정**, **디버그 → CPU 프로파일러·통계·로그**: 같은 창이 창 메뉴에도 있지만
  설정을 찾는 사람은 "설정" 을 먼저 연다. 창 목록을 훑게 하면 있는 기능도 없는 것처럼 보인다.
  차례는 기존과 같다(파일 · 시뮬레이션 · 편집 · 설정 · 디버그 · 창). 패널이 없으면 항목을 잠근다.
  `[열림]` 새 프로젝트(런처가 맡는다, D-97), 빌드·빌드 설정, GPU 프로파일링, 임포터 메뉴는 그
  기능이 없어 두지 않았다.
- **D-152. 패널의 글자·단추·메뉴·팝업·콤보는 공용 위젯을 거친다. 소스 검사가 그것을 지킨다.**
  (2026-09-22, ProjectRule §11.1)
  세어 보니 패널들이 `ImGui::` 원시 위젯을 **백 번 넘게** 곧장 부르고 있었다. 같은 "흐린 안내 글자" 가
  `TextDisabled("%s", …)` 와 `TextDisabled(…)`(글자를 서식으로 읽어 `%` 가 든 파일 이름에서 깨진다)로
  갈렸고, 메뉴의 잠금·팝업의 크기 처리도 패널마다 달랐다. 기존 엔진은 `ImText`·`ImTextButton`·팝업
  계층(`Application/Editor/ImItem/`)을 거쳤다.
  `Widget/Basic.h` 가 섰다: `Text`·`TextF`·`HintText`·`SeverityTextF`, `Button`·`HitArea`,
  `MenuItem`·`MenuToggle`, `Begin/EndContextMenu`·`Open/Begin/Close/EndModal`, `CollapsingSection`·
  `FoldNode`·`TreePop`, `Image`. 로그 창의 체크박스는 이름표를 위젯에 넘기지 않게 바꿨고(번역이 바뀌면
  Id 가 바뀌어 상태가 풀린다), 설정 창의 언어 목록은 `FilterCombo` 로 갔다(D-118).
  **새 패널이 다시 원시 호출을 들이지 않게** `TestPanelsGoThroughTheWidgetLayer` 가 패널 소스를 읽어
  금지 목록의 호출을 찾는다. 하나를 도로 넣으면 그 파일과 호출 이름을 말하며 죽는 것을 확인했다.
  배치(`SameLine`·`Separator`)와 그리기 목록은 위젯이 아니라 목록에 없다.
- **D-153. 재생 중에는 캔버스를 파일로 쓰지 않는다.** (2026-09-22, 기존 `EditorSimulationGuard`)
  기존 에디터의 파일을 하나씩 대 보다 찾았다. 단축키는 재생 중 저장을 막았지만(`EditorShortcuts`)
  **쓰는 길 자체는 열려 있어**, 메뉴의 프로젝트 저장(D-151) 같은 다른 길로 게임이 움직여 놓은 상태가
  파일이 될 수 있었다 - 정지로 되돌린 뒤에도 파일에 남는다. `SaveCanvas` 한 곳에서 막고, 프로젝트
  저장 메뉴도 재생 중에는 잠근다.
- **D-154. 에셋을 끌어 인스펙터의 에셋 칸에 놓으면 고른다.** (2026-09-22, 기존 `EditorDragDrop`·`ImAssetField::AllowDrop`)
  기존 에디터는 에셋 브라우저에서 끈 것을 에셋 칸이 받았다. 우리는 폴더로 옮기는 것만 됐다.
  꾸러미는 하나(`Widget/AssetDrag.h`)이고 **두 받는 쪽이 필요한 것을 함께 싣는다**: 폴더는 상대경로
  묶음을, 칸은 아이디를 본다. 아이디는 둘이다 - 그림 파일은 Texture·Sprite 두 레코드로 서고 목록의
  줄은 Texture 를 가리키는데 스프라이트 칸이 받는 것은 Sprite 라, 짝을 싣지 않으면 드롭이 조용히
  무시된다. 칸은 **자기 목록에 있는 것만** 받는다 - 목록이 곧 받을 수 있는 타입이다.
  만들다 드러난 것: 에셋 브라우저가 **누르는 순간** 에셋을 골라, 끌기를 시작하자마자 인스펙터가 그
  에셋의 화면으로 바뀌어 놓을 칸이 사라졌다. 이제 **끌지 않고 뗄 때** 고른다.
  테스트가 브라우저의 줄을 인스펙터의 `spriteId` 칸으로 실제로 끌어 놓고, 커맨드 하나로 들어가
  되돌려지는 것까지 본다.
- **D-155. 스프라이트 뷰어가 메인 도크와 나란히 뿌리에 붙는다.** (2026-09-22, 기존 `CSpriteViewerDockWindow`·`SpriteViewerWindow`)
  사용자가 짚은 "루트독-메인독, 그 외 임포터들 독" 구조의 빠진 쪽이다. 기존 엔진은 도구 창(패널)을 메인
  도크 안에, **파일을 여는 창**을 뿌리에 붙여 메인 도크와 탭으로 세웠다. 전에는 "임포터 창 자체가 없다"
  로 미뤘는데, 오디오 임포터만 막혔지 스프라이트 쪽은 막힌 것이 아니었다.
  `SpriteViewerWindow` 는 파일마다 탭 하나이고, 왼쪽에 시트와 칸 테두리(누르면 그 칸을 고른다), 오른쪽에
  칸 미리보기·재생(초당 프레임)·**인스펙터와 같은 함수로 그린 임포트 옵션**(`InspectorPanel::DrawAssetOptions`)이
  선다. 기존도 `SpriteImportOptionsEditor` 하나를 두 창이 나눠 썼다 - 각자 그리면 한쪽의 편집이 다른 쪽에서
  사라진다. 그래서 앞에 있는 탭의 그림이 곧 고른 에셋이고, 다른 것을 골라 두었으면 옵션 대신 "선택" 단추가
  선다. 탭은 스프라이트를 잡고 있다가 닫힐 때 놓는다. 여는 길은 에셋 브라우저의 두 번 누르기·우클릭
  "스프라이트 뷰어에서 열기"·창 → 임포터 → 스프라이트 뷰어다.
  **만들다 드러난 결함**: 뿌리에 창이 둘이 되어 탭 줄이 서자, 뷰어 탭이 앞에 오는 프레임에 메인 도크가
  그려지지 않았고, 그 프레임에 안쪽 도크 공간을 내지 않아 **붙어 있던 패널이 전부 떠 있는 창(닫기 X 달린)
  으로 흩어졌다.** 가려진 프레임에도 `KeepAliveOnly` 로 도크 공간을 살려 둔다. 테스트가 뷰어가 앞에 있는
  동안 패널마다 붙어 있는지 보고, 살려 두는 줄을 빼면 죽는 것을 확인했다. 뿌리 탭에 원래 식별자
  `MainDock` 이 그대로 보이던 것도 `메인###MainDock` 으로 고쳤다.
  시트의 격자와 칸 누르기는 칸이 여럿인 시트에서 사람이 보고 확인할 몫이 남았다 - 테스트 그림은 칸 하나다.
- **D-156. 밖의 그림을 에셋 폴더로 가져온다.** (2026-09-22, 기존 `SpriteImporterWindow`·`ImporterWindowBase`)
  기존 임포터는 밖의 파일을 골라 에셋 폴더 아래로 복사하고 `.jmeta` 를 떨어뜨렸다. 우리는 탐색기에서
  손으로 넣는 길뿐이었다. `ImportAssetFile` 이 **복사**하고 다시 훑어 등록한다 - 메타는 스캔이 만든다
  (에디터는 메타를 만드는 쪽으로 훑는다). 같은 이름이 있으면 **덮어쓰지 않는다**: 그 파일은 아이디를 들고
  있고 그 아이디를 가리키는 컴포넌트가 있어, 내용을 바꿔치면 모르는 사이에 다른 그림을 그린다. 엔진이
  모르는 확장자도 거절한다 - 복사는 되는데 목록에 나오지 않으면 가져온 것이 사라진 것처럼 보인다.
  기존 임포터는 가져오기 전에 옵션 창을 보였다. 우리는 **가져온 뒤 곧장 스프라이트 뷰어를 연다**(D-155) -
  같은 옵션을 같은 자리에서 고치고, 창을 하나 덜 만든다. 여는 길은 에셋 브라우저 빈자리의 "가져오기..."
  (지금 연 폴더로)와 창 → 임포터 → 스프라이트 가져오기(뿌리로)다.
- **D-157. 캔버스 뷰에서 누르면 맨 위 부모를 고르고, 두 번 누르면 그 안으로 들어간다.**
  (2026-09-22, 기존 `CCanvasViewEditContext`)
  기존 캔버스 뷰는 누른 자리의 **맨 위 조상**을 골랐고, 오브젝트를 두 번 누르면 그 안으로 들어가 직계 자식을
  고르는 단위로 삼았으며, 빈 곳을 두 번 누르면 한 층 나왔다. 우리는 가장 작은 것을 바로 골라, 몸통·팔·무기로
  된 오브젝트의 팔을 누르면 팔만 골라졌다 - 통째로 옮기려는 손짓이 조각 하나를 떼어 낸다.
  `CanvasViewPanel::MapToLevel` 이 걸린 오브젝트를 **지금 층**으로 올린다: 뿌리에서는 맨 위 조상, 들어가 있으면
  그 오브젝트의 직계 자식(또는 그 자신), 그 밖이면 없음. 클릭·상자 선택·3D 고르기가 모두 이것을 거친다 - 상자에
  한 부모의 조각 여럿이 걸려도 부모는 한 번만 든다. 들어가 있는 오브젝트는 **번호로** 든다(지워져도 안전하다).
  들어가 있으면 화면 위에 "○○ 안에서 고르는 중 - 빈 곳을 두 번 누르면 나옵니다" 가 선다 - 모르면 왜 부모가 안
  잡히는지 알 수 없다. 계층 창의 고르기는 그대로다(거기서는 줄이 곧 오브젝트다).
  테스트를 만들다 한 가지를 알았다: 몸을 고르면 기즈모 손잡이가 몸의 오른쪽·위로 서서, 그 자리의 오브젝트를
  누르면 손잡이가 누름을 가져간다. 테스트의 팔은 왼쪽 아래에 둔다.
- **D-158. 실제 에디터를 띄워 보고 고친 것들.** (2026-09-22)
  테스트 창이 아니라 **실제 에디터 실행 파일**(`JBroEditorHost`)을 띄워 조작해 보니 넷이 드러났다.
  1. **에디터 실행 파일이 빌드되지 않고 있었다**(5168bb2 부터). 공개 헤더 `EditorApplication.h` 가 단축키 헤더를
     통해 `imgui.h` 를 끌어오는데, 에디터 호스트는 ImGui 경로를 모른다. 테스트 프로젝트는 그 경로를 가져
     통과했고, 그래서 디스크의 에디터는 9월 20일 것에 머물렀다 - **사용자가 켠 에디터에는 이번 작업이 하나도
     없었다.** 열거형만 앞선언해 끊었다. 앞으로 검증은 테스트 프로젝트가 아니라 **솔루션 전체**를 빌드한다.
  2. **새 오브젝트에 트랜스폼이 없었다.** 메뉴로 만든 오브젝트가 캔버스 뷰에 보이지도 않고 옮길 수도 없었다.
     기존 엔진은 트랜스폼이 오브젝트의 멤버였다. `CreateObjectCommand` 가 붙일 컴포넌트의 등록 이름을 받고,
     에디터는 프레임워크의 트랜스폼을 준다(되돌렸다 다시 해도 붙는다).
  3. **마우스를 붙잡지 않았다.** 누를 때 `SetCapture` 를 하지 않아, 끌다가 창 밖에서 떼면 뗌이 다른 창으로
     가서 끌기가 끝나지 않았다(ImGui 공식 Win32 백엔드가 붙잡는 이유다). 누를 때 붙잡고 마지막 버튼을 뗄 때
     놓으며, 붙잡음을 빼앗기면(`WM_CAPTURECHANGED`) 눌린 버튼을 모두 뗀 것으로 알린다.
  4. **에셋 브라우저가 누르지 않은 줄을 골랐다**(D-154 가 들인 결함). 뗄 때 고르기로 바꾸면서 뗀 자리만 보아,
     오른쪽 칸에서 폴더를 눌러 열면 같은 자리에 나타난 파일이 골라졌다. 누른 줄을 기억해 같은 줄에서 뗐을
     때만 고른다. 회귀 테스트의 첫 판은 누름과 뗌 사이에 한 프레임만 두어 **결함을 되돌려도 통과했다** -
     사람처럼 몇 프레임 누르고 떼게 고친 뒤에야 뮤테이션이 죽는다.
  실제 에디터에서 확인한 것: 계층 창의 끌어서 부모 삼기·빈자리에 놓아 부모 해제·위로 끌어 순서 바꾸기가
  되풀이해도 되고, 게임 뷰에는 기즈모도 편집도 없으며, 뿌리 도크 노드의 X 는 없다. 탭에 마우스를 올리면 뜨는
  닫기 X 는 기존 엔진과 같다(`ImWindow` 도 도구 창에는 두고 메인 도크에서만 껐다).

- **D-159. 실제 에디터에서 스프라이트 뷰어를 끝까지 써 보고 고친 것들.** (2026-09-22) Updates D-147·D-155.
  walk.png(128×32, 원 넷)에 CellCount·열 4 를 주고 격자·칸 누르기·재생을 실제 에디터에서 확인했다. 둘이 드러났다.
  1. **인스펙터 미리보기가 그림을 정사각형으로 늘였다**(D-147 의 결함). 에셋 브라우저의 아이콘 칸도 같았다.
     `Widget::FitInside(너비, 높이, 칸)` 을 공용 위젯에 두고 미리보기·아이콘·뷰어 미리보기가 모두 쓴다
     (뷰어는 따로 셈을 들고 있었다). 원본 크기를 아직 모르면 칸 그대로다.
  2. **뷰어가 이미 떠 있으면 그림을 두 번 눌러도 아무 일도 없는 것처럼 보였다.** 뷰어가 메인 탭 뒤에 가려지면
     `Begin` 이 거짓이라 새 탭을 고르지도 못하고, 창도 앞으로 나오지 않았다. 열 것이 있는 프레임에
     `SetNextWindowFocus` 로 창을 앞으로 꺼낸다. 처음 여는 뷰어는 새로 붙는 창이라 우연히 앞에 섰기에 첫 확인에서
     놓쳤다.
  회귀 테스트: 폴더 안 그림 줄을 사람처럼(누름마다 몇 프레임, 두 번째는 `WM_LBUTTONDBLCLK`) 두 번 누르면 탭이
  생기고, 메인 탭을 앞으로 낸 뒤 다시 열면 뷰어가 보인다. 두 수정을 각각 되돌리면 각 테스트가 죽는다.
  테스트 하네스의 함정 둘을 기록한다. `ImGui::GetHoveredID()` 는 이 프레임에 호버가 없으면 **앞 프레임의 것**을
  주어, 줄 찾기의 첫 자리가 창 맨 위로 잘못 잡혔다 - 줄 찾기는 `HoveredId` 를 본다. 뮤테이션을 되돌릴 때
  `Copy-Item` 은 원본의 수정 시각을 지켜 MSBuild 가 다시 컴파일하지 않았다 - 되돌린 뒤 시각을 새로 찍는다.
  크게 키운 작은 그림이 흐린 것(ImGui 선형 샘플러)은 기존 엔진도 같아 이번에 바꾸지 않았다. 픽셀 아트용 점 샘플링은
  남은 일이다.

- **D-160. 파일 → 새 프로젝트, 그리고 켜자마자 꺼지던 에디터.** (2026-09-22) Updates D-97·D-151.
  1. **새 프로젝트를 만들 길이 어디에도 없었다.** 기존 루트 도크의 파일 메뉴 첫 항목이 `새 프로젝트` 였는데 우리는
     "런처가 맡는다(D-97)" 며 넣지 않았고, 런처의 L3(새 프로젝트)도 미착수였다. 기존과 같이 폴더를 고르고
     (`FileDialogDesc::pickFolder`, 기존 `ShowOpenFolderDialog`) 팝업(`NewProjectPopup`)이 이름을 받는다. 우리 파일은
     2D/3D 를 적어야 열리므로(D-99) 프레임워크도 거기서 고른다. `CreateProjectFile`(JBroHost)이 `<폴더>/<이름>/` 에
     `<이름>.jproject` 와 에셋 폴더를 세우고 다시 읽어 열리는 것까지 확인한다. **있는 폴더 위에는 세우지 않고**
     파일 이름이 될 수 없는 이름은 거절한다. 실패 까닭은 코드(`ProjectCreateFailure`)로 와서 팝업이 번역된 문장을
     보이고, 엔진의 영어 문장은 로그에 남는다. 만든 뒤에는 열기와 같은 길(`SwitchToProject`)로 넘어간다 - 팝업은
     프레임 안이라 프레임이 끝난 뒤에 넘어간다. 엔진 버전은 `JBro.Common.props` 한 곳에서 `JBRO_VERSION_*` 로 넘겨
     `JBro/Core/Version.h` 가 글자로 바꾼다(D-101 을 지킨다).
  2. **팝업의 폭.** 기존 `InitSize(400, 0)` 처럼 폭만 정하고 높이는 내용에 맞추는 모양을 받는다(한 축이 0 이면
     ImGui 가 그 축을 맞춘다). 처음 그렇게 하자 첫 프레임에 높이를 몰라 팝업이 화면 맨 위에 붙었다 - 크기가
     서는 몇 프레임 동안 가운데에 붙든다. 긴 경로는 `Widget::WrappedText` 로 줄을 바꾼다.
  3. **에디터가 켜지자마자 꺼지는 일이 서른 번에 세 번꼴로 있었다.** 호스트는 첫 프레임의 시간을
     `now - previous` 로 재는데 시계가 넘어가지 않으면 0 이고, `EditorUI::BeginFrame` 은 ImGui 단언을 막으려 0 을
     거절한다. 그것이 `Tick` 거짓 → 루프 끝으로 이어졌다. 시간이 흐르지 않은 프레임은 실패가 아니므로 에디터가
     아주 작은 시간을 준다(음수와 NaN 은 여전히 잘못이다). **호스트는 끝날 때 마지막 프레임 상태를, 엔진은 창이 닫혀
     멈추는지 끝내기 요청으로 멈추는지를 남긴다** - 이 결함은 "0 프레임" 이라는 한 줄밖에 없어 찾는 데 오래 걸렸다.
  4. 빈 원문에 프로젝트 파일을 쓰면 첫 줄이 빈 줄이었다(빈 원문을 빈 줄 하나로 셌다).
  실제 에디터: 파일 메뉴가 `새 프로젝트 · 구분선 · 프로젝트 열기 · 캔버스 저장 · 프로젝트 저장 · 종료` 로 서고,
  진짜 폴더 대화상자("프로젝트를 만들 폴더 선택")를 거쳐 팝업이 가운데에 뜨며, 이름을 치고 Enter 로 만들면 그 프로젝트가
  열린다. 같은 이름은 "이 폴더에 같은 이름이 이미 있습니다" 로 거절된다. 서른 번 띄워 한 번도 꺼지지 않았다.
  회귀 테스트와 뮤테이션: 있는 폴더 거절·만든 뒤 넘어가기·팝업 가운데·빈 첫 줄·0 초 프레임 - 각각 되돌리면 죽는다.

- **D-161. 도구 창 이름을 기존 엔진의 이름에 맞춘다.** (2026-09-22) Updates D-91.
  기존 `window.*` 키와 우리 `panel.*` 을 하나씩 맞대니 셋이 달랐다. 둘은 결정에 걸린 것이 없어 기존 이름으로 바꿨다:
  `에셋` → **에셋 브라우저**(Asset Browser), `단축키` → **단축키 안내**(Shortcut Reference). 창의 정체는 `###` 뒤의 영어
  이름이라 도킹 자리와 테스트는 그대로다. 캔버스 뷰·게임 뷰·인스펙터·로그는 기존과 같다.
  **계층 창은 `레이어`(Layers)다.** 기존 `CLayerTool` 의 이름이고, 이 창은 레이어를 머리로 두고 오브젝트를 묶어
  보이므로 내용과도 맞다. D-91 이 번역체를 고치며 `계층 구조` 로 정했던 것을 사용자 확인(2026-09-22)을 받아 바꿨다. 메뉴의 나머지(파일·시뮬레이션·편집·창→에디터/임포터)는 실제 에디터에서 열어
  기존 항목과 맞댔다 - 편집의 `삭제` 는 기존에 없던 항목이고 모자란 것은 없다.

- **D-162. 캔버스 뷰 눈금 숫자가 0 을 `-2.98e-08` 로 적었다.** (2026-09-22) Updates D-144.
  사용자가 처음 지적한 것들(레이어 창의 부모 삼기·빈자리에 놓아 해제·위로 끌어 순서 바꾸기, 게임 뷰에 기즈모도 편집도
  없음)을 지금 빌드의 실제 에디터에서 다시 해 보다가 드러났다 - 그것들은 그대로 된다. 눈금을 `x += step` 으로 더해 가서
  오차가 쌓였다. 선마다 번호에 간격을 곱하도록 바꿔 0 은 정확히 0 이고, 열 칸마다의 진한 선도 번호로 가른다. 왼쪽 아래
  구석에서 Y 숫자가 X 숫자 줄과 포개지던 것도 건너뛴다. 확인은 실제 에디터 화면으로 했다(간격 0.2 에서 `0`).
  드라이버가 끄는 동안 마우스를 붙잡고 있어 사용자의 실제 마우스가 섞여 들 수 있다 - 한 번씩 본 확대·도크 경계 이동은
  되풀이되지 않아 그것으로 본다.

- **D-163. 레이어 창의 오브젝트 줄에 눈 표시 - 캔버스 뷰에서만 감춘다.** (2026-09-23)
  기존 `ImEditor` 의 공개 기능을 하나씩 맞대다 찾았다(`SetCanvasViewHidden`). 기존 레이어 창은 **오브젝트마다** 눈이
  있어 누르면 그 오브젝트가 캔버스 뷰에서만 사라지고(`ObjectFlag_EditorHidden`), 캔버스 뷰의 집기·상자 선택에서도 빠졌다.
  게임과 게임 뷰는 그 비트를 보지 않았다. 우리는 레이어 줄에만 눈이 있었다.
  - **어디에 남기나(사용자 결정 2026-09-23): 캔버스 파일에 남기고 패킹할 땐 뺀다.** 오브젝트의 `m_flags`(쓰이지 않던
    자리)에 `ObjectFlagEditorHidden` 을 두고 `.jcanvas` 에 `Flags:` 로 적는다(0 이면 적지 않는다). `WriteCanvasText` 에
    `CanvasWriteMode::Package` 를 두어 `EditorOnlyObjectFlags` 를 지우고 적는다 - 패킹이 서면 이것으로 적는다(빌드 시스템은
    아직 없다).
  - **그리기**: 추출한 항목에 이미 `owner` 가 있어, 캔버스 뷰 제출(`SubmitEditorView2D`·`3D`)만 감춘 것을 건너뛴다.
    게임 뷰 제출은 그대로다.
  - **집기**: 캔버스 뷰의 2D 집기·상자 선택·3D 집기와 콜라이더 그리기가 감춘 것을 건너뛴다. 고른 것의 기즈모는 남는다 -
    레이어 창에서 골라 옮길 수 있어야 한다.
  - **편집은 커맨드다**: `SetObjectEditorHiddenCommand`. 활성 토글과 모양이 같아 둘의 몸을 `ObjectToggleCommand` 한 벌로
    묶었다(되살릴 값 먼저 뜨기·바뀌는 것이 없으면 쌓지 않기·오브젝트마다 제 값으로 되돌리기). 지웠다 되돌려도 감춘 채이도록
    오브젝트 스냅숏이 플래그를 뜬다(전에는 플래그 자체를 뜨지 않았다).
  - **위젯 하나로 그린다**: `Widget::RowEyeToggle` - 레이어 줄과 오브젝트 줄이 같은 자리·크기의 눈을 쓴다.
  회귀 테스트: 줄의 눈을 누르면 한 커맨드로 감추고, 캔버스 뷰에서 빨간 스프라이트 픽셀이 사라지며, 가운데를 눌러도
  고르지 않고, 되돌리면 같은 누름이 고르며, 지웠다 되돌려도 감춘 채고, 게임 뷰에는 그려진다. 캔버스 파일은 `Flags: 1` 로
  남고 `Package` 는 뺀다. 그리기 건너뛰기·집기 건너뛰기·패킹 빼기·스냅숏 플래그를 각각 되돌리면 각 검사가 죽는다.
  실제 에디터에서 오브젝트 줄에 눈과 툴팁("캔버스 뷰에서 이 오브젝트를 보이거나 감춥니다")이 서고 누르면 가린 눈이 된다.
  `ImEditor` 의 나머지 공개 기능 대조: 창 만들기·찾기(패널), 미룬 일(`Perform*` 요청), 팝업(같은 API), 캔버스·게임 뷰 타깃,
  캔버스 뷰 선택·들어가기 표시는 있다. 레이어 썸네일은 레이어가 자기 텍스처를 갖지 않아 해당 없음(D-142), 카메라 컬링
  통계와 GPU 프로파일러 미리보기는 렌더러에 그 수치가 없어 열림이다.

- **D-196. 에셋 브라우저가 이름·종류·수정한 날짜로 늘어놓고, 목록 보기에 열이 선다.** (2026-09-25)
  기존 에디터의 소스 이름을 대조표와 기계적으로 견주다 `AssetBrowserUtils::FileTimeToTimeT` 가
  걸렸다. 쓰임을 따라가니 기존 브라우저에는 **정렬 기준 칸**(이름·종류·수정한 날짜)이 있었고,
  목록 보기는 **머리줄이 고정된 네 열 표**(이름·종류·수정한 날짜·GUID)였다. 우리는 경로순 하나뿐이고
  한 줄에 `이름  종류` 만 찍었다 - 방금 고친 파일을 찾으려면 목록을 훑어야 했다.
  - 정렬 칸은 `Widget::EnumCombo` 다(도구줄의 보기 단추 옆). 라벨은 붙이지 않고 마우스를 올리면
    `정렬 기준` 이라고 말한다(§11.2). 날짜순은 **최근 것이 위**이고, 셋 다 마지막에 이름으로
    갈라 같은 종류·같은 시각끼리 차례가 흔들리지 않는다. 폴더는 늘 이름순으로 먼저 온다.
  - **줄은 그대로 둔다.** 표로 바꾸면 줄의 아이디가 바뀌어 끌어 놓기·범위 선택과 그것을 재는
    테스트가 전부 흔들린다. 대신 줄 안에 열을 맞춰 그리고, 머리줄이 같은 자리를 쓴다
    (`ListColumns` 한 곳). 이름이 길면 종류 칸 앞에서 잘린다.
  - 날짜는 `IPlatform::GetFileWriteTime`(새 읽기 전용 질의, `FileExists` 옆)으로 읽고, 날짜와
    아이디의 글자는 **레지스트리가 바뀔 때만 한 번** 짓는다 - 매 프레임 글자를 짓지 않는다(§7,
    기존도 `ModifiedTimeText` 를 캐시했다).
  회귀 테스트는 **마우스로만** 고른다 - 콤보를 눌러 열고 줄을 눌러 고른다(키보드 테스트가 흔들렸다).
  이름순 `abcm`, 날짜순 `bamc`, 종류순 `mabc` 로 셋이 모두 다른 차례가 되게 파일과 수정 시각을 짰다.
  열은 **픽셀로** 잰다: 줄의 날짜 자리와 아이디 자리에 줄 배경과 다른 픽셀이 있어야 한다.
  처음 재는 방식이 둘 다 틀렸다 - `findRow` 가 돌려주는 y 는 줄의 윗가장자리라 위아래로 잰 띠가
  글자를 비껴갔고, 절대 밝기 문턱은 흐린 글자를 26 픽셀밖에 못 잡았다. 1024 폭 창에서는 아이디
  열이 칸 끝에 몇 글자만 걸려 빠져도 알 수 없었으므로 이 테스트만 1600 폭으로 띄운다.

- **D-195. 규칙을 지키는지 보는 검사가 목록이 아니라 훑기로 본다.** (2026-09-24)
  D-190 이 고친 구멍 - "감시 테스트가 `Source/Panel` 만 읽어 위젯 계층 규칙이 조용히 안 지켜졌다" -
  은 **고친 방식 때문에 다시 열려 있었다.** 폴더 둘과 파일 다섯을 손으로 적어 넣었으므로,
  그 뒤에 생긴 소스는 아무도 보지 않는다. 실제로 `Source/Command` 13 개와 `Source/Gizmo` 2 개를
  포함해 **26 개가 검사 밖**이었다. 목록이 낡는 것은 시간 문제이지 사고가 아니다.
  - 이제 `Modules/JBroEditor/Source` 아래를 훑고 `Widget` 만 뺀다(그 폴더가 곧 규칙이 거치라고
    말하는 층이다). 43 개를 읽고, 규칙이 깨졌던 두 파일(`EditorActions`·`EditorApplication`)은
    이름이 바뀌어 빠지지 않도록 따로 확인한다.
  - 음성 테스트: `Source/Command/LayerCommands.cpp` 에 `ImGui::Button(` 한 줄을 넣으면
    **옛 검사는 통과하고 새 검사는 그 파일 이름과 함께 실패한다.** 둘 다 실제로 돌려 확인했다.
  - 로컬라이징도 같은 병이었다. `TestTheShippedLocalesAgree` 는 "전부 세려면 목록이 둘이 된다"
    며 키 상수 **넷만** 짚어 보고 있었다. 헤더를 읽으면 목록은 하나뿐이다 -
    `TestEveryKeyIsDeclaredTranslatedAndUsed` 가 ① 선언한 키가 두 로케일에 다 있는지
    ② 로케일 파일의 키가 헤더에 선언되어 있는지 ③ 선언한 상수를 코드가 어딘가에서 부르는지를
    본다. 갈래 이름(`component_category.*`)만 예외다 - `EditorNames` 가 그 자리에서 짓는다.
    셋 각각을 어겨 보아 셋 다 잡히는 것을 확인했다.
  - 그 검사가 곧바로 잡은 것: `hierarchy.empty` 는 **아무도 부르지 않는 키**였다(레이어마다
    `hierarchy.layer_empty` 를 내므로 자리가 없다). 키와 번역 둘을 지웠다. 242 개가 남는다.
  - **두 번째 컴파일러로 헤더를 훑어 하나 더 나왔다.** `JBro/Types/SafePtr.h` 가 `std::forward`
    를 쓰면서 `<utility>` 를 넣지 않는다. MSVC 는 `<type_traits>` 가 딸려 들여 주어 174 개
    자기포함 검사가 전부 통과했지만, 헤더가 혼자 서지 못하는 것은 사실이고 검사는 그것을
    못 본다. 넣었다. 그 하나로 자기포함 검사 173 개 중 143 개가 g++ 로도 서고, 남은 30 개의
    실패 원인은 **전부 `Reflection/Field.h` 의 `__FUNCSIG__` 하나**다(고치기 전에는 173 개가
    전부 실패했다). 모듈 소스는 104/165 가 선다 - 나머지는 Windows·D3D·Vulkan 헤더다 -
    **두 번째 컴파일러가 실제로 값을 한다**는 실측이다. `Field.h` 에 비-MSVC 갈래를 두는
    일은 하지 않았다(리플렉션 코어를 이 자리에서 건드릴 일이 아니다). 열림으로 남긴다.
  - D-194 가 `Profiler::Push`·`Pop` 을 손으로 짝지었다. `ProfileScope` 가 바로 그 이유로 있는데
    쓰지 않았다 - 시스템이 던지면 `Pop` 을 건너뛰고 그 프레임부터 겹이 0 으로 돌아오지 않는다
    (`SystemScheduler::Initialize` 는 이미 시스템이 던지는 것을 전제로 쓰여 있다). 바꿨다.
  - `Localization.cpp` 가 파일 내용과 로케일 비교에 날 `std::string` 을 쓰고 있었다(§7).
    `String` 과 `std::strcmp` 로 바꿔 에디터 모듈에 날 `std::string` 이 남지 않는다.
  - **검증의 한계를 적어 둔다.** 이 작업은 Linux 컨테이너에서 했다 - MSVC 도 `dotnet` 도 없어
    `JBroEngine.slnx` 빌드와 `JBroTests` 를 **돌리지 못했다.** 대신 g++ 로 자기포함 훑기와
    고친 소스 둘의 구문 검사를 돌렸고, 새 검사 둘은 실제 트리를 상대로 따로 돌려 통과·음성
    둘 다 확인했다. **Windows 에서 빌드와 테스트를 한 번 돌려야 이 항목이 끝난다.**
  - **Windows 에서 마저 확인했다**(2026-09-25). `JBroEngine.slnx` 전체가 경고 없이 서고
    `JBroTests` 가 통과한다. 음성 검사 셋을 **MSVC 로 지은 `JBroTests`** 로 다시 돌렸다 - 소스를
    글자로 읽는 검사라 위반을 심고 실행 파일만 돌린 뒤 되돌렸다: `LayerCommands.cpp` 에
    `ImGui::Button(` 을 넣으면 그 파일 이름과 함께 실패하고, 선언만 하고 번역하지 않은 키와
    로케일에만 있는 키도 각각 실패한다. 되돌린 트리는 통과한다. 이 항목은 끝났다.
  - `[열림]` **키보드로 치는 테스트 둘이 가끔 실패한다.** 같은 확인에서 전체 테스트를 여섯 번
    돌려 둘이 실패했다: `E must switch to rotation…`(기즈모 단축키)와 `typing in the name field
    renames the object`(인스펙터 이름 칸). 둘 다 `PostMessage` 로 키를 보내고 틱 한 번 뒤에 결과를
    본다. 이름 칸 테스트는 **칸이 입력을 받는 상태가 된 것을 먼저 확인하게** 했다 - 전에는 실패가
    "이름이 안 바뀌었다" 로만 보였다. 그 확인은 통과한 채로 한 번 더 실패했으므로, 칸은 준비됐는데
    글자가 사라진 것이다.
    가설 둘을 세웠다. ① ImGui 는 **앱이 포커스를 잃는 프레임에 쌓인 글자와 눌린 키를 버린다**
    (`ClearInputKeys` 가 `InputQueueCharacters` 를 비운다) - 그런데 테스트 창은 숨겨져 있어 OS
    포커스가 오가지 않아야 하고 ImGui 뷰포트도 꺼져 있다. ② 플랫폼이 키 이벤트의 수정 키를
    `GetKeyState` 로 **실제 키보드 상태**에서 읽는다 - 그 순간 이 컴퓨터에서 누가 Ctrl 을
    누르고 있으면 `E` 는 `Ctrl+E` 가 되고 ImGui 는 Ctrl 이 눌린 글자를 넣지 않는다.
    **둘 다 증거가 없다.** 가르는 값(ImGui 프레임 수의 증가량, 칸의 실제 글자, 활성 아이디,
    포커스 창, `AppFocusLost`)을 **실패할 때만 찍도록** 두 테스트에 넣었고, 넣은 뒤로 여덟 번
    돌려 한 번도 재현되지 않았다. 다음에 실패하면 `[flake]` 줄이 원인을 가린다 - 그때 고친다.
    그때까지 뮤테이션 결과에서 **이 두 테스트의 메시지로 "잡혔다" 는 것은 믿지 않는다.**

- **D-194. 프로파일러가 시스템마다 나뉜다.** (2026-09-24)
  기존 CPU 프로파일러는 컴포넌트 풀(시스템)마다 순회 시간을 내고, 고른 풀의 상세를 보여 주었다.
  우리는 `Systems` 한 덩어리였다 - 느려졌을 때 숫자는 올라가는데 **어느 시스템 때문인지는
  아무것도 말해 주지 않았다.** 창은 있고 쓸모가 없었다.
  - 스케줄러가 시스템을 들일 때 `typeid(T).name()` 을 함께 든다. 그 포인터는 타입의 자료를
    가리켜 프로그램이 도는 동안 바뀌지 않으므로, **이름을 복사하지 않고 주소로 묶는**
    프로파일러의 요구를 그대로 만족한다 - 매 프레임 글자를 짓지 않는다(§7).
    `GameSystem` 인터페이스에 이름을 더하지 않았다: 프로파일링 때문에 공개 계약을 넓히지 않는다.
  - `Update` 와 `FixedUpdate` 둘 다 감싼다. 같은 이름 아래 합쳐지는 것은 기존도 같았고
    (풀의 총 순회시간), 한 프레임에 몇 번 돌았는지는 **부른 횟수** 칸이 말한다.
  - **줄이는 일은 스케줄러가 한 번에 한다.** 처음에는 창이 `EditorNames::DisplayTypeName`
    으로 줄였는데, 그 한 줄을 재는 것이 없어 뮤테이션이 살아남았다. 이름은 어차피 같은 글자
    **안쪽**을 가리키면 되므로(`class JBro::Camera2DSystem` 의 마지막 마디), 들일 때 한 번
    잘라 두면 주소는 그대로 안정하고 창은 받은 것을 그냥 적는다 - 자를 자리도 하나다.

- **D-193. 인스펙터의 에셋 칸에서 그 에셋을 에셋 브라우저로 찾아간다.** (2026-09-24)
  기존 `ImReferenceField` 에 `OnActivate` 가 있었고, 에셋 칸이 그것으로 "이 에셋 어디 있어" 를
  했다 - 브라우저가 그 폴더로 옮겨 가 그 줄을 골랐다. 우리에게는 그 길이 없었다.
  `RevealAsset` 은 이름이 비슷하지만 **탐색기를 여는 다른 일**이라, 에디터 안에서 찾는 데는
  도움이 되지 않는다.
  - **우클릭이다.** 기존은 더블클릭이었는데 우리 칸은 검색 드롭다운이라 첫 누름에 팝업이
    열리고 두 번째 누름이 그 위에 떨어진다 - 같은 일을 이 위젯이 허락하는 손짓으로 한다.
  - 브라우저는 경로 글자가 아니라 **아이디로** 찾는다(기존과 같다). 구분자 표기가 어긋나도
    안전하다. 닫혀 있으면 그리지 않으므로 여는 것은 에디터 쪽이 한다.

- **D-192. 엔진이 모르는 파일을 두 번 누르면 OS 가 연다.** (2026-09-24)
  기존 브라우저의 기본 열기 처리기는 `File::OpenFile` 을 불렀다. 우리는 그림과 캔버스만 알고
  나머지는 **아무 일도 하지 않았다** - 두 번 눌렀는데 반응이 없으면 고장으로 보인다.
  `IPlatform::OpenPathWithShell` 을 `RevealInFileBrowser` 옆에 둔다(성격이 같다). 열 프로그램이
  없으면 탐색기로 그 자리를 보여 준다.

- **D-191. 에셋 파일을 지우고 이름을 바꾼 것도 되돌아온다. 저장 여부는 문서마다 따로 센다.** (2026-09-24)
  기존 엔진에는 `EditorFileCommands` 셋(`CCreateFolderCommand`·`CRenamePathCommand`·
  `CDeletePathCommand`)이 있었다. 지운 것을 임시 휴지통으로 옮겨 두었다가 되돌리기로
  되살리고, 커맨드가 스택에서 밀려날 때 그 자리를 치웠다. **우리는 곧장 지웠다** -
  잘못 지운 파일은 그것으로 끝이었고, 우리 규칙(§11 "되살릴 값을 먼저 뜨지 못했으면
  지우지도 떼지도 않는다")도 지켜지지 않고 있었다.
  - `CreateAssetFolderCommand` · `MoveAssetCommand` · `DeleteAssetCommand` 가 섰다.
    옮기기와 이름 바꾸기는 같은 일이라 커맨드도 하나다(기존도 그랬다). `.jmeta` 는 늘 함께
    간다 - 두고 오면 되살릴 때 아이디가 달라지고 그 에셋을 가리키던 참조가 전부 풀린다.
  - **휴지통은 프로젝트 안 숨김 폴더(`.jbrotrash`)다**(사용자 확인). 같은 볼륨이라 옮기기가
    즉시 끝나고 중간에 실패할 자리가 없다 - 기존처럼 시스템 임시 폴더에 두면 프로젝트가
    다른 드라이브일 때 지우기가 통째 복사가 되고 큰 폴더에서 도중에 실패할 수 있다.
    에셋 폴더 **밖**이라 스캔과 파일 감시가 보지 않는다. 프로젝트를 열고 닫을 때 치운다.
  - **캔버스를 더럽히지 않는다.** 되돌리기는 같은 Ctrl+Z 하나지만, 저장 여부는 문서마다
    따로 센다(`EditorCommandManager::AssetDatabase`). 기존도 `documentKey` 로 같은 나눔이었고
    에셋 브라우저가 `"__AssetDatabase"` 를 넘겼다. 파일 이름을 바꿨다고 캔버스가
    "저장 안 됨" 이 되면, 저장할 것이 없는데도 저장을 누르게 된다.
    문서를 가리지 않는 판번호(`GetRevision`)는 그대로 둔다 - 에셋 참조를 다시 잇는
    자리(`BindCanvasAssets`)가 그것을 본다.
    처음에는 기존처럼 **문서마다 판번호를 두는 표**를 만들었는데, `IsDirty(문서)` 를 부르는
    자리가 하나도 없어 뮤테이션이 살아남았다 - 에셋 파일 작업은 실행하는 순간 디스크에
    적히므로 "적지 않은 것" 이 남지 않는다. 저장할 문서는 캔버스뿐이라, 표를 캔버스 판번호
    하나로 줄였다. 저장할 문서가 둘이 되면 그때 표로 되돌린다.
  - **여럿을 지우면 되돌리기도 하나다**(§11). 에셋 브라우저의 여러 개 지우기가
    `CompoundCommand` 로 묶인다 - 다섯 개를 지운 뒤 Ctrl+Z 를 다섯 번 누르게 하지 않는다.
  회귀 테스트: 지우고 되돌려 파일과 `.jmeta` 가 **같은 아이디로** 돌아오는지, 다시하기와
  되돌리기를 거듭해도 같은지, 이름 바꾸기와 폴더 만들기가 되돌아오는지, 그리고 그동안
  **캔버스가 한 번도 "저장 안 됨" 이 되지 않는지**를 잰다. 휴지통이 에셋 폴더 밖인지도 본다.

- **D-190. 되돌릴 수 없는 단추는 붉게 선다. 메뉴도 위젯 계층을 거친다.** (2026-09-24)
  기존 위젯 집합에 `ImActionButton` 이 있었다. 무게(`Severity`)에 따라 단추가 물들어,
  지우기는 붉게·저장과 만들기는 푸르게 섰다. 우리 에셋 브라우저의 `삭제` 는 바로 옆
  `취소` 와 **똑같이 생겼다** - 되돌릴 수 없는 일 앞에서 손이 먼저 움직인다.
  - `Widget::ActionButton` 이 섰다. `Info` 는 테마 그대로다 - 모든 단추가 물들면 무게가
    뜻을 잃는다. 잠금과 그 까닭(`disabledReason`)은 메뉴 항목과 같은 수를 쓴다.
    쓰는 자리: 에셋 지우기(붉게)·이름 바꾸기와 새 폴더의 확인(푸르게)·프로젝트 설정의
    저장(푸르게)과 되돌리기(주의)·새 프로젝트 만들기(푸르게)·저장하지 않고 열기(붉게).
  - **여기서 더 큰 것이 나왔다.** 메뉴 막대 둘(`DrawRootMenuBar`·`DrawMainMenuBar`)과 공용
    메뉴(`EditorActions`), 그리고 팝업 둘이 `ImGui::` 를 곧장 부르고 있었다. 규칙(§11.1)은
    진작 있었지만 **감시 테스트가 `Source/Panel` 만 읽어서** 그 밖에서는 조용히 지켜지지
    않았다. 그 대가가 눈에 보이는 자리에 있었다: 그 메뉴 항목들은 D-181 이 `Widget::MenuItem`
    에 붙인 "왜 잠겼는지" 툴팁을 받지 못했다.
  - `Widget::BeginMenuBar`·`EndMenuBar`·`BeginMenu`·`EndMenu` 가 섰고, 창 목록의 켜고 끄는
    항목은 이미 있던 `Widget::MenuToggle` 로 갔다. 배치(`Separator`·`SameLine`)는 위젯이
    아니므로 그대로 둔다 - 글자도 상태도 잠금도 없는 것을 계층에 올릴 까닭이 없다.
  - 감시 테스트가 패널·도구·팝업·`EditorActions`·`EditorApplication` 을 모두 읽는다.
    **모달을 돌리는 기계만 예외다**: `EditorApplication` 이 팝업 하나를 열고 닫는 그 자리가
    곧 `Widget::BeginModal` 이 서 있는 층이라 자기 자신을 거치라고 할 수 없다.
  - `ConfirmPopup` 은 **단추의 차례가 곧 무게**다: 첫째가 하기, 둘째가 저장하지 않고 버리기,
    셋째가 그만두기. 그 차례를 따르지 않는 창은 이 클래스를 그대로 쓰지 않는다.
  회귀 테스트: 단추가 방금 칠한 정점 색을 읽어 `Info` 는 테마 색을 쓰고 `Error`·`Success` 는
  쓰지 않는지 재고, 같은 자리를 같은 손짓으로 두 번 눌러 잠갔을 때만 답이 없는지 잰다.
  감시 테스트는 파일 열넷 이상을 읽는다. 뮤테이션 다섯(칠하지 않기·잠그지 않기·공용 메뉴를
  ImGui 로 되돌리기·메뉴 막대를 ImGui 로 되돌리기·지우기 단추의 무게 빼기)이 잡힌다.

- **D-189. 에셋 감시가 건너뛸 이름을 설정 화면에서 고친다. 저장이 파일을 불리지 않는다.** (2026-09-24)
  기존 엔진의 위젯 집합(`ImItem/*`)을 항목 단위로 대조하다 `ImNameListEdit` 이 우리에게 없는 것을 봤다.
  기존 프로젝트 설정의 **에셋 감시** 칸이 그것으로 무시 패턴을 고쳤는데, 우리는 `assetIgnorePatterns` 를
  읽고 쓰기만 할 뿐 **고칠 길이 없었다** - 손으로 `.jproject` 를 열어야 했고, 열어 고쳐도 저장하면
  사라졌다. 프로젝트 파일 작성기가 **시퀀스를 아예 지나쳤기** 때문이다(값이 여러 줄이라 한 줄
  바꿔치기로 다룰 수 없었다).
  - `Widget::NameListEdit` · `SplitLines` · `JoinLines` 가 섰다. **한 줄이 한 이름**이라 더하기는 줄을
    쓰는 것이고 빼기는 줄을 지우는 것이다 - `+` / `-` 단추가 따로 없다. **버퍼는 부르는 쪽이 든다**:
    여러 줄 칸은 편집하는 동안 버퍼가 프레임을 넘어 살아 있어야 하고, 매 프레임 목록에서 새로 지으면
    커서와 선택이 풀린다. 빈 줄과 줄 끝 `\r` 은 이름이 되지 못한다 - 그 글자가 섞인 패턴은
    어떤 파일 이름과도 맞지 않는다.
    칸 자체는 **`TextField` 의 여러 줄 형태**다. 처음에는 `String` 과 문자 버퍼 사이를 옮겨 담는
    코드를 여기 한 벌 더 썼는데, 그 때문에 "아무도 치지 않았으면 값을 그대로 둔다" 는 줄이
    잴 수 없는 것이 되어 뮤테이션에서 살아남았다 - `TextField` 가 이미 같은 다리와 같은 시험을
    들고 있었다. 한 일을 두 곳에 두면 그중 하나는 시험 밖에 선다.
  - `WriteProjectFileText` 가 `AssetIgnorePatterns` 머리줄을 만나면 시퀀스를 새로 적고 원문의 항목
    줄들을 건너뛴다. 비어 있으면 `[]` 로 적고, 원문에 없고 값도 비었으면 아예 적지 않는다 -
    손대지 않은 파일이 저장만으로 길어지지 않게 한다.
  - **여기서 더 큰 것이 나왔다.** 실제 에디터로 확인하다 프로브 프로젝트 파일에 `ProductName:` ·
    `StartupCanvas:` · `LastOpenedCanvasPath:` 가 겹쳐 있는 것을 봤다. 원인 둘:
    **(1)** 값이 빈 아는 키(`ProductName: `)를 `hasValue` 로 걸러 "적지 않은 키" 로 세는 바람에,
    뒤에 같은 키를 하나 더 붙였다. **(2)** 파일 끝 `\n` 다음 자리를 빈 줄 하나로 세어
    저장할 때마다 빈 줄이 하나씩 쌓였다. 저장을 거듭할수록 프로젝트 파일이 벌어지고 있었다.
  - **불어나기를 멈추는 것만으로는 부족했다.** 이미 겹친 파일은 그대로 남고, 읽을 때는 마지막 줄이
    앞의 줄을 조용히 덮는다 - 값이 다르면 무엇이 맞는지 파일만 보고 알 수 없다. 매핑에 같은 키가
    두 번 있을 수 없으므로, 이미 적은 키를 다시 만나면 **그 줄을 지운다**(`MarkWritten`). 저장 한 번에
    낡은 파일이 고쳐진다. 모르는 키와 주석은 그대로 지나간다.
  회귀 테스트: 무시 패턴 시퀀스를 고쳐 쓰고 빈 목록은 `[]` 로 적는지, 같은 내용을 거듭 적었을 때
  **바이트 하나 달라지지 않는지**(프로젝트 파일 단위와 에디터 저장 단위 둘 다), 버퍼와 목록이
  빈 줄·`\r`·줄바꿈 없는 마지막 줄을 어떻게 다루는지를 잰다. 뮤테이션 일곱(빈 줄 세기 되돌리기·
  `hasValue` 되돌리기·시퀀스 다시 지나치기·`\r` 남기기·빈 줄을 이름으로 세기·목록 비우지 않기·
  줄바꿈 없이 잇기)이 전부 잡힌다.
  실제 에디터에서 프로젝트 설정의 `AssetIgnorePatterns` 칸에 `*.tmp` 를 넣고 저장하면
  `.jproject` 에 시퀀스로 적힌다.

- **D-188. 창을 닫는 것만으로도 보던 자리가 남는다. 저장은 늘 세션까지 적는다.** (2026-09-24)
  기존 엔진의 단축키 표를 우리 것과 항목 단위로 대조하다 나왔다. 아홉 개가 이름만 다르고 그대로인데,
  `Ctrl+S` 가 하는 일이 달랐다: 기존은 `EditorSessionPersistence::Save` 로 **캔버스를 적고 이어서 프로젝트를**
  적었고, 우리는 캔버스만 적었다. 거기서 더 큰 것이 나왔다 - **세션을 적는 길이 `프로젝트 저장` 과
  `CloseProject` 뿐인데, 창을 닫는 길은 그 둘을 지나지 않았다.** 카메라를 옮기고 `Ctrl+S` 로 저장한 뒤
  창을 닫으면 보던 자리와 보던 캔버스가 사라졌다. D-146 이 세션을 만든 뜻이 그 자리에서 깨져 있었다.
  - `Shutdown` 이 `CloseProject` 를 먼저 부른다. 그쪽이 **재생을 멈추고 세션을 적는 일을 이미 한 벌로**
    들고 있다 - 종료 경로에 같은 순서를 다시 쓰면 둘 중 하나만 고쳐질 자리가 생긴다.
  - 캔버스 저장이 늘 세션까지 적는다(기존과 같다). 파일로 열지 않은 프로젝트에는 적을 자리가 없으므로
    `SaveEditorSession` 이 그 경우를 건너뛴다.
  회귀 테스트: 저장을 **한 번도 하지 않고** 카메라만 옮긴 뒤 `Shutdown` 하고, 프로젝트 파일에서
  `CanvasViewCameraSize` 가 그 값으로 적혀 있는지 읽는다. 뮤테이션 둘(종료 경로에서 빼기·저장에서 빼기)이 잡힌다.

- **D-187. `primary` 를 켠 카메라가 없으면 첫 활성 카메라로 그린다.** (2026-09-24)
  실제 에디터에서 오브젝트를 만들고 `Camera2D` 를 붙인 뒤 재생을 눌렀는데 **게임 뷰가 "카메라가 없습니다" 였다.**
  카메라는 분명히 거기 있었고, `primary` 만 꺼져 있었다. 그 체크박스를 켜야 한다는 것을 화면의 어디에서도 알 수 없다.
  - `Camera2DSystem` 이 두 갈래로 고른다: **`primary` 를 켠 첫 활성 카메라**가 먼저고, 하나도 없으면 **첫 활성
    카메라**다. 기존 엔진도 지정이 없으면 첫 활성 카메라로 떨어졌다(`inspector.viewport.camera_auto`,
    `카메라 미지정 + 활성 카메라 %d개 — 폴백이 그중 첫 번째를 임의로 고릅니다`).
  - **`primary` 는 차례를 이긴다.** 지정한 카메라가 뒤에 있다고 해서 앞의 지정하지 않은 것으로 그리면
    지정이라는 것이 아무 뜻도 갖지 못한다. 대체로 고를 때는 **첫 번째만** 든다 - 뒤의 것으로 덮으면 순회 순서가
    바뀔 때마다 다른 카메라로 그려져 같은 캔버스가 실행할 때마다 다르게 보인다.
  - 게임 뷰가 **애매할 때 말한다**(기존 캔버스 인스펙터의 `camera_ambiguous` 경고 자리다): 지정한 것이 없는데
    활성 카메라가 둘 이상이면 `primary 카메라가 없어 첫 번째 활성 카메라로 그립니다` 를 노란 글씨로 덧붙인다.
    하나뿐일 때는 말하지 않는다 - 애매한 것이 없는데 경고를 띄우면 경고를 읽지 않게 된다.
  회귀 테스트: 지정이 없어도 그린다, 컴포넌트를 꺼야 그제야 안 그린다(에디터 쪽), 지정한 것이 뒤에 있어도 이긴다,
  지정이 없으면 늘 첫 번째다(프레임워크 쪽). **바뀐 계약을 담고 있던 기존 검사 셋을 함께 고쳤다** -
  `비-primary 는 건너뛴다`, `유일한 카메라를 끄면 그림이 사라진다`, 그리고 프레임을 새로 열지 않아 앞 프레임의
  카메라가 남던 자리. 뮤테이션 셋(대체 제거·마지막 것으로 덮기·지정 무시)이 잡힌다.

- **D-186. 캔버스에 배경색이 생겼다. 계층의 맨 위에 캔버스 줄이 선다.** (2026-09-24)
  기존 엔진의 인스펙터 함수 목록을 훑다 `DrawCanvasInspector` 가 나왔다. 그쪽은 캔버스마다 **배경색**을 가지고
  인스펙터에서 고쳤는데, 우리 캔버스 뷰는 고정된 어두운 회색으로 지워졌고 그 값을 고칠 길이 없었다.
  **사용자 확인을 받고 넣었다**(캔버스 파일 형식이 바뀌는 일이다).
  - `Canvas` 가 `Color backgroundColor` 를 든다. **렌더러가 아니라 캔버스가 드는** 이유는 배경이 그 씬이
    어떻게 보여야 하는가의 일부이기 때문이다 - 렌더러에 두면 캔버스를 바꿀 때마다 밖에서 다시 넣어 줘야 하고,
    그 자리를 빠뜨리면 앞 씬의 색이 남는다. `Canvas::Clear` 가 기본값으로 되돌리는 것도 같은 까닭이다.
  - `.jcanvas` 에 `BackgroundColor` 로 네 줄이 적힌다. **없어도 열린다** - 이 키가 생기기 전의 파일은
    기본값으로 열리고, 그것이 그때 화면에 나오던 색이다.
  - **캔버스 뷰가 이 색으로 지운다. 게임 뷰는 그대로 카메라의 `clearColor` 다.** 기존 엔진은 배경을 캔버스에만
    두었지만 우리 `Camera2D` 에는 이미 제 `clearColor` 가 있다(그쪽에는 없다). 게임에서 실제로 보일 색을 정하는
    것은 카메라이고, 캔버스의 색은 **편집하는 바탕**이다. 둘을 하나로 묶으면 카메라마다 다른 배경을 쓰는 길이 막힌다.
  - 고르는 자리는 **계층 맨 위의 캔버스 줄**이다(기존 계층도 캔버스 줄을 맨 위에 두었다). 고르기는 오브젝트·에셋과
    **셋이 배타**다 - 인스펙터는 하나만 보인다(D-120 과 같은 규칙).
  - 편집은 `SetCanvasBackgroundCommand` 다. 색 고르개를 끄는 동안 프레임마다 값이 바뀌므로 **같은 캔버스의
    잇단 편집은 합친다** - 그러지 않으면 끌기 한 번을 되돌리는 데 수십 번이 든다.
  회귀 테스트: 셋의 배타, 계층의 캔버스 줄을 누르면 골라짐, 커맨드 한 번이 되돌리기 한 칸, 합쳤을 때 되살릴 값이
  처음 것으로 남음, 파일 왕복(`BackgroundColor` 가 적히고 읽힌다), `Clear` 가 기본값으로 되돌림.
  캔버스 파일 형식 검사(`CanvasFileTests`)에 새 키를 적어 두었다 - 형식이 조용히 바뀌지 않게.
  **끌기 병합 자체는 테스트가 아니라 실제 에디터에서 확인했다** - 병합은 마우스를 누르고 있는 동안만 일어나고
  (매니저가 끌기 경계를 스스로 잰다), 그 조건을 테스트에서 만들지 못했다.
  기존 엔진과 남은 차이: **뷰포트 목록**(`inspector.canvas.viewports`)은 해당 없음이다 - 뷰포트라는 개념이 없다.

- **D-185. 스프라이트 뷰어에 확대·피벗 표시·가리킨 칸이 생겼다.** (2026-09-24)
  기존 엔진의 `sprite_viewer.*` 문구를 우리 것과 하나씩 맞춰 보다 셋이 빠진 것을 찾았다. 셋 다 **시트를 들여다보는**
  손잡이다 - 자르는 옵션을 고치는 창인데 정작 지금 무엇을 보고 있는지 알 길이 좁았다.
  - **확대**와 **창에 맞추기**: 배율이 `min(칸/그림, 32배)` 로 고정이라 큰 시트를 들여다볼 수도, 작은 것을 더 키울
    수도 없었다. 슬라이더를 두고 바닥을 `0.05`(기존과 같은 값)로 잡았다. 사람이 고르지 않았으면 `0` 이고 그때만
    칸에 맞춘다 - 지금 값을 기억해 두면 창을 늘렸을 때 다시 어긋난다.
  - **피벗 표시**: 시트의 칸마다, 그리고 미리보기에 십자를 찍는다. 피벗은 칸마다 다를 수 있어 시트 쪽에도 그린다.
    그림이 어느 점을 기준으로 놓이는지는 눈으로 봐야 안다 - `pivotX`·`pivotY` 숫자만으로는 알 수 없다.
  - **가리킴**: 마우스가 가리킨 칸의 번호와 `(x, y) 너비 x 높이` 를 시트 아래에 적고, 그 칸의 테두리를 다른 색으로
    그린다. 자르는 옵션을 고칠 때 지금 칸이 몇 픽셀인지가 유일하게 알고 싶은 값인데 그림만 봐서는 셀 수 없다.
    누를 칸을 찾는 셈도 이 하나로 합쳤다 - 가리킨 칸과 눌린 칸을 따로 세면 둘이 어긋날 자리가 생긴다.
  회귀 테스트: 세 손잡이가 시트 칸에 서 있고, 피벗을 켜면 화면 픽셀이 달라진다. 뮤테이션 다섯(십자 안 그리기·
  슬라이더 없애기·맞추기 단추 없애기·토글 없애기·고른 배율 무시)이 잡힌다.
  기존 엔진과 남은 차이: `스프라이트 열기...`·`탭 닫기`·`모든 탭 닫기` 메뉴는 두지 않는다 - 탭은 에셋 브라우저에서
  열고 탭의 `x` 로 닫는다(기존의 메뉴는 그 창에 메뉴바가 있어서 생긴 것이다). `미리보기 배율`은 미리보기가
  칸 크기에 맞춰 들어가므로 해당 없음이다.

- **D-184. 캔버스 뷰의 눈금을 픽셀로도 읽는다. 다만 PPU 는 프로젝트로 되돌리지 않는다.** (2026-09-24)
  기존 엔진의 에디터 소스 파일 목록을 우리 이식 대조표와 기계적으로 맞춰 보다 나왔다(그 대조에서 빠져 있던 두 줄,
  `Icons/FontAwesomeIcons` 와 `Localization/EditorLocalizationKeys` 도 이때 채웠다). 기존 캔버스 뷰에는 눈금을
  **월드 유닛과 픽셀 사이로 바꾸는 토글**이 있는데 우리에게는 없었다. 그림은 픽셀로 그려 오고 씬은 유닛으로 세므로,
  스프라이트를 자리에 맞출 때 그 둘을 머리로 곱하고 있어야 했다.
  - **곱하는 값을 어디서 가져올지가 이 결정의 요점이다.** 기존 엔진은 `.jproject` 의 `PixelsPerUnit` 을 썼는데,
    우리는 **D-117 에서 그 키를 없애고 PPU 를 에셋 단위로 두기로 이미 결정했다.** 그 결정을 모르고 프로젝트 키를
    되살리는 데까지 갔다가 되돌렸고, 사용자에게 물어 **에셋 PPU 의 기본값(`DefaultPixelsPerUnit`, 100)을 곱하는**
    쪽으로 정했다. 프로젝트 파일은 손대지 않으므로 D-117 이 그대로 선다.
  - 그 대가는 적어 둔다: **기본값이 아닌 PPU 로 들여온 그림에서는 눈금의 픽셀 값이 그 그림의 픽셀과 어긋난다.**
    한 프로젝트가 PPU 를 섞어 쓰기 시작하면 다시 볼 자리다.
  - 3D 에는 두지 않는다. 원근에서는 한 유닛이 거리마다 다른 픽셀이라 곱할 수가 없다.
  회귀 테스트: 단추를 누르면 눈금 숫자가 실제로 다시 그려지고(픽셀 차이), 단추 글자가 `단위: 유닛` ↔ `단위: 픽셀`
  로 오가며, 다시 누르면 유닛으로 돌아온다. 뮤테이션 넷(토글 무시·y 축만 빠뜨리기·글자 고정·한쪽으로만 켜기)이 잡힌다.
  기존 엔진과 남은 차이: `화면에 맞추기` 는 **해당 없음**이다 - 그쪽은 화면 공간 레이어가 있을 때만 뜨는 단추이고
  우리에게는 그 레이어가 없다. `버텍스 삭제` 도 해당 없음이다(폴리곤 콜라이더가 없다).

- **D-183. 레이어 이름 고치기가 글자마다 커맨드를 쌓고 있었다.** (2026-09-24)
  기존 엔진의 레이어 인스펙터를 읽다 나왔다. 그쪽에는 주석이 붙어 있다 - *"이름은 편집이 끝날 때 1회만 커맨드로
  커밋한다 — 글자마다 커밋하면 undo 가 글자 수만큼 쌓인다"*. 우리 계층의 레이어 메뉴는 정확히 그 주석이 경고한
  모양이었다: 매 프레임 `m_renameText != layer.GetName()` 을 견주어 다를 때마다 `RenameLayerCommand` 를 냈다.
  이름을 석 자 고치면 되돌리기가 세 번 든다.
  - `Widget::TextField::CommitOnFinish()` 는 **이미 있었다**(인스펙터의 오브젝트 이름 칸이 쓰고 있다).
    레이어 쪽만 그것을 지나쳤다 - 같은 일을 하는 두 자리 중 하나만 고친 전형이다.
  - `레이어 삭제` 가 마지막 하나일 때 잠기는데 까닭을 말하지 않았다. D-181 의 수로 `캔버스에는 레이어가
    하나 이상 있어야 합니다` 를 붙였고, 그러면서 남아 있던 `BeginDisabled` 쌍도 걷어 냈다.
  회귀 테스트: 계층에서 레이어 줄을 우클릭해 이름 칸에 석 자를 치는 동안 되돌리기 더미가 그대로이고, Enter 를
  누르면 **한 칸만** 쌓이며, 한 번 되돌리면 원래 이름으로 돌아온다. 뮤테이션 둘(글자마다 커밋으로 되돌리기·
  커맨드 자체를 막기)이 잡힌다.
  기존 엔진의 레이어 인스펙터와 남은 차이: `BlendMode`·`Opacity`·`ForceOwnTexture` 는 우리 `Layer` 에 없다
  (레이어가 자기 텍스처를 갖지 않는다, D-142). 남는 것은 이름과 보임인데 둘 다 계층의 줄과 메뉴에 이미 있다.

- **D-182. 에셋 브라우저에 파일 클립보드가 생겼다. 복제와 붙여넣기는 한 몸이다.** (2026-09-24)
  기존 엔진의 에셋 브라우저 메뉴와 항목 단위로 맞춰 보다 나왔다. 그쪽에는 **잘라내기·복사·붙여넣기**가 있는데
  우리에게는 끌어 놓기뿐이었다. 끌어 놓기는 **두 폴더가 한 화면에 같이 보일 때만** 쓸 수 있다 - 깊은 두 가지 사이를
  오가려면 잘라 두고 걸어가서 붙이는 길이 있어야 한다.
  - `EditorApplication::CopyAssetInto(상대경로, 폴더)` 하나가 붙여넣기와 복제를 함께 맡는다. **복제는 목적지가
    제자리인 복사**다 - 겹치지 않는 이름을 짓는 규칙이 둘로 갈리면 한쪽만 고쳐져 폴더마다 다른 이름이 나온다.
    빈 폴더에는 이름 그대로 가고(숫자를 붙일 까닭이 없다), 부딪히면 뒤에 숫자가 붙는다.
  - `.jmeta` 는 따라가지 않는다(D-175 와 같다). 뒤따르는 스캔이 새 아이디로 새 메타를 쓴다.
  - 클립보드는 **브라우저 안에 산다**. 잘라낸 것은 붙이면 한 번만 가고 클립보드가 빈다 - 남겨 두면 다음 붙여넣기가
    이미 없는 파일을 찾는다. 복사한 것은 남아서 여러 폴더에 잇달아 붙을 수 있다. 오브젝트 클립보드와 따로 산다(D-167 과 같은 까닭).
  - 폴더는 담지 않는다. 폴더를 통째로 옮기는 것은 안의 파일마다 아이디를 지켜야 하는 다른 일이고, 끌어 놓기가 이미 한다.
  - **키 조합은 적지 않았다.** 기존 엔진도 이 항목들에 단축키가 없고, `Ctrl+C`/`Ctrl+V` 는 오브젝트 쪽이 쥐고 있다.
    되지 않는 조합을 메뉴에 적는 것이 없는 것보다 나쁘다.
  회귀 테스트: `TestAssetFileOperationsCarryTheMeta` 에 다른 폴더로의 복사를 더했다 - 빈 폴더에서는 이름 그대로,
  두 번째는 숫자가 붙고, 복제가 같은 번호 규칙을 쓰며, 복사본의 아이디가 원본과 다르다. 뮤테이션 셋(늘 숫자 붙이기·
  목적지 무시·복제를 딴 길로 보내기)이 잡힌다.
  기존 엔진과 남은 차이: **`.meta` 보이기 토글**은 두지 않는다. 우리 브라우저는 디스크가 아니라 레지스트리를 보고,
  `.jmeta` 는 제 파일을 늘 따라다니는 속살이다 - 따로 보여 주면 짝을 깨뜨릴 길이 생긴다. **즐겨찾기 폴더**는
  기존 엔진에도 제목줄만 있고 안이 비어 있다(`추후 구현 예정` 주석).

- **D-181. 회색으로 잠긴 항목은 왜 잠겼는지 말한다.** (2026-09-24)
  메뉴를 기존 엔진과 맞춰 보다 나왔다. 그쪽은 저장 항목이 막혔을 때 `EditorSimulationGuard::GetSaveBlockedMessage`
  를 툴팁으로 띄웠는데, 우리는 어느 항목에도 그것이 없었다. **회색으로만 두면 무엇을 해야 켜지는지 알 수 없다** -
  D-180 의 `이미 추가됨` 과 같은 이야기이므로 같은 수를 쓴다.
  - `EditorShortcuts::WhyBlocked` 가 `CanExecute` **옆에** 산다. 떨어뜨려 놓으면 막는 조건을 고칠 때 한쪽만
    고쳐져 엉뚱한 까닭이 뜬다. 할 수 있으면 nullptr 다.
  - **막은 조건이 여럿이면 먼저 풀어야 하는 것을 말한다.** 프로젝트가 없는데 "복사해 둔 것이 없습니다" 라고 하면
    복사할 것을 찾다 끝난다. 자식으로 붙여넣기는 붙일 것이 없는 것과 들어갈 곳이 없는 것을 갈라 말한다.
  - 띄우는 자리는 `Widget::MenuItem` 의 네 번째 인자 하나다. 항목마다 `BeginDisabled` 와 `IsItemHovered` 를
    다시 쓰던 자리(오브젝트 메뉴의 `DisabledIf` 여섯, 인스펙터의 컴포넌트 붙여넣기)가 전부 이것으로 모였다.
  회귀 테스트: 할 수 있을 때는 말할 것이 없고, 고른 것이 없을 때와 프로젝트가 없을 때 서로 다른 문장이 나오며,
  붙일 것 없음과 들어갈 곳 없음이 같은 문장이 아니다. 뮤테이션 넷(전부 침묵·프로젝트 우선 규칙 제거·자식 붙여넣기의
  까닭 뭉개기·일시정지 침묵)이 잡힌다.

- **D-180. 컴포넌트에 갈래와 다중성이 생겼다. 하나만 붙는 것은 두 번 붙지 않는다.** (2026-09-23)
  인스펙터의 `컴포넌트 추가` 를 열어 보다 나왔다. 목록이 평평한 이름 다섯 줄이었고, **이미 붙어 있는 `Transform2D` 가
  그대로 다시 선택 가능**했다. 골랐다면 한 오브젝트에 트랜스폼이 둘 붙고, 그 뒤로는 조회(`FindComponentRaw`)가 먼저
  붙은 쪽만 돌려주므로 나중 것은 인스펙터에 보이면서도 화면에는 아무 영향을 주지 않는다. 기존 엔진은 이 자리에
  `CReflectionRegistry::CanAddComponent` 가 있어 항목을 회색으로 두고 "이미 추가됨" 툴팁을 띄웠다.
  - `ComponentTypeInfo` 에 `category` 와 `multiplicity` 가 붙었다(기존 `Type.Category`·`EComponentMultiplicity`).
    **기본은 `Multiple`** - 막아야 하는 타입은 등록하는 자리가 그것을 알고, 기본을 `Single` 로 두면 등록을 빠뜨린
    타입이 조용히 하나로 묶여 왜 두 번째가 안 붙는지 알 길이 없다. 2D/3D 모두 트랜스폼·카메라·강체가 `Single`,
    스프라이트/메시 렌더러와 콜라이더가 `Multiple` 이다(기존 엔진의 등록표와 같다).
  - **판정은 `ComponentRegistry::CanAttach` 한 곳**이고, 막는 자리는 `AddComponentCommand::Attach` 다.
    목록에서만 막으면 붙여넣기와 다시하기가 그 옆으로 지나간다 - 실제로 기존 엔진의 붙여넣기가 그 길로 새어
    트랜스폼을 둘씩 붙였다. 값만 붙여넣기(D-167)는 그대로다. 그쪽은 슬롯을 늘리지 않는다.
  - 목록은 `EditorActions::BuildAddComponentList` 하나가 만든다. **인스펙터의 드롭다운과 오브젝트 메뉴가 같은
    목록을 본다** - 한쪽에만 회색 규칙이 있으면 목록에서 막힌 것이 메뉴에서는 눌리고 아무 일도 일어나지 않는다.
  - `FilterCombo` 가 갈래 제목줄(`ItemGroups`)과 회색 항목(`ItemEnabled`·`DisabledTooltip`)을 받는다.
    **목록에서 빼지 않고 회색으로 남긴다** - 찾던 이름이 사라지면 어디로 갔는지를 다시 찾게 된다.
    Enter 는 보이는 첫 항목이 아니라 **고를 수 있는 첫 항목**을 고른다.
  - 오브젝트 메뉴(계층의 줄·캔버스 뷰)에 `컴포넌트 추가` 하위 메뉴가 생겼다(기존 `DrawAddComponentMenu` 가
    `CanvasViewTool`·`LayerTool` 두 곳에서 불리던 자리다). 갈래마다 하위 메뉴 하나다.
  - 타입 이름을 다듬는 세 줄(`DisplayTypeName`)이 인스펙터 안에만 있었다. `EditorNames` 로 옮겨 갈래 이름
    번역(`component_category.<갈래>`)과 함께 둔다(기존 `EditorReflectionLabels` 자리).
  회귀 테스트: 목록이 갈래로 묶이고 이미 붙은 것이 회색인지, 되돌리면 다시 붙일 수 있는지(`EditorApplicationTests`),
  같은 단일 컴포넌트의 붙여넣기가 거절되고 콜라이더는 둘씩 붙는지(컴포넌트 클립보드 테스트), 팝업이 갈래 제목줄만큼
  높아지고 Enter 가 회색 항목을 건너뛰는지(`EditorWidgetTests`). 뮤테이션 다섯(판정 무력화·커맨드 가드 제거·
  회색 무시·제목줄 생략·갈래 묶기 해제)이 모두 잡힌다.

- **D-179. 오브젝트 한가운데를 두 번 눌러도 안으로 들어간다.** (2026-09-23)
  화면을 훑다가 나왔다. 고른 오브젝트의 한가운데에는 **늘 기즈모의 가운데 손잡이**가 있는데, 캔버스 뷰가 두 번 누르기를
  입력 자리(`##canvas`)의 hover 로 재고 있어서 그 자리에서는 두 번 누르기가 **없는 일**이 되었다. 손잡이가 hover 를
  쥐고 있으면 그 값이 거짓이기 때문이다(D-170 과 같은 까닭). 게다가 집기 자체가 `기즈모에 손이 닿아 있으면 돌아간다`
  였으므로, 통과시켰더라도 그 자리에서 막혔다.
  - 마우스가 뷰 그림 위에 있는지는 `PointerInView` 한 곳이 잰다(맥락 메뉴와 같은 것을 쓴다).
  - **손잡이 위의 한 번 누르기는 여전히 고르기가 아니다** - 기즈모를 건드릴 때마다 선택이 바뀌면 여럿 골라 놓고
    옮길 수 없다. 두 번 누르기만 통과시킨다.
  - **같은 원인이 세 군데 더 있었다**: 휠 줌과 화면 끌기(`HandleCameraInput`), 사각 선택의 시작, 3D 집기.
    오브젝트의 한가운데에 마우스를 두면 **휠이 먹지 않고 화면도 끌리지 않았다**. 넷 다 `PointerInView` 를 쓴다.
  - **무언가를 끌고 지나가는 중이면 뷰는 손짓을 받지 않는다.** 에셋을 끌어 인스펙터로 가져가다 이 화면을 지나면
    그것이 사각 선택의 시작이 되어 꾸러미를 집어삼켰다(고치는 도중 테스트가 그것을 잡았다).
  회귀 테스트: 들어가 고르기 테스트(D-157)에 **오브젝트의 한가운데**를 두 번 누르는 경우와 **그 자리에서의 휠 줌**을
  더했다 - 그전까지 그 테스트는 팔(가운데에서 떨어진 자리)만 눌러서 이 자리를 재지 않았다.
  고치기 전에는 죽고, 뮤테이션(두 번 누르기 막기, 휠을 옛 hover 로 되돌리기)으로도 죽는다.

- **D-178. 재생하면 게임 뷰가 앞으로 온다. 카메라가 없으면 그렇다고 말한다.** (2026-09-23)
  화면을 훑다가 둘 다 나왔다.
  - **앞으로 오기**: 기존은 재생에서 `Editor::GameView->Focus()` 를 불렀다. 우리는 그것이 없어, 캔버스 뷰와
    탭으로 겹친 게임 뷰가 뒤에 있으면 **재생을 눌러도 화면이 그대로**였다 - 아무 일도 일어나지 않은 것처럼 보인다.
    `EditorPanel::RequestFocus` 를 두어(닫혀 있으면 함께 연다) 에디터가 그 프레임에 `SetNextWindowFocus` 로 올린다.
    패널 공통이라 다음에 같은 요구가 생기면 그대로 쓴다.
  - **카메라 없음**: 우리 게임 뷰는 **텍스처가 있는지**만 보고 상태를 골랐다. 텍스처는 카메라가 없어도 있으므로,
    카메라 없는 검은 화면을 `실행 중` 이라고 말했다 - 기존은 그 둘을 갈랐다. 엔진이 프레임마다 "게임이 낼 것을
    냈는가" 를 적고(`DidGameSubmitLastFrame`, 편집 화면의 제출은 세지 않는다) 게임 뷰가 그것을 본다.
  회귀 테스트: `TestPlayingAndStoppingRestoresTheCanvas` 에 게임 뷰를 닫아 두고 재생하면 다시 열려 앞에 서는지를
  더했고, `TestTheGameViewKnowsWhenNoCameraDrew` 는 카메라가 없을 때·있을 때·껐을 때를 잰다.
  뮤테이션: 앞으로 가져오는 줄 빼기, 늘 냈다고 적기 - 둘 다 죽는다.
  실제 에디터: 재생하면 게임 뷰가 앞으로 오고 `카메라가 없습니다` 가 뜬다.

- **D-177. 빠르게 친 글자가 하나씩 빠지던 것 - 입력은 꺼내 간 쪽이 비운다.** (2026-09-23)
  에디터에서 이름 칸에 `Beta` 를 치면 `Bea` 가 들어갔다. 화면을 훑다가 눈으로 잡았다.
  - **까닭**: 에디터는 한 프레임에 펌프를 **두 번** 돈다(UI 를 만들기 전 한 번, 엔진 `Tick` 이 한 번).
    그런데 펌프가 **먼저 비우고** 모으는 구조라, 엔진 펌프가 집어 간 입력은 아무도 꺼내 가지 못한 채
    다음 펌프에서 사라졌다. 프레임의 그 구간에 도착한 글자·클릭이 통째로 없어진다.
    입력을 꺼내 가는 쪽은 에디터 UI 하나뿐이어서(게임 쪽 입력 시스템은 아직 없다) 전부 UI 의 손실이었다.
  - **고침**: `PumpEvents` 는 쌓기만 하고, **꺼내 간 쪽이 `ClearInputEvents` 로 비운다**. 에디터는 UI 에 넣어 준
    직후 비우고, 꺼내 가는 쪽이 없으면(게임 호스트) 엔진이 프레임마다 비운다(`SetInputOwnedByHost`) -
    그러지 않으면 목록이 끝없이 자란다는 옛 테스트의 걱정이 그대로 남는다.
  회귀 테스트(`TestUnreadInputSurvivesAnotherPump`): 꺼내 가지 않은 입력이 다음 펌프에도 남고, 비우면 사라진다.
  옛 계약을 재던 테스트(`TestThePumpOwnsTheList` 와 `PostAndPump`)는 새 계약으로 고쳤다 - 그 테스트들이
  **손실을 정상으로 못 박고 있었다**.
  실제 에디터: 이름 칸에 `Beta` 를 빠르게 쳐서 온전히 들어가는 것을 확인했다.

- **D-176. 프로그램과 함께 놓인 것은 실행 파일 기준으로 찾는다.** (2026-09-23)
  **에디터를 다른 폴더에서 띄우면 영어로 떴다.** 글자 표(`Localization/`)와 아이콘 글꼴을 **현재 작업 폴더** 기준으로
  찾고 있어서, 개발 트리에서 띄울 때만 맞았다. 런처나 바탕화면 바로가기로 띄우면 둘 다 없는 자리를 보았다.
  실제 에디터를 다른 폴더에서 띄워 눈으로 확인했다 - 메뉴가 `File`·`Edit`·`Window` 였다.
  - `IPlatform::GetExecutableFolder()` 를 세우고(윈도우는 `GetModuleFileNameW`, 길이를 정해 두지 않는다),
    에디터가 상대경로를 그 폴더 기준으로 푼다. 절대경로는 그대로다.
  - **실행 파일 옆에 실제로 있을 때만 그리로 본다.** 개발 트리에서는 글꼴이 실행 폴더로 복사되지 않는다 -
    없는 것을 가리키면 그것까지 못 읽는다.
  - **아이콘 글꼴을 플랫폼으로 읽는다.** `fopen_s` 는 경로를 이 기계의 ANSI 코드페이지로 보므로, 사용자 폴더에
    한글이 있으면(`C:/Users/박주형/...`) UTF-8 절대경로를 열지 못한다 - 경로를 실행 파일 기준으로 바꾼 날
    그 자리에서 났다. 플랫폼의 읽기는 UTF-8 을 제대로 넓은 문자로 바꾼다(D-112).
  - **테마가 든 플랫폼 포인터는 에디터가 내려갈 때 비운다.** 정적 자리에 남겨 두면 다음 테마 적용이 죽은
    플랫폼으로 글꼴을 읽으러 간다 - 테스트가 그 자리에서 **세그폴트**로 죽었다.
  회귀 테스트(`TestTheEditorFindsItsFilesFromAnywhere`): 지금 폴더에서 한 번, 엉뚱한 폴더에서 한 번 띄워
  글자·아이콘·언어 수가 같아야 한다. **글자만 보면 못 잡는다** - 읽은 표가 전역에 남아 둘째 에디터가 표를 못 찾아도
  앞의 한국어가 나온다. 그래서 그 폴더에서 찾은 **언어 수**도 함께 본다(뮤테이션이 처음에 살아남아 알았다).

- **D-175. 에셋 브라우저의 복제·경로 복사·선택 개수.** (2026-09-23)
  기존 브라우저의 줄 메뉴에는 `복제`·`경로 복사`가 있었고, 여럿 골랐을 때는 맨 위에 `N개 선택됨` 을 적었다. 우리에게는
  셋 다 없어서, 같은 그림을 조금 다르게 쓰려면 탐색기로 나가 복사해 온 뒤 다시 훑어야 했다.
  - **`.jmeta` 는 따라가지 않는다.** 그 안에 아이디가 있어, 함께 베끼면 두 파일이 한 에셋 행세를 한다 -
    한쪽의 임포트 옵션을 고치면 다른 쪽을 가리키던 컴포넌트가 모르는 사이에 다른 그림을 그린다.
    새 아이디는 스캔이 매긴다(가져오기와 같은 길이다). 뮤테이션으로 메타까지 베끼게 하면 복제본이 아예 등록되지 않는다.
  - 이름은 `walk.png` → `walk1.png`, `walk2.png` 로 는다. 폴더는 복제하지 않는다 - 안의 파일마다 새 아이디를
    매겨야 해서 그냥 복사와 다르다.
  - `경로 복사` 는 **실제 경로**를 클립보드에 넣는다. 에셋 폴더 기준 상대경로로는 밖의 프로그램이 열지 못한다.
  - `N개 선택됨` 은 여럿 골랐을 때만 나온다. 지우기가 고른 것 전체에 가므로(D-141) 몇 개인지 모르고 누르면 놀란다.
  회귀 테스트(`TestDuplicatingAnAssetGivesItItsOwnId`): 복제본이 옆에 서고 등록되며 **아이디가 다르고**, 원본의
  아이디는 그대로이고, 한 번 더 복제하면 이름이 또 늘고, 폴더와 없는 파일은 거절한다.
  실제 에디터: `walk.png` 를 복제하면 `walk1.png` 이 서고 두 `.jmeta` 의 아이디가 다르다.

- **D-174. 캔버스를 새로 만들고 다른 것을 연다. 저장하지 않은 것은 묻고 버린다.** (2026-09-23)
  **에디터가 프로젝트 파일에 적힌 캔버스 하나만 편집할 수 있었다.** 새로 만들 길도, 다른 것을 열 길도 화면에 없었다.
  기존 엔진은 에셋 브라우저에서 `에셋 추가 ▸ 캔버스` 로 만들고 두 번 눌러 열었다. 이번 대조에서 찾은 **가장 큰 구멍**이다.
  - **`Canvas::Clear()`**(JBroCanvas): 오브젝트를 모두 없애고 레이어를 기본 하나로 되돌린다. 인스턴스는 버리지 않으므로
    프레임워크가 든 포인터가 산다. `ReadCanvasText` 는 빈 캔버스에만 들어가므로(섞으면 무엇이 파일에서 온 것인지 알 수
    없다) 여는 쪽이 먼저 이것을 부른다. 순회 중이면 거짓이다.
  - **`CreateCanvasAsset`**: 빈 캔버스의 글자는 **캔버스 자신이 낸다**(`WriteCanvasText`) - 기존은 상수 문자열이라
    형식이 바뀌면 그 자리만 옛 모양으로 남았다. 이름은 `NewCanvas.jcanvas`, `NewCanvas1.jcanvas`... 로 겹치지 않게 고르고,
    등록은 스캔이 한다. **레이어를 더 만들지 않는다** - 캔버스는 기본 레이어를 들고 태어나므로 하나 더 얹으면 파일에
    빈 칸이 둘로 적히고 열 때마다 칸이 늘었다(실제 에디터에서 `Default` 가 둘로 보였다).
  - **`RequestOpenCanvas`**: 프레임 밖에서 연다(D-93). 시뮬레이션을 멈추고, 고른 것·오브젝트 번호·되돌리기 더미·
    클립보드를 비운 뒤 캔버스를 비우고 읽는다. 읽다 실패하면 **빈 채로 남긴다** - 반쪽을 섞는 것보다 낫다.
  - **저장하지 않은 것을 말없이 버리지 않는다(기존보다 나은 점).** 기존은 그냥 갈아 끼웠다 - 한 시간 작업한 캔버스가
    더블클릭 한 번에 사라진다. 고칠 것이 남아 있으면 `저장하고 열기` / `저장하지 않고 열기` / `취소` 를 묻는다.
    저장이 실패하면 열지 않는다. 그 물음을 위해 **`ConfirmPopup`**(단추 셋, 닫으면 취소)을 세웠다 - `MessagePopup` 은
    확인 하나뿐이다.
  - 들어가는 자리: 에셋 브라우저의 빈자리 메뉴 `새 캔버스`, 줄 메뉴 `캔버스 열기`, 캔버스 파일 더블클릭.
  회귀 테스트(`TestTheEditorMakesAndOpensCanvases`): 만들면 파일이 서고 등록된다, 이름이 겹치지 않는다, 열면 지난 내용과
  고른 것·되돌리기가 비고 칸이 늘지 않는다, 저장한 것은 다시 열면 돌아온다, 고친 채로 열려 하면 묻고 그동안 아무것도
  바뀌지 않는다, 취소하면 그대로다, `저장하지 않고 열기` 를 고르면 그때 바뀐다.
  뮤테이션: 비우기를 건너뛰기, 묻는 길을 빼기 - 둘 다 테스트가 죽는다(뒤의 것은 처음에 살아남아, 두 캔버스의 오브젝트
  수가 같아 가릴 수 없던 것을 고쳤다).
  실제 에디터: `새 캔버스` 로 `NewCanvas.jcanvas` 가 서고 바로 열린다(칸은 하나). 고친 채로 다시 만들면 물음이 뜨고,
  `저장하지 않고 열기` 를 고르면 그때 바뀐다.

- **D-173. 에셋 인스펙터가 무엇을 보고 있는지 적는다. 그리고 경로 유틸을 한 벌로.** (2026-09-23)
  기존 에셋 인스펙터는 머리에 `에셋`·`GUID`·`경로`·`임포터` 를 적고, 그림이면 `뷰어에서 열기` 를 두었다. 우리는 상대경로
  한 줄뿐이라, 그 파일이 어떤 종류로 등록되었는지와 번호가 무엇인지 알려면 `.jmeta` 를 직접 열어야 했다.
  - 머리 네 줄(`에셋`·`종류`·`경로`·`아이디`)은 다른 칸과 같은 줄 배치(`FormLayout`)를 쓴다. 아이디는 32 자리라
    좁은 칸에서 줄을 바꾼다 - 잘라 보이면 그 번호로 파일을 찾을 수 없다.
  - **`뷰어에서 열기`**: 그림 에셋이면 인스펙터에서 바로 스프라이트 뷰어를 연다. 에셋 브라우저에서 두 번 누르는 길만
    있어서, 옵션을 고치다 칸을 확인하려면 브라우저로 건너가야 했다.
  - **경로 유틸을 진짜 한 벌로 모았다**(`JBro/Editor/EditorPaths.h`). `LeafOfPath`·`FolderOf`·`JoinPath` 가
    `EditorApplication` 과 에셋 브라우저에 **따로** 있었고(대조표에는 "완료" 로 적혀 있었다), 인스펙터가 셋째 벌을 쓸
    뻔했다. 구분자는 `/` 와 `\` 를 둘 다 받는다.
  회귀 테스트(`TestThePathHelpersAgreeOnOneAnswer`): 잎·폴더·잇기의 경계(구분자 없음·끝 구분자·빈 쪽·널).
  실제 에디터: `art/ell.png` 를 고르면 `에셋 ell.png / 종류 Texture / 경로 art/ell.png / 아이디 1c99...` 가 뜨고
  `뷰어에서 열기` 로 뷰어가 열린다.

- **D-172. 캔버스 뷰가 지금 상태를 화면에 적는다.** (2026-09-23)
  기존 캔버스 뷰는 왼쪽 위에 활성 캔버스·고른 것·편집 카메라·들어간 오브젝트를 적었다. 우리는 들어간 오브젝트 하나만
  띠로 적고 나머지는 없어서, 배율을 얼마나 당겨 두었는지와 몇 개를 골랐는지가 인스펙터를 봐야 드러났다.
  - 두 줄이다: `선택: 이름`(여럿이면 `외 N개`)과 `카메라 (x, y) 크기 s`. 3D 는 좌우·위아래 각과 거리를 적는다.
    들어가 있으면 셋째 줄에 그 사실과 나오는 법이 온다(`DrawFocusBanner` 가 여기로 합쳐졌다).
  - **고른 것을 적는 규칙은 `EditorApplication::DescribeSelection` 이 갖는다.** 상태를 아는 쪽이 그 글도 낸다 -
    화면마다 다시 쓰면 이름 없는 것과 여럿 고른 것을 저마다 다르게 적는다. 좁은 칸에도 넘치지 않는다.
  - 활성 캔버스 줄은 두지 않았다. 프로젝트가 없으면 캔버스 뷰가 아예 "열린 프로젝트가 없습니다" 를 말한다 -
    늘 참인 줄을 매 프레임 적을 자리가 아니다.
  회귀 테스트(`TestTheEditorSaysWhatIsChosen`): 아무것도 안 골랐을 때·하나·여럿·이름 없는 것·짧은 칸.
  뮤테이션: 하나 고른 자리를 여럿과 같은 글로 만들면 죽는다.
  실제 에디터: `선택: GameObject` 와 `카메라 (0.00, 0.00) 크기 5.00` 이 왼쪽 위에 선다.

- **D-171. 기즈모의 로컬·월드 축.** (2026-09-23)
  기존 기즈모 툴바의 넷째 단추(`L`/`W`)다. 우리 기즈모는 손잡이를 늘 오브젝트의 축에 두어서,
  **45도 돌아간 것을 오른쪽으로 곧게 미는 길이 없었다** - 두 축으로 나눠 밀거나 값을 직접 쳐야 했다.
  - 캔버스 뷰 툴바에 `로컬`/`월드` 단추가 선다. 월드면 손잡이에 주는 주체의 회전만 지운다 -
    옮기기와 돌리기는 이미 **월드 델타**로 쓰므로(`GizmoEditing::Write`) 그 한 줄이면 축이 바뀐다.
  - **크기는 늘 로컬이다.** 월드 축으로 늘린 결과는 로컬 스케일 세 값으로 적을 수 없다(기울어진 변형이 된다).
    크기 모드에서는 단추를 잠그고, 까닭은 툴팁이 말한다.
  회귀 테스트(`TestTheGizmoCanWorkInWorldAxes`): 90도 돌린 오브젝트의 오른쪽에는 로컬에서 손잡이가 없어
  끌어도 그대로이고, 월드로 바꾸면 그 자리의 손잡이가 오브젝트를 오른쪽으로만 민다(회전은 그대로).
  뮤테이션: 월드에서 회전을 지우지 않게 하면 죽는다.
  실제 에디터: 45도 오브젝트의 손잡이가 `로컬` 에서는 기울어 있고 `월드` 에서는 화면의 축 그대로다.

- **D-170. 캔버스 뷰에서 오브젝트를 우클릭하면 그 오브젝트의 메뉴가 뜬다.** (2026-09-23)
  기존 캔버스 뷰는 누른 자리에 오브젝트가 있으면 그것의 메뉴(자식 추가·복사·붙여넣기·삭제)를, 빈 곳이면 빈자리 메뉴를
  냈다. 우리는 무엇을 눌러도 빈자리 메뉴만 나와서, 캔버스에서 고른 것을 지우려면 계층 창으로 건너가야 했다.
  - 계층 줄의 메뉴를 `EditorActions::DrawObjectMenu` 로 올려 **두 화면이 같은 한 벌을 쓴다**(D-132). 계층 패널에
    있던 몸통이 그대로 옮겨 갔다 - 두 벌이 되면 한쪽만 고쳐지는 날이 온다.
  - **메뉴를 여는 때는 캔버스 뷰가 정한다.** ImGui 의 창 메뉴는 그 자리에 위젯이 있으면 열지 않는데(`NoOpenOverItems`),
    고른 오브젝트 위에는 늘 기즈모 손잡이가 있다 - **오브젝트의 한가운데를 우클릭하면 아무 메뉴도 열리지 않았다**
    (실제 에디터에서 그랬다). 입력 자리(`##canvas`)의 hover 도 쓸 수 없다. 그것은 기즈모를 그리기 전에 재는데 손잡이가
    앞 프레임부터 hover 를 쥐고 있으면 거짓이다. 창 위에 마우스가 있고 그 자리가 뷰 안이면 연다.
  - 오른쪽 단추로 화면을 끈 뒤에는 열지 않는다(`m_panMoved`). 그 판단은 여는 자리보다 위에 그대로 있다.
  회귀 테스트(`TestRightClickingAnObjectInTheCanvasViewOpensItsMenu`): 오브젝트 위에서 우클릭하면 `삭제` 가 있고
  누르면 지워지며 되돌릴 수 있다, 그때 그 오브젝트가 고른 것이 된다, 빈 곳에서는 `삭제` 가 없다.
  뮤테이션: 누른 자리의 오브젝트를 찾지 않게 하면 죽는다.
  실제 에디터: 기즈모 한가운데(654, 320)에서 우클릭해도 메뉴가 뜨고, 빈 곳은 여전히 `오브젝트 추가`·`붙여넣기` 다.

- **D-169. 레이어 창에서 Shift 는 범위다.** (2026-09-23)
  기존 `LayerTool` 은 선택 기준점(`m_selectionAnchorGuid`)을 두고 Shift 로 찍은 줄까지를 통째로 골랐다. 우리는 Shift 를
  Ctrl 과 똑같이 하나씩 넣고 빼는 것으로 두어, 줄이 스무 개면 스무 번 찍어야 했다. **에셋 브라우저는 이미 범위 선택을
  하고 있었다** - 같은 손짓이 창마다 다르게 굴었다.
  - 이번 프레임에 **실제로 그린 줄**만 차례대로 모아 그 사이를 고른다. 접힌 자식과 검색에 걸러진 줄은 들어오지 않는다 -
    보이지 않는 것이 딸려 오면 무엇을 고른 것인지 화면에서 알 수 없다.
  - **줄을 다 그린 뒤에 고른다.** 기준과 찍은 줄 사이에는 아직 그리지 않은 줄이 있다.
  - **기준은 Shift 로 바뀌지 않는다.** 같은 기준에서 다시 재므로 Shift 를 누른 채 위아래로 옮기면 범위가 늘고 준다.
    기준을 옮기는 것은 그냥 찍기와 Ctrl 로 찍기다.
  회귀 테스트(`TestShiftClickingTheHierarchyPicksTheWholeRange`): 그냥 찍으면 하나, Shift 로 찍으면 사이까지 셋,
  다시 찍으면 같은 기준에서 둘로 줄고, Ctrl 로 찍은 줄이 다음 범위의 기준이다. 뮤테이션: 범위를 찍은 줄 하나로
  좁히면 죽는다.

- **D-168. 새로 놓는 것이 어디로 가는가 - 누른 자리와 레이어. 그리고 되돌린 오브젝트가 레이어를 잃던 것.** (2026-09-23)
  기존 `DrawAddObjectMenu(canvas, parent, spawnWorldPos, layer)` 와 `ResolveTargetLayer` 자리다. 우리는 셋 다 없어서
  캔버스 뷰 어디에 우클릭하든 오브젝트가 원점에 생겼고(화면 밖이면 찾으러 화면을 끌어야 했다), 고른 것이 어느 레이어에
  있든 새 오브젝트는 기본 레이어로 갔다.
  - **찾다가 버그를 하나 만났다.** 나무 스냅샷(`ObjectTreeSnapshot`)이 레이어를 안 담고 있어서, 기본 레이어가 아닌 곳의
    오브젝트를 **지웠다 되돌리면 기본 레이어로 돌아왔다**. 복사·붙여넣기도 같았다. 화면에서는 오브젝트가 다른 칸으로
    옮겨 간 것으로만 보여, 되돌리기가 조용히 편집을 하나 더 한 셈이다. 항목에 `layer` 를 더해 뜨고 되살린다.
    그 번호의 레이어가 그 사이에 사라졌으면 기본 레이어다 - 없는 번호를 들고 있으면 어느 칸에도 나오지 않는다.
  - **자리와 레이어는 만들기 커맨드 안에 있다.** 레이어 칸의 `오브젝트 추가` 는 만들기와 레이어 옮기기를 둘로 쌓아
    실행 취소가 두 번 들었다. 이제 `CreateObjectCommand` 가 둘 다 받아 한 번이다.
  - **커맨드는 `Vec2` 인지 `Vec3` 인지 모른다.** 트랜스폼의 `position` 잎사귀가 내놓는 필드를 앞에서부터 채우므로
    둘 다 맞는다. 타입을 견주기 시작하면 프레임워크가 늘 때마다 그 자리가 는다.
  - **`ResolveTargetLayer` 는 한 곳이다**(`EditorActions`): 부모가 있으면 부모의 레이어, 없으면 고른 것의 레이어,
    둘 다 없으면 캔버스 기본 레이어다. 부모가 고른 것보다 먼저다 - 자식만 다른 칸에 있으면 부모를 감춰도 자식이 남는다.
  - 캔버스 뷰는 **메뉴가 열릴 때의** 마우스 자리를 묻는다(`GetMousePosOnOpeningCurrentPopup`). 그 뒤에 마우스는
    항목 위로 움직인다. 3D 뷰는 자리를 대지 않는다 - 화면 한 점이 월드의 한 점으로 정해지지 않는다.
  회귀 테스트 둘: `TestANewObjectLandsWhereItWasAskedFor`(자리를 대지 않으면 원점, 대면 그 자리·그 레이어에 되돌리기
  한 번, 고른 것의 레이어를 따름, 부모가 고른 것보다 먼저), `TestAnObjectKeepsItsLayerThroughDeleteAndPaste`(지우기
  되돌리기와 붙여넣기가 레이어를 지킴, 사라진 레이어면 기본 레이어). 뒤의 것은 **고치기 전에 먼저 죽었다.**
  뮤테이션: 자리 쓰기 빼기, 고른 것의 레이어 보지 않기 - 둘 다 테스트가 죽는다.
  실제 에디터: 캔버스 뷰의 (3.68, 2.68) 근처에서 우클릭해 만들면 그 자리에 생기고, 새 레이어 칸의 `오브젝트 추가` 로
  만든 것은 그 칸에 들어가며 `실행 취소` 한 번에 사라진다.

- **D-167. 컴포넌트를 값째로 옮긴다(기존 `DrawCopyComponentMenuItem`·`DrawPasteComponentMenuItem`).** (2026-09-23)
  인스펙터의 컴포넌트 머리 메뉴에 `컴포넌트 복사`·`컴포넌트 붙여넣기`·`값만 붙여넣기`가 섰다. 기존 엔진에도 같은 자리에
  복사와 붙여넣기가 있었는데 우리에게는 없어서, 다른 오브젝트에 같은 설정을 주려면 필드를 하나씩 손으로 옮겨 적어야 했다.
  - **뜨는 길은 이미 있던 것을 쓴다.** `ComponentSnapshot`·`CaptureComponent`·`ApplyComponent`(D-72·D-76)가 오브젝트
    삭제와 복사에 쓰이던 그 길이다. 클립보드는 스냅샷 하나다.
  - **오브젝트 클립보드와 따로 산다.** 하나로 합치면 오브젝트를 복사해 둔 채로 컴포넌트를 옮기는 순간 둘 중 하나가
    지워진다. 프로젝트를 닫을 때 둘 다 비운다 - 값 안에 이 프로젝트의 에셋 번호가 들어 있다.
  - **붙여넣기는 커맨드다.** 기존은 붙여넣기를 되돌릴 수 없었다(커맨드를 거치지 않고 바로 역직렬화했다). 우리는
    `AddComponentCommand` 가 값을 들고 붙이므로 되돌리기는 떼는 것이고 다시 하기는 값까지 돌아온다. 값을 못 써 넣으면
    붙인 것도 도로 뗀다 - 기본값짜리 반쪽을 남기고 성공했다고 말하지 않는다.
  - **`값만 붙여넣기` 는 기존에 없던 것이다.** 기존은 늘 같은 타입을 하나 **더** 붙였는데, `Transform2D` 처럼 하나만
    있어야 뜻이 서는 타입에서는 쓸 수 없는 손짓이다. 떠 둔 것이 그 컴포넌트와 같은 타입일 때만 켜지고
    (`CanPasteComponentValues`), 덮기 전 값을 먼저 뜨지 못하면 덮지 않는다(D-76 과 같은 규칙). 하나 더 붙이는 쪽도
    기존 그대로 남겨 두었다 - 같은 타입을 여럿 붙이는 컴포넌트가 있다.
  - 프로퍼티를 등록하지 않은 타입은 **복사부터 거절한다.** 붙여 봐야 기본값 하나가 생길 뿐이고, 거절한 복사는
    클립보드를 건드리지 않는다.
  회귀 테스트(`TestCopyingAComponentPastesItsValuesOntoAnotherObject`): 값째로 붙는다, 되돌리면 떨어지고 다시 하면
  값까지 돌아온다, 한 번 더 붙이면 슬롯이 는다, 값만 붙이면 슬롯이 늘지 않고 되돌리면 옛 값이 돌아온다, 다른 타입에는
  덮지 않는다, 뜰 수 없는 타입은 복사가 거절되고 클립보드가 그대로다, 프로젝트를 닫으면 비운다.
  뮤테이션: 값 적용 건너뛰기, 복사 실패 무시, 닫을 때 비우지 않기, 값 붙여넣기의 되돌리기 비우기, 타입 검사 빼기 -
  다섯 모두 테스트가 죽는다.
  실제 에디터: 한 오브젝트의 `Transform2D` 를 복사해 다른 오브젝트에 붙이면 슬롯이 하나 늘며 값이 따라오고,
  `값만 붙여넣기` 는 있던 슬롯의 값만 바뀐다. 둘 다 `편집 > 실행 취소` 로 돌아온다. 클립보드가 비어 있으면 회색이다.

- **D-166. 붙여넣기가 하나 모자랐다 - `자식으로 붙여넣기`(Ctrl+Shift+V).** (2026-09-23)
  기존 단축키 표(`CEditorShortcutManager`)의 `PasteObjectsAsChild` 자리다. 우리 표에는 `Paste` 만 있어서, 복사한 것을
  고른 오브젝트 **안에** 넣으려면 붙여넣고 나서 계층에서 다시 끌어다 넣어야 했다.
  - `EditorApplication::PasteClipboard(bool asChild = false)` 하나로 둘을 모두 처리한다. 기본값은 지금까지와 같은
    형제 붙이기(고른 것의 부모 밑)이고, `asChild` 면 고른 것이 곧 부모다. 커맨드는 그대로 `PasteObjectsCommand` 라
    되돌리기도 한 번이다. **고른 것이 없으면 형제 붙이기와 똑같이 뿌리에 붙는다** - 붙일 곳이 없다고 거절하면
    빈 계층에서 키가 먹통이 된다.
  - 들어가는 자리는 셋이고 전부 같은 곳을 부른다: 편집 메뉴(`DrawShortcutItem`), 계층 줄의 오른쪽 단추 메뉴
    (`EditorActions::DrawPasteAsChildItem` - 메뉴를 연 그 줄이 곧 부모이므로 먼저 그 줄을 고른 것으로 삼는다), 단축키 표.
    빈자리 메뉴에는 넣지 않는다 - 부모가 없는 자리다.
  - 단축키는 `Ctrl+Shift+V` 다. 누른 키를 재는 `Pressed` 가 `KeyShift` 를 **정확히** 견주므로 위에 있는 `Ctrl+V` 가
    먼저 잡아채지 않는다. 할 수 있는지 재는 값(`CanExecute`)은 `클립보드 + 고른 것`이고, 메뉴의 회색 여부도 같은 값을 본다.
  회귀 테스트(`TestCopyAndPasteMakeASiblingAndSelectIt` 에 이어 붙였다): 자식으로 붙이면 고른 것 안에 들어가고 되돌리면
  빠진다, 고른 것이 없으면 뿌리에 붙는다, 표의 글자가 `Ctrl+Shift+V` 이고 고른 것이 없으면 할 수 없다.
  뮤테이션: `asChild` 를 무시하게 되돌리기, `CanExecute` 에서 고른 것 검사 빼기 - 둘 다 테스트가 죽는다.
  실제 에디터: 오브젝트 둘 중 첫째를 복사하고 둘째 줄에서 `자식으로 붙여넣기` → 둘째 밑에 들어가고, 편집 메뉴의
  `실행 취소` 로 되돌아온다. 클립보드가 비어 있을 때는 두 붙여넣기가 모두 회색이다.

- **D-165. 인스펙터에서 스프라이트 프레임을 고른다. 그리고 프로젝트를 다시 열면 터지던 것.** (2026-09-23)
  기존 인스펙터의 `DrawSpriteFramePickRow` 다. `frameIndex` 줄 밑의 단추를 누르면 스프라이트 뷰어가 그 시트로 열리고,
  칸을 누르면 그 번호가 커맨드로 들어간다. 번호를 외워 치는 대신 그림을 보고 고른다 - 칸이 마흔 장을 넘으면 번호만으로는
  찾을 수 없다.
  - **인스펙터는 여전히 컴포넌트 타입을 모른다.** 기존은 타입마다 그리는 법을 인스펙터가 알았다. 우리는 (타입, 필드) →
    그리는 함수 표(`InspectorFieldExtras`)를 두고, 인스펙터는 맨 위 필드마다 그 표를 묻기만 한다. 매 프레임 도는 길이라
    글자가 아니라 `ComponentTypeId` 와 인턴한 `NameId` 를 견준다. 리플렉션 메타에 넣는 길은 `PropertyEditInfo` 가
    스크립트 DLL 경계를 넘는 POD(크기 단언)라 접었다.
  - 줄은 **값 칸**에 둔다. 처음에 줄 전체(`FullRow`)로 두었더니 첫 칸(라벨 칸)에 그려져 라벨 칸이 넓어지고 위의 모든
    값 칸이 밀렸다(에셋 칸을 찾는 테스트가 그래서 깨졌다). 못 누르는 까닭("스프라이트를 먼저 지정하세요")은 툴팁이 말한다 -
    옆에 글로 두면 좁은 칸에서 잘렸다.
  - **고르는 동안 고른 것을 바꾸지 않는다.** 뷰어는 열면서 그 그림을 고른 에셋으로 삼는데(옵션 칸 때문이다), 그러면
    인스펙터가 오브젝트를 떠나 방금 고친 `frameIndex` 가 보이지 않았다(실제 에디터에서 그랬다).
  - 필드 이름으로 커맨드의 길을 만드는 기즈모의 지역 함수를 `SetPropertyCommand::MakeFieldPath` 로 올려 둘이 같이 쓴다.
  - **`CloseProject` 가 이 프로젝트를 가리키는 것을 다 비운다.** 고른 것·오브젝트 번호·되돌리기 더미는 프로젝트를 바꾸는
    쪽만 비웠다 - `CloseProject` 뒤에 바로 다시 열면 옛 번호가 죽은 오브젝트를 가리켰다.
  - **그림·외곽선 캐시가 죽은 에셋 시스템을 가리켰다.** 그 둘은 UI 를 켤 때 한 번 이어 두는데, 에셋 시스템은 프로젝트를
    열 때 생기고 닫을 때 사라진다. **프로젝트를 다시 열고 그림을 하나 달라고 하면 그 자리에서 터졌다** - 테스트가 거기서
    죽어서 찾았고, 실제 에디터에서도 프로젝트를 바꾼 뒤 그림 하나에 꺼진다는 뜻이다. 열 때마다 다시 잇고(`BindAssetTools`)
    닫을 때 끊는다. 회귀 테스트: 그림이 있는 프로젝트를 열어 썸네일을 받고, 닫고, 다른 프로젝트를 열어 다시 받는다.
  실제 에디터: 오브젝트에 `SpriteRenderer2D` 를 붙이고 walk.png 를 지정한 뒤 "프레임 고르기" → 뷰어의 넷째 칸 → 돌아오니
  오브젝트가 그대로 골라져 있고 `frameIndex` 가 3, 캔버스 뷰에 노란 칸이 보인다. 뮤테이션: 칸을 커맨드로 쓰지 않기,
  고르는 동안 선택 바꾸기, 프로젝트 열 때 캐시 다시 잇지 않기 - 각각 되돌리면 죽는다.

- **D-164. 경로 칸에 "찾아보기" 가 선다(기존 `ImPathField`).** (2026-09-23)
  열림으로 두었던 까닭(플랫폼에 폴더 고르기가 없다)이 D-160 에서 풀렸다. 기존은 경로 칸과 단추를 짝지은 함수
  (`DrawReadOnlyPathWithFolderBrowse`·`...FileBrowse`)를 빌드 설정 창에서 썼다. 우리는:
  - **`Widget::PathField`**: 글자 칸 + 폴더 그림 단추(툴팁 "찾아보기"). 단추는 **알리기만 한다** - 대화상자는 막히는
    호출이라 프레임 안에서 열 수 없다(D-93).
  - **`EditorApplication::RequestBrowsePath`**: 프레임이 끝난 뒤 폴더 또는 파일(종류를 걸어) 대화상자를 열고, 고른 경로를
    **기준 폴더 기준 상대경로**로 돌려준다. 기준은 프로젝트 폴더이고, 캔버스 경로는 에셋 폴더다. 절대경로로 적으면
    프로젝트를 옮기는 순간 깨진다. 취소하면 아무것도 돌려주지 않는다.
  - **`MakeProjectRelativePath`·`MakeFolderRelativePath`**(JBroHost): `ResolveProjectRelativePath` 의 거꾸로. 대소문자와
    `\`·`/` 를 가리지 않고, `C:/Game` 과 `C:/GameAssets` 를 가른다. 밖의 경로는 그대로다.
  - 프로젝트 설정의 경로 여섯(`AssetDirectory`·`ScriptSourceDirectory`·`ScriptOutputLibraryPath`(*.dll)·
    `LastOpenedCanvasPath`(*.jcanvas)·`OutputDirectory`·`StartupCanvas`(*.jcanvas))이 한 함수(`DrawPathValue`)로 선다.
  처음에는 기존처럼 "찾아보기..." 글자 단추였는데, 오른쪽 도크의 좁은 설정 창에서 단추가 칸을 먹어 경로가 몇 글자만
  보이고 단추마저 잘렸다(실제 에디터에서 그랬다). 폴더 그림 단추로 바꿔 경로가 읽힌다.
  확인: 실제 에디터에서 폴더 단추 → "폴더 선택" 대화상자 → 프로젝트의 `Assets\art` 를 고르면 칸에 `Assets/art` 가 들어간다.
  테스트: 상대경로 여섯 경우, 요청이 그 프레임에는 돌아오지 않고 다음 틱에 한 번 폴더로 묻고 `Contents/Art` 로 돌아오며,
  취소하면 돌아오는 것이 없다. 상대경로 바꾸기를 빼면 테스트가 죽는다.

## Assumptions

- 대상은 `Documents/GitHub/JBroEngine` 신규 리포다. 기존 엔진은 **읽기 전용 기준**으로만 쓴다.
- 기초가 서면 기존 엔진을 이 구조로 마이그레이션한다.
- Windows / D3D12 를 먼저 세웠다. D3D11(D-107)·Vulkan(D-108)이 같은 계약 위에 섰다. WebGPU / Android 는
  모듈 규칙만 유지한 채 뒤로 미룬다.

## Historical Findings — 신규 리포 vs 기존 엔진

> Stage B/C 직후 작성한 비교 스냅샷이다. 아래 `잔여`와 `담당` 열은 현재 상태가 아니며,
> 구현 여부는 문서 상단의 Current Audit Snapshot과 현재 코드·테스트로 확인한다.

기존 엔진 구조 (읽어서 확인한 것):

```
CGameCanvas
 ├─ TObjectPool<CGameObject> m_objectPool        청크 32슬롯 · 주소 불변 · ForEachObject
 ├─ m_componentPools  정렬 [TypeKey → TObjectPool<T>]   ForEach<T>
 ├─ m_layers          CGameLayer
 └─ 시스템 소유       AddSystem<TSystem>

CGameObject : GameInstance, EnableSafeFromThis
 ├─ Transform2D Local / WorldTransform2D World          ← 멤버
 ├─ SafePtr<CGameLayer> m_layer                          ← GetLayerIndex() O(1)
 ├─ SafePtr<CGameObject> m_parent / m_children           ← 계층도 멤버
 └─ vector<SafePtr<CComponent>> m_components             ← 논리 소유
```

| # | 항목 | 기존 엔진 | Stage B/C 결과 | 잔여 | 담당 |
|---|---|---|---|---|---|
| F1 | 중간 계층 | Canvas 직접 | Canvas 직접 (World 제거됨) | — | ✓ |
| F2 | 오브젝트 식별 | InstanceGuid | `InstanceId` (24B `Ref`) | 생성 시 발급 (D-20) | W-ref |
| F3 | 컴포넌트 소유 | 오브젝트 `vector<SafePtr>` | 오브젝트 `Array<raw*>` | SafePtr 리트로핏 | W-ref |
| F4 | Transform | 오브젝트 멤버 (기존은 2D-only) | 컴포넌트 (POD) | `ComponentBase` 파생 유지 | W-framework (B5). D-3 완화됨 |
| F5 | 컴포넌트 성격 | 다형성 | 여전히 POD (일부) | `ComponentBase` 파생 | W-framework (B5) |
| F6 | 계층 | 오브젝트 멤버 | 오브젝트 멤버 (raw*) | SafePtr 리트로핏 | W-ref |
| F7 | 레이어 소속 | `SafePtr<Layer>` + O(1) 인덱스 | `uint32 m_layerIndex` 만 | `SafePtr<Layer>` + 인덱스 캐시 | W-ref |
| F8 | 순회 | `ForEach<T>` | `Canvas::ForEach<T>` 시그니처만 | 구현 | W-ref |
| F9 | ComponentTypeId | `MakeStableTypeId` | `MakeStableTypeId` 있음 | 사용 강제 | W-framework (B5) |
| F10 | 활성 게이트 | `IsActiveComponent()` 단일 | `ComponentBase::IsActiveComponent` 있음 | 모든 시스템 사용 | W-framework |
| F11 | 오브젝트 속성 | Tag/Flags/creationOrder | Tag/Flags 있음, order 는 InstanceId 흡수 | — | ✓ |
| F12 | 멀티 컴포넌트 | 같은 타입 여러 개 | 저장 구조 미지원 | B11 지원 | W-framework |
| F13 | 안정 식별자 | `File::Guid` + `Guid128` | `InstanceId uint64` | — | ✓ |
| F14 | 참조 시스템 | `Ref<T>` 5카테고리 + SafePtr | `Ref<T>` 24B 골격 | 구현 · GameObjectHandle | W-ref |

### 신규 리포 자체 결함

| # | 결함 | 담당 |
|---|---|---|
| F16 | 공통 모듈이 차원에 오염 (`GameSystem` 에 `RenderWorld2D`) | W-framework (D1) — 이미 스켈레톤에서 제거됨. 확인만 |
| F17 | Platform 이 GraphicsApi/SurfaceHandle 때문에 RHI 를 include | W-platform (D2 재정의: RHI→Platform 방향) |
| F18 | `IPlatform::CreateWindow` / `DestroyWindow` 가 Win32 매크로와 충돌 | W-platform (D3) |
| F19 | 합성 루트가 둘 (EngineInstance vs EditorApplication) | W-host (D4) |
| F20 | `Physics2DSystem` 이 System + Service 겸함 | W-framework (System) + W-host (Service) |
| F21 | `GameScript` 차원 종속 (`OnCollisionEnter(Collision2D&)`) | W-framework (D5) |
| F22 | `TObjectPool` 주소 안정성 계약이 문서화 안 됨 | W-ref (F6) |

## Success Criteria

- 신규 트리에 `Entity` 정수 ID, `CWorld`, `Query<A,B>`, POD 컴포넌트가 남아 있지 않다.
- `Canvas` 가 오브젝트 풀과 타입별 컴포넌트 풀을 직접 소유한다.
- `Canvas`와 공통 `Layer`는 JBroRuntime에 한 번만 정의되며 Framework별 복제본이 없다.
- Runtime `Layer`에는 2D 합성 상태가 없고, 해당 상태는 Framework2D의 `Layer2D`가 소유한다.
- 3D 게임 구성은 Framework2D 없이 Runtime Canvas의 오브젝트·컴포넌트·시스템 실행 경계를 사용한다.
- 시스템이 `ForEach<T>` 로 컴포넌트 풀을 순회한다.
- 네임스페이스가 §10.1 표대로 적용되고 타입 접두사가 없다(`I` / `m_` 제외).
- 차원 독립 공개 값 타입은 JBroCore에 단 하나만 정의되며 Framework 임시 중복 정의가 없다.
- **스크립트가 `GameObjectHandle` 로 GameObject 를, `Ref<T>` 로 나머지를 본다** (실 객체 참조 없음).
- **`GameObjectHandle` 을 `if` 없이 호출해도 크래시가 없고 로그가 남는다.**
- `Ref<T>` 가 24B 이고 프레임 루프에서 식별자 조회가 0 회다.
- 게임 스크립트 DLL 을 재로드해도 호스트가 살아 있고 참조가 복구된다.
- 모듈 간 역방향 include 0 건, 2D 스크립트 타깃에서 Framework3D include 시 컴파일 실패.
- 테스트가 Debug / Release x64 양쪽에서 통과한다.

## Historical Verification Snapshot — 워크트리 분할 시점

> 아래 체크박스는 당시 검증 기록이며 현재 완료표가 아니다. 특히 `~` 표시는 증거가 보존되지 않은
> 항목이다. 현재 통합 검증 상태는 상단 Current Audit Snapshot을 갱신해 기록한다.

- [x] Debug x64 / Release x64 전체 빌드 (Stage A~C+Types 이식 시점)
- [x] `JBroTests` Debug / Release 통과 (~)
- [x] 모듈 간 역방향 include 0 건 (~)
- [x] 경계 음성 테스트 2 건 (~)
- [ ] 5개 워크트리 병합 후 전체 재빌드 + 테스트
- [ ] 잔여 검색: `Entity`, `CWorld`, `Query<`, `dynamic_cast`, `Manager`, `C` 접두, 매직 TypeId
- [ ] 사용자 스크립트 타깃 음성 테스트 (W-build 의 E2 최종 형태)
- [ ] 스크립트 DLL 재로드 후 호스트 생존 및 `Ref` 복구 (W-host H6)
- [ ] `GameObjectHandle` 무효 접근이 `if` 없이 안전 (W-ref)

## Historical Progress — 워크트리 분할 이전

### Stage A · 빌드 단위 분리 (완료)

Stage A 결과 상세는 아래 [Historical Review Snapshots](#historical-review-snapshots) 참조.

### Stage B0 · 골격 선언 (완료 · `ce2ce97`)

각 워크트리가 나중에 채울 타입·함수의 이름·시그니처만 확정. `InstanceId`, `InstanceIdGenerator`,
`InstanceHandle`, `InstanceRef`, `Ref<T>` (24B POD, static_assert 3종), `EngineContext` /
`SystemContext` / `ServiceContext` + `BindSystemContext` / `BindServiceContext`.

### Stage B · ECS 제거 (완료 · `76fad39`)

- ECS 헤더/소스 삭제 (`Core/ECS/*`, `Runtime/World.*`, `ComponentPoolTests.cpp`)
- `TObjectPool<T>` · `StableTypeId` · `MakeStableTypeId` 도입
- `GameObject` / `ComponentBase` 실 클래스 (raw pointer 판)
- `Canvas` 가 오브젝트/컴포넌트 풀 직접 소유

### Stage C · 네임스페이스와 이름 (완료 · `76fad39`)

- `namespace JBro::Engine` → `namespace JBro` 전 파일
- `CCanvas` / `CLayer` → `Canvas` / `Layer`
- Framework2D 컴포넌트 → `JBro::Component::X2D`
- Framework2D 시스템 → `JBro::System::X2DSystem`
- Framework3D 대칭 정리

### Stage Types · JBro 값타입 이식 (완료 · `9fa0cb1`)

- 기존 엔진의 `Utillity/Types/*` 17개 헤더를 `JBroCore/Include/JBro/Types/` 로 이식
- 모두 `namespace JBro` 로 감쌈 (`StrongTypeOps.h` 매크로 파일 제외)
- 스켈레톤 리트로핏: `std::vector` → `Array`, `std::unordered_map` → `Table`, `std::make_unique` → `MakeOwnerPtr`
- Debug/Release · 테스트 통과

### Stage 워크트리 분기 (이후 철회됨)

당시 `main` 브랜치에서 5개 워크트리를 생성했다:
- `work/build` · `work/platform` · `work/framework` · `work/ref` · `work/host`

각 워크트리의 상세 작업은 [Success Criteria](#success-criteria) 위 링크된 5개 문서에.

## Historical Risks — 워크트리 분할 시점

> W-ref/W-host 병합 순서 등 아래 내용은 현재 작업 지시가 아니다. 아직 유효한 리플렉션·핫 리로드
> 위험은 Current Audit Snapshot과 Success Criteria에서 별도로 추적한다.

- **W-ref 가 가장 크다.** SafePtr 이식 + GameObject 리트로핏 + Canvas 실 구현 + Ref<T> 몸통
  + GameObjectHandle 신설. 서브 브랜치로 나눠 진행 권장.
- **W-host 는 W-ref 와 W-platform 병합 후에만 병합 가능**. 순서 강제.
- W-framework 는 컴포넌트 몸통 완성이 W-ref 의 Canvas 구현에 의존. rebase 필수.
- **리플렉션이 없다.** H5 (핫 리로드 시 Ref 캐시 무효화) 와 F4 (로드 시 InstanceId 패치업) 는
  리플렉션 없이는 계약만 정할 수 있다. 실동작은 리플렉션 붙을 때.
- **기존 엔진 마이그레이션 시 `ScriptAPI.h` 가 공개 표면 정의 역할**을 한다는 당시 계획이었다.
  현재 SDK/Dist 미러는 존재하지 않으며 ScriptAPI의 차원별 공개 표면은 별도 확정이 필요하다.

## 보류

필요해질 때 승격한다.

- Framework / Core / Runtime 등의 DLL 화 (D-14). 별도 exe 를 여럿 띄우게 되면 재검토.
- RHI DLL 화와 런타임 RHI 교체. 두 번째 Windows 백엔드가 생기면.
- `JBroVulkanRHI` / `JBroWebGPURHI` 프로젝트 승격, Android 플랫폼.
- 디바이스 로스트 런타임 복구 (D-16).
- 에디터 Play 모드 "정지 시 원상복구". Canvas 두 벌이 필요해지면 핸들 타입을 재검토해야 한다.
- 리플렉션 시스템 (F4 · H5 실동작 전제).

## Historical Review Snapshots

### Stage A (완료)

#### Changed

- 단일 `JBroEngine.vcxproj` 를 모듈 10 개 + `JBroTests` exe 로 분해.
- 헤더를 `Modules/<Mod>/Include/JBro/<Name>/` 로 재배치, 참조를 `<JBro/…>` 로 전부 교체.
- `JBro.Common.props` 로 toolset · C++20 · 경고 · 출력 경로를 한 곳에서 상속.
- `JBroFramework` 를 `JBroRuntime` 에 흡수해 상호 의존 해소.

#### Verified

- Debug / Release x64 클린 빌드 성공, 경고 0 건.
- `JBroTests.exe` 양쪽 통과.
- 모듈 간 역방향 · 미허용 include 0 건.
- 경계 음성 테스트 2 건 모두 `C1083` 으로 실패 확인 후 프로브 제거, 재빌드 정상.

### Stage B0 / B / C / Types (완료)

#### Changed

- `Stage B0`: `InstanceId` · `InstanceHandle` · `Ref<T>` · Context 3종 시그니처 확정.
- `Stage B`: ECS 걷어내고 `TObjectPool<T>` · `StableTypeId` · `GameObject` · `ComponentBase` 도입.
- `Stage C`: `JBro::Engine` → `JBro` 전 파일 · `CCanvas`/`CLayer` → `Canvas`/`Layer` ·
  컴포넌트/시스템 서브네임스페이스 정리.
- `Stage Types`: 기존 엔진의 `Array` · `Table` · `String` · `Float` · `Int` 등 17개 타입 헤더 이식,
  스켈레톤을 JBro 컨테이너로 리트로핏.

#### Verified

- Debug / Release x64 클린 빌드, 경고 0 건, 오류 0 건.
- `JBroTests` Debug / Release 통과.

#### Not Verified

- 오브젝트 풀 실 구현 (`TObjectPool<T>::Create/Destroy/ForEachLive` 몸통) — W-ref 몫.
- 컴포넌트 풀 실 구현 (`Canvas::AttachComponent<T>` 등) — W-ref 몫.
- 5개 F2D 시스템의 `OnUpdate` 몸통 — W-framework 몫.
- Renderer 실 구현 — W-platform 몫.
- 스크립트 DLL 로드/재로드 — W-host 몫.

### Stage 워크트리 (당시 진행 중 · 현재 아님)

당시에는 각 워크트리 문서에서 독립적으로 관리했다. 현재는 `main` 단일 워크트리만 사용한다.

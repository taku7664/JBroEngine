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

## Goal

`source/JBroEngine` 신규 트리를 **기존 엔진(`source/repos/JBroEngine/Engine`)의
오브젝트-컴포넌트 모델**에 맞추고, 스크립트 노출 표면만 핸들로 바꾼다.

## Editor Snapshot — 2026-09-15

이 절은 에디터 작업의 현재 상태다. **바로 아래의 2026-09-12 스냅샷은 그 시점 기록이고
에디터를 담지 않는다** - 아래가 더 오래된 것이니 순서를 읽는 방향으로 착각하지 않는다.
근거는 D-60, D-63~D-84.

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
  (오브젝트 번호, 타입, 같은 타입 중 몇 번째)로 가리킨다.
- **고르기**는 여럿이다. 주된 하나를 따로 들고, 조상이 함께 골라진 것은 대상에서
  뺀다. 편집은 고른 전부에 미치며 숫자는 델타로 간다(D-83).
- **UI 계층**: 공용 위젯(`JBro::Widget`, D-79), 로컬라이징(D-80), 2열 줄 배치와
  한 값 한 줄(D-81). 규칙은 `ProjectRule.md` §11.
- **컨테이너**: `ArrayOps`/`TableOps` 가 서 있고 배열은 목록 위젯으로 그린다(D-82).
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

**뮤테이션으로 잰다.** 에디터 구간에 지금까지 100개 남짓 돌렸고, 살아남은 것은
테스트를 채우거나(대부분) 동치라고 판단해 근거를 적었다(D-77). 진짜 결함 넷이
그 과정에서 나왔다 - 지우면 안 돌아오는 자식(D-76), 글자 하나 늘 때 죽는
디바이스(D-75), 빈 로케일 표를 성공이라 깔던 것(D-80), 읽기 전용 목록의 단언.
테스트가 `JBRO_EDITOR_SHOT` 을 주면 화면을 파일로 찍는다 - 사람이 창을 띄워 보는 것은
반복되지 않기 때문이다(§11.4).

다음에 할 만한 것 — 한 일에서 곧바로 이어지는 것들이다:
- 커맨드가 일곱이다. 기존에는 서른 개 가까이 있다 — 컴포넌트 순서, 레이어,
  복사/붙여넣기, 폴리곤 버텍스. 묶는 쪽(`CompoundCommand`)이 생겼으므로
  "한 동작이 여러 값을 바꾼다" 는 자리는 이제 새 커맨드 없이 선다.
- **뿌리끼리의 차례**를 못 바꾼다(D-84). 캔버스에 오브젝트 순서가 없다 —
  데이터 모델을 건드리는 일이라 확인이 필요하다.
- **배열 원소 편집이 커맨드가 아니다**(D-82). 프로퍼티 길이 원소 번호를 담지 못한다.
- 게임 뷰 **매 프레임 opt-in**(패널이 안 보이면 렌더를 멈춘다). D-63 에 적어 두었다.
- 팝업(기존 `ImPopupDesc` — 핸들, Id 중복 방지, 모달), FontAwesome 아이콘.
- `Save Canvas` 메뉴가 회색이다. 경로를 받을 곳이 없다 — 파일 대화상자가 먼저다.
- 표는 인스펙터에서 개수만 보인다. 키 칸을 어떻게 받을지가 정해지지 않았다.
- 못 옮긴 위젯: 에셋 필드, 오디오 셋, 경로 필드, 레이어 머리(D-79 에 이유).

## Audit Snapshot — 2026-09-12

**그 시점의 기록이다.** 2026-09-12 에 코드와 테스트를 직접 대조해 적었고, 그 뒤의 에디터
작업(D-60, D-63~D-84)은 담지 않는다 - 현재 상태는 위의 `Editor Snapshot` 이다.
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

## Assumptions

- 대상은 `Documents/GitHub/JBroEngine` 신규 리포다. 기존 엔진은 **읽기 전용 기준**으로만 쓴다.
- 기초가 서면 기존 엔진을 이 구조로 마이그레이션한다.
- Windows / D3D12 를 먼저 세운다. Vulkan / WebGPU / Android 는 모듈 규칙만 유지한 채 뒤로 미룬다.

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

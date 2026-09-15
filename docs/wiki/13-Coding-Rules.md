# 13. 코딩 규칙

`ProjectRule.md` 의 §6·§9·§10·§12·§13·§14 를 코드를 쓸 때 필요한 순서로 다시 모았다. 원문이 우선한다.

## 이름

**타입 이름에 접두사를 쓰지 않는다.** 구분은 네임스페이스가 한다. 예외는 둘이다. 인터페이스 `I`(`IRHIDevice`), private 멤버 `m_`.
제품명 `JBro` 도 타입 이름에 붙이지 않는다. `JBro::JBroHandle` 처럼 겹쳐 읽힌다.

| 대상 | 네임스페이스 | 예 |
|---|---|---|
| 컴포넌트 | `JBro::Component` | `Component::Transform2D` |
| 에셋 | `JBro::Asset` | `Asset::TextureAsset` |
| 시스템(엔진 계층) | `JBro::System` | `System::TimeSystem` |
| 서비스(스크립트 공개) | `JBro::Service` | `Service::TimeService` |
| 그 외 사용자 비공개 | `JBro::Internal` | `Internal::CanvasAccess` |
| 게임프레임워크·공통 타입 | `JBro` 직속 | `JBro::GameObject`, `JBro::Color` |

- `JBro::Game` 은 두지 않는다. 사용자의 `namespace Game` 과 충돌한다.
- 네임스페이스와 같은 이름의 타입은 만들 수 없다. 그래서 컴포넌트 베이스가 `Component` 가 아니라 `ComponentBase` 다.
- 시스템과 서비스는 네임스페이스에 있어도 이름 끝에 `System`/`Service` 를 붙인다. `System::Time` 과 `Service::Time` 은 읽는 쪽이 헷갈린다.
- `Manager` 는 쓰지 않는다. 두 역할 어디에도 해당하지 않으면 장식 없이 역할을 그대로 쓴다(`Renderer`, `SystemScheduler`, `AssetRegistry`).

**타입 이름은 `<도메인><차원><역할>` 순서다.** 차원 마커는 도메인 명사 바로 뒤다.

| 맞음 | 틀림 |
|---|---|
| `Component::Transform2D` | `Component::TransformComponent2D` |
| `System::Transform2DSystem` | `System::TransformSystem2D` |
| `Component::MeshRenderer3D` | `Component::MeshRenderer` (차원 마커 누락) |

같은 도메인의 컴포넌트와 시스템은 앞부분이 정확히 일치한다(`Camera2D` ↔ `Camera2DSystem`). 차원 마커를 붙일 이유가 없는 타입은 애초에 Framework 에 있어야 하는지 다시 본다.

## 메모리와 소유

- `new`/`delete` 를 직접 쓰지 않는다. 고유 소유는 `MakeOwnerPtr`, 비소유는 `SafePtr`.
- `std::unique_ptr`·`std::shared_ptr`·`std::vector`·`std::unordered_map`·`std::string` 대신 `OwnerPtr`·`SafePtr`·`Array`·`Table`·`String`.
- 예외 하나: 컨테이너·리플렉션처럼 **타입을 모르는 저장소**는 소유 도구를 끌어오지 않고 할당기에서 받은 자리에 `std::construct_at`/`std::destroy_at` 으로 만들고 지운다(D-88). `OwnerPtr` 는 객체마다 제어 블록을 두고 메인 스레드 전용이라 이 계층에는 과하고, POD 가 아니라 DLL 함수 표에 담을 수도 없다.
- `SafePtr`·`OwnerPtr`·`Ref<T>`·`GameObjectHandle` 은 **메인 스레드 전용**이다.
- 자기보다 짧은 스코프의 포인터를 멤버로 캐시하지 않는다. 무효화에는 명시적 재생성으로 대응한다.
- 소유권이 주석 없이도 타입으로 드러나게 한다.

## 매 프레임 경로

- `dynamic_cast`·힙 할당·문자열 생성/비교를 두지 않는다. 스폰·파괴를 포함한 정상 프레임이 기준이다.
- 타입 분기는 정적 디스패치나 타입별 저장소로, 조회는 초기화 시점 캐시로.
- 프레임 임시 배열은 `JMemoryContext.frame` 을 쓴다.
- 이 계약은 카운팅 할당기와 카운터로 테스트가 단언한다. 성능을 이유로 바꿀 때는 측정 결과를 먼저 보여 준다.

## 형식

- C++20. 경고를 오류로 본다. 새 경고를 만들지 않는다.
- **제어문 본문을 같은 줄에 쓰지 않는다.** `if (x) return;` 을 만들지 않는다.
- 헤더에는 선언만 두고 구현은 소스로. 템플릿과 인라인은 예외이되, 서비스 헤더는 예외를 두지 않는다(시스템이 프렐류드로 새어 나간다).
- 전방 선언을 우선한다.
- 포인터 연산과 배열 인덱스는 범위 검사를 넣거나 `ArrayView` 처럼 크기를 함께 넘긴다. `Array::operator[]` 의 `assert` 는 Debug 전용이라 외부 입력 검사로 삼지 않는다.
- 실패는 반환값으로 알린다. 예외는 할당 실패와 스케줄러 오용 같은 프로그래밍 오류에만 쓰고, 경계를 넓히지 않는다. DLL 경계로는 절대 내보내지 않는다.
- 한글 주석이 들어간 신규 헤더·소스는 **UTF-8 BOM** 으로 저장한다. 컴파일은 `/utf-8` 이다.
- 직렬화는 YAML 또는 바이너리 우선.

## 경계

- 공개 헤더는 `<JBro/<이름>/...>` 로만 include 한다. 외부 라이브러리는 그 라이브러리가 정한 이름으로.
- 호스트와 게임 DLL 경계를 넘는 데이터는 POD 다. `static_assert` 로 크기와 레이아웃을 고정한다.
- 서비스 헤더는 시스템을 전방 선언만 한다.
- Context 에는 시스템과 서비스만. `Canvas`·`GameObject` 같은 콘텐츠 단위는 넣지 않는다.
- 새 컴포넌트·서비스 추가와 방향을 바꾸는 설계 판단은 **사용자 확인 뒤** 진행한다.

## 에디터

- ImGui 를 직접 부르지 않고 `JBro::Widget` 을 거친다.
- 화면 문자열은 로컬라이징 키로. `Loc::TextOr(key, "English")` 로 폴백을 둔다.
- 편집은 커맨드로만. `Execute` 가 성공해야 스택에 쌓인다. 되살릴 값을 먼저 뜨지 못했으면 지우지도 떼지도 않는다.
- UI 를 바꿨으면 띄워서 보고 무엇을 봤는지 적는다.

## 검증

- 빌드 성공은 검증이 아니다.
- 경계 규칙은 어길 때 실제로 컴파일이 실패하는지 **음성 테스트**로 확인한다.
- 그래픽 테스트는 검증 레이어가 조용한지까지 본다.
- 테스트 프로세스에서 단언은 대화상자를 띄우지 않는다.
- 테스트가 무엇을 잡는지 뮤테이션으로 잰다(`tools/mutate.py`). **돌리기 전에 커밋한다**(되돌리기가 `git checkout` 이다). 돌리는 동안 소스도 테스트도 건드리지 않는다.
- 살아남은 뮤테이션은 테스트를 채우거나, 동치라면 근거를 Decisions 에 적는다.
- 툴체인 버전은 `JBro.Common.props` 에 명시적으로 고정한다. 올릴 때는 Debug/Release 전체 빌드와 테스트로 확인한다.
- `.hlsl` 을 고치면 `Compile.ps1` 로 생성 헤더를 다시 만들어 함께 커밋한다.

## 문서

- **작업마다 관련 문서를 갱신하고 커밋한 뒤 끝났다고 보고한다.** 계획과 진행은 `tasks/*.md`, 방향 결정은 `tasks/todo.md` Decisions, 지켜야 할 계약은 `docs/ProjectRule.md`.
- 대화에만 나오고 문서에 없는 결정·실측은 없는 것으로 본다.
- 사용자 확인을 기다리는 것도 `[열림]`·`[대기]` 를 붙여 적는다.
- `ProjectRule.md` 와 Decisions 가 충돌하면 임의로 고르지 않는다. `Updates`/`Obsoletes` 관계와 현재 코드를 확인하고, 방향에 영향을 주면 사용자에게 확인한 뒤 둘을 함께 고친다.

## 커밋

- 작업 단위마다 커밋한다. 여러 주제를 한 커밋에 섞지 않는다.
- 메시지는 **영어**, 한 줄 요약, 마침표 없음. 타입 접두어 `feat` / `fix` / `refactor` / `test` / `chore` / `docs`.
- 무엇을 왜 바꿨는지 적는다. "수정"·"변경" 만 쓰지 않는다. 요약만으로 근거가 전달되지 않으면 본문에 이유와 검증 결과를 붙인다.
- 기능 변경에 딸린 테스트는 같은 커밋에 둔다. 테스트만 바꾸면 `test`.
- 커밋 전에 빌드·테스트 통과와 스테이징 diff 를 확인한다.
- 히스토리 재작성과 강제 푸시는 사전 확인을 받는다.
- 비밀값·토큰·내부 URL 은 적지 않는다.
- 사용자 보고는 한국어다.

```
feat: attach scripts by name through the host script registry
fix: wait for the frame before rewinding the command allocator outside a frame
docs: record D-89 step four and the one-line value and table break rules
```

# CLAUDE.md

> 글로벌 지침(`~/.claude/CLAUDE.md`) 상속: 사고규율(Before/After Acting), 행동규율(Break/Cross/Ground), 환경식별, 호칭, Python/uv 규칙 등.

## Project Rules

**현재 계약은 [docs/ProjectRule.md](./docs/ProjectRule.md), 변경 근거는
[tasks/todo.md](./tasks/todo.md)의 Decisions 절에 있다. 작업 전에 둘 다 읽는다.**
둘이 충돌하면 임의로 하나를 선택하지 않는다. 해당 Decision의 `Updates` / `Obsoletes` 관계와
현재 코드를 확인하고, 방향에 영향을 주는 충돌은 사용자에게 확인한 뒤 두 문서를 함께 고친다.

핵심만 요약하면 다음과 같다. 세부 조건과 예외는 원문을 본다.

- 판단 순서: 정확성 → 단순성 → 영향 범위 최소화 → 검증 가능성 → 유지보수성 → 작업 속도
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
- 방향을 바꾸는 설계 판단과 새 컴포넌트·서비스 추가는 사용자 확인 뒤 진행한다
- 사용자 보고는 한국어, 커밋 메시지는 영어로 작성한다.
  작업 단위마다 커밋하고 `feat` / `fix` / `refactor` / `test` / `chore` / `docs` 타입 접두어를 붙인다.
  커밋 전에 빌드·테스트 통과와 스테이징 diff를 확인한다. 히스토리 재작성과 강제 푸시는 사전 확인을 받는다
- `new` / `delete`를 직접 쓰지 않는다. 고유 소유는 `MakeOwnerPtr`, 비소유 참조는 `SafePtr`다
- 빌드 성공만으로 검증 완료로 보지 않는다. 경계 규칙은 어길 때 실제로 컴파일이 실패하는지
  음성 테스트로 확인한다

### 관련 문서

- [docs/ProjectRule.md](./docs/ProjectRule.md) — 확정 규칙
- [docs/Jbro_Engine_Architecture_Draft_v2.md](./docs/Jbro_Engine_Architecture_Draft_v2.md) — 폐기된 역사 초안
- [docs/Jbro_CPP_Script_Object_Safety.md](./docs/Jbro_CPP_Script_Object_Safety.md) — 폐기된 스크립트 객체 안전성 초안
- [tasks/todo.md](./tasks/todo.md) — 진행 중 작업 계획과 확정 결정(Decisions) 기록

두 초안은 일부 절만 골라 현재 계약으로 사용할 수 있는 문서가 아니다. `[대체됨]` 표시가 없는
본문도 폐기된 ECS·World·전 모듈 DLL·단일 `Ref<T>` 모델을 포함한다. 설계 변천을 확인할 때만 읽고,
구현 계약은 `ProjectRule.md`와 `tasks/todo.md` Decisions에서 확인한다.

<!-- 대화 중 발견된 프로젝트 규칙이 여기에 추가됩니다. -->

# CLAUDE.md

> 글로벌 지침(`~/.claude/CLAUDE.md`) 상속: 사고규율(Before/After Acting), 행동규율(Break/Cross/Ground), 환경식별, 호칭, Python/uv 규칙 등.

## Project Rules

**확정 규칙은 [docs/ProjectRule.md](./docs/ProjectRule.md) 에 있다. 작업 전에 읽는다.**
문서 사이에 내용이 다르면 `docs/ProjectRule.md` 를 우선한다.

핵심만 요약하면 다음과 같다. 세부 조건과 예외는 원문을 본다.

- 판단 순서: 정확성 → 단순성 → 영향 범위 최소화 → 검증 가능성 → 유지보수성 → 작업 속도
- 모듈은 각자 빌드 단위를 가지고, 공개 헤더는 `<Jbro/<이름>/...>` 로만 참조한다.
  의존 선언하지 않은 모듈의 헤더를 include하면 컴파일이 실패해야 한다. 역방향·순환 의존 금지
- DLL 경계는 교체·재로드가 필요한 곳에만 만든다. 현재는 게임 스크립트 하나뿐이고 나머지는 정적 링크
- 2D/3D 배타성은 엔진 빌드가 아니라 사용자 스크립트 프로젝트와 게임 익스포트에만 적용한다.
  엔진과 에디터는 둘 다 포함한다
- 최상위 실행 단위는 `Canvas` 다. `World` 는 Canvas가 소유하는 ECS 저장소이고,
  `Scene` / `SceneManager` 같은 중복 수명 계층은 이름을 바꿔서도 만들지 않는다
- 스크립트에 노출하는 참조는 `{ index, generation }` Handle 이다. RefCount 없음,
  무효 접근은 Null Object 가 아니라 런타임 에러
- 호스트와 게임 DLL 경계를 넘는 데이터는 POD 여야 한다
- 매 프레임 도는 경로에 `dynamic_cast` · 힙 할당 · 문자열 생성/비교를 두지 않는다
- 클래스 `C`, 인터페이스 `I`, private 멤버 `m_` 접두. 직렬화는 YAML 또는 바이너리 우선
- 빌드 성공만으로 검증 완료로 보지 않는다. 경계 규칙은 어길 때 실제로 컴파일이 실패하는지
  음성 테스트로 확인한다

### 관련 문서

- [docs/ProjectRule.md](./docs/ProjectRule.md) — 확정 규칙
- [docs/Jbro_Engine_Architecture_Draft_v2.md](./docs/Jbro_Engine_Architecture_Draft_v2.md) — 아키텍처 초안 (일부 절 대체됨)
- [docs/Jbro_CPP_Script_Object_Safety.md](./docs/Jbro_CPP_Script_Object_Safety.md) — 스크립트 객체 안전성 초안 (일부 절 대체됨)
- [tasks/todo.md](./tasks/todo.md) — 진행 중 작업 계획과 확정 결정(Decisions) 기록

두 초안에는 검토를 거쳐 대체된 절이 있다. 해당 절에는 `[대체됨]` 표시가 달려 있으니
초안 내용을 근거로 삼기 전에 표시를 먼저 확인한다.

<!-- 대화 중 발견된 프로젝트 규칙이 여기에 추가됩니다. -->

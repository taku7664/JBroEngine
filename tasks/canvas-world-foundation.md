# Canvas-Owned World Foundation TODO

> **폐기된 역사 기록 — 현재 설계나 작업 지시로 사용하지 않는다.**
> 이 문서의 World/ECS/Entity 모델은 `tasks/todo.md`의 D-1·D-2와
> `docs/ProjectRule.md` §8에 의해 전부 폐기됐다. 현재 Canvas는 오브젝트 풀과 타입별 컴포넌트 풀을
> 직접 소유하며 World/ECS 계층을 두지 않는다. 아래 본문은 당시 구현 기록을 보존하기 위한 것이다.

## Goal

2D 게임의 최상위 단위를 `Canvas`로 고정하고, `Canvas`가 `World/ECS`와 포토샵식 합성 `Layer`를 소유하는 엔진 기초를 구현한다.

## Current Contract

- `Canvas`는 하나의 `World/ECS`와 순서가 있는 `Layer` 목록을 소유한다.
- `Layer`는 표시 여부, 불투명도, 블렌드 방식, 공간, 패럴랙스와 합성 순서만 담당한다.
- Entity와 Component의 생성·파괴 및 계층 수명은 `World`가 담당한다.
- 별도의 `Scene` 및 `SceneManager` 계층은 만들지 않는다.
- 컴포넌트 실데이터는 타입별 청크 풀에 저장하며 `JAllocator`를 사용한다.
- Script Handle/Proxy와 기존 `SafePtr` 결합은 이 단계에서 구현하지 않는다.

## Implemented

- [x] allocator 기반 `TComponentPool<T>`와 명시적 `CComponentRegistry`
- [x] Entity 생존 목록과 타입별 Entity-Component 저장소
- [x] Component 등록, 추가, 조회, 제거 및 1·2 타입 Query
- [x] 지연 Entity 파괴와 Component 일괄 정리
- [x] 이름, 활성 상태, 부모-자식 계층 기본 Component
- [x] `Canvas`의 `World` 소유 및 `Layer` 생성·삭제·순서 변경
- [x] Entity의 Layer 배정과 삭제된 Layer의 기본 Layer 재배정
- [x] `Framework2D`의 Canvas 생성 및 2D Component 등록
- [x] `Scene`/`SceneManager` 구현과 참조 제거

## Verification

- [x] Debug x64 컴포넌트 풀 테스트
- [x] Release x64 컴포넌트 풀 테스트
- [x] Debug x64 World/Canvas 기초 테스트
- [x] Release x64 World/Canvas 기초 테스트
- [x] Debug x64 전체 솔루션 빌드
- [x] Release x64 전체 솔루션 빌드
- [x] Scene/SceneManager 및 Script Handle 잔여 검색

## Not Implemented Yet

- Reflection 필드 메타데이터와 Canvas/World 직렬화 형식
- 실제 Layer별 렌더 추출, 중간 렌더 타깃 및 블렌드 실행
- Transform/Physics/Camera/Sprite 시스템의 실제 업데이트 로직
- Script Handle/Proxy와 게임 DLL 공개 API
- 멀티스레드 Component 접근 및 스케줄링

## Risks

- `HierarchyComponent`의 자식 목록과 World/Canvas의 관리 목록은 현재 STL 기본 allocator를 사용한다.
- Entity ID는 아직 세대 번호가 없는 단조 증가 정수이므로 Script API의 장기 참조로 공개하면 안 된다.
- `OwnerPtr<T>`는 현재 `std::unique_ptr<T>` 별칭이며 DLL 경계를 넘기지 않는 엔진 내부 소유권에만 사용한다.

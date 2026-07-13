# 런타임 성능 및 공개 API 검수 메모

## 현재 판정

스크립트 통합 후 발견된 프레임 핫패스와 공개 쓰기 API 문제는 해결했다. 이 문서는
재발 방지 기준과 현재 남아 있는 DLL ABI 제약을 함께 기록한다.

## 성능 문제

- [해결] 라이프사이클, 물리 이벤트, surface 이벤트의 `dynamic_cast<CGameScript*>` 제거.
- [해결] 구조 변경 때만 계층/컴포넌트 순서를 재구축하는 실행 캐시 적용.
- [해결] 컴포넌트→스크립트 런타임 상태 O(1) 인덱스와 오브젝트별 range 캐시 적용.
- [해결] 구체 컴포넌트 조회를 생성 시 고정한 안정 TypeId 비교로 변경.
- [해결] 렌더 시스템은 의존성 주입 때 `CForward2DRenderer*`를 캐시.
- [해결] `GetOwner()`는 `const SafePtr<CGameObject>&`를 반환.
- [해결] 스크립트 런타임 상태도 씬 로드 분석 결과의 1.5배를 연속 예약하고 삭제 슬롯을
  재사용해 인스턴스당 상태 heap allocation을 제거.
- [해결] 일반 컴포넌트의 가상 생성/파괴 훅을 제거해 스크립트 `OnCreate`가 부착 시점과
  ScriptSystem 시작 시점에 중복 호출되던 경로를 제거.

## 공개 API 문제

- [해결] `Owner`, enabled 상태, InstanceGuid, CreationOrder의 직접 쓰기를 차단.
- [해결] 씬 런타임 상태, 풀, 업데이트, 즉시 파괴, 에셋 보유권 API를 private으로 이동.
- [해결] GameObject attach/detach/raw 조회/순서 변경 API를 private으로 이동.
- [해결] 호스트 전용 쓰기는 `SceneRuntimeAccess.h`로 모으고 SDK 스테이징에서 제외.
- [해결] 시스템 설치/조회도 호스트 전용 접근점으로 이동.

## 즉시 수정 계약

- `Owner`와 `IsEnabled`는 private으로 둔다.
- 공개 상태 API는 `IsEnabled()`, `SetEnabled(bool)`,
  `const SafePtr<CGameObject>& GetOwner() const` 형태로 제공한다.
- 반환된 SafePtr 핸들은 호출자가 직접 교체할 수 없지만 `TryGet()`으로 얻은 GameObject는
  수정할 수 있다.
- 컴포넌트 생성은 `std::construct_at`을 사용한다.
- owner 바인딩은 생성 경로 내부에서만 수행하고 외부 대입을 금지한다.
- 엔진 내부 쓰기 API는 private 및 friend 또는 Internal 헤더로 분리한다.

## 후속 성능 구조

- GameObject는 단일 순서 컴포넌트 목록을 유지한다.
- 컴포넌트 엔트리에 안정적인 TypeId/Kind를 캐시해 RTTI 없이 판별한다.
- Scene은 구조 변경 때만 다시 만드는 ordered script execution cache를 보유한다.
- 물리 이벤트는 object-to-script range/index 캐시로 해당 오브젝트의 스크립트만 찾는다.
- 스크립트 실행 레코드가 런타임 상태를 직접 보유해 프레임별 선형 검색을 없앤다.
- 계층·컴포넌트 순서 캐시는 생성/삭제/부모 변경/순서 변경 때만 dirty 처리한다.

## ABI 재검토

`CGameScript : CComponent`로 인해 게임 DLL 객체 레이아웃에 `File::Guid`와 `SafePtr`가
포함되고 생성 함수도 `ComponentConstructionToken`과 `SafePtr` 참조를 받는다. 이는 일반적인
POD 전용 모듈 경계의 예외다. 엔진이 생성/소멸을 DLL 쪽 함수로 위임해 CRT 소유권 혼합은
피하지만, 엔진과 게임 DLL은 동일 SDK 헤더 및 호환 컴파일러/CRT 설정을 사용해야 한다.
라이브 컴파일에서 레이아웃이 바뀌면 기존 인스턴스를 모두 파괴한 뒤 새 DLL로 재생성한다.

## 완료 전 검증

- 프레임 라이프사이클 경로의 RTTI, 동적 할당, 정렬, 선형 상태 검색이 0인지 검색한다.
- 구조 변경 직후 실행 순서와 삭제 슬롯 재사용을 실제 에디터 플레이로 확인한다.
- 스크립트/컴포넌트 수를 키운 전용 벤치마크는 테스트 기반이 생길 때 추가한다.
- SDK 공개 심볼은 새 내부 API 추가 때 `StageSDK.targets` 제외 여부를 함께 검토한다.
- [완료] Windows 전체 빌드와 실제 Emscripten Web 패키징을 확인한다.
- 실제 에디터에서 추가, 재정렬, 저장/로드, 플레이, 라이브 컴파일을 검증한다.

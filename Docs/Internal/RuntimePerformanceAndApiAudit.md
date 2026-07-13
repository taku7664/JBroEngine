# 런타임 성능 및 공개 API 검수 메모

## 현재 판정

현재 스크립트 통합 구현은 빌드는 통과하지만 런타임 구조 검수에는 실패했다. 아래 항목을
모두 해결한 뒤 코드 전반을 다시 검수한다.

## 성능 문제

- 스크립트 `Update`, `FixedUpdate`, 물리 이벤트, surface 이벤트에서 전체 컴포넌트에
  `dynamic_cast<CGameScript*>`를 수행한다.
- 계층 순회가 호출될 때마다 루트와 자식 벡터를 만들고 정렬한다. `Update`에서는 같은
  계층을 두 번 순회한다.
- 각 스크립트 업데이트마다 씬의 런타임 상태 벡터를 선형 검색해 스크립트 수가 늘면
  제곱 비용이 발생한다.
- `GameObject::GetComponent<T>()`와 raw 조회 경로도 RTTI 또는 `type_index`를 이용한
  선형 검색이다.
- 렌더 시스템이 매 프레임 렌더러 타입을 `dynamic_cast`로 확인한다.
- `GetOwner()`가 `SafePtr`를 값으로 반환해 단순 조회에도 참조 카운트 변경이 발생한다.

## 공개 API 문제

- `CComponent::Owner`, `IsEnabled`, `InstanceGuid`가 외부에서 직접 변경 가능하다.
- 씬의 스크립트 런타임 상태, 풀 할당/해제, 내부 업데이트, 즉시 파괴, 에셋 보유권,
  GUID 인덱스 변경 API가 SDK에 노출되어 있다.
- GameObject의 attach/detach, raw/type-index 조회, 순서 변경용 내부 API가 노출되어 있다.
- SDK 스테이징이 GameFramework 디렉터리를 넓게 복사해 내부 계약까지 공개한다.

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

`CGameScript : CComponent`로 인해 게임 DLL 객체 레이아웃에 `File::Guid`, SafePtr와 제어
블록 계약이 포함된다. 고정 크기 POD 경계 규칙과 충돌하므로 후속 전반 검수에서 반드시
레이아웃, CRT, 라이브 컴파일 호환성을 다시 확인한다.

## 완료 전 검증

- 프레임 라이프사이클 경로의 RTTI, 동적 할당, 정렬, 선형 상태 검색이 0인지 확인한다.
- 스크립트/컴포넌트 수를 키운 벤치마크를 추가한다.
- SDK 공개 심볼 허용 목록을 검토한다.
- Windows 전체 빌드와 Web 소스 계약을 확인한다.
- 실제 에디터에서 추가, 재정렬, 저장/로드, 플레이, 라이브 컴파일을 검증한다.

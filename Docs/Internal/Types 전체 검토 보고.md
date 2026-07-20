# Types 전체 검토 보고

Types 범위만 읽기 검토했다. 이번 검토에서는 파일을 수정하지 않았다.

결론부터 말하면 `Array`와 `Table`에 먼저 고쳐야 할 실제 결함 후보가 있다. 현재 smoke test는 통과하지만 해당 반례를 테스트하지 않는다.

## 우선순위 높은 문제

### 1. `Table` 재해시가 hasher/equality 상태를 잃음

`Engine/Utillity/Types/Table.h:564`에서 새 `Table replacement`를 기본 생성한다. 기존 `m_hasher`, `m_equal`을 복사하지 않은 채 원소를 옮기고 swap한다.

상태를 가진 hasher가 기본 생성 때마다 다른 seed를 사용하면 재해시 후 기존 키를 찾지 못할 수 있다. 복사 생성자도 같은 문제가 있다.

### 2. `Table`의 무조건 `noexcept`가 일반 템플릿 계약과 맞지 않음

`Engine/Utillity/Types/Table.h:159`의 move 생성자, `Engine/Utillity/Types/Table.h:204`의 move 대입, `Engine/Utillity/Types/Table.h:391`의 `Swap`은 hasher/equality 이동이나 swap이 예외를 던져도 `noexcept`다.

지금 기본 functor는 안전하지만 사용자 functor를 받는 공개 템플릿으로서는 `std::terminate` 가능성이 있다.

### 3. `Table::Rehash` 예외 안전성이 깨짐

`Engine/Utillity/Types/Table.h:564`에서 기존 키와 값을 하나씩 move한다. 중간 원소 생성이나 할당이 실패하면 기존 Table 일부가 moved-from 상태로 남는다.

최소 basic guarantee를 명시하거나, 복사 가능한 타입은 copy하고 noexcept move만 이동하는 정책이 필요하다.

### 4. `Array::EmplaceAt` 예외 경로에 추적되지 않는 객체가 남을 수 있음

`Engine/Utillity/Types/Array.h:297`에서 마지막 원소를 `m_data + m_size`에 먼저 생성한 뒤 `move_backward`와 move assignment를 수행한다.

그 뒤 이동/대입이 예외를 던지면 `m_size`는 증가하지 않았는데 끝 위치 객체는 살아 있다. 이후 소멸자가 해당 객체를 파괴하지 않아 수명과 자원이 누락된다.

### 5. `Int::Abs()`는 최솟값에서 signed overflow

`Engine/Utillity/Types/Int.h:34`의 `-Value`는 `INT64_MIN`에서 표현할 수 없어 undefined behavior다.

반환 정책을 정해야 한다.

- assert/예외
- saturating abs
- unsigned magnitude 반환

### 6. 강타입끼리 단위가 다시 섞일 수 있음

`Degree`와 `Radian` 모두 암시적 `operator float()`를 제공한다. 따라서 명시적인 `Degree + Radian` 연산자가 없어도 두 값을 `float`로 바꾸는 기본 연산 경로가 열린다. 결과도 각도 타입이 아닌 `float`가 될 수 있다.

`Int/UInt`의 혼합 연산 템플릿도 모든 산술 타입을 허용해서 다음이 가능하다.

- `Int + 0.9` → double을 `int64_t`로 잘라 계산
- `UInt - -1` → 음수를 `uint64_t`로 변환
- 큰 unsigned와 `Int` 혼합 → 구현 의존 변환 또는 overflow 위험

## 타입별 검토

| 타입 | 상태 | 주요 내용 |
|---|---|---|
| `Allocator` | 개선 가능 | `EMemoryTag`와 size가 전부 무시된다. 통계·누수 추적·arena 연결 지점으로 활용 가능 |
| `AngleConstants` | 양호 | 단순 상수. `std::numbers::pi_v<float>` 사용 여부 정도만 선택 사항 |
| `Array` | 수정 필요 | `EmplaceAt` 예외 수명 문제, null `end()` 포인터 연산의 엄밀한 UB 가능성 |
| `ArrayView` | 보완 권장 | bounds 검사, `First/Last`, subview가 없다. 빈 view의 `nullptr + 0` 문제도 동일 |
| `Asset` | 유지 | `Ref<T>`와 의미가 다르다. GUID 값 타입과 런타임 참조 타입을 구분하는 역할 |
| `Bool` | 양호 | 레이아웃 검사 있음. 추가 기능 필요성 낮음 |
| `Degree` | 정책 확인 | 정규화는 정상. 암시적 float 변환 때문에 단위 안전성이 약해짐 |
| `EngineTypes` | 양호 | 편의 umbrella header. 규모가 커지면 컴파일 비용 관찰 필요 |
| `Float` | 보완 권장 | `CutDecimal`의 큰 digits에서 scale이 무한대가 될 수 있다. NearlyEquals는 절대 오차만 사용 |
| `Hash` | 양호 | 단순하고 heterogeneous equality와 조합 가능 |
| `Int` | 수정 필요 | `INT64_MIN.Abs()` UB, 부동소수 혼합 연산의 묵시적 절삭 |
| `Radian` | 정책 확인 | `Degree`와 동일한 암시 변환 문제 |
| `Simd128` | 최적화 가능 | SSE/WASM은 적절. NEON은 벡터 결과를 메모리에 저장한 뒤 scalar loop로 mask 생성 |
| `String` | 보완 권장 | `ReserveAdditional` 덧셈 overflow 검사 없음. 자기 자신을 가리키는 `ReplaceAll` view는 재할당 후 무효화될 수 있음 |
| `StrongTypeOps` | 수정 필요 | 모든 arithmetic 타입 허용이 signed/unsigned 및 정수/실수 혼합 오류를 숨김 |
| `Table` | 우선 수정 | functor 상태 유실, 예외 안전성, noexcept 계약 문제 |
| `Types.h` | 정리 후보 | 전역 `typedef unsigned int Handle/Length`는 이름 충돌과 폭 불명확 가능성 |
| `TypeTraits` | 정책 문서 필요 | custom trivially-relocatable 선언의 소멸자/자원 이전 계약이 문서화되지 않음 |
| `UInt` | 정책 확인 | overflow는 unsigned wrap으로 정의되지만, 음수 및 실수 혼합을 허용하는 것이 위험 |
| Engine/SDK 미러 | 양호 | 현재 Types 파일 전부 hash 일치 |

## 성능 개선 후보

우선 측정 가치가 있는 항목은 다음과 같다.

- `Table::TryAdd`가 중복 키인지 확인하기 전에 capacity 확보/rehash를 수행한다. 중복 삽입 시 불필요하게 전체 재해시되고 기존 포인터도 무효화될 수 있다.
- `Table`은 control과 entry를 별도 할당한다. lookup에는 유리할 수 있지만 생성 비용은 두 번이다. allocator 측정 후 단일 block 배치를 비교할 가치가 있다.
- 삭제가 많은 `Table::Clear`는 항상 전체 capacity를 순회한다. live index를 또 두는 것은 손해일 수 있으므로 실제 workload 벤치마크가 먼저다.
- `String::LocalCapacity()`가 매번 임시 `std::string`을 생성한다. 구현별 SSO를 직접 상수화하면 ABI 문제가 있으므로 현재 방식은 안전하지만 hot path 사용 여부를 확인해야 한다.
- NEON `MatchBytes`의 mask 생성이 scalar 16회 반복이다. ARM 벤치마크 후 전용 축약 경로를 검토할 수 있다.

## 테스트 공백

현재 `TypeSystemSmoke`는 통과했지만 다음을 검사하지 않는다.

- throwing move/copy 타입의 `Array::EmplaceAt`
- stateful 또는 seed 기반 hasher를 사용한 `Table` 재해시·복사
- `Table` 재해시 도중 생성자 예외
- `INT64_MIN.Abs()`
- signed/unsigned 및 정수/실수 혼합 연산
- 자기 참조 `String::ReplaceAll`
- allocator alignment와 allocation/deallocation 쌍
- scalar/SSE2/NEON/WASM 결과 일치

## Inspect 결과

`Shared code + user concern` 신호에 따라 dependency trace, side effect, assumption, rollback을 확인했다.

- Types 헤더는 Engine과 SDK 양쪽에 공개되며 `Array`, `Table`, strong type 변경은 모든 소비 코드를 재컴파일해야 한다.
- 현재 실제 `Table` 런타임 사용처는 Canvas의 GUID→오브젝트 lookup이다. 기본 stateless hasher라 당장 stateful-hasher 버그가 발현되지는 않는다.
- 이번 검토에서는 코드와 상태를 변경하지 않았으므로 rollback 대상은 없다.
- 총 6개 주요 정확성/계약 문제와 5개 성능 후보를 찾았다. 우선순위는 `Table` → `Array` → `Int/StrongTypeOps` 순서가 적절하다.

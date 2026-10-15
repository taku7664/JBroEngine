# JBroScript 단일 `ref`와 엔진 로그 연동

2026-10-14 · D-317 · Updates: D-262의 타입·null 접근, D-316의 편집 시 분석. 현재 작업 브랜치: `codex/jbroscript-language-server`.

## 목표와 경계

`ref T` 하나로 nullable 값·엔진 핸들·호출 중 빌림을 적는다. `target1.target2.position`은 각 `ref` 접근에서 유효성을 검사하고,
첫 실패만 엔진 로그에 남긴 뒤 현재 스크립트 콜백을 중단한다. 엔진 ImGui 로그 패널과 외부 JBro Script Editor는
같은 호스트 로그를 보여준다. 오류 반복은 작성자가 명시한 키로만 묶는다.

종전 D-262의 `?`/`ref` 분리와 검사 없는 접근의 컴파일 오류는 이 결정으로 교체한다. 단순히 문장만 건너뛰던
옛 자동 검사 방식은 되살리지 않는다. 그 방식은 뒤의 코드가 잘못된 기본값으로 계속 실행될 수 있었다.

## 1단계 — 문법·편집 진단·로그 전달

- 파서·분석·API 카탈로그·편집기 문법에서 `ref T`를 사용한다. 옛 `T?`는 변경 진단을 낸다. `ref T`의 일반 접근은
  null 미검사 오류를 내지 않으며 없는 멤버·타입 오류와 빌림 탈출은 계속 검사한다.
- 엔진 호스트가 시간·일련번호·등급·분류·메시지·선택적 묶음 키를 가진 로그를 소유한다. 기존 `Log::Write`는 유지하고,
  묶기를 원하는 작성자에게 별도 기록 경로를 제공한다. 같은 키는 메시지와 객체가 달라도 한 묶음이다.
- 세션 동안 묶음의 총횟수와 최신 시각은 정확히 유지한다. 펼친 발생 이력은 최근 10,000건까지 보유하며 이전
  발생이 생략됐음을 표시한다. 키 없는 항목은 각각 표시한다. Clear는 호스트 저장소를 지우고 양쪽 화면을 동기화한다.
- 기존 루프백 제어 포트에 YAML 로그 스냅샷·증분 조회를 더한다. 세션 ID·일련번호·Clear 세대로 재접속과 누락을
  식별한다. 외부 편집기는 이를 별도 엔진 로그 패널에 표시한다. LSP의 JSON-RPC 채널과 섞지 않는다.
- 단계 종료 시 실제 편집기에서 저장하지 않은 잘못된 멤버·문법 오류의 표시와 해제를 확인한다. 엔진 로그는
  반복 키·무키·Clear·재접속·두 화면 일치를 사용자 시나리오로 공격적으로 시험하고, DLL 경계·메모리 상한·
  UI 계층·IPC 복구를 아키텍처 관점에서 코드 검토한다. 발견 사항을 고친 뒤 1단계를 닫는다.

## 2단계 — C++ 이미터·실행 자동 검사

- C++ 이미터가 언어 AST와 분석 결과에서 게임 DLL 소스를 만든다. `ref` 값은 소유 nullable 저장소, 엔진 객체는
  핸들, 빌린 인자는 콜백 안의 수명 제한으로 내린다. 빌린 주소를 멤버·반환·컨테이너로 내보내지 않는다.
- `ref` 접근은 수신자를 한 번만 평가하고 각 단계마다 null 또는 무효 핸들을 확인한다. 실패한 첫 단계의
  스크립트 위치·접근 경로를 호스트 로그로 보낸 뒤 현재 콜백만 멈춘다. 게임 루프와 다음 콜백은 계속된다.
  생성 로그의 묶음 키는 스크립트 위치와 실패한 접근 지점으로 정한다. 일반 `T`에는 이 검사를 넣지 않는다.
- 게임 DLL에는 별도 정적 로그 버퍼를 만들지 않는다. 호스트가 제공하는 버전 있는 로그 수신 컨텍스트를 사용하고,
  재로드·언로드 뒤 오래된 포인터를 호출하지 않도록 검사한다.
- `target1`·`target2` 각각의 실패, 같은 콜백 안의 선행 부작용, 반복 로그, 다른 게임 오브젝트, `?.`·`??`,
  `ref Int` 값 없음, 빌림 중 컨테이너 변경, Debug/Release 빌드와 실제 게임 콜백을 공격적으로 시험한다.
  코드 검토에서 생성 코드의 평가 횟수·콜백 중단 범위·ABI·할당·재진입을 확인하고 수정한 뒤 2단계를 닫는다.

## 현재 확인 상태

- 1단계의 `ref` 문법·분석·API 카탈로그·편집기 문법, 호스트 묶음 로그와 루프백 증분 프로토콜, ImGui 및 외부 편집기의 별도 엔진 로그 화면을 구현했다. 같은 키의 다른 메시지, 키 없는 항목, 10,000건 이력 밀림, Clear, 64건 페이지, 세션 재시작을 시험했다. Debug/Release 빌드와 선택한 엔진 시험 및 편집기 자동 시험이 통과했다. 실제 Code-OSS에서 저장하지 않은 `target.missing.x`의 `JBC3032`와 `scri!t`의 `JBC2001`을 만들고, 밑줄·Problems 목록의 표시와 원본 복원 후 해제를 Python Pillow 캡처로 확인했다. 엔진 로그 두 화면의 실제 표시·펼침·Clear·재접속 일치는 아래 2026-10-15 GUI 증거로 확인했으며, 단계 검토 결과와 범위는 마지막 확인표에 둔다.
- 실제 화면 검증에서 편집기의 언어 태그 `ko`를 서버가 `ko-KR.yaml`로 연결하지 못해 진단만 영어로 표시하는 결함을 발견했다. 지원하는 한국어 태그는 `ko-KR`, 나머지 언어는 영어 폴백 `en-US`로 연결하고 빈 태그에는 서버 기본 언어를 적용한다. 언어 태그를 파일 경로로 쓰지 않는다. `ko`·대소문자·다른 지역 태그·미지원 언어·경로 모양 문자열·빈 값의 LSP 회귀 시험과 Debug/Release 빌드·선택 시험 2개가 통과했다. 새 `JBroc` 프로세스의 실제 한국어 진단 표시·해제도 확인했다. 증거는 `F:\AI\Temp\jbroscript-korean-member-error-20261014-203220.png`, `jbroscript-korean-syntax-error-20261014-203322.png`, `jbroscript-korean-cleared-20261014-203336.png`다.
- 1단계 아키텍처 검토: 호스트가 로그를 소유하고 DLL과 LSP의 로그 저장소를 공유하지 않는다. 그룹 총횟수 정확성을 위해 키별 요약은 Clear 전까지 유지하므로 서로 다른 키의 수만큼 메모리가 늘어난다. 2단계 생성 키는 스크립트 위치와 접근 지점으로 고정하고, 실행 중 문자열 키 생성·할당이 없는 호스트 수신 경로를 사용해야 한다.
- 2단계의 첫 실행 경로가 섰다. `JBroc --emit-cpp`가 단일 2D `.jscript`를 분석한 뒤 지원하는 구문을 C++로 만들고, 시험 프로젝트가 그 결과를 실제 게임 DLL로 빌드한다. 생성 DLL은 호스트 오류 수신 컨텍스트를 필수로 요구하며, 잘못된 수신 데이터는 로드 전에 거부한다. 호스트는 로더 세대가 맞을 때만 같은 로그 저장소에 기록한다.
- 실제 DLL의 콜백에서 `target1.target2.position`의 첫째·둘째 실패, 선행 부작용 뒤 콜백 중단, 다른 오브젝트의 같은 오류 묶기, `?.`·`??`, 빈/있는 `ref Int`와 `ref Array<Int>` 인덱스, `for (ref item in values)`, 재로드 뒤 로그 재연결을 시험했다. 빌림 중 배열 변경은 의미 분석이 막는다. 스크립트 참조의 실행 중 번호와 캔버스 파일 번호 왕복, `[prop]` 필드만 등록되는 것도 확인했다. 생성 DLL 프로젝트는 `/W4`와 경고 오류 처리를 통과했다. Debug/Release 전체 솔루션 빌드와 선택한 실행 시험이 통과했다.
- 2단계 아키텍처 검토: DLL에는 로그 고리가 없다. 생성된 키·메시지는 리터럴이고, 반복 오류는 호스트의 고정 로그 항목을 갱신한다. 처음 보는 키의 묶음 요약은 한 번 할당하며, 다른 키가 계속 늘면 Clear 전까지 그 요약도 늘어난다. 로그는 메인 스레드 계약이다. 콜백 함수 포인터는 버전·크기·세대 확인을 거치고 예외를 DLL 경계 밖으로 내보내지 않는다. 오류 수신 경로에서 호스트 로그가 다시 자신을 호출하는 콜백은 두지 않았다.
- 에디터가 만드는 기존 C++ `GameScript.vcxproj`는 엔진 소유 `JBro.GameScript.targets`에서 `Scripts/**/*.jscript`를 찾아 생성 C++를 중간 폴더에 둔다. 사용자 프로젝트 파일과 `ScriptModule.cpp`는 다시 쓰지 않는다. 생성 스크립트가 하나라도 있는 DLL은 기존 C++ 진입점을 쓰더라도 호스트 오류 수신 컨텍스트를 필수로 요구한다. 여러 `.jscript`의 선언을 한 번에 분석·생성하며 파일 간 참조와 양방향 `ref` 필드를 MSBuild로 빌드했다. 소스 삭제 뒤 생성 코드의 옛 타입이 없어지는 것도 확인했다.
- 실제 `EditorApplication::BuildScripts()`에서 C++와 `.jscript`를 함께 빌드하고, 추가·삭제·문법 오류를 차례로 시험했다. 오류는 빌드 결과에 원본 `.jscript`의 파일과 줄로 표시됐다. 실제 `ScriptSystem`의 두 게임 오브젝트를 두 프레임 갱신하여 한 객체의 접근 실패 뒤 다음 객체·프레임이 실행되고 같은 오류가 네 번 한 그룹에 쌓이는 것을 확인했다. Debug/Release 게임 프로젝트 빌드가 통과했고 3D 프로젝트는 명시적 생성 오류를 낸다.
- 생성 DLL에서 `Table<K, V>`의 두 변수 순회와 빌린 값 변경, 상속된 엔진 API의 암시적 호출, 제네릭 컴포넌트 메서드, 사용자 클래스 메서드, `?.` 메서드 호출을 검증했다. 빈 `ref` 수신자에서 메서드를 건너뛰고 현재 콜백만 중단하며, 다음 콜백은 실행된다. 부작용이 있는 수신자 함수는 실패·성공 모두 한 번만 실행됐다. 반환형이 `Void`인 `?.` 호출도 빈 수신자에서 로그 없이 건너뛴다. Debug/Release 생성 DLL의 `/W4` 빌드와 선택한 실행 시험이 통과했다.
- 상속된 엔진 API를 `this.GetGameObject()`처럼 명시적으로 호출해도 생성 DLL이 부모 API를 찾아 실행한다. 사용자가 선언한 멤버가 있으면 그것을 우선한다. 실제 첨부 스크립트의 Debug/Release 선택 시험이 통과했다.
- 의미 분석이 `Array<Int>()` 생성 결과의 타입 인자를 보존하고, C++ 생성기가 제네릭 이름 식을 실제 타입 생성으로 내리도록 고쳤다. 생성 DLL에서 배열 생성·메서드 호출·인덱스 읽기를 연속 실행했다. Debug/Release 생성 DLL의 `/W4` 빌드와 선택한 분석·실행 시험이 통과했다.
- 생성기의 `this` 식과 `Table<String, Int>` 문자열 인덱스를 실제 게임 DLL에서 검증했다. 테이블 키를 배열 인덱스처럼 `size_t`로 변환하던 경로를 타입에 맞는 키 생성으로 고쳤다.
- 여러 파일로 생성한 DLL에서 enum의 이름 기반 리플렉션·캔버스 저장/복원, 범위 `for`, `switch`의 두 분기를 실행했다. 이어서 `switch` 조건의 `Int`·`Float`·`Bool`·`String`·스크립트 enum과 실행 시 계산하는 `case`를 생성 DLL에서 확인했다. 조건과 각 `case`는 일치할 때까지만 한 번씩 평가하고, 분기 후 반복문의 `continue`도 유지한다. 비교 의미가 없는 클래스 값은 `JBC3042`로 분석 단계에서 거부한다. Debug/Release 선택 시험이 통과했다. 재빌드 중 오래된 MSVC PDB 서버가 Debug PDB를 잡아 `C1041` 오류가 났고, 해당 서버를 종료한 뒤 파일 삭제 없이 두 구성 모두 재검증했다.
- 생성 DLL의 실제 `Light2D`·`Transform2D`·게임 오브젝트에서 읽기/쓰기 속성의 대입을 확인했다. 무효 `ref`의 복합 대입은 우변 부작용 전에 콜백을 멈추고 위치별 로그를 남긴다. 유효해지면 숫자·`String`·`Bool`·`Vector2` 속성이 엔진 setter에 반영된다. Debug/Release 선택 시험이 통과했다.
- 분석기가 허용하던 `Vector2`의 덧셈·뺄셈·배율 곱셈/나눗셈과 복합 대입을 생성 C++의 성분 계산으로 내렸다. 두 피연산자와 배열 원소 복합 대입의 인덱스를 각각 한 번만 평가하며, `Float(Int)` 명시 변환도 생성한다. 실제 DLL의 부작용·성분 값을 Debug/Release 선택 시험으로 확인했다.
- `class` 생성자, `static` 필드·메서드와 `const` 필드·지역 변수·매개변수를 생성 DLL에서 실행했다. 정적 상태가 호출 사이에 유지되는지 확인했고 Debug/Release 선택 시험이 통과했다.
- 사용자 클래스·스크립트의 상속, 인터페이스의 다이아몬드와 가상 메서드 구현을 생성 DLL에서 실행했다. 부모·자식의 `[prop]` 필드를 합친 표를 만들고 다중 상속의 두 번째 부모 주소를 보정한다. 이름·필드 개수·실제 주소와 Debug/Release 게임 DLL 실행을 확인했다.
- 필드 어트리뷰트 `prop`·`range`·`clamp`·`name`·`tooltip`·`category`·`visiblewhen`·`readonly`·`noserialize`·`hidden`과 `protected` 반영 필드를 생성 DLL에서 검증했다. 사용자 결정에 따라 `[prop] const`는 읽기 전용으로 표시하고 인스턴스 저장에서 제외한다. `[prop] static`은 모든 인스턴스가 같은 주소를 편집하고 인스턴스 저장에서는 자동 제외한다. `[noserialize]`를 생략한 `[prop] static`에는 `JBC3041` 경고를 내되 빌드는 계속하며, 명시하면 경고가 사라진다. 실제 DLL의 두 인스턴스와 저장 파일, Debug/Release 선택 시험으로 확인했다.
- 사용자 결정에 따라 `class`·`struct`의 `==`·`!=`는 명시한 인스턴스 연산자 오버로드에서만 허용한다. 선언 문법·서명 진단·후보 선택·상속 이름 가리기를 추가했다. 오버로드가 없거나 `!=`만 빠진 비교는 편집 시 오류로 잡고, 타입이 다른 오른쪽 피연산자와 부모 연산자를 포함한 비교는 생성 DLL에서 실행했다. Debug/Release 선택 시험이 통과했다.
- `ref Int` 같은 값 소유형 nullable 저장소는 대상 포인터가 없다. 사용자 결정에 따라 직접 `==`·`!=`를 편집 시 오류로 막고, 비어 있는지 보려면 `is null`을 사용한다. Debug/Release 분석·생성 DLL 회귀 시험이 통과했다.
- 호출 오버로드가 반환 타입별로 다를 때 생성기가 첫 선언의 타입을 쓰던 경로를 수정했다. 의미 분석이 고른 호출 결과 타입을 생성기에 전달하며, `factory.Build("x").LabelScore()`처럼 두 번째 오버로드의 결과에서만 있는 메서드를 실제 DLL에서 실행했다. 기존 `?.` 호출의 nullable 반환도 분석 결과 타입으로 보존한다. Debug/Release 선택 시험이 통과했다.
- JBroScript 이름 `template`·`delete`·`namespace`·`virtual`·`new`·`xor`와 `jbro_gen_` 접두사, `_Upper`·`value__part`, 엔진 내부 이름 `GetTypeId`·`JBroFieldAt`, 전역 네임스페이스 `JBro`·`std`가 생성 C++에서 충돌하던 경로를 고쳤다. 생성용 C++ 이름은 일대일로 바꾸고, 스크립트 타입 등록·필드 반영 표는 원래 이름으로 직접 만든다. 실제 게임 DLL에서 메서드 실행, 원래 타입 이름으로 첨부, 원래 필드 이름의 반영·캔버스 저장/복원을 확인했다. Debug/Release `/W4` 빌드와 선택 시험이 통과했다. 임의의 사용자 C++ 헤더가 만드는 전역 이름·매크로와의 충돌까지 보장한 것은 아니다.
- 분석기가 허용한 핸들의 `== null`·`!= null`을 생성기가 `handle == {}`로 내어 C++ 빌드가 깨지던 경로를 고쳤다. `is null`과 같은 실행 시 유효성으로 낮추며, null이 왼쪽에 있는 비교와 `null == null`도 처리한다. 없는/있는 `Transform2D` 핸들과 빈 사용자 스크립트 참조를 실제 게임 DLL에서 시험했다.
- 핸들끼리의 동일성 비교도 검증했다. `Handle::GameObject`에는 기존 리플렉션과 같은 인스턴스 ID 비교 `operator==`가 없어서 생성 DLL이 실패했다. 이를 추가하고, 엔진 핸들의 `operator==`가 엔진 `Bool`을 반환해 C++20이 `!=`를 자동으로 만들지 않는 경로는 이미터에서 `!(a == b)`로 내렸다. 게임 오브젝트·컴포넌트 핸들을 생성 DLL에서, 서로 다른 게임 오브젝트 ID를 엔진 시험에서 확인했다. 비교는 저장된 ID 기준이며 현재 유효한지는 별도의 `is null`/`IsValid()`가 판단한다.
- 의미 분석이 허용한 `ref ChildScript` → `ref ParentScript` 변환과 서로 다른 정적 타입의 동일성 비교가 생성 DLL에서 C++ 오류를 내던 경로를 고쳤다. 같은 참조 범주의 파생 타입에서 부모 타입으로 저장된 ID·캐시를 복사하고, 부모·자식 참조는 ID로 비교한다. 초기화와 대입 후 상속 메서드 호출을 실제 게임 DLL에서 검증했다. `Ref<T>`의 크기·표준 레이아웃·단순 복사 가능성 정적 검사는 유지되며 Debug/Release `/W4` 빌드와 선택 시험 7개가 통과했다.
- 인터페이스를 저장하는 `ref`는 기존 `JBC3020`이 이미 막는다. 별도로 순수 가상 함수를 가진 `class`의 `ref` 필드·지역 변수·반환형, 추상 클래스·인터페이스의 값 매개변수는 분석이 통과해도 생성 C++에서 값 소유가 불가능한 빈틈을 확인했다. 이 위치를 원본 스크립트의 `JBC3016`·`JBC3020`으로 진단하고, `ref` 빌림 매개변수는 계속 허용한다. Debug/Release 분석·생성 DLL 선택 시험이 통과했다.
- 인터페이스 다이아몬드를 구현한 클래스 값을 `ref ICounter` 매개변수에 빌려 주는 호출을 추가했다. 분석기는 저장형 `ref`의 변환과 달리 빌림의 바탕 타입 상속을 검사하고, 관계없는 타입은 거부한다. 생성기가 인터페이스 메서드를 C++ `private`으로 내던 기본 접근도 문법의 `public` 규칙에 맞췄다. 메서드가 실제 DLL에서 실행되어 값을 돌려주는지 Debug/Release `/W4` 빌드와 선택 시험으로 확인했다.
- nullable 값을 일반 값으로 반환하거나 사용자 함수·제네릭 엔진 API·생성자 인자로 넘기는 네 경로가 분석을 통과해도 생성 C++에서 실패하는 결함을 재현했다. 분석이 선택한 호출의 치환된 매개변수 타입을 생성기에 전달하고, 반환형과 함께 값 변환에 적용했다. 없는 값은 한 번 평가한 뒤 현재 콜백만 중단하며 반복 로그는 같은 위치로 묶인다. 값이 생긴 다음 콜백은 정상 실행된다. 일반 값을 소유 nullable로 만들 때 생성자를 명시하고, 임시 nullable에서 꺼낸 일반 값은 임시 객체가 사라지기 전에 복사한다. 실제 게임 DLL의 네 경로와 긴 `String`의 수명을 Debug/Release `/W4` 빌드·선택 시험 5개로 확인했다.
- `JBroc --emit-project <폴더>`와 실제 게임 프로젝트·시험 DLL을 소스별 생성으로 전환했다. `.jscript`마다 `.cpp` 하나를 만들고, 사용자 타입마다 정의·전방 선언 헤더를 분리한다. 메서드·프로퍼티 표·타입 설명자는 `.cpp`에서 정의한다. 스크립트 `ref`는 헤더에서 전방 선언만 사용하며, 값 소유 필드·상속에는 필요한 타입 정의를 포함한다. 인스턴스 필드 초깃값은 헤더에서 소스 정의의 생성자 초기화 목록으로 옮겼다. 생성 `.cpp`는 메서드와 필드 초깃값에서 실제로 참조하는 사용자 타입의 헤더를 포함한다. 기존 `--emit-cpp`의 합친 출력도 명시적으로 유지한다. SDK 진입점은 기존 `<JBro/ScriptAPI.h>`다.
- 같은 내용의 생성 파일과 소스 순서 변경은 `.cpp` 수정 시각을 보존한다. 실제 `EditorApplication::BuildScripts()`에서 한 스크립트의 메서드만 고치면 다른 스크립트의 `.obj`가 다시 컴파일되지 않는 것을 확인했다. 소스 추가·삭제·마지막 소스 삭제·원본 소스 위치의 오류도 시험했다. 마지막 `.jscript`를 삭제한 DLL의 실제 진입점을 읽어 이전 생성 코드의 필수 오류 컨텍스트 요구가 남지 않는 것을 확인했다.
- 생성기 변경의 아키텍처 검토: 컴파일러 모듈은 메모리 산출물만 만들고, CLI가 중간 폴더의 파일을 쓴다. 생성 목록의 파일명과 정규화한 부모 경로를 확인한 뒤 이전에 소유한 파일만 삭제한다. 변조한 목록의 `../` 경로를 거부하며 사용자 파일은 보존한다. 소스 진단이 있으면 이전 생성 결과에 쓰지 않는다. 각 파일은 임시 파일을 완성한 뒤 교체한다. 프로젝트 전체의 파일 교체를 하나의 트랜잭션으로 보장하지는 않으며, I/O 오류는 MSBuild 생성 단계를 실패시켜 그 결과를 컴파일하지 못하게 한다. 남은 임시 파일을 발견하면 자동 삭제하지 않고 오류로 알린다.
- 소스별 생성 전환까지의 변경은 Debug/Release 생성 DLL의 `/W4` 빌드와 분석·파서·CLI·DLL 로더·실제 에디터 프로젝트·LSP 선택 시험 6개가 통과했다. Release 컴파일러에서 기존 PDB 손상 경고가 나와 `JBroc`만 재빌드했고, 이후 빌드에서 그 경고가 사라졌다. 시험의 에디터 프로젝트는 Debug 게임 DLL을 빌드하며 Release 생성 DLL 실행은 DLL 로더 시험에서 별도로 검증한다. 아래 D-331 초기화 검사 변경의 최종회귀를 이 결과로 대신하지 않는다.
- D-331의 사용자 결정에 따라 필드를 선언 순서대로 초기화하고, 아직 초기화하지 않은 필드의 읽기를 오류로 처리하는 경로를 추가했다. 직접 자기 필드·후속 필드 읽기는 `JBC3044`로 편집·컴파일 진단하며 영어·한국어 메시지를 추가했다. `static` 초기화식에서 암시적으로 인스턴스 필드·메서드·`this`에 접근하면 `JBC3028`로 거절한다. 초깃값을 소스 정의의 생성자 초기화 목록으로 옮긴 뒤, 상호 참조하는 두 파일의 메서드 호출 초깃값을 Debug/Release `/W4 /WX` 생성 DLL에서 실행했다(`InitializerScore == 195`).
- 인스턴스 생성 중 검사에서는 타입·주소·완료한 필드 순서를 256개 고정 TLS 배열에 기록한다. 다음 초기화식에 진입하면 앞선 필드의 생성·복사가 끝났음을 확인하며, 생성자의 함수 try-block은 생성 실패 시 상태를 제거한다. 생성 완료 뒤에는 이 생성 추적 상태를 남기지 않는다. 실패는 기존 `AbortCallback` 처리에 전달해 현재 실제 콜백만 중단한다. 타입 해시는 생성 시점에 계산해 숫자 리터럴로 내므로 실행 시 문자열 해시는 없다. 이 기록은 현재 구현 구조를 설명하며 전체 타입 조합과 재진입 검토를 완료했다는 뜻은 아니다.
- Debug/Release 생성 DLL에서 메서드 직접 읽기·`ref this` 빌림·값 인자·`Array.Add`·값 반환·nullable 소유 저장을 통한 미완성 필드 읽기·객체 복사를 거절했다. 동등성 연산자의 값 매개변수와 `??` 대체 값의 미완성 객체 복사 두 경로도 보완했다. 호스트 로그는 8개 실패 위치를 기록하며, 중첩 생성 실패 한 번을 더해 총 9회 실패를 확인했다(`InitializationGuard == 933`). 두 객체 콜백의 같은 오류가 한 그룹에 2회 기록되고, 후속 정상 콜백이 실행되는 것도 확인했다. 상속된 앞선 필드 읽기, 중첩 생성 실패 뒤 다음 생성, nullable 소유 객체 필드의 변경에서 lvalue 유지, 임시 `String` 소유자에서 복사한 필드 수명도 두 구성의 DLL에서 통과했다. 실제 `AttachScript` 생성 실패는 기존 `noexcept Construct` 경계에서 `nullptr`로 바뀌며, 같은 객체·슬롯의 두 실패가 한 로그 그룹에 2회 기록된다. 정상 조건으로 바꾼 뒤 같은 객체에 첨부가 성공하는 것도 확인했다.
- 합법적으로 사용하지 않는 함수 매개변수가 생성 C++의 `/WX`에서 `C4100`으로 실패해 생성 매개변수에 `[[maybe_unused]]`를 적용했다. CLI에서 필드 초깃값을 `1`에서 `2`로 바꾸었을 때 헤더와 다른 `.cpp`의 수정 시각을 보존하는 시험도 통과했다.
- 과거 nullable 자기 타입을 생성하는 시험에서 생성자가 재귀 호출되어 `C00000FD` 스택 오버플로가 발생했다. 시험은 정상 생성 조건의 `mode`로 잠시 바꾸어 빈 nullable을 준비한 뒤 순수한 `??` 복사 검사만 수행하도록 조정했다. 현재도 저장 모델은 `Bool present`와 `T value`이며 빈 상태에서 `T value{}`의 기본 생성이 실행된다. 아래 owned 값 검증은 이 구조에서 기본 생성 부작용·실패 전달과 재귀 Array 조합을 확인한 것이며, 프로그램이 작성한 생성자의 재귀를 일반적으로 금지하거나 해결했다고 보지 않는다.
- 명시적인 `Type.staticField` 접근을 `(Type)::member`로 잘못 생성해 C++ 빌드가 실패하던 경로를 `Type::member`로 고쳤다. 임시 `class`·nullable·`Array` 소유자를 반환받아 `!`·인덱스를 거쳐 `String` 필드를 읽는 시험도 `C0000005`로 실패했다. 수신자를 검사 람다의 전달 인자로 받아 바깥 전체 표현식까지 C++ 임시 수명을 유지하도록 고쳤으며, Debug/Release DLL 실행에서 통과했다. 수신자를 한 번 평가하고 lvalue/rvalue 및 배열 원소 빌림을 보존하며, 추가 복사로 수명 문제를 덮지 않는다.
- 초기 LSP 회귀 실패는 입력 큐가 `didOpen`·`didChange`를 합쳐 최종 상태만 발행하는데 시험이 중간 오류 발행도 기대한 데서 발생했다. 오류만 보낸 세션과 수정까지 보낸 세션을 나누어 원본 위치의 `JBC3044` 및 버전 2의 진단 해제를 각각 검증했다. 이 시점의 검증은 LSP 프로토콜이었다. 당시 실제 Code-OSS 샘플은 `dirty=true`여서 사용자 내용을 보존하려고 화면 편집 시험을 멈췄으며, 이후 작업용 임시 샘플의 실제 `JBC3044` 표시·해제는 아래 GUI 기록으로 구분한다.
- 변수 바인딩에 필드 여부를 보존해 의미 분석 결과를 생략하는 기존 `EmitCpp` API에서도 암시적 필드 읽기 검사를 생성한다. 생성자 매개변수와 지역·반복 변수의 이름 가리기는 기존 범위 저장·복원과 함께 구분한다. 이 변경 뒤 최종 Debug/Release 빌드는 모두 종료 코드 0이며, 생성 DLL은 `/W4 /WX`를 통과했다. 두 구성의 분석·파서·CLI·DLL 로더·스크립트 프로젝트·LSP 선택 시험 6개를 같은 임시 폴더의 충돌을 피하도록 순서대로 다시 실행해 각각 종료 코드 0과 `selected tests passed (6 suites)`를 확인했다. 한국어·영어 메시지 표, 신규 Runtime `ScriptInitialization` 헤더의 독립 컴파일과 실제 생성 DLL의 헤더 사용, 초깃값 변경의 `.cpp`만 수정 시각 갱신도 포함한다. 그때는 생성 타입 헤더를 각각 단독으로 포함한 별도 번역단위 시험을 실행하지 않았고, 이후 현재 생성 헤더 126개를 독립 검사한 결과는 아래 후속 기록에 둔다. 최종 실행 로그는 [Debug 최종 로그](F:/AI/Temp/jbro-initialization-debug-final.txt)와 [Release 최종 로그](F:/AI/Temp/jbro-initialization-release-final.txt)다.
- 앞선 Code-OSS 확장호스트 재시작 뒤 새 Debug `JBroc` 프로세스 PID 9584를 확인해 언어서버 연결을 복구했다. 당시 사용자 버퍼 해시는 전후 모두 `0a8f8dd5c914cfbfd3e2670815d22f538b06dd9c81e3d48342568fdf90f55fa5`였고 `dirty=true`를 유지했다. 사용자 편집을 원복하거나 저장하지 않았다. 이후 Code-OSS 종료를 확인했고 이전 프로필의 `Backups`는 비어 있어 원래 `PatrolProbe` 미저장 버퍼의 복구를 확인하지 못했다. 종료 주체는 미확인이다. 이 과거 해시 확인을 현재 버퍼의 보존·복구 증거로 사용하지 않는다.
- 이번 변경의 코드 검토에서는 생성 상태의 예외 정리, DLL 생성 진입점의 catch 경계, 소스 의존·임시 수명, 정상 검사 경로의 고정 메모리·리터럴과 변수 이름 가리기를 읽었다. 이는 이번 변경의 검토 범위이며 전체 계획의 단계 종료 시나리오·아키텍처 리뷰를 완료한 것은 아니다.
- D-332의 사용자 지시 "최대한 애매하면 c++에 맞춰"에 따라 미정·애매한 동작은 가능한 한 C++에 맞춰 판단한다. 이미 정한 단일 `ref`와 실행 검사, D-331의 초기화 전 읽기 오류는 유지한다. 사용자는 정적 초기화 시점 질문의 특정 옵션을 선택하지 않았다. C++ 기본 정적 초기화 경로와 엔진 연결 전 API 호출·실행 오류 로그의 충돌을 검토한 뒤 아래 경계를 구현했다. 이는 D-332에 따른 구현 판단이며 사용자가 엔진 연결 뒤 초기화를 별도로 선택한 것으로 기록하지 않는다.
- 2026-10-15 현재 정적 값은 DLL의 OS 로드 중 만들지 않는다. SDK `Load`가 호스트 컨텍스트·스키마·스크립트 타입 표를 연결한 뒤 생성하고 첫 사용자 콜백 전에 전체 생성을 마친다. 타입 내부 선언 순서를 지키며, 다른 타입 의존은 접근자를 통해 초기화한다. 아직 미완료인 정적 필드를 간접 읽으면 `ScriptGuard` 오류로 `Load`가 실패한다. SDK `Unload`는 성공적으로 생성한 타입들을 생성의 역순으로 파괴한 뒤 바인딩을 해제한다.
- 정적 값 활성화 중의 오류 전달을 위해 실패한 SDK `Load`도 고유 세대를 소비한다. 호스트는 활성화 중이거나 실린 모듈의 현재 세대 오류만 로그에 허용하고 실패 뒤 오래된 세대의 오류는 거절한다. 자기 타입을 가진 반영 정적 값의 메타데이터는 주소가 안정된 표를 먼저 공개하고 SDK 스키마 준비 단계에서 채워 순환 정적 초기화를 피한다.
- 실제 DLL에서 `StaticOwned` 싱글턴의 자기 타입 설명자 동일성, 타입 사이 정적 값 의존과 초기화 중 호스트 `Time.TimeScale()` 값 `2.25` 조회를 확인했다. `Bad.jscript`에서 후속 정적 필드를 간접 읽으면 `Load`가 `false`로 끝나고 원본 위치의 엔진 묶음 로그가 기록된다. 실패한 DLL의 타입 등록이 남지 않고 이후 정상 DLL을 다시 로드하는 것도 확인했다.
- 정적 초기화 구현의 Debug/Release 실제 DLL 빌드가 성공했다. 두 구성의 분석·파서·CLI·DLL 로더·스크립트 프로젝트·LSP 선택 시험 6개도 각각 종료 코드 0과 `selected tests passed (6 suites)`를 확인했다. 최종 회귀 로그는 [Debug 정적 초기화 로그](F:/AI/Temp/jbro-static-debug-final.txt)와 [Release 정적 초기화 로그](F:/AI/Temp/jbro-static-release-final.txt)다. 이 결과는 시험한 정적 초기화 시나리오의 증거이며 전체 언어 계약의 완료 판정은 아니다.
- 사용자 `script`의 `override`를 모두 엔진 콜백으로 취급해 `OverrideChild::Read() -> Int`에 `AbortCallback` catch의 `return;`을 넣던 결함을 실제 Debug 게임 DLL의 `C2561`로 재현했다. 명시적 `callback` 또는 API 계층의 `IsCallback` 엔진 메서드만 실패를 잡도록 고쳤으며, 사용자 void/nonvoid `override`의 실패는 실제 엔진 콜백까지 전파한다. 새 `Overrides.jscript`로 가상 부모 호출의 결과 `7`, `Read`·`Touch` 안의 빈 `ref` 실패 시 선행 효과 유지·후행 건너뛰기, 정상 조건의 다음 콜백 실행과 원본 접근 두 위치의 호스트 로그 그룹 각 1회를 확인했다. 시험 콜백의 접근 지정 누락 `C2248`과 첫 수정의 `string_view.View` 오류 `C2039`도 수정한 뒤 재실행했다. Debug/Release `/W4 /WX` 실제 DLL 빌드와 각 구성의 분석·DLL 로더 선택 시험 2개가 모두 종료 코드 0으로 통과했다. 증거는 [실패 재현 로그](F:/AI/Temp/jbro-custom-override-red.txt), [Debug 검증 로그](F:/AI/Temp/jbro-custom-override-debug.txt), [Release 검증 로그](F:/AI/Temp/jbro-custom-override-release.txt)다.
- `Nullable(nullptr_t)`가 소유 값 `T`의 기본 생성자를 실행하면서 `noexcept`를 약속하던 결함은 `FailingNullablePayload`의 trait 검사 실패로 재현했다. `noexcept`를 제거해 생성 실패가 실제 엔진 콜백 중단 경계까지 전달된다. 저장 모델과 값 소유 `ref`의 직접 비교 금지는 유지했다. `Array<ref RecursiveOwned>`의 실제 DLL 빌드를 `C2079`로 막던 `MaxSize` 변수의 조기 `sizeof(T)` 평가도 `constexpr` 함수로 지연하고, 기존 세 사용 지점에서 같은 상한을 호출하도록 고쳤다.
- 실제 `OwnedValues` DLL에서 빈 nullable의 값 기본 생성 부작용 1회·nullsafe 대체 값, 기본 생성자가 없는 값의 명시적 `RequiredPayload(6)` 소유 생성, 재귀 owned Array 노드·중첩 값 변경과 재귀 프로퍼티 표의 포인터 동일성을 확인했다. null 값의 생성자가 후속 필드를 읽어 실패하면 두 콜백에서 `before == 2`, `after == 0`, 호스트 로그 한 그룹의 횟수 2가 된다. 직접 인라인 owned `ref` 순환은 기존 이미터 음성 시험이 거부하고, 스크립트 핸들 `ref` 순환의 허용은 유지한다. 초기 재현 샘플의 지원하지 않는 owned `ref != null`·`Array.Count`·삼항 사용은 제거하고 기존 null 확인·nullsafe·메서드 규칙을 따르는 샘플로 수정했다.
- owned nullable·Array 변경의 Debug/Release `/W4 /WX` 실제 DLL 빌드가 통과했다. Debug는 기본 값 타입과 언어 관련 6개를 합한 선택 시험 7개 및 `ReflectionContainer`·`ReflectedYaml` 2개가, Release는 해당 선택 시험 9개가 통과했다. 증거는 [실패 재현 로그](F:/AI/Temp/jbro-owned-values-red.txt), [Debug 7개 시험](F:/AI/Temp/jbro-owned-values-debug.txt), [Debug 컨테이너 2개 시험](F:/AI/Temp/jbro-owned-values-debug-containers.txt), [Release 9개 시험](F:/AI/Temp/jbro-owned-values-release.txt)다.
- 2026-10-15 실제 Code-OSS에서 작업용 `InitializationOrderProbe.jscript`의 후속 필드 읽기 `JBC3044`를 hover·Problems·밑줄로 확인했다. [오류 화면](F:/AI/Temp/jbro-log-screen-proof/editor-initialization-error-20261015-004929.png) 이후 자기 임시 샘플을 원본으로 복원해 해제했으며, 원본 해시 `40082ad4…`, `dirty=false`, `diagnostics=[]`를 확인했다. 이 임시 샘플 복원은 원래 사용자 `PatrolProbe` 버퍼의 복구가 아니다.
- 실제 SDK 오류 로그 시험 DLL의 `screen-proof-repeat` 3회와 무키 2건을 엔진 ImGui·Code-OSS 별도 엔진 로그 패널에서 확인했다. [편집기 펼침](F:/AI/Temp/jbro-log-screen-proof/editor-log-expanded-20261015-014310.png)과 [엔진 이력](F:/AI/Temp/jbro-log-screen-proof/engine-log-history-20261015-014312.png)의 최신 `.494`·횟수 3·이력 `.403/.447/.494`가 일치했다. 이력 순서는 편집기 최신순·엔진 시간순으로 다르다. Clear 뒤 양쪽이 비었고 `cleared.json`은 세션 `84859529889892`, Clear `1`, 항목 0이다. 엔진 종료 뒤 편집기 연결 끊김, 재실행 뒤 새 시간 `01:52:25.097`의 그룹 3회·무키 2건을 확인했다. `reconnected.json`은 새 세션 `79854472612884`, Clear `0`, 항목 9이며 이전 세션 로그의 중복은 없었다. [재접속 엔진 이력](F:/AI/Temp/jbro-log-screen-proof/engine-reconnected-history-20261015-015623.png)의 최신 `01:52:25.097`·횟수 3·발생 `01:52:24.996/01:52:25.050/01:52:25.097`도 편집기 재접속 화면과 새 스냅샷에 일치했다. Clear·재접속 이미지와 JSON 증거는 [편집기 계획 §16](./script-editor-integration-plan.md#16-jbroscript-진단과-엔진-로그-실제-화면-2026-10-15)에 연결한다.
- 실제 활성 외부 설치의 `F:/Project/JBroScriptEditor/upstream/src/vs/workbench/common/jbroVisibility.ts`에 있던 미커밋 허용 목록이 엔진 로그 패널을 `when:false`로 숨기던 원인을 확인했다. 기존 규칙을 유지하고 `panelContainers`에 `workbench.view.extension.jbro-engine-logs`, `panelViews`에 `jbro.engineLogs`만 추가했다. 해당 `out` 모듈을 esbuild로 갱신한 뒤 작업용 새 clean 프로필을 공식 다시 로드했고 가시성 red/green 선택 시험 2개가 통과했다. 이는 외부 설치 소스·로컬 빌드 수정이며 엔진 브랜치 커밋이나 관리 중인 패치 5개 적용의 증거가 아니다. 백업·검증 범위는 편집기 계획 §16에 둔다.
- `require` 메서드가 남은 `RequiredScript`와 이를 상속한 `InheritedRequiredScript`까지 생성 등록해 `C2338`·`C2259`로 실제 DLL 빌드가 실패하는 결함을 [음성 재현](F:/AI/Temp/jbro-abstract-script-red.txt)했다. 생성 등록의 dependent template에서 `if constexpr (std::is_abstract_v<Script>)`로 두 추상 타입은 인스턴스 생성 등록을 건너뛰고 구체 타입만 등록하도록 수정했으며, 런타임 공개 API는 바꾸지 않았다. 실제 DLL에서 두 추상 타입의 생성 레지스트리 등록 없음, `ConcreteRequiredScript` 부착 성공, 상속 프로퍼티 `seed` 1개와 부모 포인터를 통한 가상 호출 결과 7을 확인했다. 시험의 Canvas 할당자 인자 누락 `C2512`를 수정했고, 최초 회귀 실패를 낸 `ScriptProjectTests`의 옛 등록 문자열 탐색도 새 생성 형식에 맞췄다. 중간 `LNK1168`은 작업용 새 clean GUI가 실행한 `JBroc`의 파일 잠금으로 확인해 해당 임시 창을 공식 닫고 빌드를 재개했으며, 이는 앞선 원래 사용자 창의 종료 주체 미확인과 별개다.
- 추상 스크립트 수정 뒤 Debug/Release의 `/W4 /WX` 실제 DLL 및 `JBroTests` 빌드는 모두 종료 코드 0이다. 파서·분석·CLI·LSP·DLL 로더·스크립트 프로젝트 선택 시험 6개도 각 구성에서 종료 코드 0으로 통과했다. 다른 작업트리의 동시 `JBroTests` 실행을 확인해 재시험은 `TEMP`·`TMP`를 `F:/AI/Temp/jbro-final-debug`와 `F:/AI/Temp/jbro-final-release`로 나누었으며 외부 프로세스를 종료하지 않았다. 증거는 [Debug 빌드](F:/AI/Temp/jbro-abstract-script-debug-build.txt), [Debug 시험](F:/AI/Temp/jbro-abstract-script-debug-tests.txt), [Release 빌드](F:/AI/Temp/jbro-abstract-script-release-build.txt), [Release 시험](F:/AI/Temp/jbro-abstract-script-release-tests.txt)다.
- 현재 Debug 생성 목록의 헤더 126개를 manifest로 선택해 각 헤더만 포함하는 별도 번역 단위 126개로 MSVC `/Zs /W4 /WX` 검사를 실행했고 종료 코드 0을 확인했다. [선택 목록](F:/AI/Temp/jbro-log-screen-proof/standalone-headers/inventory.txt)과 [검사 로그](F:/AI/Temp/jbro-standalone-headers.txt)는 이 생성 스냅샷의 헤더 독립성 증거다. [MSBuild 항목 평가](F:/AI/Temp/jbro-log-screen-proof/standalone-headers/evaluation.json)에서도 번역 단위 126개가 모두 `/Zs`를 사용하고 PCH 사용과 강제 include는 각각 0개임을 교차 확인했다. 초기 임시 검사 드라이버가 `ProjectConfigurations`를 제거해 낸 `MSB8013`은 해당 label을 보존한 뒤 정상 실행했으며 제품 결함이 아니다. 이전 Runtime 헤더 검사와 달리 이번에는 생성 타입 헤더도 각각 독립 검사했지만 모든 향후 생성 조합의 증명으로 확대하지 않는다.
- 외부 엔진 로그 뷰의 YAML 묶음·구분자 포함 분할 응답·과대 응답 거절·Clear/재시작 선택 시험 4개도 [최종 Node 로그](F:/AI/Temp/jbro-final-engine-log-view-tests.txt)에서 종료 코드 0으로 통과했다. 외부 설치의 두 ID 추가는 [별도 재적용 diff](F:/AI/Temp/jbro-log-screen-proof/engine-log-visibility.patch)로 보존했으며 기존 presentation policy에 대한 미커밋 변경이다. 관리 중인 패치 5개에 포함한 것으로 기록하지 않는다.

## 단계 종료 확인과 검토 범위 (2026-10-15)

검토 대상은 사용자가 선택한 2D JBroScript 전체 문법과 엔진 로그 전달 계약이다. `SyntaxKind`의 선언·필드/어트리뷰트·생성자·상속/인터페이스/`require`·`static`/`const`·`==`/`!=`·제어문·순회·식과 nullable/`ref` 값 조합을 현재 compiler suite와 생성 DLL 자료
`Attributes`·`Control`·`Guard`·`Inheritance`·`InitializationGuard`·`InitializersLeft/Right`·`Names`·`Overrides`·`OwnedValues`·`StaticValues`에서 확인했다.
이는 정의된 구문 종류와 현재 유한 시험 목록의 확인이며, 모든 합법적인 프로그램·타입 조합을 증명한 것은 아니다.

| 단계·검토 | 확인 결과 |
|---|---|
| 1단계 사용자 시나리오 | 파서·LSP 오류 표시/해제와 이전 문서 버전 거절, 실제 로그 키 3회·무키 2건·최신 시각/이력 일치·Clear·끊김/재접속을 확인했다. 64건 페이지·10,000건 이력 밀림과 누락 판별의 시험을 함께 검토했다. |
| 1단계 아키텍처 | Compiler/LSP가 소스 모델을 공유하고 생성 산출물 파일 쓰기는 CLI에서 맡는다. 호스트 로그 소유·메인 스레드 계약, 컨텍스트 버전/크기/세대 거절과 호스트 수신 콜백의 catch 경계를 확인했다. 문자열과 그룹 키는 호스트가 복사·소유하며 DLL 언로드 뒤 포인터를 보관하지 않는다. 64건 페이지·Base64·응답 크기 상한과 세션/Clear의 BigInt 비교·재접속 reset을 검토했다. |
| 2단계 사용자 시나리오 | 체인의 첫째/중간 실패, 선행 효과 유지·후행 중단·다음 객체/프레임 실행, 수신자 1회 평가와 `?.`·`??`·`!`, 빌림 탈출·빌림 중 컨테이너 변경의 의미 오류를 확인했다. 초기화·사용자 override·추상 스크립트·owned nullable/Array의 실패 재현과 수정 후 실제 DLL 실행을 검토했다. |
| 2단계 아키텍처 | 로더 재활성화/실패 정리·생성 중 예외 상태 정리·정적 초기화 순환 실패·언로드 전 역순 파괴, 소스 추가/삭제와 내용 불변/국소 변경의 증분 생성, 임시 수명·변수 가리기·고정 검사 상태·리터럴 오류 경로와 현재 헤더 126개의 독립성을 확인했다. 유효 `ref`의 정상 검사는 inline 유효성 판단과 리터럴을 사용하지만 실패 경로에는 최초 키 할당·문자열 비교·예외 해제가 있다. 실패 경로까지 무할당으로 보지 않는다. |

두 단계의 사용자 시나리오 및 아키텍처 리뷰는 이 2D 계약 범위에서 완료했고 추가 차단 결함을 발견하지 않았다. 발견한 결함의 수정과 위 회귀 증거를 반영했다.
발생 이력은 10,000건이며 키별 요약은 서로 다른 키 수만큼 Clear 전까지 유지한다. 신규 묶음 행은 `Widget::Tree/Text/IdScope`를 거치지만 기존 `LogPanel`의 직접 ImGui 배관 호출은 잔여 기술 부채다. 패널 전체의 Widget 이관을 완료했다는 주장이 아니며 이번 기능 범위를 전 엔진 UI 이관으로 넓히지 않는다.
3D 생성, 모든 합법적 타입 조합의 증명과 메모리·성능 벤치마크는 이 검증에 포함하지 않는다. 헤더 검사는 현재 126개 스냅샷에 한정하며, 원래 사용자 버퍼의 종료 주체·복구는 미확인이고 외부 UI policy 변경은 미커밋이다.

## 메인 병합의 결정 번호 정리 (2026-10-15)

기능 브랜치의 JBroScript D-318(필드 초기화)은 메인 D-331, D-319(C++ 판단 기준)은 메인 D-332로 번호를 옮겼다.
기존 메인의 D-318(MSVC 빌드 도구)·D-319(DAP/PDB)와 D-330(에디터 프로세스 안의 디버깅)는 유지한다.
JBroScript D-316(언어 서버)·D-317(단일 ref)은 번호를 유지한다. 병합의 번호 충돌과 참조 앵커만 정리한 것이며 새 사용자 결정이나 의미·동작 변경이 아니다.

메인 병합 통합 검증(2026-10-15): Debug/Release `JBroTests` 빌드는 모두 종료 코드 0이며, Debug 선택 11개 suite는 모두 통과했다. Release는 NodeGraph·NodeGraphView·ScriptCompilerParser·ScriptCompilerCommandLine·ScriptCompilerAnalysis·ScriptLanguageServer·Log·EditorControlPort·ScriptDLLLoader·ScriptProject의 10개 suite가 통과했고 DebugAdapter 검사 1개가 실패했다. `TargetScript.cpp`의 `BREAK_HERE`는 20줄이지만 `setBreakpoints` 응답이 `verified: true, line: 21`이어서 정확한 줄 일치 조건을 만족하지 못했다. DAP 코드와 fixture는 병합 전 main HEAD와 동일하며, 실패 원인과 기존 main에서의 재현 여부는 확인하지 않았다. 증거: [Debug 빌드](F:/AI/Temp/jbroscript-main-merge-debug-build.txt)·[Debug 시험](F:/AI/Temp/jbroscript-main-merge-debug-tests.txt)·[Release 빌드](F:/AI/Temp/jbroscript-main-merge-release-build.txt)·[Release 시험](F:/AI/Temp/jbroscript-main-merge-release-tests.txt).

# JBroScript 언어 서버

2026-10-14 · D-315 · Updates: D-263의 분석·LSP 중단, D-262의 null 좁히기·서비스 이름표·API 투영.

이 문서는 JBroScript를 편집하는 클라이언트를 만드는 개발자를 위한 사용법과 구현 범위다.
빡대리가 승인한 범위는 독립 서버와 공용·2D 언어 기능 전체다. 엔진 패널과 코드 에디터가 같은 서버 실행 파일을
각자 띄우고 표준 LSP로 연결한다. C++ 스크립트의 언어 서비스는 별도 대상이다.

## 1. 제공 범위

| 기능 | 서버 동작 |
|---|---|
| 진단 | 렉서·파서·의미 분석의 오류를 열린 문서에 `publishDiagnostics`로 보낸다. 타입·접근 제어·상속·오버로드·null·borrow·엔진 읽기 전용 필드를 검사한다 |
| 자동 완성 | 현재 범위의 이름, 사용자·엔진 타입과 멤버를 제안한다. 덜 입력한 멤버 접근도 처리한다 |
| 정의·타입 정의 | 사용자 선언은 프로젝트 소스로, 엔진 선언은 카탈로그의 C++ 헤더 위치로 연결한다 |
| 문서·워크스페이스 심볼 | 현재 파일의 선언과 프로젝트의 사용자 선언을 조회한다 |
| 호버·인자 도움말 | 타입·시그니처·설명과 사용자·엔진 오버로드를 반환한다 |
| 참조·이름 변경 | 해석한 사용자 심볼의 선언·사용 위치를 찾고 충돌을 검사한 편집을 반환한다 |
| 의미 토큰·접기 | 선언·타입·멤버·주석 등의 전체 토큰과 중괄호 블록의 접기 범위를 반환한다 |
| 포맷 | 문법이 유효한 문서의 **줄 앞 들여쓰기만** 고친다 |

`semanticTokens/full`을 제공하며 range·delta 토큰 요청은 제공하지 않는다. 코드 생성·빌드·게임 실행은 제공하지 않는다.
기능 선언은 [Server.cpp](../source/JBroEngine/Modules/JBroScriptLanguageServer/Source/Server.cpp)의 `Capabilities`와
`FeatureMethod`, 각 기능의 구현은 [LanguageFeatures.cpp](../source/JBroEngine/Modules/JBroScriptLanguageServer/Source/LanguageFeatures.cpp)에 있다.

## 2. 빌드와 실행

Windows, Visual Studio 2026의 MSVC `v145`, C++20, x64를 사용한다. 저장소 루트에서 실행한다.

```powershell
MSBuild.exe source/JBroEngine/Modules/JBroc/JBroc.vcxproj /m:3 /p:Configuration=Debug /p:Platform=x64 /nr:false
```

기본 산출물은 `source/JBroEngine/Build/x64/Debug/JBroc.exe`이며 Release는 같은 경로의 `Release` 아래다.
`/p:JBroBuildDir=<별도 절대 폴더>\`로 출력 뿌리를 바꿀 수 있다. 빌드는 실행 파일 옆에
`Localization/jbroc/ko-KR.yaml`과 `en-US.yaml`을 복사한다.

```powershell
& .\source\JBroEngine\Build\x64\Debug\JBroc.exe --lsp --locale ko-KR
```

이는 에디터가 자식 프로세스로 띄울 명령이다. `--lsp`는 소스 파일 인자나 `--dump-tree`와 함께 쓸 수 없다.
기존 `JBroc [--locale ...] [--dump-tree] <파일.jscript>...`는 파일의 렉서·파서 진단용 경로로 유지한다.
프로젝트 전체 의미 분석은 현재 LSP 경로가 사용한다.

## 3. 클라이언트 연결 계약

- 클라이언트는 stdin/stdout으로 LSP 3.17의 `Content-Length: <UTF-8 본문 바이트 수>\r\n\r\n` 뒤에
  JSON-RPC 2.0 본문을 보내야 한다. `Content-Length`는 문자열 글자 수가 아니다. (MUST)
- stdout에는 프로토콜 메시지만 나온다. 운영 로그는 stderr로 받는다. 프로젝트 파일은 기존 YAML을 유지하고,
  JSON은 외부 에디터와 통신하는 표준 형식으로만 사용한다.
- 서버 프로세스 하나는 클라이언트 하나와 활성 프로젝트 하나를 맡는다. 워크스페이스 폴더 교체·여러 클라이언트의 공유 프로세스는 없다.
- 클라이언트는 `initialize`에 활성 `.jproject`를 `initializationOptions.projectFile`로 명시해야 한다. (MUST)
  `rootUri`만으로 프로젝트를 추측하지 않는다. 절대 경로와 `file:` URI를 받을 수 있다.

최소 초기화 본문 예시다. 경로와 버전은 사용하는 프로젝트에 맞춘다.

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "initialize",
  "params": {
    "processId": null,
    "locale": "ko-KR",
    "capabilities": {
      "workspace": {
        "workspaceEdit": { "documentChanges": true },
        "didChangeWatchedFiles": {
          "dynamicRegistration": true,
          "relativePatternSupport": true
        }
      }
    },
    "initializationOptions": { "projectFile": "C:/Games/MyGame/Project.jproject" }
  }
}
```

프로젝트 YAML에서 `Framework: 2D`와 이 실행 파일의 `EngineVersion`을 요구한다. 현재 엔진 버전은
`JBro.Common.props`에서 오는 `0.1.0`이다. `RootPath`의 기본값은 `.`, `ScriptSourceDirectory`는 `Contents`이며
그 디렉터리 아래의 `.jscript`를 재귀로 읽는다. 3D 프로젝트나 다른 엔진 버전은 초기화에서 거절한다.
공용 수학 값인 `Vector3`·`Vector4`·`Quaternion`·`Matrix4x4`의 사용과 3D Framework 지원은 다른 범위다.

초기화 응답 뒤 `initialized` 알림을 보낸다. dynamic watcher 등록을 지원하면 서버가
`client/registerCapability`로 소스 디렉터리의 `**/*.jscript` 생성·변경·삭제 감시를 요청한다.
클라이언트는 등록 응답과 `workspace/didChangeWatchedFiles` 알림을 보내야 디스크 변경을 반영할 수 있다. (MUST)
동적 등록을 지원하지 않는 클라이언트도 같은 파일 변경 알림을 직접 보낼 수 있다. 서버가 운영체제 감시기를 소유하지 않는다.

문서를 열 때 `didOpen`에 `languageId: "jbroscript"`, URI, 버전, 전체 text를 보낸다.
`didChange`는 전체 교체 또는 UTF-16 범위의 증분 편집을 받는다. 버전은 증가해야 하며 이전 버전의 변경은 버린다. (MUST)
파일 저장은 클라이언트가 맡는다. `didSave`는 열린 버퍼를 디스크로 덮지 않으며 서버도 파일을 저장하지 않는다.
`didClose`와 파일 삭제에서는 이전 진단을 비운다. 종료는 `shutdown` 요청의 응답을 받은 뒤 `exit` 알림을 보낸다.
`$/cancelRequest`를 처리하며 취소된 요청은 `-32800`으로 끝난다.

서버는 `positionEncoding: "utf-16"`을 반환한다. 내부 UTF-8 바이트 범위를 LSP의 0부터 시작하는 줄·UTF-16 열로 변환한다.
BOM은 화면 열에 포함하지 않고, 비 BMP 글자는 UTF-16 두 단위로 센다. `initialize.params.locale`이 CLI 기본 locale보다 우선하며
번역 파일을 찾지 못하면 영어를 시도한다. 소스 진단 번역과 프로토콜 오류 문구는 별도이며, 프로토콜 오류 문구는 현재 영어다.

## 4. 구현 경계와 증분 분석

| 모듈 | 책임·의존 |
|---|---|
| `JBroScriptCompiler` | 소스·AST·공용 API DTO·의미 분석·포맷. `JBroCore`에만 의존 |
| `JBroScriptApiCatalog2D` | 공용·2D 엔진 공개 선언을 분석용 카탈로그로 만든다. 실제 Tier S 타입을 참조하며 엔진 호스트를 실행하지 않는다 |
| `JBroScriptLanguageServer` | 프로토콜·문서 상태·분석 조회·편집 응답. Core·Compiler·JSON 헤더에 의존 |
| `JBroc` | Windows 진입점·프로젝트/스크립트/SDK 소스 읽기·스크립트 열거·stdio·번역 디렉터리·카탈로그를 조립한다 |

서버의 프로젝트·스크립트·SDK 소스 읽기와 스크립트 열거는
[Server.h](../source/JBroEngine/Modules/JBroScriptLanguageServer/Include/JBro/ScriptLanguageServer/Server.h)의
`WorkspaceFiles`를 통해 실행 파일 호스트에 위임한다. 진단 번역표는 기존 Compiler의 `DiagnosticMessages::Load`가 읽으며,
이는 `ProjectRule.md` §2의 컴파일러 진단 파일 예외다. 카탈로그는 헤더를 파싱하거나 파일을 읽지 않는다.
JSON 구현 타입은 서버 내부에만 두고 공개 분석 인터페이스에 내보내지 않는다.

[Analysis.h](../source/JBroEngine/Modules/JBroScriptCompiler/Include/JBro/ScriptCompiler/Analysis.h)의 `DocumentAnalysis`가
입력과 `SourceText`를 소유하고 `SyntaxTree`와 분석 결과를 함께 보관하므로 AST의 `string_view`가 임시 입력을 가리키지 않는다.
`AnalysisSession`은 변경하지 않은 문서의 소스·AST를 재사용한다. 본문 변경은 해당 문서, 선언 모양 변경·추가·삭제는 이름과
기존 참조 관계로 찾은 의존 문서를 다시 검사한다. API 카탈로그 시그니처가 바뀌면 전체 분석으로 돌아간다.
이는 모든 작업 비용이 변경 파일에만 비례한다는 보장은 아니다. 선언 수집과 의존 판정에는 프로젝트 조회가 남아 있다.
`ParsedDocuments`·`CheckedDocuments`와 소스 소유권을 정식 분석 시험에서 검사한다.

열린 문서의 저장하지 않은 text가 디스크보다 우선한다. 디스크 다시 읽기에서 오류가 나면 기존 문서 집합을 보존한다.
프로토콜 입력 읽기와 분석/응답을 나누되, 분석 결과와 공개 `SafePtr`의 소유권을 입력 워커로 넘기지 않는다.

## 5. 문법·엔진 API

파서는 D-262의 `T?`·`?.`·`??`·뒤붙이 `!`를 읽는다. `??`는 비교보다 강하고 덧셈보다 약하다.
borrow `ref`는 매개변수와 `for` 변수에서만 허용한다. null 검사 뒤에는 흐름을 좁히며 대입·빌린 인자의 변경·상태 변경 호출과
핸들을 무효로 만들 수 있는 호출 뒤에는 해당 보장을 보수적으로 지워 다시 검사하게 한다.
`!`의 타입 검사만 구현했고, 잘못된 단언을 로그로 남기고 콜백을 중단하는 실제 실행은 이미터의 남은 범위다.

엔진 API는 명시적인 공개 선언 표를 사용한다. C++ 함수·멤버 포인터로 실제 반환·인자 타입을 추출하고,
별도로 이름·기본 인자·접근·읽기 전용·상태 변경·출력 투영·정의 위치를 선언한다.
선언되지 않은 native 타입은 컴파일에서 거절한다. 리플렉션 필드는 실제 필드 표를 사용하며
리플렉션에 등록되지 않은 공개 값 필드는 실제 멤버 포인터로 검사한다. 사용자 함수와 엔진 함수의 오버로드를 모두 해석한다.

서비스 전역 이름은 공용 `Time`·`Random`·`Objects`·`Audio`·`Input`·`Save`·`Localization`·`Network`·`Session`,
2D `Physics`·`Text`·`Screen`·`DebugDraw`다. 출력 인자는 사용자 시그니처에 노출하지 않고 값·nullable·목록·문자열 반환으로 투영한다.
예를 들어 성공/실패와 hit 출력은 nullable hit로, `Input.CaptureBinding()`의 세 상태는
`InputCapture { result: InputCaptureResult, binding: InputBinding }`로 보존한다.
`InputCapture`는 카탈로그 안의 투영 값이며 엔진에 새 런타임 타입을 추가한 것이 아니다.

엔진 정의 위치는 빌드 당시 SDK 소스 경로를 가리킨다. 선언 토큰뿐 아니라 소유 타입의 범위도 시험한다.
빌드한 실행 파일을 소스와 별도로 설치하거나 SDK 폴더를 옮기는 배포는 검증하지 않았다.

## 6. 편집 응답의 제한

이름 변경은 사용자 심볼만 허용하고 예약어·이름 충돌·숨겨지는 참조를 검사한다. 엔진·서비스·SDK 선언은 읽기 전용이다.
클래스와 사용자 생성자는 함께 바꾼다. 메서드는 명시적인 `override`·`require`·인터페이스 구현으로 연결된 선언과
호출을 함께 바꾸고, 관계없는 같은 이름의 멤버 숨김과 다른 오버로드는 유지한다. 연결된 모든 타입에서 이름 충돌을 검사하며,
엔진 API·콜백의 고정 이름과 연결된 변경은 거절한다. 중복 편집 범위는 제거한다.
서버는 `WorkspaceEdit`을 반환할 뿐 파일을 쓰지 않는다. 클라이언트는 적용 전에 열린 문서 버전을 확인해야 한다. (MUST)
이름 변경을 사용하는 클라이언트는 `workspace.workspaceEdit.documentChanges: true`를 선언해야 한다. (MUST)
서버 편집은 열린 문서의 버전과 닫힌 문서의 `null` 버전을 포함한다. 해당 기능을 지원하지 않는 클라이언트의
이름 변경은 지원하지 않는다.

포맷은 `tabSize`·`insertSpaces`에 따라 줄 앞 공백만 바꾼다. 주석·문자열·토큰 내용·BOM·각 줄의 LF/CRLF·마지막 줄바꿈 유무를 보존한다.
문법 오류가 있으면 편집을 반환하지 않는다. 연산자 간격·긴 줄 배치·range formatting은 이번 구현 범위가 아니다.

## 7. 검증과 남은 범위

정식 시험은 `Tests/ScriptCompilerAnalysisTests.cpp`·`ScriptCompilerFormattingTests.cpp`·`ScriptApiCatalogTests.cpp`·
`ScriptLanguageServerTests.cpp`와 실제 `JBroc --lsp` 프로세스를 사용하는 `Tests/lsp_process_tests.py`다.
기존 렉서·파서·CLI 회귀도 함께 실행한다. 프로세스 시험은 초기화·종료·등록 감시·저장하지 않은 버퍼·파일 생성/삭제·
UTF-16/BOM·버전·취소·전체 편집 기능·엔진 투영·읽기 전용·한국어/영어 진단을 확인한다.

```powershell
MSBuild.exe source/JBroEngine/Tests/JBroTests.vcxproj /m:3 /p:Configuration=Debug /p:Platform=x64 /nr:false
$env:JBRO_TESTS = "Script"
& .\source\JBroEngine\Build\x64\Debug\JBroTests.exe
$env:JBRO_TESTS = $null
py -3.12 source/JBroEngine/Tests/lsp_process_tests.py --exe source/JBroEngine/Build/x64/Debug/JBroc.exe
```

2026-10-14 실행 결과다. 빌드·서버 시험과 게임·에디터 실제 사용을 구분한다.

| 확인 | Debug | Release |
|---|---|---|
| 최신 의미 분석·이름 변경 수정 뒤 `JBroTests`와 `JBroc` 의존 프로젝트 재빌드 | 성공 | 성공 |
| 렉서·파서·CLI·분석·포맷·카탈로그·서버의 7개 suite | 모두 통과 | 모두 통과 |
| 실제 `JBroc --lsp` 프로세스의 34개 시험 | 모두 통과 | 모두 통과 |
| 수정 전 추가 스크립트 기반 회귀 5개 suite | 모두 통과 | 모두 통과 |
| 수정 전 기본 SDK 디렉터리 배치의 Project/Build/HotReload 3개 suite | 모두 통과 | Project 통과, Build 실패로 중단 |

최신 재빌드와 주요 7개 suite·실제 34개 프로세스 시험의 로그는
`{debug,release}-final-{build,language-tests,process-tests}.log`다.
별도 빌드 뿌리는 `F:/AI/builds/jbroscript-language-server`다. 최신 두 구성의 재빌드와 관련 7개 suite를 확인했다.
첫 독립 검토에서 발견한 세 결함을 수정했다: nullable 멤버를 좁힌 뒤 `null`을 대입할 수 없던 문제,
`break` 뒤 도달할 수 없는 `return`이 반환 경로를 채우던 문제, 순회 중 `RemoveAtSwap`을 허용하던 문제다.
그 뒤 `switch`의 `break`, 반복문의 `continue`·`break`에서 빠져나가는 흐름을 보존하고,
여러 번 반복하며 바뀌는 null 상태가 더 이상 바뀌지 않을 때까지 검사하도록 보완했다.
이 네 상황의 회귀는 수정 전 `exit-flow-red.log`·`loop-break-flow-red.log`·`fixedpoint-flow-red.log`에서 실패했고,
기존 27개에 이 네 상황을 추가한 31개 실제 프로세스 시험은 두 구성 모두 통과했다.
이후 독립 검토에서 명시적 생성자가 있는 클래스 이름 변경이 생성자 호출을 빠뜨리는 결함을 확인했다
(`constructor-rename-red.log`, 필요한 4곳 대신 3곳만 변경). 공용 분석의 `GetRenameSymbols`로 클래스와
사용자 생성자를 한 그룹으로 묶고 서버가 중복 범위를 제거하도록 수정했다.
`test_class_rename_updates_constructor_declarations_and_calls`는 클래스 선언·생성자 호출·생성자 선언에서
차례로 이름 변경을 실제 적용하며 매번 4곳의 편집·문서 버전·중복 없음·빈 진단·디스크 불변을 검사한다.
이 회귀를 포함한 당시 32개 시험은 Debug/Release 모두 통과했다.
후속 검토에서는 `Base.Run`의 이름을 바꿀 때 `Child.Run`의 명시적 override가 빠지는 결함을 확인했다
(`override-rename-red.log`, 필요한 3곳 대신 1곳만 변경). override·require·인터페이스 구현의 연결 관계를
이름 변경 그룹에 포함했다. 추가한 `test_function_rename_updates_override_family_and_calls`는 base 선언·
override 선언·호출 각각에서 요청하고, 매번 중복 없는 3곳의 버전 편집을 적용한 뒤 빈 진단과 디스크 원본 보존을 확인한다.
이 회귀를 포함한 당시 33개 실제 프로세스 시험은 두 구성 모두 통과했다.
native 회귀는 유효한 초기 표본으로 3단계 override·숨김·오버로드·그룹의 모든 타입에서의 충돌·인터페이스·require·
콜백 이름 고정을 검사한다.
후속 검토에서는 `while` 조건을 다시 평가할 때 본문의 null 대입을 반영하지 않는 결함을 확인했다
(`while-condition-red.log`의 fallthrough·continue 두 실패). `test_while_condition_is_checked_again_on_reachable_back_edges`는
조건으로 돌아오는 두 경로에서 null 접근 진단 `JBC3007`을 확인하고, null을 검사한 조건·break·return 경로는 안전함을 검사한다.
native `TestWhileConditionReevaluation`은 조건이 거짓이 되어 빠져나갈 때도 조건의 상태 변경을 보존하고,
반복 검사에서 호출 위치와 진단 위치를 중복으로 남기지 않는지 검사한다. 이 회귀를 포함한 최신 34개 실제 시험과
관련 7개 suite는 Debug/Release 모두 통과했다.
최종 검토에서 활성 파서 시험에 남은 저장형 `ref` 예시를 확인했다. 필드·지역·컨테이너·반환의 예시와
기대 구문 트리를 `T?`로 교체했고, 빌림 매개변수의 `ref`는 유지했다. 파서 공개 헤더와 현재 문법 설명의
선언 예시도 맞췄다. `jbroscript-syntax.md` §13의 D-262 이전 예시는 이력으로 명시되어 있어 보존했다.
교체 후 Debug/Release 재빌드·관련 7개 suite·실제 서버 34개 시험을 모두 다시 통과했다.
편집기 확장의 활성 문법 예시·시험에서도 저장형 `ref`를 `T?`로 바꾸고, 문법 강조에 nullable 타입과
`?.`·`??`·뒤붙이 `!`를 반영했다. §14의 `Enemy` 예시로 스냅숏을 갱신한 뒤 `npm run test:grammar`의
6개 구문 시험과 스냅숏 비교가 모두 통과했다. 이는 문법 강조와 시험 자료의 갱신이며 LSP 클라이언트 구현은 아니다.
이전 27개 시험·7개 suite·재빌드 기록은 `{debug,release}-post-witness-{build,language-tests,process-tests}.log`,
최초 24개 시험은 `debug-process-tests.log`·`release-process-tests.log`에 남겼다.
카탈로그는 별도로 Debug/Release `/W4 /WX` 빌드를 통과했다. 정식 카탈로그 conformance probe는
정상 설정에서 컴파일되며, 미등록 native 타입과 틀린 인자 수 설정은 각각 의도한 `C2338` 단언으로 거절됐다.
이는 헤더 독립성·선언 일치 확인이며 생성 C++의 `/W4` 보장과는 다른 검증이다.

넓은 `JBRO_TESTS=Script` 실행에서는 앞의 12개 suite가 통과한 뒤, 외부 빌드 폴더에서 SDK 개발 디렉터리를
찾지 못하는 기존 시험 조건으로 중단됐다. 같은 해시의 산출물·라이브러리·번역표를 격리 작업트리의 기본
`source/JBroEngine/Build/x64/<구성>` 배치에 두고 남은 3개 suite를 따로 확인했다. TEMP/TMP도 구성별로
나눠 Debug/Release 시험 폴더의 충돌을 피했다. Release는 `ScriptProject` 통과 뒤 `ScriptBuild`에서
`the first build has only its speed` 검사 실패와 종료 코드 `-1073741819`를 관찰했다.
언어 서버·새 카탈로그·새 컴파일러를 링크하지 않는 별도 LoaderProbe에서도 Release 호스트가 Debug DLL의
`Speed` 필드 이름을 찾지 못했다(`loader-Debug.log`, `speedMatch=0`). 같은 호스트에 같은 스크립트의
Release DLL을 넣으면 필드 이름을 찾고 종료 0이었다(`loader-Release.log`, `speedMatch=1`).
이 프로브는 조회 후 `ExitProcess`로 끝내므로 DLL 해제·핫 리로드 전체의 성공을 검증한 것은 아니다.
`layout-results.log`의 실측은 Debug/Release `String`이 40/32바이트, Key+String 항목이 48/40바이트이며,
`NameTable` 본체는 양쪽 모두 48바이트였다. 공유 표의 항목 배치가 달라 이름 조회가 깨지는 C++ DLL 경계를 확인했다.
호스트 구성과 관계없이 스크립트 빌드를 `Debug`로 고정한 기존 `EditorApplication.cpp:2846`과
Debug DLL을 요구하는 `ScriptBuildTests.cpp:397,476`이 이 혼합 구성을 만들었다.
기준 커밋의 `TestMain`과 별도로 빌드한 기존 Compiler 라이브러리를 사용한 binary에서도 같은 `Speed` 검사 실패를 재현했다.
`baseline-link-inputs.txt`의 Compiler 의존은 기준 소스로 별도 빌드한 라이브러리이며 새 server/catalog 라이브러리는 없다.
`out/JBroTestsBaseline.map`과 `BaselineTestMain.cpp`에서도 기존 컴파일러 3개 suite만 확인되며 추가된
분석·포맷·카탈로그·서버 4개 suite의 심볼은 없다. `full-baseline-scriptbuild.log`의 동일 검사 실패와
`full-baseline-scriptbuild-exit.txt`의 종료 코드 `-1073740940`을 확인했다.
이는 기존 `ScriptBuild`의 별도 실행이며 전체 엔진 시험을 돌린 결과가 아니다.
이번 서버의 주요 7개 suite와 실제 LSP 시험은 양 구성에서 통과했으며, 이 별도 C++ 실행 경계 실패를 서버 실패로 세지 않는다.
제품 런타임 소스는 이번 작업에서 수정하지 않았고 후속 버그를 공용 todo에 남겼다.
Release의 `ScriptBuild`·`ScriptHotReload` 전체를 완료로 세지 않는다.

최초 Release 전체 테스트 실행 파일의 LTCG가 변경하지 않은 `Size.h:49`·`Float.h:99`·`CameraView2D.cpp:75~76`에서
`C4723` 경고 6개를 냈다. 새 카탈로그 소스의 경고는 없었다. 전체 엔진 테스트, UI·게임 실사용,
설치·Web 검증은 실행하지 않았다.
최종 독립 검토는 승인 계획과 코드·문서의 범위 일치를 확인하고 PASS로 보고했다. 검토자가 직접 편집기 문법 단언
154개와 스냅숏, Debug/Release의 실제 LSP 프로세스 시험 각 34개, 관련 C++ 시험 각 7개 suite를 다시 확인했다.
서버·컴파일러·2D 카탈로그는 `a574fe64`, 문법 강조·시험 자료는 `db25f8ae`에 로컬 커밋했다.

구현 중 발견한 UTF-8 한글 SDK 경로의 ANSI 변환, 반복된 선언 이름의 잘못된 소유 타입 위치,
native `string`과 스크립트 `String`의 대응 누락에는 각각 경로·소유 범위·카탈로그 일치 회귀를 추가했다.

남은 범위는 C++ 이미터와 생성 C++의 `/W4` 보장, nullable 값의 런타임 저장 형식, `!`의 콜백 중단,
코드 에디터 확장·엔진 ImGui 패널의 LSP 클라이언트, UI/webview, 디버거·게임 실행, 3D 프로젝트/Framework,
설치 패키지와 SDK 재배치다. 이 항목을 서버 기능의 완료와 함께 완료로 세지 않는다.

서버의 상세 구조는 이 개발자 문서에 둔다. 게임 사용자를 위한 위키에서 미구현 JBroScript 게임 실행을 설명하지 않는
기존 범위를 유지하며, 이번 작업에서 외부 위키를 게시하지 않았다. 기존 엔진 도면은 계층 개관의 JBroScript 상태 라벨만
분석/LSP 구현·이미터/게임 실행 미구현으로 교정했다. 실행 객체·렌더링 흐름과 도면 구조는 바꾸지 않았다.

## 8. 2026-10-14 편집기 연결

이 절은 §7의 서버 완료 기록 뒤에 구현한 편집기 연결을 기록한다. §7의 "코드 에디터 확장 미구현"은
당시 상태이며 현재 `.jscript` 개발 편집기에는 적용되지 않는다.

`jbro-languages` 확장은 `.jscript` 파일을 열면 작업 폴더의 `.jproject` 하나를 찾아 `JBroc --lsp`를
프로젝트별 stdio 프로세스로 시작한다. 실행 파일·프로젝트 파일을 설정으로 지정할 수도 있다.
신뢰하지 않은 작업 폴더에서는 서버를 시작하지 않는다. 서버의 진단은 Code-OSS Problems로,
자동완성 등 언어 기능은 편집기 API로 들어온다. 서버 호출 오류는 JBroScript Output 채널에 남긴다.

실제 Code-OSS가 `file:///f%3A/...` 형태로 보내는 Windows URI를 기존 서버가 `file:///F:/...`와
같은 문서로 찾지 못해 `-32602`를 반환했다. URI의 `%3A`를 해독한 뒤 드라이브 앞 슬래시를 제거하도록
서버를 고쳤다. 새 프로세스 회귀는 수정 전 실패, 수정 후 Debug/Release 모두 통과했다.

저장소의 `test/fixtures/sample-project`로 실행한 실제 Code-OSS 확장 호스트 시험은 종료 코드 0이었다.
`Time.DeltaTime` 자동완성, 저장하지 않은 null 검사 삭제 시 `JBC3007`, `script` 철자 훼손 시
`JBC2001`, 각각 복구 시 진단 해제, 디스크 원본 보존을 확인했다. Debug/Release 서버 재빌드와
실제 프로세스 시험 각 35개·관련 C++ 7개 suite, 편집기 번역·계정·문법 시험도 통과했다.
확장 호스트 API로 Problems 진단 자료를 검증했으며, 설치본 화면의 밑줄·패널 렌더링을 별도로
촬영·검사한 결과는 아니다. 실행·수동 재현 방법은 [편집기 README](../source/JBroScriptEditor/README.md#jbroscript-편집-진단)에 있다.

이 연결은 JBroScript 파서·의미 분석의 결과를 보여준다. C++ 이미터, C++ 컴파일 결과를 Problems에
가져오는 태스크, 게임 실행·엔진 ImGui 패널·3D Framework·설치 패키지는 구현 범위 밖이다.

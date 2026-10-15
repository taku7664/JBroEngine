# 스크립트 요약 패널과 기본 중단점 (2026-10-15, D-336)

## 승인 범위와 현재 상태

사용자가 승인한 범위는 현재 `.jscript`를 따라가는 오른쪽 요약 도크, 타입별 함수·멤버 목록, 멤버 속성·소스 초기값 편집과 기본 중단점이다.
확정 계약은 [ProjectRule §22](../docs/ProjectRule.md#22-스크립트-요약-패널과-기본-중단점-2026-10-15-d-336)에 둔다.
이 문서는 [내장 코드 편집기](./imgui-code-editor-integration.md)의 후속 구현이며, 기존 [엔진 중단점·어댑터](./script-debug-plan.md)를 재사용한다.

작업 브랜치는 `codex/imgui-code-editor-spike`다. 기존 편집기와 디버거의 병합 기준은 `35ca1c3d`이며 메인 checkout은 clean이다.
**D-336 최초 범위의 완료 기록과 감사 전 편집 보조의 22개 회귀·실제 화면 확인은 아래 이력이다. 기존 §11 감사 결함 5건과 추가 반례를 수정한 뒤 Debug/Release 전체 빌드와 각 24개 관련 suite·장기 `EditorApplication` 1개, 구성별 25개가 종료 코드 0으로 통과했다. 요청 기능·감사 수정의 해당 검증을 마쳤고 코드는 `af60d387`로 로컬 커밋했으며 검증 기록을 함께 갱신했다. 앞선 GUI 통과나 유한 회귀를 전체 규칙 준수의 증거로 확대하지 않는다.**
언어 변경 `34711c84`, 네이티브 변경 `095e5bf9`와 문서 `c9554f9d` 뒤의 후속 코드는 `af60d387`에 로컬 커밋했다. 해당 코드의 검증 기록을 함께 갱신했다. 메인 소스 수정·병합·원격 푸시·기존 에디터 교체는 하지 않았으며 사용자 호스트 74560을 보존했다.

## 사용 동작과 경계

- `코드 편집기` 오른쪽의 `스크립트 요약` 패널은 마지막 활성 코드 문서를 따른다. 요약 패널을 클릭해도 대상 파일을 잃지 않는다. C++ 파일의 요약 편집은 지원하지 않는다.
- 위쪽 목록은 타입별 함수·멤버를 선언 순서대로 표시하고 타입·플래그를 한 줄에서 읽게 한다. 항목을 선택하면 아래쪽에 해당 선언의 상세 설정을 보여 준다. 함수 시그니처·반환형·콜백 여부와 멤버 타입·접근 제한자·`ref`·`static`·`const`는 읽기 전용이고, 코드 이동으로 선언 위치를 연다.
- 편집 대상은 소스 초기값과 `[prop]`, `[range]`, `[clamp]`, `[name]`, `[tooltip]`, `[category]`, `[visiblewhen]`, `[readonly]`, `[noserialize]`, `[hidden]`이다. 체크·콤보 변경은 즉시 요청하고, 표현식은 Enter 또는 입력란 이탈 때 검사한다. 잘못된 표현식은 소스를 고치지 않고 입력란에 오류로 남긴다.
- 명시한 속성과 실제 적용되는 프로퍼티·읽기 전용·저장 제외·숨김 상태를 나란히 표시한다. 기존 의미를 유지하므로 `const`는 읽기 전용·저장 제외, `static`은 저장 제외이고 구조체의 필드는 기본 프로퍼티다. 다른 속성이 남아 있다면 `[prop]`만 제거해도 프로퍼티가 될 수 있다.
- 초기값은 소스 표현식이다. 패널은 실행 중인 인스턴스 값을 읽거나 쓰지 않고, 초기화 코드나 getter를 실행하지 않는다. 요약 편집은 버퍼를 수정하여 미저장 상태로 만들며 자동 저장·빌드를 하지 않는다.
- 여러 최상위 타입과 실제 중첩 타입을 처리한다. **사용자는 후속 확인에서 중첩 타입을 언어 문법에 포함하기로 명시했다.** 클래스·구조체·스크립트·인터페이스 안의 클래스·구조체·인터페이스·열거형을 여러 단계까지 지원한다. 중첩 `script`는 거절하고 스크립트 진입 타입은 최상위에 둔다. 상세 의미는 [문법 §21](./jbroscript-syntax.md#21-중첩-타입-선언과-이름-해석-2026-10-15-d-336)에 둔다.

## 분석·편집 인터페이스

| 계층 | 공개 경계와 책임 |
|---|---|
| 컴파일러 | `Summary.h`의 `SummarizeScript`는 소스·AST·기존 분석 결과로 선언/필드 속성과 유효 설정을 만든다. `GetFieldAttributeDefinitions`는 이미터와 공통 속성 이름·인자 개수를 사용한다. `EditFieldSettings`는 분석한 소스를 쓰지 않고 겹치지 않는 UTF-8 바이트 범위 편집을 반환한다. |
| 언어 서버 | `jbro/scriptSummary`와 `jbro/editFieldSettings`는 문서 URI와 정확한 `textDocument.version`을 요구한다. 편집 요청은 현재 필드 선택 위치와 설정을 받고 UTF-16 `TextEdit`를 반환한다. 버전 불일치는 `-32801`, 잘못된 설정·위치는 `-32602`다. capability는 `experimental.jbroScriptSummary`·`jbroEditFieldSettings`다. |
| 엔진 클라이언트 | `Code::ScriptSummary`, `SummaryEntry`, `FieldSettings`, `FieldEditResult`에는 엔진 값 타입을 사용한다. `LanguageClient::{GetSummary,EditFieldSettings,GetFieldEditResult}`가 요청 수명·버전 일치를 소유한다. JSON과 AST는 공개 에디터 API를 넘지 않는다. |
| 버퍼·패널 | `CodeEditorPanel::ApplySourceEdits`가 문서 버전과 읽기 전용 상태를 재검사하고 `Widget::CodeEditor::ApplyEdits`로 하나의 Undo 작업으로 적용한다. 요약 패널에 포커스가 있어도 저장·Undo·Redo는 해당 코드 문서에 연결한다. |
| 스크립트 빌드 구성 | `JBroScriptHostConfiguration` MSBuild 속성은 실행 에디터의 Debug/Release를 전달한다. 지정하지 않으면 기존 `Configuration`을 따른다. 스크립트의 디버깅 산출물 구성과 별개로 CRT·전처리 정의·엔진 정적 라이브러리는 호스트 구성에 맞춘다. 실제 빌드는 실행 폴더의 `JBroc.exe` 경로를 넘기며 테스트 전용 번역기 덮어쓰기를 요구하지 않는다. |

편집은 필요한 범위만 바꾸고 관련 없는 선언·공백·주석과 알 수 없는 기존 속성을 보존한다. BOM·CRLF 저장 규칙은 기존 파일 버퍼를 따른다.
직접 입력한 변경도 같은 문서 분석으로 요약에 반영한다. 구문 오류·분석 중·끊김에는 마지막 목록을 갱신 대기로 표시하고 편집·위치 이동을 막는다. 오래된 응답은 적용하지 않는다.
탭을 닫을 때 해당 문서의 진행 중 요청을 폐기하고, 다시 열린 문서에도 이전과 겹치지 않는 단조 증가 버전을 부여한다. 같은 경로를 닫고 다시 열었을 때 옛 수정 결과가 적용되던 결함을 수정하고 최종 회귀에서 재확인했다.
요약 UI의 첫 구현은 선언마다 읽기 전용 설정을 여러 줄 반복하여 편집 항목을 찾기 어려웠다. 사용자 지적 뒤 위쪽 목록과 선택 상세를 분리하고 초기값·프로퍼티·읽기 전용·저장을 먼저 배치했으며 범위·표시 속성은 접는 설정에 모았다. 기능 계약은 같고 화면의 정보 순서를 고친 것이다.
`const`의 읽기 전용은 자동 적용과 명시적 `[readonly]`를 콤보로 구분한다. 직접 지정을 제거해도 `const`의 실효 읽기 전용·저장 제외는 유지하며, 저장 상태는 자동 제외로 표시한다.

별도 리플렉션 인스펙터는 채택하지 않았다. 컴파일되지 않았거나 노출되지 않은 필드도 보여야 하고, 런타임 인스턴스와 소스 초기값의 의미가 다르기 때문이다.
정규식 선언 분석은 기존 파서와 규칙이 갈라지므로 사용하지 않는다. 전체 선언 재생성이나 `SetText`로 교체하는 방법은 주석·수동 편집·Undo 이력을 잃을 수 있어 사용하지 않는다.

## 기본 중단점과 수명

`Code::SourceDebugger`가 저장·빌드·DLL 반영·소스 스냅숏·시뮬레이션 시작 순서를 소유하고, `Code::DebugClient`는 기존 `JBroDebugAdapter`와 DAP를 주고받는다.
네이티브 주소 해석은 기존 어댑터가, 실제 중단점 설치와 멈춤 루프는 기존 엔진이 맡는다. 새 VM이나 두 번째 주소 해석기를 만들지 않는다.

- `.jscript` 줄 번호 옆을 눌러 중단점을 토글한다. 요청 위치·실제 위치·메시지를 보관하고, 실행 불가능한 요청 줄이 다른 실행 줄로 해석되면 실제 줄과 요청 위치를 안내한다.
- 주소를 찾았다는 것만으로 완료 표시하지 않는다. 엔진의 성공 설치 응답을 받아야 `verified`가 되며, 모든 해석된 요청의 설정 응답 뒤에만 시뮬레이션을 시작한다. 빌드·로드·연결 실패 때는 실행을 시작하지 않는다.
- 성공한 엔진 빌드의 소스와 실제 로드된 DLL을 연결한다. 버퍼나 디스크 소스가 달라지면 기존 빌드 기준의 중단점을 비우고 미확정으로 표시하며 잘못된 실행 줄 표시를 하지 않는다. 다시 디버그 실행하여 새 빌드와 연결한다.
- `Code::Breakpoints`는 프로젝트 세션의 문서별 줄 앵커를 소유한다. 위젯의 실제 편집 트랜잭션 범위를 받아 앞줄 삽입·삭제에 따라 이동하고, 중단점 줄 삭제는 제거·텍스트 Undo는 복원한다. 동일한 줄을 앞에 붙인 경우나 서로 먼 두 편집 사이에 있는 앵커를 전체 텍스트 유사도로 추측하지 않는다. 사용자가 직접 끈 중단점을 텍스트 Undo가 임의로 되살리지 않는다. 파일 탭을 닫아도 유지하고 프로젝트 종료 때 비운다.
- 멈춘 줄 표시·계속 실행·디버깅 종료를 제공한다. 중단 상태에서는 코드·요약 설정 편집을 잠그며 탐색과 실행 제어는 가능하다. 종료·연결 해제·DLL 교체 때 설치한 중단점을 정리한다. 다른 디버거가 엔진 통로를 소유하면 빼앗지 않는다.

조건부 중단점·변수 감시·호출 스택 UI·세션 재시작 후 중단점 저장·C++ 요약은 이번 범위 밖이다. 기존 외부 편집기와 어댑터 경로는 유지한다.

## D-336 최초 완료 당시 검증 (이력)

| 단계 | 최초 완료 당시 결과와 증거 |
|---|---|
| Debug 빌드·회귀 | 전체 솔루션 빌드와 관련 21개 suite가 종료 코드 0·skip 없이 통과했다([빌드](F:/AI/Temp/script-summary-host-debug-build.log)·[시험](F:/AI/Temp/script-summary-host-debug-tests.log)). Debug 출력은 격리된 `Build/SummaryDebug`다. 기본 Debug 산출물을 덮어쓴 것으로 해석하지 않는다. |
| Release 빌드·회귀 | 전체 솔루션 빌드와 같은 21개 suite가 종료 코드 0·skip 없이 통과했다([빌드](F:/AI/Temp/script-summary-host-release-build.log)·[시험](F:/AI/Temp/script-summary-host-release-tests.log)). 출력은 `Build/x64/Release`다. |
| 마지막 UI 수정 뒤 확인 | 입력란 이탈 편집 뒤 선택한 멤버 위치를 유지하는 수정 후 전체 빌드·`ScriptSummaryPanel/CodeEditor/EditorUI/EditorLocalization/EditorShortcut` 5개를 양 구성에서 다시 실행해 모두 종료 코드 0이었다([Debug 빌드](F:/AI/Temp/script-summary-ui-final-debug-build.log)·[시험](F:/AI/Temp/script-summary-ui-final-debug-tests.log), [Release 빌드](F:/AI/Temp/script-summary-ui-final-release-build.log)·[시험](F:/AI/Temp/script-summary-ui-final-release-tests.log)). 이 UI 수정 뒤 21개 전체를 다시 실행했다는 뜻은 아니다. |
| 실제 JBroScript 전체 시작 | `SourceDebugger`는 실제 JBroScript 저장·빌드·DLL 반영·설정 ACK 뒤 첫 콜백 중단과 계속 실행, 중단 중 편집 잠금·소스 불일치 뒤 설치 해제·재빌드/DLL 교체·종료·외부 통로 충돌·어댑터 비정상 종료 정리를 검사한다. Debug 12.1초·Release 13.6초로 통과했다. `NativeDebug`·`DebugAdapter`도 양 구성 21개 회귀에 포함한다. |
| 중첩 타입 언어·실행 | `NestedScriptType`가 파서·이름/접근 분석·재귀 요약·C++ 이미터와 실제 게임 DLL 컴파일/실행을 양 구성에서 통과했다(Debug 5.1초·Release 4.5초). 실제 공개 `result`는 **30**을 검사한다. 다단계·동명 타입 구분·외부 비공개 접근 거절·바깥 인스턴스 암묵 캡처 거절·점 주변 공백/주석·생성자 호출을 포함한다. |
| 줄 앵커 반례 | 전체 텍스트 비교 기반 위치 추정이 동일 줄 앞삽입과 1500줄 내 먼 두 편집 사이 앵커를 잘못 옮겼다. 실제 위젯 트랜잭션 범위를 전달하도록 수정하고 최종 양 구성의 `CodeEditor`에서 반례·Undo/Redo를 확인했다. |
| 실제 화면 | 아래 표의 요약 편집 화면을 확인했다. 설치 완료·실행 중단 표시는 당시 미확인이었으며, 아래 후속의 실제 디버그 화면과 stdout 교차 확인으로 갱신됐다. |
| 독립 검토 | 최신 코드와 이전/현재 실제 화면의 의도 검토 PASS. 후속 빌드 구성·최적화 검토와 마지막 위치 이동의 코드/실제 화면 검토도 PASS. 이 판정을 미실행 회귀나 미확인 화면의 통과로 확대하지 않는다. |
| 호스트 구성 평가 | [MSBuild 평가 JSON](F:/AI/Temp/script-summary-host-config-evaluation.json)에서 Debug 스크립트+Debug 호스트는 Debug CRT·`_DEBUG`·Debug lib, Debug 스크립트+Release 호스트는 Release CRT·`NDEBUG`·Release lib를 확인했다. 두 Debug 스크립트 구성 모두 최적화 끔·ProgramDatabase를 유지한다. 설정 평가와 실제 실행 시험은 구분한다. |

| 재설계 실제 화면 | 확인한 내용 |
|---|---|
| 목록·선택 상세 | [이전 화면](F:/AI/Temp/script-summary-ui/speed-expanded.png)과 [최종 재설계](F:/AI/Temp/script-summary-ui/final-summary.png)를 비교했다. 위쪽 목록에서 필드·함수를 함께 볼 수 있고 아래쪽은 선택 필드만 편집한다. |
| 초기값·한 번 Undo | `4.25`로 편집한 뒤 Undo 한 번의 [최종 화면](F:/AI/Temp/script-summary-ui/final-ui.png)에서 소스·입력란의 `3.0` 복원과 미저장 표시 제거, `speed` 선택과 코드 4줄 이동을 확인했다. |
| 입력란 이탈 반영·이동 | Enter 없이 초기값을 `3.0 → 4.25`로 바꾸고 `limit`을 선택한 [최종 화면](F:/AI/Temp/script-summary-ui/final-navigation-after-edit.png)에서 소스의 `4.25`, 선택 상세와 코드 6줄의 커서를 함께 확인했다. 초기값 수정 위치가 뒤늦게 선택 위치를 덮지 않는다. |
| 잘못된 표현식·한국어 | [최종 잘못된 입력](F:/AI/Temp/script-summary-ui/final-invalid-korean.png) `1+`는 한국어 오류를 표시하고 코드의 `4.25`를 유지했다. 이전 [영어 오류 캡처](F:/AI/Temp/script-summary-ui/redesign-invalid.png)는 발견 이력으로만 보존한다. |
| 속성 편집 | [읽기 전용](F:/AI/Temp/script-summary-ui/redesign-readonly.png) 체크로 `[readonly]`가 소스에 추가되고 미저장 상태가 됐다. |
| const 자동·직접 속성 | [선택지](F:/AI/Temp/script-summary-ui/final-const-options.png)의 자동 적용/직접 지정에서 [직접 지정](F:/AI/Temp/script-summary-ui/final-const-explicit.png)은 소스에 `[readonly]`를 추가하고, [자동 적용](F:/AI/Temp/script-summary-ui/final-const-auto.png)은 명시 속성을 제거하면서 const의 실효 읽기 전용·자동 저장 제외를 유지했다. |
| 미확정 중단점 | [줄 표시](F:/AI/Temp/script-summary-ui/redesign-breakpoint.png)의 빈 원과 `중단점 설치 대기` 툴팁을 확인했다. 설치 완료·실행 중단 표시를 이 화면만으로 확인했다고 세지 않는다. |
| 파일 전환 | [다른 탭](F:/AI/Temp/script-summary-ui/redesign-other-tab.png)을 선택하면 `Other.jscript`의 멤버 1개·함수 0개와 `label` 설정으로 바뀐다. |

### 최종 회귀에서 발견한 편집 잠금 결함

실제 `SourceDebugger` 정식 회귀에서 계속 실행한 직후 모델은 편집 가능해졌지만 위젯에는 직전 프레임의 읽기 전용 상태가 남아 `ApplySourceEdits`가 거절됐다.
화면 갱신 때만 맞춘 잠금 캐시를 API 경계에서도 현재 상태라고 가정한 것이 원인이다. `ApplySourceEdits`·Undo·Redo의 경계에서 현재 잠금을 동기화했고 재개 직후 편집을 포함한 실제 `SourceDebugger` 재회귀가 통과했다(최신 12.1초).
Undo/Redo 수정은 처음 다중 줄 치환이 CRLF 불일치로 적용되지 않아 소스를 직접 재확인한 뒤 추가했다. 변경 명령의 의도와 실제 소스 반영을 구분한 사례다.
앞선 격리 실행 통과로 새 실패를 덮지 않고 수정 뒤 재실행 결과로 갱신했다. 이 원시 신호는 이번 작업 기록에만 남기며 메모리나 CLAUDE/AGENTS 규칙을 추가하지 않는다.

### Release 회귀에서 발견한 호스트 구성 불일치

Release 에디터가 디버깅용 Debug 스크립트 DLL을 만들 때 CRT·공개 `String` 배치·엔진 정적 라이브러리 구성이 어긋나 필드 이름 조회/복원 경고와 첫 콜백 값 검증 실패가 발생했다.
빌드가 실행 호스트의 `JBroScriptHostConfiguration`과 실행 폴더 `JBroc.exe`를 전달하고, 공통 설정과 스크립트 링크 경로가 이를 따르게 고쳤다. 스크립트의 Debug 출력 위치·PDB·최적화 끔은 유지한다.
`UseDebugLibraries=false`만 지정하면 VS 기본값이 `/O2`가 되는 후속 문제도 확인하여 Debug 스크립트에 `Optimization=Disabled`·`DebugInformationFormat=ProgramDatabase`를 명시했다.
중첩 DLL 시험의 번역기 경로 덮어쓰기는 제거하여 제품 `BuildScripts` 경로를 검사한다. 설정 평가 뒤 Debug/Release 전체 빌드와 각 21개 suite를 완료했으며 실제 첫 콜백과 중첩 DLL 시험도 양 구성에서 통과했다.

### 입력란 이탈 편집 뒤 선택 위치

초기값을 입력하고 다른 멤버를 클릭하면 비동기 편집 응답이 원래 수정 위치로 커서를 돌려 새 선택을 덮을 수 있었다. 변경 뒤 현재 버전의 요약 응답을 받은 다음 선택한 선언 위치로 이동하도록 고쳤다.
최종 `ScriptSummaryPanel` 회귀와 위 실제 6줄 커서 화면으로 확인했으며 이 마지막 수정 후 양 구성의 전체 빌드·UI 관련 5개 suite를 다시 통과했다.

정식 회귀의 유한 범위는 여러 최상위/중첩 타입과 선언 순서, `static`·`const`·구조체의 유효 속성, 알 수 없는 속성과 주석 보존, BOM/CRLF·한글/UTF-16 좌표, 구문 삽입 거절, 문서 버전 불일치·닫기/재열기 수명, 단일 Undo/Redo, 실제 편집 범위에 따른 중단점 줄 이동·삭제/복구, DAP 설정 응답·오래된 결과·오류·정리와 실제 디버그 시작을 포함한다.
설치 완료·현재 중단 줄 표시는 최초 완료 때 별도 캡처하지 않았으나 아래 후속에서 확인했다. 기존 실제 DLL 시험과 이후 시각 증거의 시점을 구분한다. 조건부 중단점·변수 감시·호출 스택 UI·재시작 후 저장은 이번 범위 밖이다.
임시 하네스·화면 캡처·검증 로그는 D-244에 따라 제품 소스나 정식 회귀와 구분하며, 시험용 분기를 커밋·메인 병합하지 않는다.

## 2026-10-15 후속: 편집 보조와 공통 UI

Updates: D-336의 코드 이동·선택·메뉴 경계와 검증 범위. 원계약의 소스 속성/초기값 편집·중단점 수명은 유지한다.
사용자는 요약 항목을 누를 때 **선언 이름만 선택**하도록 확정했다. 이는 위 최초 완료 화면의 커서 이동보다 구체적인 후속 요구다. 공통 Widget·행동·팝업 경로는 새 규칙이 아니라 기존 §11의 준수 사항이며, 발견한 결함과 현재 점검 상태를 [ProjectRule §23](../docs/ProjectRule.md#23-코드-편집-보조-후속과-기존-ui-규칙-점검-2026-10-15)에 기록한다. 새 결정 번호는 부여하지 않는다.

- 줄 번호 칸을 가리키면 해당 줄의 가로 전체를 강조하고, 누르면 줄 문자열과 줄 끝을 선택하며 드래그로 줄 선택을 늘린다. 중단점 토글 영역과 줄 선택을 구분한다.
- 요약에서 선택한 선언의 **이름 범위만** 코드에 선택한다. 동일 문서에서 같은 심볼을 실제로 사용하는 위치는 노란 배경으로 표시하며 선언 자체·동명이인은 제외한다.
- 타입 툴팁은 기존 LSP `textDocument/hover`, 사용 위치는 `textDocument/references`를 재사용한다. `LanguageClient`의 버전이 있는 `HoverResult`·`ReferencesResult`와 위젯의 범위 선택·툴팁·강조 API를 연결하며 JSON은 통신 경계에만 둔다.
- 우클릭 메뉴의 실행 취소·다시 실행·잘라내기·복사·붙여넣기·전체 선택은 기존 `code_editor.*` 행동 6개를 공유한다. 표시 단축키와 실행 모두 사용자 키 매핑을 따르고, 팝업 열기·시작·끝까지 공용 `Widget` 경로를 거친다. 코드 캔버스의 0 간격·16px 글꼴을 메뉴에 강제하지 않고 호출한 UI의 글꼴·간격을 이어받는다.
- 실제 텍스트 입력란이 활성화된 동안 편집기 행동이 입력을 가로채지 않는다. 코드 버퍼 자체의 텍스트 입력 요청과 일반 `InputText`의 활성 ID를 구분한다. 붙여넣기는 한 번만 실행하고 한 번의 Undo로 되돌린다.

### 현행 빌드·시험 상태

| 대상 | 현재 상태와 증거 |
|---|---|
| 감사 전 이력 | 양 구성 [Debug 빌드](F:/AI/Temp/script-editor-assistance-debug-build.log)·[Release 빌드](F:/AI/Temp/script-editor-assistance-release-build.log)와 [Debug 22개](F:/AI/Temp/script-editor-assistance-debug-tests.log)·[Release 22개](F:/AI/Temp/script-editor-assistance-release-tests.log)는 종료 코드 0이었다. 아래 §11 위반을 검출하지 못했으므로 감사 수정 뒤의 통과로 사용하지 않는다. |
| 감사 수정 뒤 전체 빌드 | [Debug](F:/AI/Temp/script-editor-final-debug-build.log)·[Release](F:/AI/Temp/script-editor-final-release-build.log) 전체 솔루션 빌드 종료 코드 0이다. 이후 마지막 변경은 CLI 번역 키 coverage 시험 한 곳이며, Release는 [시험 대상 추가 빌드](F:/AI/Temp/script-editor-final-release-tests-build.log) 종료 코드 0을 확인했다. Debug 전체 빌드에는 그 변경도 포함한다. |
| 최신 관련 24개 회귀 | [Debug](F:/AI/Temp/script-editor-final-debug-regression.log)·[Release](F:/AI/Temp/script-editor-final-release-regression.log)의 suite 목록과 마지막 `selected tests passed (24 suites)`·종료 코드 0을 확인했다. `ScriptSummaryIntegration`의 실제 두 문서·UTF-16/BOM/CRLF·한글/emoji·주석·단일 Undo·저장·재연결·키 재매핑·한 번 붙여넣기·입력란 보호·빠른 포커스 경합, `SourceDebugger`의 5→6줄 해석·256개 설치 거절/실행 미시작과 `EditorWidget`·`EditorGuideFocus`를 포함한다. |
| 장기 회귀·완료 판단 | `EditorApplication`은 [Debug 410.2초](F:/AI/Temp/script-editor-final-debug-editorapplication.log)·[Release 394.1초](F:/AI/Temp/script-editor-final-release-editorapplication.log) 모두 `selected tests passed (1 suites)`·종료 코드 0이다. 위 24개와 합쳐 구성별 25개이며 네 시험 로그에 skip·시험 실패 표시는 없다. 행동 수 증가로 깨진 첫 화면 가정은 기존 검색 UI에서 안정된 행동 ID로 필터하도록 고쳤고, 원래 클릭·키 재매핑·검색 단언을 유지한 장기 재검사도 통과했다. 코드 로컬 커밋 `af60d387`의 해당 검증과 기록을 마쳤다. |
| 독립 검토의 한계 | 코사원의 내부 독립 소스 검토는 아래 세 API의 필요성 제한을 확인했지만 공통 프레임워크 전범위 무결함 판정은 아니다. 실제 Claude CLI 소스 검토는 사용량 제한으로 최종 판정 없이 끝났다. 이를 PASS로 세지 않으며 사용자가 추가 호출을 금지했다. |

### 현행 실제 화면과 교차 증거

아래 화면은 감사 전 편집 보조·디버그 기능의 확인 이력이다. 감사 수정 뒤 화면을 다시 촬영·비교한 증거로 세지 않는다.

| 확인 항목 | 확인한 대상과 한계 |
|---|---|
| 줄 번호 가리키기·선택 | [가리키기](F:/AI/Temp/script-summary-ui/final-api-gutter-hover-confirmed.png)와 [선택](F:/AI/Temp/script-summary-ui/final-api-gutter-line-selection-confirmed.png)을 비교해 가로 전체 강조·줄 문자열 선택·다음 줄 처음의 커서를 확인했다. |
| 이름 선택·사용 위치 | [이름/사용 위치](F:/AI/Temp/script-summary-ui/final-api-summary-name-references-confirmed.png)에서 선언의 `speed` 5글자만 선택되고 10줄의 사용 위치가 노란 배경인 것을 확인했다. 동명이인 제외 등 전체 의미 범위는 통합 회귀와 구분한다. |
| 공통 지연 툴팁 | [표시 전](F:/AI/Temp/script-summary-ui/final-api-float-tooltip-before-confirmed.png)과 [표시 후](F:/AI/Temp/script-summary-ui/final-api-float-tooltip-after-confirmed.png)를 비교해 공통 지연 뒤 `Float` 타입 툴팁이 생기는 것을 확인했다. |
| 공통 우클릭 메뉴 | [직접 비교](F:/AI/Temp/script-summary-ui/final-common-boundary-menu-comparison.png)에서 기존 편집 메뉴와 코드 메뉴의 6개 행동·단축키·글꼴/간격을 비교했고 첫 두 행의 픽셀 일치를 측정했다. [닫힘](F:/AI/Temp/script-summary-ui/final-boundary-context-closed.png)·[다시 열림](F:/AI/Temp/script-summary-ui/final-boundary-context-reopened.png)도 확인했다. |
| 속성 입력란 이탈·Undo | [입력 중](F:/AI/Temp/script-summary-ui/assistance-range-draft.png)은 최대값 `12.5` 초안과 소스 `10.0`을 유지한다. [이탈](F:/AI/Temp/script-summary-ui/assistance-range-blur.png)은 소스 `12.5`와 `limit` 이름 선택, [한 번 Undo](F:/AI/Temp/script-summary-ui/assistance-range-single-undo.png)는 `10.0` 복원과 미저장 표시 제거를 보인다. |
| 속성 Enter·Undo | [Enter](F:/AI/Temp/script-summary-ui/assistance-range-enter.png)는 최대값 `15.0`의 소스 반영, [Undo](F:/AI/Temp/script-summary-ui/assistance-range-enter-undo.png)는 `10.0` 복원과 미저장 표시 제거를 보인다. |
| 미확정→설치·중단 표시 | [미확정](F:/AI/Temp/script-debug-ui/08-pending-gutter.png)의 5줄 빈 원과 [해석/중단](F:/AI/Temp/script-debug-ui/13-relocation-tooltip.png)의 6줄 설치 위치·금색 실행 화살표·`5 → 6` 툴팁을 나란히 확인했다. |
| 계속 실행·종료 정리 | [계속 실행 뒤](F:/AI/Temp/script-debug-ui/14-continue-clicked.png) 재중단과 [종료 뒤](F:/AI/Temp/script-debug-ui/23-stopped-cleanup.png) 화살표 제거·5줄 미확정을 확인했다. [stdout](F:/AI/Temp/script-debug-ui/host.stdout.log)의 15줄 재개→16줄 재중단→18~19줄 종료로 교차 확인했으며 한 장의 중단 화면만으로 재개를 추정하지 않았다. |
| 화면 조작의 원본 보존·종료 | [프로세스/해시 기록](F:/AI/Temp/script-summary-ui/final-boundary-process.json)은 소스 해시 불변·메뉴 닫기/다시 열기·임시 호스트 85760 정상 종료를 기록한다. 기존 호스트 74560은 보존했다. 사용자 메인 창을 새로 배포하거나 종료한 검증이 아니다. |

### 이번 후속에서 드러난 확인 누락

- 코드 캔버스의 간격 0·16px 글꼴이 팝업으로 이어졌고 초기 메뉴는 공용 경로를 쓰지 않았다. 호출 UI의 스타일과 공통 팝업 수명을 연결한 뒤 실제 메뉴를 비교했다.
- 코드 버퍼도 `WantTextInput`을 요청하지만 일반 입력란의 활성 ID가 있는 상태와 다르다. 둘을 같게 막으면 코드 단축키가 멎거나 입력란이 가로채이므로 실제 활성 입력란을 구분했다.
- 독립 클립보드 모드 전환 뒤 시험에는 shortcut routing 등록을 위한 두 프레임이 필요했고 처음 설정이 이를 빠뜨렸다. 비활성 메뉴를 누른 후 우클릭으로 확인하던 helper도 메뉴를 다시 닫고 있었다. 제품 실패와 시험 설정·조작 실패를 구분하여 고쳤다.

### 기존 §11 준수 감사의 수정과 추가 반례

새 규칙이나 사용자 결정을 추가하지 않고 기존 §11 감사 결함 5건을 수정했다. 앞선 GUI·22개 기능 회귀는 이를 검출하지 못했다. [수정 전 실패](F:/AI/Temp/script-editor-rule-red-tests.log)는 메뉴/도구 막대의 `WhyBlocked`·아이콘 누락으로 종료 코드 1이었으며, 수정 뒤의 결과는 위 최신 24개 회귀와 구분한다.

1. `Widget/ScriptOutline.cpp`는 `TreeNodeEx` 직접 호출 대신 기존 `TreeBegin`/`TreeDrawContext`와 가이드 포커스를 사용한다. 목록 툴팁은 요약 응답 때 캐시하고, 중단점 툴팁 결합도 `Widget/CodeEditor.cpp::SetDebugMarkers`에서 캐시한다.
2. 디버그 도구 막대는 기존 공통 아이콘 단추를 사용하고 CodeEditor/Summary 행동에 `WhyBlocked`를 제공한다. 기존 File 메뉴의 Reload/External/Close도 행동 표를 거친다. 이 세 경로는 `35ca1c3d`에도 있던 잔여지만 같은 기능의 감사 범위에 포함했다.
3. `DebugClient`/`LanguageClient`의 자체 오류 28개를 기존 로컬라이징 경로로 옮겼다. 요약 편집 오류 15개(컴파일러 6개·LSP 9개)는 기존 `DiagnosticMessages::FormatKey`와 ko/en 표를 재사용하며 새 공개 API를 추가하지 않았다. [요약 오류 번역 실패](F:/AI/Temp/script-editor-summary-localization-red-tests.log) 뒤 실제 서버 검증과 `ScriptCompilerCommandLineTests.cpp`의 키 존재·언어 차이·정확한 전체 개수 검사를 보강했다.
4. `Widget/CodeEditor.cpp` 첫 Render의 중복 `SetChangeCallback`이 새 분석을 지우던 문제는 기존 transaction callback을 재사용해 등록 한 줄을 제거했다. EOL의 폭 0 진단도 기존 위치 변환을 거쳐 밑줄과 툴팁의 끝 범위를 일치시켰다. [진단 툴팁 실패](F:/AI/Temp/script-editor-rules-focus-ui-tests.log)와 최신 `CodeEditor` 결과를 구분한다.
5. 두 파일을 연 직후 첫 파일→요약으로 포커스를 옮기면 둘째 자식의 오래된 요청이 새 선택을 덮었다([강화 반례 실패](F:/AI/Temp/script-editor-focus-selected-release-tests.log)). `CodeEditorPanel::{FocusAt,SelectDefinition}`의 비공개 `CancelSiblingFocusRequests`는 기존 `TakeFocusRequest`로 형제 요청을 비우며, `EditorApplication::DrawPanels`는 실제 Hidden/도크 선택 상태를 반영하고 버퍼 요청은 visible/focused일 때 소비한다. 실제 선택 탭과 요약 포커스를 함께 검사하는 같은 강화 fixture가 [수정 뒤](F:/AI/Temp/script-editor-focus-request-fix-release-tests.log)와 최신 24개 회귀에서 통과했다.
6. [빈 초기값 Enter 실패](F:/AI/Temp/script-editor-empty-initializer-red-tests.log)를 확인한 뒤 `ScriptOutline`·`ScriptSummaryPanel`의 빈 문자열 무시 guard 두 개를 제거했다. `present=true`의 빈 식은 서버 오류로 보이고 소스를 유지하며, 제거 동작만 `present=false`/null을 보낸다. Enter·이탈 UI 반례와 실제 `JBroc` 검증을 최신 회귀에 포함했다.

API 필요성은 기존 호출 경로로 교차 확인했다. vendor의 기존 `textContextMenuCallback`은 `BeginPopup` 내부에서 호출돼 공통 팝업 수명을 위임할 수 없고, `ReplaceSectionText`는 범위마다 transaction을 만들어 다중 범위를 한 Undo로 묶지 못한다. `Glyph::squiggle`도 한 슬롯이어서 진단과 노란 사용 위치 배경을 동시에 보존하지 못한다. 이에 현재 `TextEditor::{SetTextContextMenuRenderer,ReplaceSectionsText,SetRangeHighlights}`가 필요한 제한은 확인했으나 다른 공통 API 중복을 추가로 확정하거나 전체 무결함을 주장하지 않는다. 최신 vendor h/cpp는 고정 원본에 패치를 재적용한 [기존 LF 해시](F:/AI/Temp/imgui-code-editor-spike/gutter-reapply-20261015/vendor-hashes.json)와 여전히 일치한다.

사용자는 규칙 미준수를 두 차례 지적했다. 기존 guard가 Panel의 원시 호출을 잡으면서 Widget 안의 재구현을 검출하지 못한 확인 누락은 이 작업 기록에만 남긴다. 별도 메모리·CLAUDE/AGENTS·새 결정 번호는 추가하지 않는다. 요청 기능·감사 수정과 양 구성의 해당 검증을 마쳤고 후속 코드는 `af60d387`로 로컬 커밋했으며 검증 기록을 함께 갱신했다. 감사 수정 뒤 새 화면 확인과 전체 공통 프레임워크 무결함은 미확인 한계로 유지한다. 메인 병합·원격 푸시·기존 에디터 교체 없이 메인 checkout과 사용자 호스트 74560을 보존한다.

## 2026-10-15 규칙 재검수 후 수정

언어 서버 최초 구현부터 C++ 생성·런타임·로그와 네이티브 코드 편집기 후속까지 52개 커밋의 현재 영향 파일 265개를 대조했다. 검색 후보는 `git blame`으로 이 작업의 변경과 구분하고 실제 라이브러리 경계·원본 vendor 코드·템플릿 예외를 별도로 확인했다. 이 검수는 미준수와 기능 결함을 발견했으며, 앞선 회귀 통과를 규칙 전체 준수로 판단하지 않는다. 초기 검수 인벤토리와 재현은 [검수 기록](F:/AI/Temp/jbro-rule-audit-20261015-report.txt)에 보존한다.

- 컴파일러·CLI·편집기·시험의 내부 수/참거짓을 엔진 값 타입으로 바꾸고, JSON·ImGui·문자 변환·진입점에 필요한 원시 타입은 해당 경계에 한정했다. C++ 생성 문자열은 별도로 대조해 내부 등록 표식·switch 분기 번호를 `JBro::Bool`·`JBro::Int64`로 바꾸고 생성하는 조건문도 블록으로 적는다. 열거형의 기본 정수형처럼 C++ 문법이 요구하는 원시 타입은 유지한다. 같은 줄의 제어문 본문 70곳도 블록으로 정리했다.
- 로그의 그룹 인덱스/저장소·패널 캐시와 프로세스 통신 버퍼/송신 대기열은 기존 `Table`·`Array`를 사용한다. 로그 키 조회는 `Hash<std::string_view>`를 사용해 기존 키 조회에 임시 소유 문자열을 만들지 않는다. 송신 큐는 잠금 안에서 묶음을 넘기고 잠금 밖에서 순서대로 전송한다.
- 패널의 행동 표 경로뿐 아니라 위젯의 단독 우클릭 메뉴도 기존 `Widget::MenuItem`의 아이콘·비활성 사유를 사용한다. 취소 단추의 아이콘과 코드 글꼴의 원본 크기도 기존 위젯/폰트에서 가져온다. 새 메뉴 프레임워크나 제품용 공개 UI API를 만들지 않았다.
- 성공 경로의 포커스·클립보드 추적 출력을 제거했다. 시험 실패 때만 나오는 원인 진단과 정식 assertion은 남긴다. 외부 편집기 로그의 자체 오류 상세는 기존 l10n 경로와 ko/en 표로 번역하며, 불완전한 실제 로컬 응답의 오류 상세도 번역되는 정식 시험을 추가했다.
- 언어 클라이언트는 기존 `EnableSafeFromThis`·`SafePtr` 경로를 사용해 재설정 뒤 문서/보조 기능이 해제된 클라이언트를 참조하지 않는다. 소유자보다 오래 살지 않는 디버거의 에디터와 보조 기능의 버퍼는 재지정/널 상태가 필요 없는 C++ 참조다.
- 파생 스크립트를 담은 `ref Base`의 저장 유실을 수정했다. 기존 코덱의 타입 검사 콜백에서 호환성을 확인하며 RTTI 검사는 저장/복원에만 있다. 동일 타입을 먼저 세고 호환되는 파생 타입을 뒤에 세어 기존 `objectIndex:ordinal` 형식과 기존 동일 타입 순번을 보존한다. 일반 ID·장면 remap 왕복, 섞여 붙은 두 파생 인스턴스, 기존 동일 타입 순번 및 무관한 타입의 거부를 정식 시험으로 확인한다. 잘못된 타입이 유효 오브젝트에서 조용히 빈 값으로 저장되는 것도 거부한다.
- 연산자 선언의 AST 텍스트 범위에 실제 `==`·`!=` 토큰까지 포함한다. 표시 이름은 기존 `FunctionName`을 사용한다. 띄어 쓴 연산자 선언도 이름 선택에 실제 소스 간격을 보존하는 시험을 추가했다.
- 시험 DLL의 기존 함수 51개는 엔진 값 타입을 쓴 공통 선언 헤더를 DLL/호출자가 함께 사용하고 호출 포인터는 그 선언의 `decltype`을 쓴다. MSVC C4190은 C 호출자 경고이므로 C++ 전용 시험 ABI를 정의하는 헤더/구현에만 제한적으로 억제한다. 생성기가 더 이상 사용하지 않는 `MakeStaticFieldEntry`·static 필드 매크로 3개는 제거하고 실제 `MakeStaticAccessorFieldEntry` 경로는 유지한다.

`WindowsProcessChannel`의 전용 OS 파이프 스레드는 유지한다. 자식 수명 동안 끝나지 않는 동기 읽기/쓰기를 공유 `TaskManager`에 등록하면 연결마다 워커를 계속 점유하므로 §6의 SHOULD에 대한 이번 구현 판단을 코드에도 적었다. 자식은 채널이 소유하며 종료 때 자식을 내리고 각 파이프 스레드를 join한다. TaskManager 사용 규칙 자체를 바꾸거나 새 일반 예외를 만들지 않는다.

### 재검증 상태

최종 소스의 구성별 검증은 2026-10-16에 끝났다. 최초 빌드/회귀와 최종 소스의 검증을 섞지 않는다.

| 구성 | 전체 솔루션 빌드 | 관련 31개 suite | 실제 LSP 프로세스 31개 |
|---|---|---|---|
| Debug | [종료 코드 0](F:/AI/Temp/jbro-rule-remediation-debug-build-bounded-final.log) | [최종 재실행 종료 코드 0](F:/AI/Temp/jbro-rule-remediation-debug-tests-final-repeat.log) | [31개 통과](F:/AI/Temp/jbro-remediation-lsp-debug-final.log) |
| Release | [종료 코드 0](F:/AI/Temp/jbro-rule-remediation-release-build-bounded-final.log) | [종료 코드 0](F:/AI/Temp/jbro-rule-remediation-release-tests-final.log) | [31개 통과](F:/AI/Temp/jbro-remediation-lsp-release-final.log) |

관련 suite에는 요약/탭/이름 선택/사용 위치/UTF-16/CRLF/BOM/버전/Undo/재연결, 생성 C++·실제 게임 DLL, 첫 콜백 중단/줄 이동/소스 불일치/재로드/설치 거부/연결 종료가 들어 있다. 로그 확장 Node 시험도 새 한국어 오류 상세 시험을 포함해 5개 통과했다. 전체 엔진의 모든 suite나 수동 GUI 조작을 새로 확인한 결과로 확대하지 않는다.
- 격리된 `SummaryDebug` 호스트와 기본 스크립트 연결용 `Build/x64/Debug` 라이브러리가 달라 실제 게임 DLL 연결이 처음 실패했다. 기본 에디터 실행 파일을 교체하지 않고 Framework2D와 의존 정적 라이브러리를 해당 기본 연결 경로로 빌드한 뒤 실제 게임 DLL을 다시 검증했다.
- JBroScript fixture의 의도한 `JBC3041` 한 건은 C++ 경고와 구분한다. 앞선 임시 fixture의 MSB8029도 시험이 TEMP 아래 프로젝트를 만들기 때문에 발생하며 제품 C++ 경고라고 판단하지 않는다.
- 최초 최종 Debug 묶음은 어댑터의 DLL 재로드 뒤 스크립트 호출 스택 확인에서 실패했다([실패 로그](F:/AI/Temp/jbro-rule-remediation-debug-tests-final.log)). 어댑터와 뒤의 세 시험을 분리한 실행은 4개 모두 통과했다([분리 실행](F:/AI/Temp/jbro-remediation-debug-adapter-isolated.log)). 어댑터/대상 프로그램/해당 시험 소스는 이번 수정에서 바꾸지 않았지만 실패 원인은 확인하지 못했으며 재실행 통과로 실패 기록을 지우지 않는다.
- 최초 Release 빌드의 `/m:3`과 공통 `/MP`가 겹쳐 컴파일러를 과도하게 띄웠다. 빌드 종료 뒤 해당 빌드가 남긴 MSBuild 노드 2개를 종료했고, 후속 빌드는 `/m:1 /p:CL_MPCount=1 /nr:false`로 제한한다. 시험이 만드는 DLL에도 `CL_MPCount=1`을 전달한다. 앱 하위의 다른 세션/도구 프로세스까지 이 빌드가 만든 것으로 단정하거나 일괄 종료하지 않았다.
- 파생 참조 저장 수정은 `caf036e9`, 연산자 이름 선택 수정은 `ce400d22`로 로컬 커밋했다. 나머지 규칙 정리와 이 최종 기록은 함께 커밋한다. 이전 검수에서 확인한 커밋 메시지 3건의 과거 미준수는 기록에 유지하고 히스토리를 재작성하지 않았다. 원격 푸시·메인 병합·기본 에디터 실행 파일 교체·설치·배포는 하지 않았다. 메인의 작업 상태와 기존 미추적 `source/JBroEngine/Build/`는 보존한다.

## 2026-10-16 메인 통합과 실행

사용자가 메인 병합·빌드·실행을 요청했다. 시작점은 메인 `7e855869`, 작업 브랜치 `7183e3a7`이며 메인의 미커밋 변경은 없었다. 사용자의 실행 우선 지시에 따라 전체 시험을 중단하고, 성공한 빌드 결과로 병합과 실행을 진행한다.

- 충돌은 `DebugSession.h` 한 곳이었다. 메인의 조건·로그 포인트 필드와 작업 브랜치의 설치 대기·거부·전송 시각 필드를 모두 보존했다. 메인의 `04216f91`이 제거한 불안정한 재로드 스택 소스 단언도 보존했다. 이 단언의 제거가 DbgEng 재로드 스택 심볼화의 한계 자체를 해결한 것은 아니다.
- 메인 경로의 전체 솔루션을 `MSBuild source/JBroEngine/JBroEngine.slnx /m:1 /p:CL_MPCount=1 /nr:false /p:Configuration=Release /p:Platform=x64`로 빌드했다. 중단 전 결과는 성공으로 세지 않는다. 렉으로 인한 사용자 중단 뒤에는 MSBuild와 자식 링커에 `BelowNormal` 우선순위와 논리 CPU 2개 제한을 적용했다. 최종 증분 빌드는 종료 코드 0, 오류 0개·경고 1개다. 동일 입력의 C++ 생성 진단에서 경고는 `Attributes.jscript:31`의 의도한 `JBC3041` 한 건으로 확인했다.
- 로그 확장 정식 Node 시험은 5개 통과·실패 0개·skip 0개다. 임시 검수 훅·Claude 호출·화면 비교 자료는 추가하지 않았다.
- 전체 시험은 `JBRO_TESTS`·`JBRO_BENCH`를 해제하고 `source/JBroEngine`을 작업 폴더로 실행했다. 최초 실행은 작업 폴더 때문에 문법 문서 검사가 빠져 중단했다. 올바른 작업 폴더에서 다시 시작한 실행은 71개 suite 완료 후 사용자의 실행 우선 지시로 중단했다. 중단 전 실패는 기록되지 않았지만 전체 통과로 판단하지 않는다. 별도 옵션이 필요한 실제 스피커 검사와 SPIR-V 프로브가 없는 Vulkan 세부 검사는 실행되지 않은 범위로 남긴다.
- 빌드 로그는 `source/JBroEngine/Build/Validation/script-editor-main-merge-20261016/release-build-low-priority.log`, 중단한 전체 시험 로그는 같은 폴더의 `full-release-tests-correct-cwd.log`다.

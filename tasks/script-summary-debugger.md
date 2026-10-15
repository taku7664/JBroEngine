# 스크립트 요약 패널과 기본 중단점 (2026-10-15, D-336)

## 승인 범위와 현재 상태

사용자가 승인한 범위는 현재 `.jscript`를 따라가는 오른쪽 요약 도크, 타입별 함수·멤버 목록, 멤버 속성·소스 초기값 편집과 기본 중단점이다.
확정 계약은 [ProjectRule §22](../docs/ProjectRule.md#22-스크립트-요약-패널과-기본-중단점-2026-10-15-d-336)에 둔다.
이 문서는 [내장 코드 편집기](./imgui-code-editor-integration.md)의 후속 구현이며, 기존 [엔진 중단점·어댑터](./script-debug-plan.md)를 재사용한다.

작업 브랜치는 `codex/imgui-code-editor-spike`다. 기존 편집기와 디버거의 병합 기준은 `35ca1c3d`이며 메인의 별도 작업 중 파일은 이번 변경 대상이 아니다.
**요청 기능의 구현과 아래 범위의 검증을 마쳤다. Debug/Release 전체 솔루션 빌드와 각 21개 관련 suite가 skip 없이 통과했고, 마지막 선택 위치 수정 뒤 양 구성의 전체 빌드와 UI 관련 5개 suite도 통과했다. 독립 의도 검토와 후속 구성 검토는 PASS다.**
언어 변경은 `34711c84`, 네이티브 편집기/디버거 변경은 `095e5bf9`에 기록했다. 메인 병합·원격 푸시는 하지 않았으며 메인의 별도 작업 중 파일을 보존했다.

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

## 최종 검증과 확인 범위

| 단계 | 현재 확인 결과와 증거 |
|---|---|
| Debug 빌드·회귀 | 전체 솔루션 빌드와 관련 21개 suite가 종료 코드 0·skip 없이 통과했다([빌드](F:/AI/Temp/script-summary-host-debug-build.log)·[시험](F:/AI/Temp/script-summary-host-debug-tests.log)). Debug 출력은 격리된 `Build/SummaryDebug`다. 기본 Debug 산출물을 덮어쓴 것으로 해석하지 않는다. |
| Release 빌드·회귀 | 전체 솔루션 빌드와 같은 21개 suite가 종료 코드 0·skip 없이 통과했다([빌드](F:/AI/Temp/script-summary-host-release-build.log)·[시험](F:/AI/Temp/script-summary-host-release-tests.log)). 출력은 `Build/x64/Release`다. |
| 마지막 UI 수정 뒤 확인 | 입력란 이탈 편집 뒤 선택한 멤버 위치를 유지하는 수정 후 전체 빌드·`ScriptSummaryPanel/CodeEditor/EditorUI/EditorLocalization/EditorShortcut` 5개를 양 구성에서 다시 실행해 모두 종료 코드 0이었다([Debug 빌드](F:/AI/Temp/script-summary-ui-final-debug-build.log)·[시험](F:/AI/Temp/script-summary-ui-final-debug-tests.log), [Release 빌드](F:/AI/Temp/script-summary-ui-final-release-build.log)·[시험](F:/AI/Temp/script-summary-ui-final-release-tests.log)). 이 UI 수정 뒤 21개 전체를 다시 실행했다는 뜻은 아니다. |
| 실제 JBroScript 전체 시작 | `SourceDebugger`는 실제 JBroScript 저장·빌드·DLL 반영·설정 ACK 뒤 첫 콜백 중단과 계속 실행, 중단 중 편집 잠금·소스 불일치 뒤 설치 해제·재빌드/DLL 교체·종료·외부 통로 충돌·어댑터 비정상 종료 정리를 검사한다. Debug 12.1초·Release 13.6초로 통과했다. `NativeDebug`·`DebugAdapter`도 양 구성 21개 회귀에 포함한다. |
| 중첩 타입 언어·실행 | `NestedScriptType`가 파서·이름/접근 분석·재귀 요약·C++ 이미터와 실제 게임 DLL 컴파일/실행을 양 구성에서 통과했다(Debug 5.1초·Release 4.5초). 실제 공개 `result`는 **30**을 검사한다. 다단계·동명 타입 구분·외부 비공개 접근 거절·바깥 인스턴스 암묵 캡처 거절·점 주변 공백/주석·생성자 호출을 포함한다. |
| 줄 앵커 반례 | 전체 텍스트 비교 기반 위치 추정이 동일 줄 앞삽입과 1500줄 내 먼 두 편집 사이 앵커를 잘못 옮겼다. 실제 위젯 트랜잭션 범위를 전달하도록 수정하고 최종 양 구성의 `CodeEditor`에서 반례·Undo/Redo를 확인했다. |
| 실제 화면 | 아래 표의 목록/상세 분리·초기값 편집/Undo·입력란 이탈 반영·한국어 오류·const 자동/직접 설정·미확정 중단점·파일 전환을 확인했다. 설치 완료·실행 중단 상태의 최종 화면은 미확인이다. |
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
설치 완료·현재 중단 줄의 색상/아이콘은 별도 실제 화면으로 캡처하지 않았다. 중단점 설치·엔진 멈춤·계속 실행을 실제 DLL로 검증한 것과 그 표시의 시각 검증은 구분한다. 조건부 중단점·변수 감시·호출 스택 UI·재시작 후 저장은 후속 기능이며 이번 미완료 작업이 아니다.
임시 하네스·화면 캡처·검증 로그는 D-244에 따라 제품 소스나 정식 회귀와 구분하며, 시험용 분기를 커밋·메인 병합하지 않는다.

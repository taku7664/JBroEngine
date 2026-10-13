# C++ 스크립트 편집기 — JBro 확장·개발 도구·레이아웃 작업 계획

> 작성·현황 확인: 2026-10-13. 독자: 빡대리와 이 작업을 이어받을 구현 담당자.
> 상태: **계획 초안**. C++ 대상과 코사원 담당 범위는 사용자 확인 사항이고, 아래 구현 방법·배치는 제안이다.
> Updates: [ide-plan.md](./ide-plan.md)의 `.jscript` 전용 범위와 §6·§8의 착수 순서. 확정 범위는 [todo.md](./todo.md)의 D-310.
> 기존 문법 강조·패치·조사 이력은 보존한다. 이 문서는 구현 완료나 설치본 제공을 뜻하지 않는다.
>
> **2026-10-14 후속 상태:** GitHub 로그인 연동 코드와 개발 확장 자동 로딩을 구현했다. 실제 계정 인증은 사용자 요청으로 미뤘다.
> §2~§8은 2026-10-13 조사·계획의 기록이며, 최신 진행 범위와 검증 결과는 §9를 따른다. A·E 전체 완료나 설치본 제공을 뜻하지 않는다.

## 1. 목표와 담당 범위

게임 개발자가 기존 `.jproject`를 열어 C++ 스크립트를 편집하고, 빌드 오류의 해당 줄로 이동하며,
같은 작업 공간에서 Git·터미널을 사용할 수 있는 JBro Script Editor를 만든다.

사용자가 확인한 코사원 담당 범위는 다음 셋이다.

1. **JBroEngine 전용 확장**: 프로젝트 인식, 엔진·스크립트 프로젝트 연결, 빌드·진단, 씬 에디터 실행과 파일 열기 연동.
2. **개발 도구 연동**: Git·GitHub·변경 비교·충돌 해결, 터미널·작업 실행, 제품에 함께 싣는 확장의 구성.
3. **Code-OSS 전체 레이아웃**: 메뉴·사이드바·편집 영역·하단 패널·상태 표시줄, 기본 배치와 저장한 배치의 복원.

C++ 언어 서비스·디버거는 이 확장이 연결할 대상이다. 구현 주체와 제공 방식은 빡대리가 담당자를 지정한 뒤
그 담당자와 합의한다. 이 계획에서 컴파일러·LSP·DAP를 새로 구현하거나 `jbroc` 개발을 재개한다고 정하지 않는다.

## 2. 현재 상태 — 소스로 확인한 것과 실행으로 확인하지 않은 것

### 2.1 JBro 확장과 엔진 연결

| 항목 | 2026-10-13 확인 결과 | 남은 일 |
|---|---|---|
| JBro 확장 | `extensions/jbro-languages` 하나. manifest에는 언어·문법 기여만 있고 실행 진입점·JBro 명령·뷰·태스크가 없다 | C++ 프로젝트를 다루는 실행 확장 |
| 파일 형식 | `.jscript` 문법 강조, `.jproject`·`.jcanvas`·`.jprefab`의 YAML 언어 연결 | 프로젝트를 여는 동작·버전·차원·스크립트 경로 확인 |
| 번역 | `package.nls.json`·`package.nls.ko.json`이 있다. 기존 검사 실행 결과 manifest 키 2개가 모두 채워져 있다 | 새 명령·설정·실행 중 문구와 번역 검사 |
| C++ 기본 강조 | upstream의 `extensions/cpp`가 있다. 실행 진입점 없는 선언형 확장이다 | 언어 서비스 제공자 연결은 별도이며 기본 강조와 구분한다 |
| 스크립트 프로젝트 | 엔진 `ScriptProject`가 `.sln`·`.vcxproj`·`.h/.cpp`를 만들고 `JBroEngine.props`를 맞춘다 | 확장이 기존 프로젝트를 발견하고 사용하기 |
| 스크립트 빌드 | 엔진 `ScriptBuild`와 `EditorApplication::BuildScripts`가 MSBuild·UTF-8 파일 로그를 쓴다 | Code-OSS의 작업·문제 패널에 연결 |
| 핫 리로드 | 엔진 `PollScriptModuleFile`·`ReloadScripts`가 DLL 변경을 확인하고 재생 중 교체를 미룬다 | Code-OSS에서 빌드한 DLL로 실제 연결 확인 |
| 파일·줄 열기 | 엔진의 편집기 선택은 기본 앱·Visual Studio·VS Code뿐이다 | JBro Script Editor 선택·경로 발견·이미 열린 창에서 줄 이동 |
| 언어 설정 | 엔진에 `EditorPreferences.yaml`이 있으며 `Version`·`ScriptEditor`·`Shortcuts`를 읽고 쓴다. 에디터 언어는 프로젝트의 `editorLocale`로 세션 저장된다 | 두 편집기의 언어 설정 공유 계약 |

위 엔진 항목은 구현 소스의 존재와 경로를 확인한 것이다. 이번 조사에서는 빌드·핫 리로드·줄 이동을 실행하지 않았다.
프로젝트 생성·소스 템플릿의 원문은 엔진에 있다. 확장에서 이를 복제하는 생성기를 먼저 만들지 않는다.

### 2.2 개발 도구

| 항목 | 확인 결과 | 남은 일 |
|---|---|---|
| Git | upstream `git`·`git-base`와 컴파일된 진입점이 있다. 시스템 Git은 `2.54.0.windows.1`이다 | JBro 개발 실행에서 저장소 발견·변경 비교·선택 스테이징·커밋·브랜치 기능 확인 |
| GitHub 로그인 | **현재 JBro 편집기에서 로그인 기능이 없다(2026-10-13 빡대리 확인).** upstream `github`·`github-authentication`의 소스·컴파일 출력은 재사용 후보다 | 로그인 진입점·인증 제공자 활성화·계정 상태·인증 복귀를 제품에 연결하고 원격 작업까지 확인 |
| 충돌 해결 | upstream `merge-conflict`와 컴파일된 진입점이 있다 | 변경 비교·병합 편집기의 실제 동작 확인 |
| 터미널·작업 | upstream의 워크벤치 기능이 있으며 JBro 태스크 제공자는 없다 | JBro 빌드 작업·취소·결과 표시와 연결 |
| 배포에 포함할 확장 | `product.json`은 upstream 구성이고 JBro 전용 포함 목록·검사가 없다 | 필수 확장·한국어 팩·C++ 제공자를 명시하고 포함 여부 검사 |

**로그인은 단순 검증 대기가 아니라 제품에서 제공해야 할 남은 기능이다.** 재확인한 upstream `extension.ts`에는 인증 제공자 등록 코드가 있고,
JBro 확장에는 로그인 명령·계정 UI 연결이 없다. 화면에서 로그인이 없는 구체적인 원인은 이번 소스 확인만으로 확정하지 못했다.
GitHub 인증 소스는 `env.uriScheme`과 외부 복귀 URI를 사용하므로, 개발 실행과 제품 이름·URI 스킴을 바꾼 설치본에서 각각 연결해야 한다.
로그인·로그아웃과 계정 상태 표시, 취소·실패 안내, 브라우저 인증 뒤 편집기 복귀를 완료 조건에 포함한다.
GitHub 확장의 로그인 세션과 Git 실행 파일이 원격 작업에 쓰는 인증은 따로 확인한다.

### 2.3 레이아웃·코어 패치·실행 환경

| 항목 | 확인 결과 | 남은 일 |
|---|---|---|
| 기본 배치 | upstream 설정 기본값은 사이드바 왼쪽·패널 아래다. JBro 배치 기여는 없다 | 프로젝트 작업에 맞는 기본 배치와 메뉴 결정 |
| 패치 0001~0003 | 첫 실행 로케일 `ko`, AI 기능 기본 끄기, VSIX 설치 차단. 현재 upstream에 세 패치의 역적용 검사 모두 성공 | 실제 개발 실행 회귀·릴리스의 동작 확인 |
| 패치 0004 | `menu-structure` 파일이 없다 | 실제 화면과 제안안을 비교하고 메뉴 구조 확정 |
| 제품 설정 | `Code - OSS`·CLI `code-oss`·설정 폴더 `.vscode-oss`다. `extensionsGallery`는 없다 | JBro 브랜딩·CLI·프로필·URI·설치본 |
| 개발 환경 | `F:\Project\JBroScriptEditor\upstream`에 태그 `1.137.0`, `node_modules`, 컴파일 출력, Electron이 있다. 휴대용 Node는 `24.18.0`이다 | 환경과 확장 구성을 재현하는 진입점 |
| 환경 변수 | `JBRO_EDITOR_WORK`가 현재 프로세스·사용자·시스템 모두에 없다 | 실행할 프로세스에 작업 폴더를 명시하는 사용법·진입점 |
| JBro 확장 포함 | upstream `extensions`에 `jbro-languages`가 없다. launch 스크립트가 자동으로 추가하지도 않는다 | 개발 호스트 로딩과 제품 내장 과정 |
| 편집기 측 시험 의존성 | `source/JBroScriptEditor/node_modules`가 없다 | 문법 시험을 돌릴 재현 가능한 의존성 준비 |

현재 upstream의 수정 파일 다섯 개는 세 패치가 건드리는 파일이다. 조사 중 이 체크아웃을 초기화하거나 패치를 다시 적용하지 않았다.
개발 출력이 있다는 사실만으로 현재 소스와 출력이 일치하거나 실행이 성공한다고 판단하지 않는다.
이번 조사에서는 편집기 창을 띄우지 않았으므로 실제 저장된 배치·잘림·포커스·언어 표시를 확인하지 못했다.

## 3. 기존 계획에서 바로잡을 전제

- **`.jscript` 전용이라는 범위**: 이번 사용자가 C++ 스크립트용으로 변경했다. C++ 작업은 `jbroc` 타입체커를 기다리지 않는다.
- **DLL 변경 감지가 없다는 설명**: 지금 엔진에는 변경 확인·핫 리로드 코드가 있다. 별도 알림 IPC가 필수라는 전제로 설계하지 않는다.
- **사용자 설정 파일이 없다는 설명**: 파일은 생겼으나 언어 공유 형식은 없다. 파일의 존재와 공유 계약의 완료를 구분한다.
- **“패치 없는 upstream”, “패치 적용 완료”가 함께 쓰인 README**: 개발 준비를 정리할 때 현재 패치 상태·개발 실행·설치본을 따로 설명한다.
- **Git을 새로 만드는 일**: 먼저 upstream Git·GitHub·충돌 해결 기능을 검증하고 재사용한다. 추가 UI는 발견한 공백에만 만든다.

## 4. 설계 기준과 연결 계약

아래 MUST는 사용자 확정 사항 또는 현재 ProjectRule·Decisions의 요구를 적용한 것이다.
SHOULD는 이 계획의 제안이며, 구현 방법을 달리 고르면 이유와 검증을 이 문서에 남긴다.

- **MUST**: 사용자 프로젝트는 기존 `.jproject`다. `EngineVersion`·`Framework`·설정된 스크립트 소스·산출물 경로를 사용하며 두 번째 프로젝트 형식을 만들지 않는다.
- **MUST**: 사용자 `.vcxproj`·소스·설정 파일을 연결 동작에서 재생성하지 않는다. 엔진의 스크립트 프로젝트 계약(D-266)을 보존한다.
- **MUST**: JBro 기능은 내장 확장으로 제공한다. 코어 패치는 확장 기여로 바꿀 수 없는 제품 모양에 한정한다(D-87·D-102).
- **MUST**: 확장 마켓플레이스를 연결하지 않는다. 기본 한국어·영어 폴백과 화면 문구의 로컬라이징을 유지한다.
- **SHOULD**: 실행 기능은 새 `jbro-engine` 확장으로 모으고, 기존 선언형 `jbro-languages`는 보존한다. 확장 분할은 공개 명령과 책임을 늘릴 때만 한다.
- **SHOULD**: 작업 공간은 `.jproject`가 있는 프로젝트 뿌리를 연다. 스크립트·에셋·Git이 같은 프로젝트를 보되 `Scripts`로 빠르게 이동할 수 있게 한다.
- **SHOULD**: 프로젝트 열기·씬 에디터 실행·빌드·취소·진단 이동은 명령 한 벌로 등록하고 메뉴·키·상태 표시줄이 같은 명령을 호출한다.
- **SHOULD**: 빌드는 태스크 제공자로 기존 `.vcxproj`를 MSBuild에 넘긴다. 사용자 `tasks.json`을 덮어쓰지 않는다.
  공식 [Task Provider 안내](https://code.visualstudio.com/api/extension-guides/task-provider)는 자동 발견과 `ProcessExecution`을 제공한다.
- **SHOULD**: MSBuild 진단은 엔진처럼 UTF-8 파일 로그를 읽어 문제 패널에 낸다. 터미널 코드페이지·영문 출력만으로 한글 경로의 성공을 판단하지 않는다.
- **SHOULD**: 첫 빌드 구성은 현재 씬 에디터와 맞는 Debug다. Release는 엔진 라이브러리·DLL 산출물 경로 정책을 합의한 뒤 제공한다.
- **MUST**: 파일·CLI·DLL 변경 감지를 통한 기존 경계를 유지한다(D-60·D-97·D-300). 기존 가이드 제어 포트는 빌드 API가 아니다.
- **SHOULD**: 기본 배치는 새 프로필의 기본값으로 제공하고 저장된 사용자 배치를 반복 실행 때 덮어쓰지 않는다.
  명령·뷰·설정 기본값은 공식 [Contribution Points](https://code.visualstudio.com/api/references/contribution-points)와 고정 upstream API를 함께 확인한다.
- **SHOULD**: 프로젝트 파일을 읽는 동작과 빌드·프로세스 실행을 구분하고, 후자는 upstream Workspace Trust 판정을 따른다.
  메뉴 숨김만으로 실행을 막았다고 판단하지 않는다([공식 안내](https://code.visualstudio.com/api/extension-guides/workspace-trust)).

## 5. 구현 단계 — 2026-10-13 계획 시점에는 미착수

| 순서 | 작업 | 완료 조건·검증 |
|---|---|---|
| A. 개발 기준 세우기 | 작업 폴더 명시, 개발 호스트에 JBro 확장 로딩, 포함 목록, 기존 README의 상태 설명 정리 | 정해진 진입점으로 개발 실행. 실제 로드된 확장·빌드 버전 확인. 개인 VS Code 프로필과 기존 F: 작업 상태 보존. 화면 기준 자료 확보 |
| B. JBro 프로젝트 연결 | 기존 `.jproject` 발견·선택, 엔진 버전·차원·경로 확인, 스크립트 폴더 이동, 프로젝트 상태, 씬 에디터 실행 | 프로젝트 여러 개·없음·형식 오류·한글/공백 경로 확인. 읽기·연결만으로 프로젝트 파일이 바뀌지 않음. 실행 실패 사유를 표시 |
| C. 빌드와 진단 | MSBuild 발견, Debug 빌드 태스크, UTF-8 로그·문제 패널, 중복 빌드 제어·취소, 성공/실패 표시 | 정상·컴파일 오류·링커 오류·MSBuild 없음·로그 읽기 실패 확인. 오류 클릭으로 정확한 파일·줄 이동. 취소 후 자식 빌드 프로세스가 남지 않음 |
| D. 씬 에디터 왕복 | 씬 에디터의 편집기 선택에 JBro 제품 연결, CLI 줄 이동·창 재사용, DLL 산출물 확인 | 두 편집기 사이 파일·줄 이동. Code-OSS에서 빌드한 DLL을 씬 에디터가 감지하고 멈춘 상태에서 교체. 재생 중 정책은 D-300 유지 |
| E. Git·로그인·도구 연동 | 로그인 진입점·인증 제공자·계정 상태·복귀 경로 연결. upstream Git·충돌 해결·터미널·태스크 재사용 | 로그인·로그아웃·재실행 후 세션·인증 취소/실패·브라우저 복귀 확인. 격리한 검증 저장소에서 저장소 발견·diff·선택 스테이징·커밋·브랜치·충돌 해결. 사용자 저장소의 커밋·원격을 시험 대상으로 변경하지 않음. Git 원격 인증은 로그인 세션과 별도로 확인 |
| F. 레이아웃 정리 | 아래 배치안 검토, 기능이 연결된 메뉴·상태, 패치 0004, 초기 화면·배치 복원 | 현재 화면과 변경안을 나란히 비교. 좁은 창·넓은 창에서 메뉴·글자·패널 확인. 닫기·이동·재실행 후 사용자 배치 보존. 없는 기능의 동작 버튼이 없음 |
| G. 제품 묶음 | 제품 설정·내장 확장·언어 팩·CLI·URI·프로필·라이선스 목록·설치본 | 새 프로필의 첫 실행 한국어와 영어 전환. 필수 확장 실제 로딩·VSIX 정책·GitHub 복귀·파일 줄 이동. 기존 VS Code와 동시 설치·실행 확인 |

B·C·D가 최소 JBro 기능 묶음이다. E의 로컬 Git·로그인 연결은 A 이후 독립적으로 할 수 있고,
F의 화면 검토는 A에서 기준을 잡고 기능이 생긴 뒤 마무리한다. G는 개발 확장만 제공하는 단계와 구분한다.
언어 서비스가 아직 연결되지 않았으면 B~F를 마쳐도 **자동완성까지 되는 C++ 편집기**라고 보고하지 않는다.

### 5.1 레이아웃 제안 — 빡대리 검토 전

| 영역 | 제안 내용 | 적용 방법 후보 |
|---|---|---|
| 상단 메뉴 | 파일·편집·선택·보기·프로젝트·터미널·도움말. 프로젝트에서 씬 에디터 실행·스크립트 빌드 접근 | 기존 메뉴 기여 우선, 불가능한 구조만 0004 |
| 왼쪽 | 탐색기·검색·소스 제어. 탐색기 안에서 스크립트·프로젝트 정보에 접근 | 기존 뷰와 작은 JBro 뷰, 새 전용 사이드바는 필요할 때만 |
| 가운데 | C++ 코드 탭·변경 비교·병합 편집기 | upstream 편집기 사용 |
| 아래 | 문제·출력·터미널. 빌드는 이곳으로 결과를 보내고 화면을 막지 않음 | 기존 패널·태스크 사용 |
| 상태 표시줄 | 현재 프로젝트·차원·빌드 상태. Git 브랜치·문서 언어·인코딩은 upstream 표시 사용 | 중복 표시 없이 명령에 연결 |
| 초기 화면 | 프로젝트가 없으면 열기 안내, 있으면 마지막 편집 상태 | 설정 기본값·뷰 안내 우선 |

색·간격·패널 너비는 아직 정하지 않았다. A에서 실제 기본 화면을 확인한 뒤 F에서 정한다.
C++ 디버그 메뉴는 제공자와 실행 경로가 연결됐을 때 노출한다. 현재 JS 디버거의 존재를 C++ 디버깅 완료로 세지 않는다.

## 6. 담당 간 합의와 사용자 결정이 필요한 것

| 결정할 사람 | 무엇을 정하는가 | 아직 열려 있는 이유·영향 |
|---|---|---|
| 빡대리가 지정한 C++ 담당자와 코사원 | 언어 서비스·디버거의 확장 ID, 명령, SDK/컴파일 인자 전달 방식, 로딩 시점 | 현재 JBro 확장에 제공자가 없다. 이 정보가 있어야 내장 목록·메뉴·상태가 실제 기능과 연결된다 |
| 빡대리와 코사원 | 메뉴 0004의 최종 항목과 기본 JBro 뷰의 자리 | 기존 결정은 실제 화면을 보고 정하도록 남겼다. §5.1은 검토 가능한 제안이다 |
| 빡대리와 씬 에디터 담당자·코사원 | 화면 언어 공유의 저장 범위·우선순위 | 지금 엔진 언어는 프로젝트 세션에 남고 Code-OSS 언어는 사용자 프로필에 남는다. 어느 쪽을 우선할지 정하지 않으면 두 편집기가 서로 덮어쓴다 |
| 빡대리와 빌드/SDK 담당자·코사원 | 설치본 SDK 위치와 Release DLL 경로 | 현 엔진 props는 개발 소스·동일 구성의 라이브러리를 사용한다. 개발 경로를 설치본 지원으로 보고할 수 없다 |

위 결정을 모두 기다린 뒤 A를 시작할 필요는 없다. 각 결정을 사용하는 단계 전에 합의한다.
GitHub 로그인은 현재 없는 기능으로 계획한다. 공급자 활성화·화면 진입점·실제 인증 경로를 먼저 확인하고,
연결에 필요한 제품 설정 변경은 그 근거로 정한다. upstream 코드가 있다는 사실을 제품 로그인 제공 완료로 세지 않는다.

## 7. 완료 판정과 이번 확인 범위

- **확장 연결 완료**: 프로젝트·빌드·진단·씬 에디터 왕복을 실제 프로젝트에서 확인하고 제공자가 없는 기능은 별도로 표시한다.
- **도구 연결 완료**: 로컬 Git·충돌 해결·터미널·작업과 GitHub 로그인·원격 연동의 검증 결과를 구분해 남긴다.
- **레이아웃 완료**: 실제 화면 비교·조작·배치 복원을 확인한다. 소스의 기본값이나 컴파일 성공만으로 완료라 하지 않는다.
- **제품 제공 완료**: 설치본·내장 목록·첫 실행 언어·CLI·프로필 공존까지 확인한다. 개발 실행 성공과 구분한다.

이번 조사에서 수행한 검사는 세 패치의 `git apply --reverse --check`(종료 0), 기존 번역 키 검사(키 2개, 통과),
소스·manifest·컴파일 출력·환경 변수·실행 도구 버전 확인이다.
문법 전체 시험, 엔진/Code-OSS 빌드, UI 실행·조작, Git UI·인증·원격 동작, 실제 핫 리로드, 설치본은 확인하지 않았다.
코드 구현·로컬 설치·원격 푸시는 이번 계획 작업에 포함하지 않는다.

## 8. 확인한 원문과 구현 자리

- [확장 manifest](../source/JBroScriptEditor/extensions/jbro-languages/package.json)·[번역 검사](../source/JBroScriptEditor/scripts/check-nls.mjs)
- [개발 환경](../source/JBroScriptEditor/scripts/upstream-env.cmd)·[실행](../source/JBroScriptEditor/scripts/upstream-launch.cmd)·[패치 적용](../source/JBroScriptEditor/scripts/apply-patches.mjs)
- [편집기 README](../source/JBroScriptEditor/README.md)·[옛 Code-OSS 계획](./ide-plan.md)·[C++ 스크립트 계획](./cpp-script-plan.md)
- [프로젝트 파일 계약](../source/JBroEngine/Modules/JBroHost/Include/JBro/Host/ProjectFile.h)·[스크립트 프로젝트](../source/JBroEngine/Modules/JBroEditor/Source/ScriptProject.cpp)
- [빌드·줄 이동](../source/JBroEngine/Modules/JBroEditor/Source/ScriptBuild.cpp)·[빌드/핫 리로드/설정](../source/JBroEngine/Modules/JBroEditor/Source/EditorApplication.cpp)
- [편집기 선택 UI](../source/JBroEngine/Modules/JBroEditor/Source/Panel/EditorSettingsPanel.cpp)·[스크립트 props](../source/JBroEngine/JBro.GameScript.props)·[targets](../source/JBroEngine/JBro.GameScript.targets)
- upstream 확인 자리: `F:\Project\JBroScriptEditor\upstream\product.json`, `extensions/{git,git-base,github,github-authentication,merge-conflict,cpp,yaml}`, `src/vs/workbench/browser/{layout.ts,workbench.contribution.ts}`

upstream의 경로와 버전은 이번 기계의 확인값이다. 다음 작업은 `JBRO_EDITOR_WORK`가 가리키는 실제 체크아웃에서 다시 확인한다.

## 9. 2026-10-14 후속 진행 — GitHub 로그인 연동 코드

사용자는 실제 Git 로그인 연결을 요청한 뒤, 이번에는 **연동 코드까지 마무리하고 실제 계정 인증은 나중에 확인**하도록 범위를 정했다.
이번 결과는 내장 GitHub 인증 제공자를 호출할 수 있는 JBro 진입점과 개발 호스트 연결이다. Git 전송에 쓰는 자격 증명이나 계정 인증 성공까지 확인한 결과는 아니다.

### 9.1 구현 범위와 남은 일

| 단계·항목 | 현재 상태 | 남은 일 |
|---|---|---|
| A: 개발 확장 로딩 | `upstream-launch.cmd`가 `jbro-engine`·`jbro-languages`를 개발 확장으로 자동 로딩한다. `JBRO_EDITOR_USER_DATA`로 별도 프로필을 지정한다 | 제품의 내장 확장 포함 목록·화면 기준 자료·기존 프로필과의 공존 확인. A 전체는 미완료 |
| E: 로그인 진입점 | `jbro.git.signIn`·`jbro.git.manageAccount`, 상태 표시줄, 소스 제어 제목줄·명령 팔레트 항목을 구현했다 | 실제 GUI에서 글자·아이콘·클릭·상태 전환 확인 |
| E: 인증 제공자 연결 | 내장 `github` 제공자의 `authentication.getSession`을 사용한다. 내장 GitHub 확장과 같은 범위(`repo`·`workflow`·`user:email`·`read:user`)를 요청하고 기기 코드 방식을 기본값으로 기여한다 | 실제 로그인·취소·인증 실패·로그아웃·재실행 후 세션 확인은 사용자 요청으로 유보 |
| E: 계정 상태·관리 | 시작 시 조용한 세션 조회, 세션 변경 시 상태 갱신, 내장 `workbench.action.manageAccounts` 연결. JBro 코드는 토큰을 저장하지 않고 인증 제공자가 자격 증명을 관리한다 | 실제 계정 상태로 검증. 모의 API 검사 통과와 구분한다 |
| E: Git·도구 | upstream Git·충돌 해결·터미널 기능을 재사용하는 계획은 유지한다 | 저장소 발견·diff·선택 스테이징·커밋·브랜치·충돌 해결·터미널·Git 원격 인증 검증. E 전체는 미완료 |
| 제품·레이아웃 | 세 코어 패치가 적용된 Code-OSS 개발 빌드다. 검토용 기본 개발 창을 실행했다 | 화면 검증·전체 레이아웃·메뉴 0004·JBro 브랜딩·설치본. B·C·D·F·G 완료로 세지 않는다 |

### 9.2 검증 결과와 확인하지 않은 것

- Node 계정 연동 검사 **9개 통과**: 조용한 시작·로그인 호출·중복 요청·취소·실패 안내·상태 갱신·런처의 프로필 인자 거절 등.
- 번역 검사 통과: `jbro-engine` manifest 키 **4개**, `jbro-languages` 키 **2개**, 실행 문구의 `l10n` 번들.
- JavaScript 구문 검사 통과.
- 실제 Code-OSS 확장 호스트 검사 통과, 프로세스 **종료 코드 0**: JBro 명령 등록·내장 GitHub 제공자 활성화·기기 코드 기본값·새 프로필의 조용한 비로그인 상태.
- 실제 계정 로그인·로그아웃·세션 복원은 사용자 요청으로 유보했다. Git 원격 인증도 수행하지 않았다.
- GUI 캡처는 `FrameArrived`·창 캡처 시간 초과로 실패했다. 따라서 현재 창의 모양·배치·글자·클릭 동작은 시각적으로 확인하지 못했다.
- 이번 검사를 문법 전체 시험, 전체 엔진/Code-OSS 재빌드, JBro 프로젝트·빌드·핫 리로드 연결 검증이나 설치본 검증으로 확대해 보고하지 않는다.

### 9.3 실행 중 발견한 문제와 조치

| 발견한 문제 | 원인·영향 | 조치·후속 확인 |
|---|---|---|
| 사용자 데이터 인자 중복 | 런처의 `--user-data-dir`에 같은 인자를 다시 넘기자 단일 경로가 Array가 되어 main 프로세스에서 `ERR_INVALID_ARG_TYPE` 발생 | 런처가 명령줄의 `--user-data-dir`·`--extensions-dir`를 거절한다. 검증 프로필은 `JBRO_EDITOR_USER_DATA`로 지정한다 |
| Windows Node의 배치 실행 인자 전달 | Node 회귀 검사 드라이버에서 Windows 인자 전달 방식 지정이 필요했다 | `spawnSync`에 `windowsVerbatimArguments: true`를 지정해 인자 거절·실패 종료 코드를 검사한다 |
| 중첩 배치 FOR의 실패 종료 | 중첩 FOR 안의 종료 처리로 인자 거절이 종료 코드 0을 반환할 수 있었다 | 최상위 오류 레이블에서 종료 코드 1을 반환하도록 고쳤고, 회귀 검사가 거절 메시지와 종료 코드 1을 확인한다 |

위 문제는 이번 실행에서 확인한 기록이다. 새 실행 드라이버에서도 프로필 경로를 같은 CLI 인자로 반복하지 않고,
배치 실행 결과는 메시지만이 아니라 프로세스 종료 코드로 함께 확인한다.

### 9.4 구현·사용법 연결

- [JBroEngine 확장 manifest](../source/JBroScriptEditor/extensions/jbro-engine/package.json)·[진입점](../source/JBroScriptEditor/extensions/jbro-engine/extension.cjs)·[계정 연동](../source/JBroScriptEditor/extensions/jbro-engine/accounts.cjs)
- [계정 검사](../source/JBroScriptEditor/extensions/jbro-engine/test/accounts.test.cjs)·[실제 확장 호스트 검사](../source/JBroScriptEditor/extensions/jbro-engine/test/extension-host.cjs)
- [개발 실행 스크립트](../source/JBroScriptEditor/scripts/upstream-launch.cmd)·[README의 로그인·격리 프로필 사용법](../source/JBroScriptEditor/README.md)

# JBro Script Editor

JBroEngine 의 C++ 스크립트 편집기다. Code-OSS 를 얇게 포크하고, JBro 기능은 전부 내장 확장으로 만든다.
엔진 리포의 `source/JBroScriptEditor` 에 있다. 현재 작업 계획은 [script-editor-integration-plan.md](../../tasks/script-editor-integration-plan.md),
결정은 `tasks/todo.md` D-87·D-261·D-310·D-311·D-312·D-313·D-314·D-315 에 있다. 기존 `.jscript` 문법 확장은 보존한다.
원래는 따로 있던 로컬 리포 `F:\Project\JBroScriptEditor` 였고, 그 커밋은 `git subtree` 로 히스토리째 들어왔다.
옛 리포의 `.git` 과 추적 파일은 지웠고, 그 폴더에는 upstream 체크아웃과 `.toolchain` 만 남아 아래의 작업 폴더로 쓴다.

**현재는 코어 패치 0001~0005를 적용한 Code-OSS 개발 빌드이며, JBro 브랜딩·설치본은 아직 없다.**
아래 실행 스크립트가 `jbro-engine`·`jbro-languages`·한국어 팩 `jbro-korean`을 개발 확장으로 자동 로딩한다.
GitHub 로그인 연결 코드는 구현했으며, 실제 계정 인증은 사용자 요청으로 미뤘다. Git 원격 인증도 아직 확인하지 않았다(2026-10-14).
상단 다섯 한국어 드롭다운 메뉴, 워크스페이스 트리, JBroEngine 테마를 구현하고 실제 개발 창에서 확인했다(D-313·D-314).
앞선 D-311의 직접 버튼 구현·검증 이력은 작업 계획 §10에 보존한다.

## 기본 화면 (2026-10-14)

빡대리는 기존 Activity Bar와 File/Edit/... 메뉴를 치우고 **프로젝트·Git·빌드·실행·오류/로그**를 기본으로 표시하도록 정했다(D-311).
상단 다섯 메뉴에서 하위 명령을 선택하고 기존 코드 영역·하단 패널과 왼쪽 워크스페이스를 사용한다.
사이드바·패널은 기존 드래그·크기 조절 기능을 사용한다. 빌드는 신뢰한 폴더의 빌드 태스크를 실행하거나 선택하고, 실행 메뉴는 태스크 선택 창을 연다.
엔진 전용 태스크 자동 생성은 이번 변경에 포함하지 않는다. 오류/로그 메뉴에는 오류 목록·출력·터미널이 있다.

표시 설정은 다음과 같다.

- `jbro.layout.enabled=false`: JBro 메뉴바 대신 기존 제목줄 도구 표시.
- `jbro.layout.showProject`·`jbro.layout.showGit`·`jbro.layout.showBuild`·`jbro.layout.showRun`·`jbro.layout.showDiagnostics`: 각 메뉴 표시·숨김.

메뉴바와 메뉴 표시 전환은 즉시 반영한다. 기존 Activity Bar는 `workbench.activityBar.location=default`,
기본 메뉴는 `window.menuBarVisibility=classic`으로 별도 복원한다.
코어 패치 `0004-jbro-layout`는 공통 메뉴바와 기본 메뉴·보조 사이드바 숨김을 제공한다.
`jbro-engine`은 메뉴·번역·명령·워크스페이스·테마와 Activity Bar 숨김 기본값을 제공한다. 사용자에게 저장된 표시 설정을 덮어쓰지 않는다.
격리 프로필에서 다섯 한국어 드롭다운·메뉴 동작·키보드·show* 설정·700px 창을 확인했다.
새 프로필에서 오른쪽 Chat 보조 패널이 기본으로 숨겨지는 것도 확인했다.
실제 엔진 빌드·실행 태스크, 미신뢰 폴더에서의 동작, 패널 드래그 실기는 아직 확인하지 않았다.
현재 구현·검증 결과는 [작업 계획 §12·§13](../../tasks/script-editor-integration-plan.md#12-2026-10-14-후속-계획--메뉴-우선공통-ui-구조)에 있다.

## 하위 메뉴·공통 UI

처음 범위는 메뉴였고, 후속 요청으로 왼쪽 워크스페이스와 JBroEngine 테마를 함께 구현한다(D-314). 공통 구조는 Code-OSS 본체의 `ScriptEditorNativeUI`에 두고,
제목줄은 공통 메뉴바 하나를 소유한다. 제품별 메뉴와 한국어·영어 문구는 내장 `jbro-engine` 확장에서 기여한다.
공통 UI는 번역된 문구를 받아 그리며 JBro 제품 문구를 자체적으로 갖지 않는다.

| 메뉴 | 제공 항목 |
|---|---|
| 프로젝트 | 프로젝트 폴더 열기·파일 열기·워크스페이스 |
| Git | 소스 제어·GitHub 로그인 또는 계정 관리 |
| 빌드 | 빌드 태스크 실행·터미널 |
| 실행 | 실행할 태스크 선택 |
| 오류/로그 | 오류 목록·출력·터미널 |

메뉴별 `show*` 설정과 기존 명령·신뢰 제한은 유지한다. 실제 계정 인증은 계속 유보한다.
자유 도킹·드래그드롭 위젯·로딩 UI는 공통 구조를 사용할 후속 대상으로 남기며 이번 구현에 포함하지 않는다.
이 구조는 엔진의 ImGui `JBroWidget`과 별개다. 공통 메뉴바의 본체 타입 검사와 실제 메뉴·키보드·표시 설정 검사가 통과했다.
계획과 교정 근거는 [작업 계획 §12](../../tasks/script-editor-integration-plan.md#12-2026-10-14-후속-계획--메뉴-우선공통-ui-구조)에 있다.

### 워크스페이스·테마

확장 활성화 뒤 왼쪽 사이드바에는 `워크스페이스` 컨테이너만 표시하고 Outline·Timeline은 넣지 않는다.
Code-OSS `TreeView` API를 사용해 열린 작업 폴더의 파일을 읽기 전용으로 탐색하고 파일을 연다. Git 등은 메뉴 선택 시 기존 패널을 연다.
루트·하위 폴더에는 엔진과 같은 MDI 7.4.47의 `folder`·`folder-open` SVG를 사용하며 실제 트리에서 표시를 확인했다.

엔진의 [EditorTheme.h](../JBroEngine/Modules/JBroEditor/Include/JBro/Editor/EditorTheme.h)에 이미 있는 의미별 팔레트를 UI 색의 출처로 사용한다.
엔진 C++ 파일을 수정하지 않고 생성기가 상수의 숫자를 읽어 Code-OSS 색상 테마를 만든다. 메뉴·트리·코드 영역은 native ThemeService와 CSS 변수를 공유한다.
코드 문법 강조에만 필요한 색은 별도 이름 붙은 토큰 파일에 두며 엔진의 오류·주의·상호작용 색과 구분한다.
별도 실제 작업 폴더에서 트리·파일 열기·C++ 구문 표시·코드 배경 `#11151a`·Outline 없음이 확인됐다.
확장 로딩 전에는 Explorer가 잠시 보였다가 워크스페이스로 전환된다. 검증은 확장 활성화 이후 최종 화면에 대한 결과이며 초기 전환을 제거한 결과는 아니다.
자유 도킹·드래그드롭 위젯·로딩 UI는 계속 후속이다. 추가 범위와 검증 결과는 [작업 계획 §13](../../tasks/script-editor-integration-plan.md#13-2026-10-14-추가-계획--워크스페이스사이드바테마)에 있다.

## 구조

| 경로 | 내용 |
|---|---|
| `extensions/jbro-engine` | 다섯 드롭다운 메뉴·워크스페이스 트리·테마·표시 설정·기존 패널/태스크 명령, GitHub 로그인·계정 관리 연결. 엔진 전용 프로젝트·빌드 연결은 아직 구현하지 않았다 |
| `extensions/jbro-languages` | `.jscript` 문법 강조와 `JBroc --lsp` 연결, `.jproject`·`.jcanvas`·`.jprefab` 의 YAML 연결 |
| `extensions/jbro-korean` | 고정 vscode-loc 한국어 팩의 번역 파일 95개와 MIT 라이선스. 본체·내장 확장 번역 |
| `scripts/check-nls.mjs` | 확장의 번역 파일이 빠짐없이 채워졌는지 검사한다 |
| `scripts/check-korean-pack.mjs` | 번역 파일·라이선스·문제 패널/제한 모드 필수 한국어 키를 검사한다 |
| `scripts/run-grammar-tests.mjs` | 문법 단언 테스트와 스냅숏 테스트를 돌린다 |
| `scripts/generate-theme.mjs` | 엔진 팔레트에서 `themes/jbro-engine-color-theme.json`을 생성한다. `themes/syntax-tokens.json`은 문법 강조용 색만 제공한다 |

## JBroScript 편집 진단

개발 확장의 의존성을 설치하고 엔진의 `JBroc.exe`를 빌드한다. `npm test`는 언어 서버를 실행하지 않으므로
실제 편집기 연결을 확인하려면 아래 설치와 확장 호스트 검사가 별도로 필요하다.

```powershell
npm ci --prefix extensions/jbro-languages
```

`scripts/upstream-launch.cmd`로 `.jproject` 파일이 하나 있는 프로젝트 폴더를 열고 `.jscript` 파일을 연다.
확장은 해당 폴더에서 `JBroc --lsp`를 시작한다. 저장하지 않은 수정도 분석하며 문법·타입·null 접근 등의
진단은 편집기 밑줄과 **오류/로그 → Problems**에 나타난다. 오류를 고치면 진단이 갱신된다.
자동완성·정의 이동 등 서버의 편집 기능도 이 연결을 사용한다. 서버 로그는 **Output → JBroScript**에서 확인한다.

기본 실행 파일은 이 저장소의 `source/JBroEngine/Build/x64/Debug/JBroc.exe`, 없으면 `Release/JBroc.exe`다.
다른 빌드 위치나 루트에 여러 `.jproject` 파일이 있는 경우 작업 폴더 설정에 경로를 지정한다.

```json
{
  "jbro.jbroscript.serverPath": "F:\\AI\\builds\\jbroscript-language-server\\x64\\Debug\\JBroc.exe",
  "jbro.jbroscript.projectFile": "Game.jproject"
}
```

상대 경로는 작업 폴더 기준이다. 신뢰한 작업 폴더에서만 서버를 실행한다. 현재의 진단은 JBroScript 분석 결과이며
C++ 코드 생성·C++ 컴파일·게임 실행 결과는 아니다. C++ 스크립트 빌드 태스크의 결과를 Problems에 모으는
작업도 아직 별개다.

저장소 안의 예제를 직접 보려면 `source/JBroScriptEditor`에서 다음처럼 개발 편집기를 연다.

```powershell
$env:JBRO_EDITOR_WORK = 'F:\Project\JBroScriptEditor'
./scripts/upstream-launch.cmd (Resolve-Path 'extensions/jbro-languages/test/fixtures/sample-project').Path
```

`Contents/Scripts/PatrolProbe.jscript`를 열어 `target.position`을 `target.missing`으로 바꾸면 `JBC3032`, 첫 줄의
`script`를 `scri!t`로 바꾸면 `JBC2001`이 Problems에 나타난다. 수정 전으로 되돌리면 사라진다.
`ref` 대상의 일반 멤버 접근은 편집 시 null 오류로 막지 않는다. 실제 실행 중 자동 검사·콜백 중단은 C++ 이미터 단계에 남아 있다.
이 예제의 자동 확장 호스트 시험은 `extensions/jbro-languages/test/extension-host.cjs`다.

## 테스트

```bash
npm install
npm test
```

계정 연동과 번역만 검사할 때:

```bash
npm run test:accounts
npm run test:nls
npm run test:theme
```

`test:accounts`는 계정 API 연결·취소·실패·상태 갱신·런처의 프로필 인자 거절을 검사한다.
실제 GitHub 인증이나 Git 원격 작업을 수행하지 않는다. `npm test`는 이 검사와 번역·테마·문법 검사를 함께 실행한다.
`npm run test:nls`에는 한국어 팩 파일 95개·라이선스·필수 한글 키 검사도 포함된다.
엔진 팔레트를 바꾼 뒤에는 `npm run generate:theme`으로 테마를 다시 만들고 `npm run test:theme`으로 생성 결과를 확인한다.

- **문법 단언**: `extensions/*/test/syntax/*.jscript`. 형식은 `vscode-tmgrammar-test` 의 것이다.
  음수 단언은 `- scope1 scope2` 처럼 `-` 를 **한 번만** 쓴다. `- a - b` 로 쓰면 도구의 해석기가 멈춘다.
- **스냅숏**: `extensions/*/test/snap/*.jscript` 와 옆의 `.snap`. 문법을 고쳐 토큰이 바뀌면
  `npm run update-snapshots` 로 다시 기록하고 **바뀐 줄을 검토한 뒤** 커밋한다.
  `.snap` 이 없으면 실패한다 — 기록하지 않은 스냅숏이 조용히 통과하지 않게 하기 위해서다.
- 도구의 `vscode-tmgrammar-test`·`vscode-tmgrammar-snap` 명령을 직접 쓰지 않는 이유는
  `scripts/run-grammar-tests.mjs` 머리 주석에 있다(Windows 의 Node 24 에서 종료 코드가 통과와 실패를 가르지 못한다).

## upstream Code-OSS 빌드

개발 빌드의 실행 환경이다. 현재 작업 체크아웃에는 아래 네 코어 패치가 적용되어 있다.
패치 목록과 각 우회의 이유는 `tasks/ide-plan.md` §4.2 와 §5.2 에 있다.

**upstream 체크아웃과 도구는 엔진 리포 밖에 둔다.** 둘을 담는 폴더를 환경 변수 `JBRO_EDITOR_WORK` 로 준다
(이 기계에서는 `F:\Project\JBroScriptEditor`). 빌드는 C: 에 쓰지 않는다(`tasks/ide-plan.md` §4.2).
변수가 없으면 스크립트는 멈춘다 - 스크립트 옆으로 되돌아가면 C: 에 조용히 수 GB 를 쓰기 때문이다.

| 경로 | 내용 |
|---|---|
| `%JBRO_EDITOR_WORK%\upstream` | Code-OSS 릴리스 태그의 얕은 클론. 엔진 리포 밖이다 |
| `%JBRO_EDITOR_WORK%\.toolchain` | 휴대용 Node, node-gyp·Electron·npm 캐시, 임시 폴더, 개발 실행의 사용자 데이터. 엔진 리포 밖이다 |
| `scripts/upstream-env.cmd` | 빌드 환경. 아래 스크립트가 `call` 로 부르고, `JBRO_UPSTREAM`·`JBRO_TOOLCHAIN` 을 정한다 |
| `scripts/upstream-npm-ci.cmd` | 의존성 설치 |
| `scripts/upstream-prelaunch.cmd` | Electron 받기, 컴파일, 내장 확장 받기 |
| `scripts/upstream-compile.cmd` | 패치로 소스를 바꾼 뒤 다시 컴파일한다. `preLaunch` 는 `%JBRO_UPSTREAM%\out` 이 있으면 컴파일하지 않는다 |
| `scripts/upstream-launch.cmd` | 다시 빌드하지 않고 띄우며 두 JBro 확장을 `--extensionDevelopmentPath`로 자동 로딩한다 |
| `patches/NNNN-이름.patch` | 코어 패치. 파일 하나에 바꾸는 것 하나 |
| `scripts/apply-patches.mjs` | 깨끗한 upstream 체크아웃에 패치를 이름 순서대로 적용한다. `--check` 는 적용되는지만 본다 |

처음 한 번:

```bat
set JBRO_EDITOR_WORK=F:\Project\JBroScriptEditor
git clone --depth 1 --branch 1.137.0 https://github.com/microsoft/vscode.git %JBRO_EDITOR_WORK%\upstream
```

`%JBRO_EDITOR_WORK%\upstream\.nvmrc` 와 같은 판의 Node 를 `%JBRO_EDITOR_WORK%\.toolchain\node` 에 푼다(nodejs.org 의 `win-x64` zip, `SHASUMS256.txt` 로 해시를 대조한다).
Visual Studio 에는 C++ 작업과 **"x64/x86용 C++ Spectre 완화 라이브러리(최신 MSVC)"** 개별 구성 요소가 있어야 한다.

```bat
scripts\upstream-npm-ci.cmd
node scripts\apply-patches.mjs
scripts\upstream-prelaunch.cmd
scripts\upstream-launch.cmd
```

패치 적용은 깨끗한 새 체크아웃에서 한 번 한다. 이미 적용된 작업 폴더를 초기화하거나 다시 적용하지 않는다.

### GitHub 로그인과 계정 관리

개발 창에서 상태 표시줄의 `GitHub 로그인`, 소스 제어 제목줄의 GitHub 아이콘, 또는 명령 팔레트의
`JBroEngine: GitHub 로그인`을 선택한다. 내장 GitHub 인증 제공자의 안내에 따라 브라우저에서 기기 코드를 승인한다.
기기 코드 방식이 기본값이며, 인증 후에는 상태 표시줄에 계정 이름이 표시된다.
계정 이름이나 `JBroEngine: GitHub 계정 관리`를 누르면 Code-OSS의 계정 관리 화면에서 로그아웃할 수 있다.

편집기를 여는 것만으로 로그인 창을 띄우지 않는다. 저장된 세션은 내장 제공자에게 조용히 조회하고,
로그인·로그아웃에 따른 계정 상태를 갱신한다. JBro 확장은 토큰을 저장하지 않으며 자격 증명은 내장 제공자가 관리한다.
**실제 계정으로 로그인·로그아웃하거나 Git 원격 작업을 수행한 결과는 아직 확인하지 않았다.**

### 격리 프로필과 확장 호스트 검사

별도 사용자 데이터를 쓰려면 `JBRO_EDITOR_USER_DATA`를 지정한다. 지정하지 않으면 `.toolchain\user-data`를 쓴다.
런처는 명령줄의 `--user-data-dir`·`--extensions-dir`·`--locale`을 거절한다. 실행 언어를 명시할 때는 `JBRO_EDITOR_LOCALE`을 지정한다.
지정하지 않으면 로케일 인자를 전달하지 않아 기존 사용자 언어 선택을 보존한다. 새 `argv.json`의 기본 한국어는 0001과 한국어 팩 등록이 제공한다.
세 내장 개발 확장을 별도로 설치하거나 같은 인자를 추가할 필요가 없다. 런처는 선택한 개발 프로필에 한국어 팩을 등록한다.

아래 확장 호스트 검사는 새 프로필에서 명령 등록·내장 제공자 활성화·기기 코드 기본값·로그인하지 않은 초기 상태를 확인한다.
`source/JBroScriptEditor`에서 실행하며, 사용자 데이터 경로는 이전에 로그인하지 않은 새 폴더를 지정한다.

```bat
set JBRO_EDITOR_WORK=F:\Project\JBroScriptEditor
set JBRO_EDITOR_USER_DATA=%JBRO_EDITOR_WORK%\.toolchain\user-data-auth-test-new
scripts\upstream-launch.cmd --extensionTestsPath="%CD%\extensions\jbro-engine\test\extension-host.cjs" --skip-welcome --skip-release-notes
```

검사 뒤 일반 개발 창을 열 때는 `set JBRO_EDITOR_USER_DATA=`로 기본 프로필을 다시 사용한다.

### 코어 패치

```bat
rem Run only against a clean upstream checkout that does not already have the patches.
node scripts\apply-patches.mjs
scripts\upstream-compile.cmd
```

`upstream-compile.cmd`는 전체 `compile-client`를 실행한다. 본체 패치의 개발 실행만 갱신할 때는
`upstream-env.cmd`로 환경을 준비한 뒤 upstream 폴더에서 `npm run transpile-client`를 선택할 수 있다.
소스 변환과 타입 검사는 별도이며, 필요한 출력 파일·종료 코드·타입 검사 결과를 함께 확인한다.
이번 0004 검증에서는 본체 변환·변경 파일의 증분 변환·최종 native 타입 검사가 통과했다.
첫 전체 `compile-client`의 종료 결과는 확보하지 못했으므로 전체 컴파일 성공으로 보고하지 않는다.

패치 하나를 고칠 때는 upstream 체크아웃에서 직접 고친 뒤 그 패치가 건드리는 파일만 골라 다시 뽑는다
(`git -C %JBRO_EDITOR_WORK%\upstream diff -- <파일들> > patches\NNNN-이름.patch`). 패치끼리 같은 파일을 건드리지 않게 둔다.
뽑은 뒤에는 되돌리고 `apply-patches.mjs` 로 다시 적용해서, 손으로 고친 결과와 같은지 `git -C %JBRO_EDITOR_WORK%\upstream diff` 로 비교한다.

| 패치 | 상태 |
|---|---|
| `0001-default-locale-ko` | 적용. 새로 만들어지는 `argv.json` 에 `"locale": "ko"` 가 들어가는 것을 확인했다 |
| `0002-remove-chat-ai` | 적용. `chat.disableAIFeatures` 기본값을 `true` 로 바꾼다. 에이전트 호스트가 뜨지 않고 새 에러가 없으며, 화면에 채팅·Copilot 안내가 뜨지 않는다. **없애지 않고 기본으로 끈다** - 사용자가 설정에서 다시 켤 수 있다. 기여 import 를 빼는 첫 방식은 태스크·디버그가 채팅 서비스에 기대고 있어 쓸 수 없었다 |
| `0003-block-vsix-install` | 적용. CLI 의 VSIX 설치가 거절되고, 명령 팔레트에 VSIX 설치 항목이 없으며, 시작할 때 새 에러가 없다 |
| `0004-jbro-layout` | 적용·역적용 검사·본체 타입 검사·실제 메뉴 renderer 검사 통과. 공통 `JBroMenuBar`와 하위 메뉴 기여, 기본 메뉴·보조 사이드바 숨김. 제품 메뉴·Activity Bar 기본값은 확장이 제공하며 저장된 사용자 설정은 보존 |
| `0005-korean-dev-localization` | 적용·본체 변환·native 타입 검사 통과. 개발 모드의 번역 키+영어 원문 대응표를 사용하며 영어 폴백 유지. 실제 창의 문제 패널·제한 모드 한국어 확인 |

**C: 에 쓰지 않는다.** `upstream-env.cmd` 가 `LOCALAPPDATA`·`TEMP`·`npm_config_cache` 를 `.toolchain` 아래로 돌린다.
예외는 홈 폴더의 `.vscode-oss-dev\argv.json` 과 `.vscode-oss-shared` 두 가지로, 위치가 `--user-data-dir` 를 따르지 않는다.

## 화면 글자

빡대리 지시에 따라 JBro 화면 작업은 한국어를 우선한다(D-312). `jbro-engine`의 manifest 기본 문구는
`package.nls.json`의 한국어이며, 영어는 `package.nls.en.json`, 한국어 번역 파일은 `package.nls.ko.json`에 보존한다.
현재 upstream의 개발 모드는 manifest 번역에서 locale을 사용하지 않고 기본 파일을 읽으므로 이 기본값을 바꿨다.
기존 `jbro-languages`의 영어 기본 파일과 실행 문구의 `l10n/bundle.l10n*.json` 구조는 그대로다. 키 누락은 번역 검사에서 실패한다.

한국어 확인 범위는 JBro 메뉴뿐 아니라 Code-OSS 본체·내장 UI를 포함한다(D-315).
Microsoft `vscode-loc@0f157dee0cdcbf58cdf51a221ee94751c7f35e75`의 MIT 한국어 팩을 `jbro-korean`으로 싣고,
0001이 새 `argv.json`의 기본 한국어를 설정하고 런처가 프로필 `languagepacks.json`에 한국어 팩을 등록한다.
런처는 `JBRO_EDITOR_LOCALE`이 명시됐을 때만 로케일 인자를 전달한다. 개발 모드에는 키+영어 원문 대응 22,422개를 생성해 적용하며 영어 폴백은 유지한다.

첫 직접 Playwright 검사는 `--locale=ko`를 명시했다. 마지막 검사는 CLI 로케일 인자 없이 새 격리 프로필·새 작업 폴더로 실행했고,
현재 공유 `.vscode-oss-dev/argv.json`의 `locale=ko`가 적용됐다. `문제·출력·디버그 콘솔·터미널·포트`,
`작업 영역에서 문제가 발견되지 않았습니다.`, 제한 모드 경고와 `관리·자세한 정보`의 한국어, 루트·확장한 하위 폴더 아이콘을 확인했다.
이는 직접 Playwright 실행의 결과이며 런처 자체 실행을 검증한 결과는 아니다.
번역 파일 95개·라이선스·필수 한글 키 검사와 생성 대응표의 실행 출력 값 일치 검사도 통과했다.
이전 영어 내장 UI 기록은 당시 이력이며 후속 결과는 [작업 계획 §14](../../tasks/script-editor-integration-plan.md#14-2026-10-14-후속--본체-한국어폴더-아이콘)에 있다.
설치본·전 화면 완전 한국어·실제 GitHub 인증은 아직 확인하지 않았다.

## 문법이 덮는 범위

`tasks/jbroscript-syntax.md` 를 따른다(2026-09-17 기준). 그 문서의 [제안] 항목(예약어 목록, `is not null`,
`switch`/`case`, 생성자 모양)도 칠한다. 바뀌면 문법 파일을 고치면 된다.

- **예약어**는 문서 §2.1 목록이다. 타입이나 이름 자리에 오지 못하므로 `return total`·`is not null` 이 선언으로 칠해지지 않는다.
- **`callback`·`override`·`require` 는 함수 선언 줄의 끝에서만, `in` 은 `for` 괄호 안에서만** 키워드다. 그 밖에서는 이름으로 칠한다.
  그래서 함수 선언 줄은 한 규칙이 통째로 맡는다. 일반 규칙에 맡기면 `Int override` 가 `override` 라는 필드 선언이 된다.
- 엔진 타입 `Int`·`Float`·`Bool`·`String` 은 `support.type.primitive`, `Vector2`·`Rect`·`Color`·`Array`·`Table` 은 `support.type.builtin` 이다.
  옛 문법의 `int`·`float`·`bool` 과 `if let` 은 없다.
- 미완성 줄도 칠해진다. `target is` 까지만 친 줄이 선언으로 칠해지지 않는 것을 테스트한다.

`test/snap/enemy.jscript` 는 `tasks/jbroscript-syntax.md` §13 전체 예시를 그대로 옮긴 것이다. 문서의 예시가 바뀌면 다시 옮기고
스냅숏을 새로 기록한다.

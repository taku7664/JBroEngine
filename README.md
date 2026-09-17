# JBro Script Editor

JBroEngine 의 스크립트 편집기다. Code-OSS 를 얇게 포크하고, JBro 기능은 전부 내장 확장으로 만든다.
계획과 결정은 엔진 리포의 `tasks/ide-plan.md` 와 `tasks/todo.md` D-87 에 있다.

**아직 포크는 없다.** 확장을 먼저 만들고 일반 VS Code 에서 개발·테스트한다.
upstream Code-OSS 는 패치 없이 빌드해서 띄울 수 있게 해 두었다(아래 "upstream Code-OSS 빌드").

## 구조

| 경로 | 내용 |
|---|---|
| `extensions/jbro-languages` | `.jscript` 언어 등록과 TextMate 문법, `.jproject`·`.jcanvas`·`.jprefab` 의 YAML 연결 |
| `scripts/check-nls.mjs` | 확장의 번역 파일이 빠짐없이 채워졌는지 검사한다 |
| `scripts/run-grammar-tests.mjs` | 문법 단언 테스트와 스냅숏 테스트를 돌린다 |

## 테스트

```bash
npm install
npm test
```

- **문법 단언**: `extensions/*/test/syntax/*.jscript`. 형식은 `vscode-tmgrammar-test` 의 것이다.
  음수 단언은 `- scope1 scope2` 처럼 `-` 를 **한 번만** 쓴다. `- a - b` 로 쓰면 도구의 해석기가 멈춘다.
- **스냅숏**: `extensions/*/test/snap/*.jscript` 와 옆의 `.snap`. 문법을 고쳐 토큰이 바뀌면
  `npm run update-snapshots` 로 다시 기록하고 **바뀐 줄을 검토한 뒤** 커밋한다.
  `.snap` 이 없으면 실패한다 — 기록하지 않은 스냅숏이 조용히 통과하지 않게 하기 위해서다.
- 도구의 `vscode-tmgrammar-test`·`vscode-tmgrammar-snap` 명령을 직접 쓰지 않는 이유는
  `scripts/run-grammar-tests.mjs` 머리 주석에 있다(Windows 의 Node 24 에서 종료 코드가 통과와 실패를 가르지 못한다).

## upstream Code-OSS 빌드

포크 빌드를 준비하는 단계다. 코어 패치는 아직 적용하지 않고 upstream 을 그대로 빌드해서 띄운다.
패치 목록과 각 우회의 이유는 엔진 리포 `tasks/ide-plan.md` §4.2 와 §5.2 에 있다.

| 경로 | 내용 |
|---|---|
| `upstream/` | Code-OSS 릴리스 태그의 얕은 클론. 커밋하지 않는다 |
| `.toolchain/` | 휴대용 Node, node-gyp·Electron·npm 캐시, 임시 폴더, 개발 실행의 사용자 데이터. 커밋하지 않는다 |
| `scripts/upstream-env.cmd` | 빌드 환경. 아래 스크립트가 `call` 로 부른다 |
| `scripts/upstream-npm-ci.cmd` | 의존성 설치 |
| `scripts/upstream-prelaunch.cmd` | Electron 받기, 컴파일, 내장 확장 받기 |
| `scripts/upstream-compile.cmd` | 패치로 소스를 바꾼 뒤 다시 컴파일한다. `preLaunch` 는 `upstream\out` 이 있으면 컴파일하지 않는다 |
| `scripts/upstream-launch.cmd` | 다시 빌드하지 않고 띄운다 |
| `patches/NNNN-이름.patch` | 코어 패치. 파일 하나에 바꾸는 것 하나 |
| `scripts/apply-patches.mjs` | 깨끗한 `upstream/` 에 패치를 이름 순서대로 적용한다. `--check` 는 적용되는지만 본다 |

처음 한 번:

```bat
git clone --depth 1 --branch 1.137.0 https://github.com/microsoft/vscode.git upstream
```

`upstream\.nvmrc` 와 같은 판의 Node 를 `.toolchain\node` 에 푼다(nodejs.org 의 `win-x64` zip, `SHASUMS256.txt` 로 해시를 대조한다).
Visual Studio 에는 C++ 작업과 **"x64/x86용 C++ Spectre 완화 라이브러리(최신 MSVC)"** 개별 구성 요소가 있어야 한다.

```bat
scripts\upstream-npm-ci.cmd
scripts\upstream-prelaunch.cmd
scripts\upstream-launch.cmd
```

### 코어 패치

```bat
git -C upstream checkout -- .
node scripts\apply-patches.mjs
scripts\upstream-compile.cmd
```

패치 하나를 고칠 때는 `upstream/` 에서 직접 고친 뒤 그 패치가 건드리는 파일만 골라 다시 뽑는다
(`git -C upstream diff -- <파일들> > patches\NNNN-이름.patch`). 패치끼리 같은 파일을 건드리지 않게 둔다.
뽑은 뒤에는 되돌리고 `apply-patches.mjs` 로 다시 적용해서, 손으로 고친 결과와 같은지 `git -C upstream diff` 로 비교한다.

| 패치 | 상태 |
|---|---|
| `0001-default-locale-ko` | 적용. 새로 만들어지는 `argv.json` 에 `"locale": "ko"` 가 들어가는 것을 확인했다 |
| `0002-remove-chat-ai` | **아직 없다.** 기여 import 를 빼는 방식은 태스크·디버그가 채팅 서비스에 기대고 있어 쓸 수 없었다 |
| `0003-block-vsix-install` | 적용. CLI 의 VSIX 설치가 거절되고, 시작할 때 새 에러가 없는 것을 확인했다 |

**C: 에 쓰지 않는다.** `upstream-env.cmd` 가 `LOCALAPPDATA`·`TEMP`·`npm_config_cache` 를 `.toolchain` 아래로 돌린다.
예외는 홈 폴더의 `.vscode-oss-dev\argv.json` 과 `.vscode-oss-shared` 두 가지로, 위치가 `--user-data-dir` 를 따르지 않는다.

## 화면 글자

기본은 한국어이고 원문은 영어다. 확장이 화면에 내는 글자는 `package.nls.json`(영어) 과
`package.nls.ko.json` 에, 실행 중 글자는 `l10n/bundle.l10n*.json` 에 둔다. 키가 빠지면 `npm test` 가 실패한다.

## 문법이 덮는 범위

엔진 리포 `tasks/jbroscript-plan.md` §12 의 1차 확정 문법만 덮는다. 표현식 문법(연산자 우선순위, 형변환 표기),
`fn` 의 반환 타입, 오류 처리는 아직 정해지지 않아서 문법 파일에도 없다. 연산자는 흔한 산술·비교·대입만 칠한다.

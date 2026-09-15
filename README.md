# JBro Script Editor

JBroEngine 의 스크립트 편집기다. Code-OSS 를 얇게 포크하고, JBro 기능은 전부 내장 확장으로 만든다.
계획과 결정은 엔진 리포의 `tasks/ide-plan.md` 와 `tasks/todo.md` D-87 에 있다.

**아직 포크는 없다.** 확장을 먼저 만들고 일반 VS Code 에서 개발·테스트한다.
포크 빌드는 배포할 것이 생길 때 한다.

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

## 화면 글자

기본은 한국어이고 원문은 영어다. 확장이 화면에 내는 글자는 `package.nls.json`(영어) 과
`package.nls.ko.json` 에, 실행 중 글자는 `l10n/bundle.l10n*.json` 에 둔다. 키가 빠지면 `npm test` 가 실패한다.

## 문법이 덮는 범위

엔진 리포 `tasks/jbroscript-plan.md` §12 의 1차 확정 문법만 덮는다. 표현식 문법(연산자 우선순위, 형변환 표기),
`fn` 의 반환 타입, 오류 처리는 아직 정해지지 않아서 문법 파일에도 없다. 연산자는 흔한 산술·비교·대입만 칠한다.

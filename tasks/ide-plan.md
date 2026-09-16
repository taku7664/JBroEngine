# JBro Script Editor — Code-OSS 포크 계획 (초안)

> 2026-09-15 작성, 2026-09-16 갱신. **P0 스파이크를 닫았고 P1 문법 강조 확장이 섰다(옛 문법 기준).**
> 편집기 리포는 `F:\Project\JBroScriptEditor` 다. **upstream 소스를 `upstream/` 에 받아 두었고 코어 패치 목록을 정했다(§5.2).**
> **진행 현황·남은 일·막힌 곳은 §8 에 있다.** §7 에 남은 세부 질문은 포크 빌드 전에 정한다.
> 근거는 [jbroscript-plan.md](./jbroscript-plan.md) §9·§10·§13·§18.7 과 [todo.md](./todo.md) D-56·D-60 이다.
> 확정 계약이 아니라 계획이므로 `docs/ProjectRule.md` 가 아니라 여기에 둔다.

---

## 0. 결론 먼저

- 제품 이름은 **JBro Script Editor** 다.
- **이 편집기는 `.jscript` 전용이다.** C++(엔진 빌트인 컴포넌트와 C++ 스크립트 경로)은
  Visual Studio 에서 편집한다. 따라서 C++ 언어 서비스(clangd·`compile_commands.json`)는 만들지 않는다.
- **JBro 기능은 전부 내장 확장으로 만들고, 코어 패치는 기능이 아니라 제품 모양에만 쓴다.**
  코어 패치는 하기로 했지만 항목은 아직 정하지 않았다(§5.2, §7 Q1).
- **확장 마켓플레이스는 연결하지 않는다.** 편집기에 필요한 확장은 편집기가 직접 싣는다(§5.3).
- **화면은 기본 한국어이고, 다국어를 전제로 만든다.** 원문은 영어, 한국어는 번역 파일이다.
  본체·JBro 확장·`jbroc` 진단의 세 출처가 모두 번역을 거친다(§5.5).
- **포크 빌드(브랜딩·설치본)는 배포할 것이 생길 때 한다.** 그 전에는 스파이크(P0)와 확장 개발을
  일반 VS Code 의 확장 개발 호스트에서 진행한다.
- **편집기 리포는 엔진 리포와 분리한다.** 언어 지식은 엔진 리포의 `jbroc` 에 두고, 편집기 쪽 확장은
  그 실행 파일을 띄워 LSP 로 대화하는 얇은 클라이언트로 둔다.
- **편집기의 쓸모는 `jbroc` 에 묶인다.** `jbroc` 없이 만들 수 있는 것은 문법 강조(P1)뿐이고,
  에러 표시·자동완성·디버깅은 전부 `jbroc` 이 선 뒤에 가능하다. 실질적인 선행 작업은 엔진 리포 쪽이다.

---

## 1. 문서가 이미 정한 것

| 항목 | 내용 | 출처 |
|---|---|---|
| 역할 분담 | 씬 에디터는 네이티브(ImGui), 코드 에디터는 Code-OSS 다 | D-60 |
| 둘의 대화 방식 | 파일(`.jproject`·`.jcanvas`·`.jscript`)로 대화한다. 씬 뷰를 웹뷰에 넣지 않는다 | D-60 |
| `.jscript` 지원 네 조각 | ① 문법 강조 ② 에러 표시 ③ 디버깅 ④ 자동완성·정의로 이동 | jbroscript-plan §13.2 |
| 권하는 순서 | ① → ② → ③(위험 때문에 앞당김) → ④ | jbroscript-plan §13.4 |
| LSP 의 재료 | 트랜스파일러의 AST 를 그대로 쓴다. 파서 하나가 컴파일러와 LSP 를 함께 먹인다 | jbroscript-plan §10 6번, §13.2 ④ |
| 디버깅 | 지금은 편집기에서 편집만 하고 디버깅은 Visual Studio 로 한다 | jbroscript-plan §18.7 |
| 나중의 디버깅 후보 | 스크립트 DLL 만 `clang-cl -gdwarf` 로 빌드하고 `lldb-dap` 으로 붙인다. **아직 재지 않았다** | jbroscript-plan §18.7 |

## 2. 이번에 정한 것 (2026-09-15)

| 항목 | 결정 |
|---|---|
| 착수 순서 | P0 스파이크와 확장 개발을 먼저 하고, 포크 빌드는 배포할 것이 생길 때 한다 |
| 제품 이름 | JBro Script Editor |
| 편집기 범위 | `.jscript` 전용. C++ 편집 지원은 넣지 않는다 |
| C++ 스크립트 경로 | jbroscript-plan §9 대로 남는다. 편집기의 대상이 아닐 뿐이며 D-56 은 바뀌지 않는다 |
| 코어 패치 | 한다. 무엇을 바꿀지는 미정이다(§7 Q1) |
| 확장 마켓플레이스 | 연결하지 않는다. `product.json` 한 곳이라 나중에 열 수 있다(§5.3) |
| 식별자 | CLI 명령어 `jbro-script-editor`, 설정 폴더 `.jbro-script-editor`, 리포 `JBroScriptEditor`, 로컬 위치는 F: |
| 화면 언어 | 기본 한국어, 폴백 영어. 로컬라이징을 전제로 하며 한국어를 소스에 쓰지 않는다(§5.5) |
| 언어 설정 공유 | 씬 에디터와 공유한다. 공유 파일은 씬 에디터의 사용자 설정 파일이 생길 때 정한다(§5.5) |

---

## 3. 이 기계에서 확인한 사실 (2026-09-15)

| 항목 | 값 | 계획에 주는 영향 |
|---|---|---|
| Node | 시스템 24.14.1 | Code-OSS 1.137.0 의 `.nvmrc` 는 24.18.0 이고, `preinstall` 이 부버전까지 검사한다. 스파이크는 휴대용 24.18.0 을 F: 에 두고 쓴다 |
| npm / yarn | 11.11.0 / 1.22.22 | Code-OSS 는 npm 을 쓰고, npm 13 이상과 yarn 은 `preinstall` 이 거절한다 |
| Python | 3.14.6 | node-gyp 가 쓴다 |
| Visual Studio | 2026 Community(18.9), MSVC 14.51 | node-gyp 12.3.0 은 인식한다. Spectre 완화 라이브러리는 스파이크 중에 추가했다. SDK 10.0.26100.0 은 헤더가 빠져 있다(§4.2) |
| clang-cl | 시스템에 없다 | 스파이크는 LLVM 23.1.1 의 `clang-cl` 만 F: 에 풀어 썼다 |
| C: 여유 공간 | **10.4 GB** | Code-OSS 소스·`node_modules`·빌드 산출물을 담기에 부족하다고 판단한다(추정) |
| F: 여유 공간 | 80.5 GB | 포크 리포와 npm·Electron 캐시를 F: 에 둔다(§2). node-gyp 캐시는 옮기지 않는다(§4.2) |

엔진 쪽 사실:

- 스크립트 DLL 은 MSBuild(`JBro.Script.props`)로 빌드한다. `.jscript` 도 `jbroc` 이 C++ 을 뽑은 뒤
  같은 길로 빌드되므로, 편집기의 "빌드" 는 `jbroc` → MSBuild 를 부르는 일이 된다.
- 호스트에 파일 감시가 없다. 편집기에서 빌드한 뒤 씬 에디터가 새 DLL 을 알아채는 길이 아직 없다.

**Code-OSS 는 바깥과 통신하지 않는다(2026-09-16, `product.json` 으로 확인).**
`updateUrl`·`extensionsGallery`·`telemetryOptInStatusUrl`·`experimentsUrl`·`surveys` 가 모두 없다.
자동 업데이트·텔레메트리·마켓플레이스·실험·설문이 보낼 주소를 갖고 있지 않다.
채팅도 로그인하지 않으면 요청을 내지 않는다. `chatEntitlementService.ts:1063` 의 `resolve()` 는
계정이 없으면 상태를 `Unknown` 으로 두고 끝내고, 네트워크 요청은 `if (defaultAccount)` 안쪽에만 있다.
**편집기는 오프라인에서 온전히 동작한다.**

---

## 4. 먼저 재야 할 위험

### 4.1 clang 으로 컴파일하면 `JBRO_FIELD` 가 멈춘다 (2026-09-15 실측으로 확인)

`JBroCore/Include/JBro/Reflection/Field.h` 는 필드 이름을 `__FUNCSIG__` 에서 잘라 온다.
자르는 규칙은 MSVC 형식에 맞춰져 있고, 이름을 못 찾으면 **헤더 안의 `static_assert` 가
컴파일을 멈춘다**(`Field.h:70`).

**실측**(MSVC 19.51.36257 대 clang-cl 23.1.1, 둘 다 `/std:c++20 /utf-8`, 같은 include 경로):

같은 멤버 포인터 `&Game::Player::Speed` 에 대해 `FieldSignature` 가 돌려주는 문자열:

```
MSVC  : class std::basic_string_view<char,struct std::char_traits<char> > __cdecl JBro::Detail::FieldSignature<&Game::Player::Speed>(void)
clang : std::string_view __cdecl JBro::Detail::FieldSignature(void) [MemberPointer = &Game::Player::Speed]
```

`DeriveFieldName` 의 결과는 MSVC 가 `Speed`, clang 이 **빈 문자열**이다. clang 은 템플릿 인자를
`<...>` 가 아니라 끝의 `[MemberPointer = ...]` 로 적으므로 `rfind(">(")` 가 찾지 못한다.

실제 테스트 파일 `Tests/ReflectionFieldTests.cpp` 를 `/c` 로 컴파일한 결과:

| 컴파일러 | 결과 |
|---|---|
| MSVC(대조군) | 성공 |
| clang-cl | **실패, 에러 12개.** `Field.h:70` 의 "the toolchain format changed" 10개와, 테스트의 이름 고정 `static_assert` 2개 |

대조군이 통과하므로 원인은 include 경로나 플래그가 아니라 서명 형식이다. 프로브는 세션 스크래치패드에
있었고 커밋하지 않았다. 위 두 줄의 문자열과 이 절의 설명만으로 다시 만들 수 있다.

편집기를 `.jscript` 전용으로 정했으므로 **편집 쪽에는 영향이 없다.** 남는 영향은 디버깅이다.
jbroscript-plan §18.7 의 `clang-cl -gdwarf` 경로는 스크립트 DLL 을 clang 으로 컴파일하는데,
생성된 C++ 도 리플렉션 등록에 같은 기계를 쓰면 **실제 컴파일이 멈춘다.** 편집기에서 `.jscript` 를
디버깅하는 유일한 후보 경로가 여기서 막힐 수 있다.

**고치는 방향**(아직 고치지 않았다): `DeriveFieldName` 이 두 형식을 모두 읽게 한다. 문자열이 `]` 로
끝나면 마지막 `]` 앞의 마지막 `::` 다음이 이름이다. 그리고 이름 고정 테스트를 두 컴파일러에서 모두
돌린다. 지금 테스트는 MSVC 로만 돌아서 이 구멍을 볼 수 없었다.
Core 공개 헤더를 고치는 일이고 방향을 바꾸는 판단은 아니다. **P5(디버깅)를 시작하기 전에는 반드시
고쳐야 하며**, 그 전에 고칠지는 엔진 쪽 작업 순서에 달렸다.

### 4.2 Code-OSS 빌드 환경

**실측**(2026-09-15, Code-OSS 1.137.0 태그 얕은 클론, 휴대용 Node 24.18.0, npm 11.16.0):

- **클론**: 77초, 작업 트리 832 MB.
- **node-gyp 는 VS 2026 을 인식한다.** 의존성 빌드는 npm 에 딸린 node-gyp 12.3.0 이 하고,
  `find VS using VS2026 (18.9.12128.139)` 로 찾았다.
- **Spectre 완화 라이브러리가 필요하다.** `npm ci` 가 170초 뒤 `@vscode/deviceid` 빌드에서
  `MSB8040`(스펙터 완화된 라이브러리가 필요합니다)으로 멈췄다. VS 설치 관리자의 개별 구성 요소에서
  **"x64/x86용 C++ Spectre 완화 라이브러리(최신 MSVC)"**
  (`Microsoft.VisualStudio.Component.VC.Runtimes.x86.x64.Spectre`)를 추가해야 한다.
  이름이 비슷한 ATL·MFC ARM64 용 Spectre 구성 요소를 먼저 잘못 설치했고, 그것으로는 해결되지 않았다.
  설치 뒤 `MSVC\14.51.36231\lib\spectre\x64` 가 생겼는지로 확인한다.
- **Windows SDK 10.0.26100.0 이 헤더가 빠진 채 설치되어 있다.** Spectre 를 넣은 뒤 `npm ci` 가
  `@vscode/native-watchdog` 에서 `C1083: 'specstrings_strict.h'` 로 멈췄다. 26100 의 `shared` 헤더는
  253개이고 22621 은 266개이며, 그 파일은 22621 에만 있다. node-gyp 는 VS 에 등록된 SDK 중 **가장 새 것**을
  고르므로 26100 을 쓴다. 엔진은 `WindowsTargetPlatformVersion` 을 22621 로 고정해서 이 문제를 겪지 않았다.
  **우회**: `vcvars64.bat 10.0.22621.0` 으로 만든 개발자 환경에서 `npm ci` 를 돌린다. node-gyp 는
  `VCINSTALLDIR`·`VSCMD_VER` 가 있으면 그 환경의 `WindowsSDKVersion` 을 쓴다(`find-visualstudio.js`).
  근본 해결은 SDK 26100 을 복구하거나 지우는 것이며, 이 기계의 설치 상태 문제라 다른 기계에서는 겪지 않을 수 있다.
- **`preinstall` 의 VS 검사가 VS 2026 경로를 모른다(실행으로 확인).** `build/npm/preinstall.ts` 는
  `Microsoft Visual Studio\2022` 와 `\2019` 만 찾고, 우회 없이 돌리면 "Invalid C/C++ Compiler Toolchain" 으로
  멈춘다. `vs2022_install=C:\Program Files\Microsoft Visual Studio\18\Community` 로 넘긴다.
- **세 우회를 모두 넣은 `npm ci` 가 성공했다: 850초.** `vscode` 폴더 7.1 GB, npm·Electron 캐시 1.0 GB,
  스파이크 전체 9.0 GB(휴대용 Node·LLVM 포함). C: 에는 node-gyp 캐시 180 MB 가 생겼다.
- **node-gyp 캐시는 `%LOCALAPPDATA%` 에 둔다(소스로 확인).** `preinstall.ts` 가 Electron 헤더 위에 덮어쓸 헤더를
  `%LOCALAPPDATA%\node-gyp\Cache` 라는 **고정 경로**에서 찾는다. 캐시를 F: 로 옮기면 그 덮어쓰기가
  에러 없이 건너뛰어진다.

**포크 빌드 스크립트가 알아야 하는 것**: 위 우회 중 Spectre 와 `vs2022_install` 은 VS 2026 을 쓰는
모든 기계에 필요하다. SDK 지정은 기계에 따라 다르지만, 엔진과 같은 22621 로 고정해 두면 해가 없다.

---

## 5. 포크 방식

### 5.1 선택지

| | A. 전체 브랜치 포크 | **B. 얇은 포크 (패치 묶음 + 내장 확장)** | C. 포크 없음 (VSCodium + 확장팩) |
|---|---|---|---|
| upstream 따라가기 | 매번 병합 충돌 | 패치 몇 개만 다시 맞춘다 | 비용 없음 |
| 제품 정체(이름·아이콘·설치본) | 된다 | 된다 | 안 된다 |
| 코어 수정 | 자유롭다 | 가능하지만 패치 하나하나가 유지 비용이다 | 안 된다 |
| JBro 기능 위치 | 코어에 섞이기 쉽다 | 확장 | 확장 |

**B 를 권한다.** jbroscript-plan §13.2 의 네 조각은 전부 확장 API 로 만들 수 있고 코어를 고칠 이유가 없다.
코어를 고치지 않으면 upstream 갱신이 "제품 설정과 패치 몇 개를 다시 맞추는 일" 로 남는다.

### 5.2 무엇을 어디서 고치는가

고치는 자리는 세 층이다. 아래로 갈수록 upstream 을 올릴 때마다 다시 맞춰야 하는 비용이 커진다.

| 층 | 고치는 방법 | 할 수 있는 것 |
|---|---|---|
| **확장** | 확장 폴더를 싣는다. upstream 갱신과 거의 무관하다 | 언어 등록·문법 강조, 에러 밑줄(LSP), 자동완성·정의로 이동, 명령·단축키·우클릭 메뉴, 빌드 태스크, 디버거 연결, 사이드바 트리 뷰, 상태 표시줄, 웹뷰 패널, 특정 확장자 전용 편집 화면, 설정 항목, 테마, 시작 안내(walkthrough) |
| **`product.json`** | 코드가 아니라 설정 파일 하나다 | 제품 이름·CLI 이름·설정 폴더, 확장 갤러리 주소, 기본 설정값, 함께 실을 확장 목록, 내장 확장에게 제안 단계(proposed) API 허용 |
| **코어 패치** | Code-OSS 소스를 직접 고친다. 갱신 때마다 충돌할 수 있다 | 기본 UI 요소 제거(채팅·Copilot 자리, 계정 메뉴 등), 워크벤치 기본 배치 자체의 변경, 시작 화면 통째 교체, 메뉴 구조 재편, 편집기(Monaco) 내부 동작 변경, 확장 설치 정책 강제 |

아이콘과 설치 프로그램 이름은 리소스 파일 교체와 빌드 설정이라 코어 패치와 `product.json` 사이에 있다.
VSCodium 이 같은 일을 하는 방식이며, 코드 로직을 바꾸지 않는다.

**코어 패치는 한다(2026-09-15).** 다만 기능을 코어에 넣지 않는다. JBro 기능은 확장 층에 두고,
코어 패치는 확장으로 할 수 없는 제품 모양(기본 UI 제거·배치·메뉴 등)에만 쓴다.
기능까지 코어에 들어가면 §5.1 의 A 와 같은 비용이 된다.

패치를 다루는 규칙:

- **패치 하나에 바꾸는 것 하나.** 파일로 두고 번호를 붙여 차례대로 적용한다(VSCodium 의 `patches/` 와 같은 방식).
- **패치마다 이유를 적는다.** 이 절의 표에 항목과 이유를 더한다. upstream 을 올리다 충돌이 나면
  그 이유를 보고 "다시 맞출지, 버릴지" 를 정하게 된다.
- **upstream 을 올릴 때 패치 전체가 깨끗하게 적용되는지를 첫 검증으로 둔다.**

#### 패치 목록 (2026-09-16 확정)

줄 번호는 upstream `1.137.0`(커밋 `645f29c`)을 `F:\Project\JBroScriptEditor\upstream` 에 받아
직접 읽고 확인한 것이다. upstream 을 올리면 다시 확인한다.

| 번호 | 이름 | 바꾸는 것 | 이유 |
|---|---|---|---|
| 0001 | `default-locale-ko` | `src/main.ts:422` 의 `defaultArgvConfigContent` 에 `"locale": "ko"` 를 넣는다 | OS 언어와 무관하게 첫 실행을 한국어로 연다(§5.5). 기본 `argv.json` 내용이 `product.json` 이 아니라 코드에 문자열로 박혀 있어 설정으로 닿을 수 없다 |
| 0002 | `remove-chat-ai` | `src/vs/workbench/workbench.common.main.ts` 의 230~234·237~241·394·400·434·474·480 줄과 `workbench.desktop.main.ts` 의 189·190·193·202 줄에서 부수효과 import 를 뺀다 | 채팅·AI 기능을 화면에서 없앤다. 마켓플레이스가 없어 Copilot 확장을 설치할 길이 없으므로, 두면 로그인만 권하는 죽은 UI 가 남는다 |
| 0003 | `block-vsix-install` | `contrib/extensions/browser/extensions.contribution.ts:935`·`964` 의 명령 등록, `extensionsActions.ts:228` 의 메뉴 항목, `extensionsViewlet.ts:695~701` 의 끌어놓기 처리, `platform/environment/node/argv.ts:130` 의 `--install-extension` 을 뺀다 | 남의 확장이 들어오는 마지막 통로를 막아 편집기 구성을 배포한 그대로 고정한다(§5.3) |

**0002 를 "설정으로 끄면 된다" 로 대신할 수 없는 것을 소스로 확인했다(2026-09-16).**

- `chat.disableAIFeatures` 설정은 있지만(`platform/chat/common/chatSettings.ts:6`), 하는 일은 Copilot **확장을
  비활성화하는 것**이 중심이다(`chatSetupContributions.ts:799`). 마켓이 없어 그 확장이 설치되지 않으므로 끌 대상이 없다.
- `product.json` 으로 기본 설정값을 바꾸는 길은 없다. `IProductConfiguration` 에 `configurationDefaults` 가 없고,
  기본값 덮어쓰기는 확장의 기여이거나 웹 호스트의 `options.configurationDefaults` 뿐이다
  (`services/configuration/browser/configuration.ts:48`).
- `product.json` 에서 `defaultChatAgent` 만 지우는 것으로도 안 된다. `chatEntitlementService.ts:459` 가
  `if (!productService.defaultChatAgent) { return; }` 로 빠져나가는데 이 갈래는 `Setup.hidden` 을 켜지 않고,
  그 컨텍스트 키의 기본값은 `false` 다(`chatEntitlementService.ts:41`). 채팅 UI 의 표시 조건 다수가
  `ChatContextKeys.Setup.hidden.negate()` 이므로 **설정 논리만 죽고 UI 는 남는다.**

**0002 의 위험: 빌드해서 띄워 봐야 안다.** `defaultChatAgent` 를 참조하는 파일이 51개이고 그중에
`contrib/scm/browser/scmInput.ts`·`contrib/extensions/browser/extensionsWorkbenchService.ts`·
`editor/contrib/inlineCompletions` 처럼 채팅이 아닌 기능도 있다. 부수효과 import 를 빼면 컴파일은 통과해도
그 자리들이 런타임에 서비스를 못 찾을 수 있다. **0002 의 검증은 창을 띄워 렌더러 에러가 없는 것까지 본다.**

**0002 에서 어디까지가 "AI" 인지는 아직 정하지 않았다.** 위 줄 번호는 `chat`·`inlineChat`·`agentsVoice`·`mcp`·
`welcomeOnboarding`·`welcomeAgentSessions`·`remoteCodingAgents`·`editTelemetry`·`inlineCompletions` 를 모두 포함한다.
이 중 `inlineCompletions` 는 AI 전용이 아니라 인라인 제안의 일반 틀이라서, 나중에 `jbroc` 의 LSP 가 쓸 수 있다.
**뺄지 남길지는 포크 빌드를 시작하기 전에 정한다.**

**0003 의 위험**: `--install-extension` 은 `code/node/cliProcessMain.ts` 와
`electron-utility/sharedProcess/contrib/defaultExtensionsInitializer.ts` 도 쓴다. 내장 확장을 싣는 경로에
영향이 없는지 확인한 뒤 뺀다. `--list-extensions` 는 포크 빌드의 검증에 쓰므로 남긴다(§6).

### 5.3 확장 마켓플레이스를 연결하지 않는다

마켓플레이스는 앱 안의 확장 탭에서 확장을 검색하고 설치하는 곳이다. VS Code 는 Microsoft 마켓을
쓰지만 포크는 약관상 그곳에 접속할 수 없고, 대안은 공개 마켓인 Open VSX 다.

**연결하지 않아도 편집기는 동작한다.** JBro 기능은 전부 편집기가 싣는 내장 확장이기 때문이다.
YAML 색칠(`.jproject`·`.jcanvas`)도 Code-OSS 가 기본으로 들고 있다.

**이미 되어 있다(2026-09-16 실측).** Code-OSS 의 `product.json` 에는 `extensionsGallery` 키가 아예 없고,
`platform/extensionManagement/common/extensionGalleryService.ts:625` 가 `productService.extensionsGallery?.` 로
옵셔널 접근을 한다. "갤러리를 비운다" 라고 적었지만 비울 것이 없다. 포크 빌드에서 할 일이 아니다.

잃는 것과 남는 것:

- 사용자가 테마·Vim 키 배치 같은 남의 확장을 검색해서 설치할 수 없다.
- 내장 확장은 편집기를 새로 배포할 때만 갱신된다.
- **`.vsix` 파일을 직접 설치하는 길도 막는다(2026-09-16 결정).** 코어 패치 0003 이다(§5.2).
- **한국어 화면이 저절로 생기지 않는다.** VS Code 의 한국어 화면은 본체가 아니라 언어 팩 확장이 준다.
  마켓이 없으므로 편집기가 그 팩을 직접 싣는다(§5.5).

### 5.4 upstream 고정 정책

`main` 이 아니라 **릴리스 태그**에 고정한다. "Electron 보안 갱신이 포함된 릴리스가 나오면 올린다" 를
기준으로 삼는 안을 제안한다. 오래 고정할수록 패치 맞추기는 쉬워지지만 Chromium 이 낡는다.

올릴 때 함께 확인하는 것:

- 코어 패치 전체가 깨끗하게 적용되는가(§5.2).
- **내장 언어 팩을 같은 릴리스의 것으로 올렸는가.** 언어 팩은 Code-OSS 버전에 묶여 있어서,
  본체만 올리면 새로 생긴 글자가 번역되지 않은 채 영어로 섞여 나온다(§5.5).

### 5.5 화면 언어: 기본은 한국어, 구조는 다국어 (2026-09-15)

씬 에디터(D-80)와 같이 **기본은 한국어이고 폴백은 영어**다. 한국어를 소스에 직접 쓰지 않는다.
언어를 하나 더하는 일은 번역 파일을 더하는 것으로 끝나야 하고, 코드를 고치게 되면 이 절을 어긴 것이다.

화면에 나오는 글자는 출처가 셋이고, 번역하는 방법이 출처마다 다르다.

| 글자의 출처 | 예 | 번역하는 방법 |
|---|---|---|
| Code-OSS 본체 | 파일 메뉴, 설정 화면, 명령 팔레트 | 언어 팩 확장. 한국어 팩을 내장 확장으로 싣는다 |
| JBro 확장 | 명령 이름, 설정 설명, 알림 | 선언 쪽은 `package.nls.json`(영어, 폴백) + `package.nls.ko.json`, 실행 중 글자는 `vscode.l10n.t` + `l10n/bundle.l10n.ko.json` |
| `jbroc` 의 진단 | 에러 밑줄에 뜨는 메시지 | 편집기가 LSP 초기화 때 `locale` 을 넘기고, `jbroc` 이 그 언어로 메시지를 고른다. **엔진 리포의 요구 사항이다**(P2) |

규칙:

- **원문은 영어로 쓰고, 기본 표시는 한국어로 한다.** VS Code 확장은 기본 파일(`package.nls.json`)이
  곧 폴백이므로 영어를 거기에 둔다. D-80 에서 `TextOr` 가 영어를 들고 있는 것과 같은 모양이다.
- **처음 실행할 때 한국어로 연다. 이것은 코어 패치가 필요하다**(1.137.0 소스로 확인).
  `src/main.ts` 가 화면 언어를 고르는 순서는 ① `--locale` 인자 ② `argv.json` 의 `locale`
  ③ OS 언어 ④ 영어다. `argv.json` 은 **사용자 데이터 폴더가 아니라 홈 폴더**의
  `~/<dataFolderName>/argv.json` 에 있다(개발 실행이면 `-dev` 가 붙는다). `--user-data-dir` 를 바꿔도
  같은 파일을 쓰는 것을 실행으로 확인했다. 이 파일이 없을 때 새로 만드는 기본 내용은 `product.json` 이
  아니라 `createDefaultArgvConfigSync` 안에 **코드로 적혀 있다**(`main.ts:422`). 그러므로 OS 언어와
  무관하게 한국어로 열려면 그 기본 내용에 `"locale": "ko"` 를 넣는 패치가 필요하다. 화면에 새 글자를
  만들지 않는 패치라 아래 원칙과 부딪히지 않는다. 한국어 Windows 라면 패치 없이도 ③ 에서 한국어가 된다.
  씬 에디터와 언어 설정을 공유할 때(위) JBro 확장이 고쳐 쓸 대상도 이 파일이며, 바꾼 뒤에는 재시작이 필요하다.
- **내장 언어 팩은 첫 실행에 먹지 않을 가능성이 높다**(소스 + 실행 일부, 확신 약 80%).
  - **실행으로 확인한 것**: 언어 팩을 `extensions/` 에 내장 확장처럼 넣고 새 사용자 데이터 폴더로 띄우면,
    확장은 보이고(`vscode.extensions.getExtension` 이 찾는다) 사용자 데이터 폴더의 `languagepacks.json` 은
    **실행 도중에** `ko` 로 채워진다. 즉 내장 확장도 언어 팩으로 등록되지만, 그 파일은 실행 전에는 없다.
  - **소스로 확인한 것**: 화면 언어를 정하는 `resolveNLSConfiguration`(`src/vs/base/node/nls.ts`)은 창을
    열기 전에 `languagepacks.json` 을 읽고, 없으면 영어로 간다. 파일을 채우는 쪽은 그 뒤에 뜨는 공유
    프로세스다(`localizationsUpdater.ts`). 그러므로 **설치 직후 첫 실행은 영어, 두 번째부터 한국어**가 된다.
  - **실행으로 끝까지 재지 못한 이유**: 같은 함수가 `VSCODE_DEV` 가 켜져 있거나 `product.commit` 이 없으면
    언어 팩을 **무조건 무시한다**(`nls.ts:45-52`). 개발 실행은 둘 다 해당해서, 두 번째 실행도 영어였다.
    실제 확인은 릴리스 빌드(포크 빌드 단계)에서 한다.
  - **대비책**: 설치 프로그램이나 첫 실행 전 단계가 `languagepacks.json` 을 미리 만들어 두거나, 없을 때
    내장 확장을 훑는 코어 패치를 하나 더 둔다. 앞의 것이 코어를 건드리지 않는다.
- **Open VSX 의 한국어 팩은 upstream 보다 늦다.** 2026-09-15 기준 최신이 1.131.0(2026-07-28)이고
  Code-OSS 는 1.137.0 이다. 원본 리포 `microsoft/vscode-loc` 의 `main` 도 1.131.0 이다. 여섯 판 사이에 생긴
  글자는 영어로 나온다. 라이선스는 MIT(`LICENSE.md`)로 확인했다. VSIX 는 636 KB, 번역 묶음 94개다.
- **코어 패치는 새 글자를 만들지 않는 것을 원칙으로 한다.** 언어 팩은 upstream 의 글자만 번역하므로,
  패치가 더한 글자는 어느 팩에도 없어 영어로 나온다. 피할 수 없으면 한국어 팩을 JBro 쪽에서 고쳐 싣는데,
  그러면 유지할 대상이 하나 더 는다. UI 를 **지우는** 패치는 이 문제가 없다.
- **번역 누락은 테스트로 잡는다.** VS Code 는 번역이 없으면 조용히 영어로 떨어지므로 화면만 봐서는
  모른다. JBro 확장마다 `package.nls.json` 과 `package.nls.ko.json` 의 키 집합, l10n 번들의 키 집합이
  서로 같은지 확인한다.
- **MSVC 에러 메시지의 언어는 설치된 MSVC 가 정한다.** 편집기가 바꾸지 않는다.
- **씬 에디터와 화면 언어 설정을 공유한다(2026-09-15).** 한쪽에서 언어를 바꾸면 다른 쪽도 따른다.
  D-60 대로 둘 다 읽는 파일로 공유하며, 그 파일은 씬 에디터에 사용자 설정 파일이 생길 때 함께 정한다.
  그 전까지는 둘 다 기본 한국어라 어긋날 일이 없다.
  로케일 이름 체계가 다르므로 대응표가 필요하다. VS Code 는 `ko`·`en` 을, 씬 에디터는 `ko-KR`·`en-US` 를 쓴다.
  **파일에 적는 이름은 씬 에디터 쪽(`ko-KR`)으로 하는 안을 제안한다.** 엔진의 로케일 파일
  (`Localization/<로케일>.yaml`)이 이미 그 이름이고, 대응표를 아는 쪽을 편집기 확장 하나로 줄일 수 있다.

---

## 6. 단계

각 단계는 **완료 조건**과 **검증 방법**을 함께 적는다. 빌드 성공만으로 완료로 보지 않는다.

### P0. 스파이크 — 재고 나서 정한다

작업 위치는 `F:\Project\JBroSpike` 였다(Code-OSS 클론·휴대용 Node·LLVM·npm/Electron 캐시).
**스파이크를 닫으며 2026-09-15 에 지웠다.** 스파이크가 C: 에 만든 것(`~/.vscode-oss-dev`, `~/.vscode-oss-shared`,
node-gyp 캐시의 42.10.0·24.18.1·22.22.1)도 함께 지웠다. 포크 빌드 때는 §4.2 의 우회를 보고 다시 만든다.

| 항목 | 상태 (2026-09-15) |
|---|---|
| upstream Code-OSS 를 **수정 없이** 빌드하고 `scripts\code.bat` 로 띄운다. 디스크 사용량과 시간을 적는다 | **실측 완료.** 우회 셋(§4.2)을 넣고 `npm ci` 850초, 컴파일(`preLaunch`) 367초에 TypeScript 에러 0개. 창이 `Code - OSS Dev` 로 뜨고 렌더러 에러가 없다. 디스크 9.0 GB |
| node-gyp 와 VS 2026 의 조합 | **실측 완료.** 인식한다. `preinstall` 검사는 `vs2022_install` 없이 실패하는 것을 실행으로 확인했다. SDK 26100 결함은 이 기계의 문제다 |
| §4.1 `__FUNCSIG__` | **실측 완료. 깨진다** |
| `$msCompile` 문제 매처가 코어에 있는가 | **소스로 확인했다. 있다**(`src/vs/workbench/contrib/tasks/common/problemMatcher.ts:1945`). 생성 C++ 의 MSVC 에러는 `#line` 덕분에 `.jscript` 줄을 가리키므로(jbroscript-plan §5.2), `jbroc` 진단이 서기 전에도 빌드 에러를 문제 패널에서 받을 수 있다 |
| OS 언어와 무관하게 한국어로 뜨는 방법 | **소스로 확인했다. 코어 패치 한 줄이 필요하다**(§5.5). 내장 언어 팩은 첫 실행에 먹지 않을 가능성이 높다. **개발 실행은 언어 팩을 무조건 무시해서 끝까지 잴 수 없었고, 포크 빌드 단계로 넘긴다** |

**완료 조건**: 다섯 결과가 이 문서의 §3·§4·§5.5 에 기록되어 있다. **넷은 실측, 하나(언어 팩의 실제 적용)는
개발 실행의 한계로 포크 빌드 단계에 넘겼다(2026-09-15).** P0 는 이것으로 닫는다.

### P1. 문법 강조 확장 (`jbro-languages`)

- `.jscript` 언어 등록과 TextMate 문법(jbroscript-plan §13.2 ①). 문법은 jbroscript-plan §12 의
  1차 확정 범위만 덮는다. 표현식 문법은 아직 정해지지 않았기 때문이다(jbroscript-plan §12.8).
- `.jproject`·`.jcanvas`·`.jprefab` 을 YAML 로 연결한다.
- 일반 VS Code 에서 개발하고 테스트한다. 포크가 없어도 된다.

**완료 조건**: jbroscript-plan §12.1 의 예제 파일이 의도한 토큰으로 칠해진다.
**검증**: `vscode-tmgrammar-test` 스냅샷 테스트. 대괄호 어트리뷰트와 배열 인덱싱(`lines[0]`)이
서로 다른 토큰으로 갈리는 경우를 반드시 넣는다(jbroscript-plan §12.6).
이 확장이 화면에 내는 글자(언어 이름 설명 등)가 있으면 첫 확장부터 §5.5 의 번역 파일을 거친다.
키 집합 비교 테스트도 이 확장에서 처음 세운다. 나중 확장이 복사해 쓸 틀이 된다.

**완료했다(2026-09-15).** 편집기 리포 `F:\Project\JBroScriptEditor` 를 만들었고(원격 없음) `83c1151`·`d41a28e` 다.

- **언어 id 는 `jbroscript` 다.** 확장자는 `.jscript` 그대로지만, id 를 `jscript` 로 두면 JScript 를 뜻하는
  다른 확장과 겹칠 수 있다.
- **어트리뷰트는 줄을 여는 `[` 만이다.** jbroscript-plan §12.6 의 "선언 자리에서 `[` 로 시작할 수 있는 것은
  어트리뷰트뿐" 을 그대로 옮겼다. 인덱싱(`lines[0]`)과 배열 리터럴(`= [1, 2, 3]`)은 줄을 열지 않는다.
- **키워드는 타입 자리에 오지 못한다.** 그래서 `return speed` 가 선언으로 칠해지지 않는다(`var` 가 없는 대가, §12.4).
- **테스트**: `npm test` 가 번역 키 검사, 단언 45개, §12.1 예제의 스냅숏을 돌린다.
  **변이 8개를 모두 잡는다.** 어트리뷰트의 줄 시작 조건 제거, `return` 을 타입으로 허용, `bool` 을 기본 타입에서 제거,
  `let` 스코프 이름 변경, 중첩 제네릭 금지, 한국어 번역 누락·빈 값, 정의되지 않은 번역 키 사용.
- **테스트 도구의 명령을 쓰지 않고 라이브러리를 부른다.** 확인한 문제가 셋이다.
  ① Windows 의 Node 24.14.1 에서 도구가 `process.exit` 를 부를 때 libuv 가 중단해서, 통과하면 `0xC0000409`,
  실패하면 `-1` 이 나온다. 종료 코드로 둘을 가를 수 없다.
  ② 스냅숏 명령은 `.snap` 이 없으면 새로 쓰고 통과로 친다. 기록한 적 없는 스냅숏이 조용히 통과한다.
  ③ 음수 단언을 `- a - b` 처럼 두 번 이어 쓰면 도구의 해석기가 끝나지 않는다. `- a b` 로 쓴다.
- **YAML 연결은 에디터 안에서 확인하지 않았다.** `package.json` 의 선언만 있고, VS Code 에 확장을 올려 `.jcanvas`
  가 YAML 로 열리는지는 보지 않았다. 확인하려면 확장 개발 호스트로 창을 띄워야 한다.

**문법이 바뀌어서 갱신이 필요하다(2026-09-15).** 이 확장은 jbroscript-plan §12 의 1차 문법(`int`·`float`, `if let`,
괄호 없는 조건)만 안다. 그 뒤 [jbroscript-syntax.md](./jbroscript-syntax.md) 에서 정한 것을 더하고 옛 것을 빼야 한다.

- 더할 것: `class`·`struct`·`interface`·`enum`, `public`·`protected`·`private`·`static`·`const`, `ref`,
  `callback`·`override`·`require`(함수 끝에서만), `->`, `and`·`or`·`not`, `is null`·`is not null`,
  `else`·`for`·`in`·`while`·`switch`·`case`·`default`·`break`·`continue`, 엔진 타입 이름(`Int`·`Float`·`Bool`·`String`·`Vector2` …)
- 뺄 것: `if let`, 기본 타입으로 칠하던 `int`·`float`·`bool`
- 테스트의 예제와 스냅숏(`test/snap/tetris-game-manager.jscript`)도 새 문법으로 다시 쓴다

### P2. `.jscript` 진단 (jbroscript-plan §13.2 ②)

**선행 조건**: `jbroc` 의 렉서·파서·타입체커. 엔진 리포 작업이며 이 계획의 범위 밖이다.

- LSP 서버는 `jbroc --lsp`(엔진 리포)이고, 편집기 확장은 그 실행 파일을 띄우는 클라이언트뿐이다.
  **편집기 리포는 JBroScript 문법을 모른다.** 그래야 언어가 바뀌어도 편집기를 다시 배포하지 않는다.
  (P1 의 TextMate 문법은 예외다. 색칠은 LSP 가 아니라 정규식 파일이 한다.)
- 첫 단계는 저장할 때의 진단만 한다.
- 빌드 태스크: `jbroc` → MSBuild 를 부르고 에러를 문제 패널로 받는다.
- **진단 메시지는 번역된다.** 편집기는 LSP 초기화의 `locale` 에 화면 언어를 넣는다. `jbroc` 은
  메시지를 문장이 아니라 키로 들고 있다가 그 언어로 고르고, 없으면 영어를 낸다(§5.5).
  같은 메시지 표를 `jbroc` 을 명령줄로 돌릴 때도 쓴다. **`jbroc` 설계에 들어가야 하는 요구 사항이다.**

**완료 조건**: 타입 에러가 저장 시 빨간 줄로 뜨고, MSVC 까지 가서야 드러나는 에러가 문제 패널에서
구분된다(jbroscript-plan §5.4 "MSVC 까지 도달한 에러는 타입체커의 구멍").
**검증**: 확장 통합 테스트(`@vscode/test-electron`)가 샘플을 열고 정상 파일의 진단이 0 개인지,
일부러 넣은 타입 에러에서는 1 개 이상인지 본다. **0 만 확인하면 서버가 아예 안 돌아도 통과한다.**
같은 타입 에러를 `ko` 와 `en` 으로 각각 열어 메시지가 서로 다른지도 본다. 한쪽만 보면 번역이
아예 안 되어도 통과한다.

### P3. 씬 에디터와의 연결

D-60 대로 **파일로만 대화한다.** 자체 IPC 는 만들지 않는다.

- 씬 에디터 → 편집기: 인스펙터의 스크립트에서 "코드 열기" 를 누르면 편집기 CLI 의 `--goto 파일:줄` 로 연다.
- 편집기 → 씬 에디터: DLL 을 다시 빌드하면 씬 에디터가 알아챈다. 호스트에 파일 감시가 없으므로
  **엔진 쪽에 무엇을 둘지는 별도 결정**이다. 편집기는 빌드만 한다.

**완료 조건**: 씬 에디터에서 편집기가 해당 줄로 열린다. 편집기가 이미 떠 있으면 새 창을 만들지 않는다.

### P4. 자동완성·정의로 이동 (jbroscript-plan §13.2 ④)

`jbroc` 의 AST 가 선 뒤에 한다.

### P5. 디버깅

jbroscript-plan §18.7 을 따른다. 그때까지는 Visual Studio 로 한다.
`clang-cl` + DWARF + `lldb-dap` 을 jbroscript-plan §18.2 와 같은 방법으로 잰다.
**§4.1 이 깨진 채로 남아 있으면 이 경로는 컴파일 단계에서 막힌다.**

### 포크 빌드 (배포할 것이 생길 때)

- 편집기 리포(`JBroScriptEditor`)에 §5.2 의 세 층을 적용한다. 리포는 이미 있다.
- 빌드 스크립트: upstream 태그를 받아 패치를 차례대로 적용하고, `extensions/jbro-*` 를 소스 트리에 복사한 뒤 빌드한다.
  **upstream 은 `1.137.0` 을 `upstream/` 에 얕은 클론으로 받아 두었고(2026-09-16, 432 MB), `.gitignore` 에 넣어
  리포에 커밋하지 않는다.** 빌드 작업 폴더이지 관리 대상이 아니다.
- 패치 0001~0003 을 적용한다(§5.2 의 패치 목록).
- 확장 갤러리는 손댈 것이 없다(§5.3).
- 한국어 언어 팩을 내장 확장으로 싣고, 처음 실행 때 한국어로 열리게 한다(§5.5). 언어 팩 라이선스는
  MIT 로 확인했다. **릴리스 빌드에서 설치 직후 첫 실행이 한국어인지, `--locale=en` 으로 영어로 돌아오는지를
  이 단계에서 잰다.** 개발 실행으로는 잴 수 없었다.
- 홈 폴더에 생기는 폴더도 공존 검사에 넣는다. 개발 실행 한 번에 `~/.vscode-oss-dev`(`argv.json`)와
  `~/.vscode-oss-shared`(공유 저장소)가 `--user-data-dir` 와 무관하게 생겼다. 이름이 `dataFolderName`
  에서 나오므로 `.jbro-script-editor` 로 바꾸면 VS Code 와 겹치지 않는지 설치본에서 확인한다.
- 설치본(user setup)을 만든다.

**완료 조건**: VS Code 가 설치된 기계에 나란히 설치해도 설정 폴더·레지스트리·파일 연결·단일 인스턴스
뮤텍스가 서로 섞이지 않는다.
**검증**: 두 제품을 동시에 띄우고 각자 설정을 바꾼 뒤 다른 쪽에 반영되지 않는지 본다.
`--list-extensions --show-versions` 로 내장 확장이 실제로 실렸는지 본다.

---

## 7. 답이 필요한 질문

1. ~~코어 패치로 무엇을 바꿀 것인가.~~ **정했다(2026-09-16). 패치 셋이다(§5.2 의 패치 목록).**
   0001 첫 실행 한국어, 0002 채팅·AI 제거, 0003 `.vsix` 설치 차단.
   남은 세부 질문 둘은 포크 빌드를 시작하기 전에 정한다.
   - 0002 에서 `inlineCompletions` 를 뺄 것인가 남길 것인가. AI 전용이 아니라 인라인 제안의 일반 틀이고,
     나중에 `jbroc` 의 LSP 가 쓸 수 있다(§5.2).
   - 계정 메뉴(활동 표시줄 아래의 깃허브·마이크로소프트 로그인)를 기본에서 감출 것인가.
     사용자가 우클릭으로 감출 수 있으므로(`browser/parts/globalCompositeBar.ts:843`) 패치의 목적은 기본값을 바꾸는 것이다.
     채팅을 없애면 남는 쓰임은 Git·GitHub 확장의 저장소 접근뿐이다.
   - 메뉴 구조를 재편할 것인가. 빡대리가 생각 중이다(2026-09-16).

   **시작 화면(환영 탭)은 코어 패치 목록에서 뺐다.** 뜨지 않게만 하면 되고, 그것은 확장이
   `workbench.startupEditor` 의 기본값을 `none` 으로 기여하면 된다(`platform/extensions/common/extensions.ts:217`).
   그 자리에 JBro 만의 시작 화면을 넣기로 하면 그때 다시 패치 후보가 된다.
2. ~~씬 에디터와 화면 언어 설정을 공유할 것인가.~~ **공유한다(2026-09-15, §5.5).** 공유 파일의 위치와
   모양은 씬 에디터에 사용자 설정 파일이 생길 때 정한다.

## 8. 진행 현황과 남은 일 (2026-09-15)

**완성 날짜는 정하지 않았다.** 날짜보다 무엇이 남았고 무엇에 막혔는지를 적는다. 작업이 끝날 때마다 이 표를 고친다.

### 8.1 단계별 상태

| 단계 | 상태 | 크기 | 막힌 곳 |
|---|---|---|---|
| P0 스파이크 | **완료** | — | — |
| P1 문법 강조 | **완료(옛 문법)**, 갱신 필요 | 작음 | 없음. 바로 할 수 있다 |
| `jbroc` 렉서·파서 | 시작 전 | 중간 | 없음. 문법이 거의 확정됐다([jbroscript-syntax.md](./jbroscript-syntax.md)) |
| `jbroc` 타입체커·이미터 | 시작 전 | **큼** | §8.3 의 결정 1·2·3 |
| P2 에러 표시 | 시작 전 | 작음 | `jbroc` 타입체커 |
| P3 씬 에디터 연결 | 시작 전 | 작음 | 엔진에 스크립트 DLL 파일 감시가 없다 |
| P4 자동완성·정의로 이동 | 시작 전 | 중간 | `jbroc` AST, 엔진 함수 선언 표 |
| P5 디버깅 | 시작 전 | 중간 | `Field.h` 의 clang 서명 대응(§4.1), clang-cl + DWARF + `lldb-dap` 실측 |
| 포크 빌드(브랜딩·설치본) | 시작 전. **upstream 소스는 받아 두었다** | 중간 | 첫 실행이 영어로 뜨는 문제(§5.5). 코어 패치 목록은 정해졌고(§5.2) 세부 둘만 남았다(§7) |

편집기 쪽 일은 작다. **오래 걸리는 것은 컴파일러 `jbroc` 이다.** 에러 표시와 자동완성이 `jbroc` 의 파서와 타입 정보를
그대로 쓰므로(§6 P2·P4), `jbroc` 이 서는 속도가 곧 편집기의 속도다.

### 8.2 "다 만들었다" 의 두 기준

| 기준 | 들어가는 것 |
|---|---|
| **쓸 만한 편집기** | 새 문법 색칠(P1 갱신), 저장할 때 에러 표시(P2), 씬 에디터에서 코드 열기(P3). 이를 위해 `jbroc` 렉서·파서·타입체커 |
| **완성된 편집기** | 위에 더해 자동완성(P4), 디버깅(P5), 설치본(포크 빌드) |

### 8.3 속도를 좌우하는 결정

`jbroc` 의 타입체커와 이미터를 끝까지 만들려면 아래가 정해져야 한다. 셋 다 빡대리가 정한다.

1. 스스로 null 이 될 수 없는 대상을 멤버 `ref` 로 들고 있는 경우(jbroscript-syntax §12 의 1번, jbroc-rules §10 의 1번)
2. 자동 null 검사로 걸러진 뒤의 동작 — `return`, `if`/`else`, `while`(jbroscript-syntax §12 의 4번)
3. 엔진 함수 선언 표(jbroc-rules §7). 대기 중이며, 타입을 컴파일러가 뽑는 방식은 2026-09-15 에 프로브로 확인했다

### 8.4 권하는 순서

1. **P1 문법 강조 갱신.** 작고, 바로 눈으로 확인할 수 있다
2. **`jbroc` 렉서·파서.** 문법이 거의 확정돼 다시 만들 일이 적다
3. 그사이 §8.3 의 결정을 정리한다
4. 타입체커 → P2 → P3 으로 "쓸 만한 편집기" 에 닿는다

## 9. 기각한 안

- **씬 뷰를 편집기 웹뷰에 넣는 안**: D-60 에서 이미 기각했다. 매 프레임 렌더 결과를 복사해 넘기고
  입력을 되돌려받아야 하는데 얻는 것이 없다.
- **편집기와 씬 에디터 사이에 자체 IPC 를 두는 안**: 지금 필요한 두 방향(줄로 열기, 빌드 알림)은 CLI 인자와
  파일로 된다. IPC 는 양쪽 수명 순서를 계약으로 만든다.
- **C++ 편집 지원(clangd + `compile_commands.json`)**: 편집기를 `.jscript` 전용으로 정했다(2026-09-15).
  C++ 은 Visual Studio 에서 편집한다. MS C/C++ 확장은 포크에서 라이선스와 바이너리 환경 검사로 막혀 있어
  넣을 수도 없다(jbroscript-plan §18.7).
- **전체 브랜치 포크(§5.1 A)**: Code-OSS 는 매달 릴리스되고 워크벤치 내부가 자주 바뀐다. 코어에 섞인 기능은
  갱신할 때마다 다시 이식해야 하고, Electron·Chromium 보안 갱신이 그만큼 늦어진다.
- **포크 없이 VSCodium + 확장팩(§5.1 C)**: 엔진이 자기 편집기를 들고 가고, 파일 연결과 설정 폴더를 VS Code 와
  따로 두려면 제품 정체가 필요하다. 다만 확장 개발 단계는 이 방식과 같은 환경에서 한다.

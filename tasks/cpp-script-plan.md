# C++ 게임 스크립트 지원 계획

> 결정은 [todo.md](./todo.md) 의 D-263 이다. JBroScript 컴파일러 `jbroc` 을 렉서·파서에서 멈추고, 사용자가 C++ 로 게임 스크립트를
> 쓰고 빌드하고 핫 리로드하는 길을 먼저 세운다. 스크립트 DLL 의 ABI 와 수명 계약은 [ProjectRule.md](../docs/ProjectRule.md) §5·§6.2 에 있다.
> 확정 계약이 아니라 계획이므로 여기에 둔다. 단계마다 새 서비스·새 공개 타입이 들어가는 자리는 진행 전에 확인을 받는다.

## 1. 기존 엔진

`Engine/Editor/LiveCompile/`(`LiveCompileManager`·`CompilePipeline`·`MSBuildLocator`), `Engine/Editor/Project/GameScriptProjectGenerator.cpp`,
`SDK/Templates/GameScript.vcxproj.template`, `Samples/GameScriptSample/`.

- 에디터가 스크립트 소스 폴더를 감시하고, 바뀌면 MSBuild 로 비동기 빌드한다. 산출물은 빌드마다 번호를 붙인 DLL·PDB 로 내고 오래된 것은 열 개만 남긴다.
- 새 DLL 을 싣기 전에 스크립트 인스턴스의 필드 값을 리플렉션으로 떠 두고(`ScriptFieldSnapshot`, 오브젝트 GUID·타입 이름으로 찾는다) 실은 뒤 되살린다.
- 빌드 직전마다 프로젝트 생성기가 `vcxproj` 를 다시 쓰고, 헤더의 `JPROP` 을 **정규식으로 읽어** 스크립트 레지스트리 소스를 생성한다.
- 진입점은 `CreateGameModule`·`DestroyGameModule` 두 C 심볼이고, 샘플의 `GameModuleEntry.h` 가 선언한다.
- 소스 자리는 프로젝트 폴더의 `Contents/` 다. 거기에 `GameScript.sln`·`GameScript.vcxproj` 가 있고 스크립트 `.h`/`.cpp` 는 `Contents/Scripts/` 에 있다
  (`TestProject/Test` 로 확인). `.jproject` 의 `ScriptSourceDirectory: Contents` 가 그 자리를 가리킨다.

**이 구조는 참고만 하고 모방하지 않는다**(2026-09-29 사용자 지시). 조금만 다르게 개발돼도 컴파일이 안 되는 버그가 있었다(아래 C3).

아팠던 것:

- **C1. 정규식으로 읽은 등록.** 생성기가 스크립트를 못 읽으면 레지스트리가 비어 "등록된 스크립트 없음" 이 떴다(생성기 주석). 지금 엔진은
  `JBRO_FIELD` 리플렉션과 `RegisterScriptType2D` 로 등록을 코드 안에 두고, 코드 생성을 금지한다(ProjectRule §6 "스크립트별 핸들 타입을 코드 생성으로 만들지 않는다").
- **C2. 빌드마다 프로젝트 파일을 다시 쓴다.** 사용자가 `vcxproj` 에 넣은 설정이 사라지고, 생성기가 곧 빌드 시스템이 된다.
- **C3. 조금만 다르게 써도 컴파일이 안 됐다**(사용자 증언). 생성기가 정한 모양(정규식이 읽는 선언 모양, 생성 레지스트리가 기대하는 헤더)에서
  벗어난 코드는 빌드가 깨졌다. 그래서 여기서는 **사용자 코드의 모양을 읽는 도구를 빌드 경로에 두지 않는다** - 빌드는 사용자가 가진 `vcxproj` 를
  MSBuild 가 그대로 빌드하는 것뿐이고, 등록과 필드는 컴파일러가 보는 C++ 코드(`RegisterScriptType2D`·`JBRO_FIELD`)에만 있다.

## 2. 지금 엔진에 있는 것과 없는 것 (2026-09-29, 코드로 확인)

| 있는 것 | 자리 |
|---|---|
| 단일 C 진입점 `JBroScriptModule_GetApi` 와 판번호 검사 | `Runtime/ScriptModule.h`, ProjectRule §6.2 |
| DLL 을 섀도 복사본으로 싣고, `Reload` 로 갈아 끼우는 로더 | `Host/ScriptDLLLoader.h`(D-39) |
| DLL 없이도 프로젝트가 열린다 | D-98 |
| 스크립트 프로젝트용 include 경로 묶음 | `JBro.Common.props` 의 `JBroScriptIncludes`, `JBro.Script.props` |
| 소스·산출물 자리 키. 기본값이 기존 엔진과 같다 | `.jproject` 의 `ScriptSourceDirectory`(기본 `Contents`)·`ScriptOutputLibraryPath`(기본 `x64/Debug/GameScript.dll`), `Host/ProjectFile.h` |
| 2D 스크립트 베이스·훅·등록 | `GameScript2D`, `RegisterScriptType2D`(D-207) |

| 없는 것 | 확인한 곳 |
|---|---|
| **진입점 보일러플레이트를 엔진이 대신 해 주는 길.** 시험용 DLL(`Tests/ScriptModuleProbe/Source/Probe.cpp`)은 컨텍스트 검증, 모듈마다의 `Bind*Context` 여덟 번과 해제 여덟 번, `GetApi` 내보내기를 손으로 한다. 서비스 모듈이 늘면 모든 사용자 DLL 을 고쳐야 하고, 하나를 빠뜨려도 컴파일은 된다 | `Probe.cpp` 59~165 줄 |
| **사용자 스크립트 프로젝트를 만드는 곳.** 런처도 에디터도 `vcxproj` 를 만들지 않는다. 스크립트 DLL 을 만드는 프로젝트는 시험용 둘뿐이다 | `source/JBroLauncher`, `Tests/ScriptModuleProbe/*.vcxproj` |
| **에디터에서 빌드하기** | 엔진 모듈에 MSBuild 를 부르는 코드가 없다 |
| **핫 리로드를 거는 곳.** `ScriptDLLLoader::Reload` 를 호스트·에디터가 부르지 않는다. 폴더 감시(`WatchDirectory`)는 에셋 폴더에만 걸려 있다 | `EngineInstance.cpp` |
| **스크립트가 오브젝트를 만들고 찾는 길.** `PrefabSpawner::Spawn` 은 선언만 있고 정의가 없으며, 스크립트가 그것을 얻을 자리도 없다 | `Framework2D/Prefab/Prefab.h` |
| **`ReadOnly` 필드를 스크립트가 못 쓰게 하는 것.** `Transform2D` 의 월드 캐시(`worldPosition` 등)는 `ReadOnly()` 지만 이 표시는 인스펙터만 본다 | `Framework2D/Component/Transform2D.h` |
| 3D 스크립트 베이스·충돌 훅 | 3D 프렐류드에 `GameScript3D` 가 없다. 3D 는 2D 뒤의 순서다(D-116) |

**고치지 않는 것.** `Ref<T>::operator bool` 이 "설정됨" 만 보고 `operator->` 가 무효에서 멈추는 것은 결함이 아니라 계약이다(ProjectRule §6 의 `Ref<T>` 접근자 절).
2026-09-29 분석에서 `GameObjectHandle` 과 맞추자고 했던 것은 그 계약을 못 본 제안이라 거둔다.

## 3. 단계

### 3.1 진입점을 한 줄로

사용자 DLL 은 `JBRO_SCRIPT_MODULE_2D()` 같은 한 줄로 진입점을 낸다. 컨텍스트 검증·바인딩·해제 목록은 엔진 쪽 한 곳에 두어,
서비스 모듈이 늘어도 사용자 코드가 바뀌지 않게 한다. 스크립트 타입 등록(`RegisterScriptType2D`)은 사용자가 하던 대로 둔다.

- 완료 조건: `Probe.cpp` 가 손으로 하던 바인딩을 이것으로 바꿔도 기존 스크립트 모듈 시험이 모두 통과한다.
- 검증: 바인딩 목록에서 서비스 하나를 빼는 뮤테이션을 시험이 잡는다. 2D 매크로를 3D 프렐류드에서 쓰면 컴파일이 실패한다(음성 시험).
- 확인이 필요한 것: 매크로 이름과, 구현을 헤더에 둘지 정적 라이브러리에 둘지.

### 3.2 사용자 스크립트 프로젝트

**소스 자리는 기존 엔진과 같다**(2026-09-29 사용자 지시): `<프로젝트>/Contents/GameScript.sln`·`GameScript.vcxproj`, 스크립트는 `Contents/Scripts/`,
산출물은 `x64/Debug/GameScript.dll`. `.jproject` 의 두 키가 이미 이 기본값이다. 새 프로젝트를 만들 때 이것을 **한 번만** 만든다.
설정은 엔진이 주는 `.props` 하나로 가져와서, 프로젝트 파일을 다시 쓰지 않아도 엔진 쪽 경로가 바뀌면 따라가게 한다(C2).
반대 차원의 include 경로는 넣지 않는다(D-15). 기존 엔진의 생성 레지스트리(`GeneratedScriptRegistry.*`)·`GameModule.cpp` 같은 생성 파일은 두지 않는다(C1·C3).

- 완료 조건: 빈 프로젝트를 만들고 스크립트 하나를 더해 빌드하면 에디터가 그 DLL 을 싣고 스크립트를 컴포넌트로 붙일 수 있다.
- **만드는 곳은 에디터다**(2026-09-29 사용자 지시: "에디터에서 할거야. 스크립트를 만들려면 어떤 프로젝트에서 만들지 알아야하니까"). 런처는 만들지 않는다.
- 검증: 만들어진 프로젝트의 `.cpp` 에 선언 모양을 바꾼 스크립트(예: 여러 줄에 걸친 클래스 머리, 매크로로 감싼 선언)를 넣어도 빌드와 등록이 된다(C3 의 음성 시험).

### 3.3 에디터에서 빌드

에디터가 MSBuild 를 찾아(Visual Studio 설치 위치) 스크립트 프로젝트를 빌드하고, 진단을 에디터 로그에 파일·줄과 함께 낸다. 새 스크립트 파일(`.h`/`.cpp`)을
틀에서 만들고, Visual Studio 로 여는 메뉴를 둔다. 빌드는 워커가 아니라 자식 프로세스이고, 에디터는 끝을 기다리지 않고 프레임을 계속 돈다.

- 완료 조건: 에디터 메뉴로 빌드하면 성공과 실패가 로그에 보이고, 실패한 줄로 이동할 수 있다.

### 3.4 핫 리로드

빌드가 끝나면 `ScriptDLLLoader::Reload` 로 갈아 끼운다. 스크립트 필드 값은 리플렉션(`JBRO_FIELD`)으로 떠 두었다가 되살리고, 가리키는 것은
`InstanceId` 와 타입 이름이다. 소스 감시로 저절로 빌드하는 것은 기존 엔진을 따르지 않고 따로 정한다. 재생 중에는 갈아 끼우지 않고 재생이 끝난 뒤로 미룬다 - 이것은 확인이 필요하다.

- 완료 조건: 필드를 바꾼 스크립트를 다시 빌드하면 에디터를 끄지 않고 새 동작이 돌고, 인스펙터 값과 `Ref<T>` 가 이어진다(ProjectRule §6 의 핫 리로드 MUST).

### 3.5 스크립트 API 의 구멍

- 오브젝트 만들기·찾기: `PrefabSpawner` 를 구현하고 스크립트가 얻는 자리를 정한다. **새 서비스라 확인 뒤에 한다.**
- `ReadOnly` 필드: 월드 캐시를 스크립트가 쓰지 못하게 한다(getter 뒤로 옮기거나 쓰기를 컴파일 에러로 만든다).

### 3.6 3D

`GameScript3D` 와 충돌 훅. 3D 물리가 없으므로 [todo-3d.md](./todo-3d.md) 의 순서를 따른다.

## 4. 열림

- `[열림]` 3.1 의 매크로 이름과 구현 자리.
- ~~`[열림]` 3.2 의 프로젝트를 만드는 곳~~ 에디터다(2026-09-29). 폴더 자리도 정했다(기존 엔진과 같다).
- `[열림]` 스크립트 한 개를 파일 하나로 쓰는 방법(헤더·소스 나누기를 없애는 것, 2026-09-29 사용자 요청). 방안을 제안했고 답을 기다린다.
- `[열림]` 3.4 의 재생 중 리로드 정책.
- `[열림]` 3.5 의 오브젝트 만들기·찾기 서비스의 모양.

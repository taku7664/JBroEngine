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

### 2.1 에디터·캔버스 쪽 연결 (2026-09-29, 코드로 확인)

스크립트 DLL 이 실려도 **에디터 안에서 스크립트를 붙이고 저장하고 재생하는 길이 끊겨 있었다.** 가장 먼저 이어야 할 곳이었다.
**2026-09-29 에 §3.1 로 이었다(D-264).** 아래 표는 그 전의 상태다.

| 끊긴 곳 | 확인한 것 | 결과 |
|---|---|---|
| **스크립트 프로퍼티 표가 호스트에 오지 않는다** | `PropertyRegistry` 에 스크립트 표(`ScriptLocal`·`Script`·`BindScript`·`RegisterScript`)가 있지만, 호스트도 로드 컨텍스트도 `BindScript` 를 부르지 않고 `RegisterScript` 를 부르는 곳도 없다. `ScriptTypeInfo` 에는 크기·생성·파괴·입력만 있고 필드가 없다 | 인스펙터에 스크립트 필드가 안 나온다 |
| **스크립트가 붙은 캔버스를 저장하지 못한다** | `CanvasFile.cpp` 의 `WriteComponent` 가 `PropertyRegistry::Lookup` 이 비면 "this component type never registered its properties" 로 실패한다. 시험 `EditorObjectCommandTests.cpp` 도 "`AttachScript` 가 붙인 것도 `GetComponents()` 에 들어오는데 리플렉션 표는 없다" 고 적는다 | 저장이 실패한다. 에디터 재생(`StartSimulation`)도 같은 `WriteCanvasText` 로 스냅숏을 떠서 **스크립트가 붙으면 재생이 시작되지 않는 것으로 읽힌다**(코드로 읽었고 실행으로 재지 않았다) |
| **캔버스 파일이 스크립트 타입을 모른다** | 읽기가 `ComponentRegistry`(빌트인)만 찾고, 없으면 "this engine has no component by that name" 으로 캔버스 전체를 실패시킨다. `ScriptRegistry` 를 보지 않는다 | 스크립트를 저장할 수 있게 되어도 다시 못 연다. **DLL 을 아직 못 실은 프로젝트(D-98)는 스크립트가 든 캔버스를 통째로 못 연다** - 모르는 스크립트의 저장된 값을 그대로 들고 있다가 다시 쓰는 자리가 필요하다 |
| **컴포넌트 추가 메뉴에 스크립트가 없다** | `EditorActions.cpp` 가 `ComponentRegistry::CollectTypes()` 만 모은다. 에디터 소스에 `ScriptRegistry` 가 없다 | 사용자가 스크립트를 붙일 수 없다 |
| 되돌리기 | "되살릴 값을 먼저 뜨지 못했으면 지우지 않는다"(CLAUDE.md) 는 값을 리플렉션으로 뜬다 | 프로퍼티 표(`PropertyTable`)가 없으면 스크립트가 붙은 오브젝트의 삭제·컴포넌트 제거가 막히거나 값을 잃는다. 표가 서면 같이 풀린다 - 확인할 것 |

이 다섯은 뿌리가 하나다: **스크립트 타입의 프로퍼티 표(`PropertyTable`)를 DLL 에서 호스트로 넘기고, 내릴 때 거두는 길**. `ScriptRegistry` 가 이미 하는 일
(DLL 이 로드 때 호스트의 표에 등록하고 `Clear` 로 거둔다)을 프로퍼티 표(`PropertyTable`)에도 하면 된다. 표에는 DLL 코드를 가리키는 함수 포인터가 들어 있으므로
**내릴 때 거두지 않으면 사라진 코드를 부른다.**

### 2.2 프로젝트 밖에서 빌드하는 쪽 연결

| 끊긴 곳 | 확인한 것 |
|---|---|
| ~~**엔진 리포 밖의 스크립트 프로젝트가 엔진을 찾을 길**~~ `JBroEngine.props`·`JBRO_ENGINE_ROOT` 로 섰다(D-266). 설치본의 SDK 모양은 남았다 | `JBro.Script.props` 는 `JBro.Common.props` 를 같은 폴더에서 가져오고, 시험 DLL 은 엔진 모듈 `vcxproj` 를 `ProjectReference` 로 끌어와 링크한다. 사용자 프로젝트는 엔진 소스 옆에 있지 않으므로 **엔진이 어디 있는지(헤더·정적 라이브러리)를 알려 줄 자리**가 필요하다. 에디터가 프로젝트를 만들 때 자기 위치를 적어 넣거나, 엔진이 정해진 자리에 `.props` 와 `.lib` 를 둔다 |
| **게임 빌드의 DLL 구성** | `GameBuild.cpp` 는 `ScriptOutputLibraryPath`(기본 `x64/Debug/GameScript.dll`)를 그대로 복사한다. 내보낸 게임에 Debug DLL 이 실린다. 게임 빌드가 Release 로 스크립트를 다시 빌드하는지 정해야 한다 |
| 디버깅 | 섀도 복사본(D-39)으로 실어도 Visual Studio 가 원본 PDB 를 찾아 중단점이 걸리는지 재지 않았다 |

**고치지 않는 것.** `Ref<T>::operator bool` 이 "설정됨" 만 보고 `operator->` 가 무효에서 멈추는 것은 결함이 아니라 계약이다(ProjectRule §6 의 `Ref<T>` 접근자 절).
2026-09-29 분석에서 `GameObjectHandle` 과 맞추자고 했던 것은 그 계약을 못 본 제안이라 거둔다.

## 3. 단계

**순서는 [제안]이다**(2026-09-29 Claude 가 코드 분석 뒤 정했고 사용자 확인 전이다). 3.1 을 맨 앞에 둔 까닭은 §2.1 - 이것이 없으면
에디터에서 스크립트를 붙이고 저장하고 재생하는 길이 모두 끊긴다.

### 3.1 스크립트 프로퍼티 표를 호스트로 (§2.1 의 뿌리)

DLL 이 로드 때 스크립트 타입마다 프로퍼티 표(`PropertyTable`)를 호스트의 스크립트 표(`PropertyRegistry::Script`)에 등록하고, 내릴 때 거둔다.
그 위에서 캔버스 파일의 읽기·쓰기가 스크립트 타입을 찾고, 컴포넌트 추가 메뉴가 스크립트를 보이며, 인스펙터가 필드를 보인다.
DLL 이 없거나 그 타입이 없어진 스크립트는 **저장된 값을 그대로 들고 있다가 다시 쓴다**(지우지도 캔버스를 막지도 않는다).

- 완료 조건: 시험 DLL 의 스크립트를 붙인 캔버스를 저장하고 다시 열면 필드 값이 같다. 그 캔버스로 재생을 시작하고 멈출 수 있다.
  DLL 없이 열어도 캔버스가 열리고, 다시 저장해도 스크립트 값이 남는다. DLL 을 내리면 스크립트 표가 빈다.
- 검증: 거두기를 빼는 뮤테이션, 모르는 스크립트 값을 버리는 뮤테이션을 시험이 잡는다.
- ~~확인이 필요한 것: 모르는 스크립트를 들고 있는 모양~~ 캔버스 쪽 자료(`UnresolvedComponent`)다(D-264 (3)).

**진행 (2026-09-29, D-264).** 섰다.

- DLL 경계: 로드 컨텍스트 ABI 6(80 바이트)의 `Properties`, `BindScriptModuleContexts` 의 `PropertyRegistry::BindScript`,
  `RegisterScriptType<T>` 의 두 표 등록, 로더의 `ClearModuleTables`(언로드·로드 실패).
- 캔버스: `ComponentRegistry::FindAttachable`·`CollectAttachableTypes`, `Canvas::DetachScript`·`AddUnresolvedComponent`·`FindUnresolvedComponents`,
  `WriteYamlNode`, 캔버스 파일의 스크립트 읽기와 모르는 컴포넌트 보관·되쓰기·`Package` 거절.
- 에디터: 추가 목록의 `Script` 갈래, 추가/제거 커맨드와 나무 스냅숏이 `FindAttachable` 을 쓴다. 나무 스냅숏이 모르는 컴포넌트를 뜬다(`Capture` 가 캔버스를 받는다).
  인스펙터가 모르는 컴포넌트를 타입 이름 섹션과 안내 한 줄로 보인다.

검증(시험 `ScriptDLLLoaderTests`·`CanvasFileTests`·`EditorObjectCommandTests`):

- 실제 시험 DLL 의 스크립트가 필드 표를 호스트에 등록하고, 그 스크립트를 붙인 캔버스를 저장했다 다시 열면 값(`Speed` 7.25)이 같다. 재로드 뒤 표가 새 DLL 것으로 바뀌고,
  언로드 뒤 두 표가 빈다. DLL 을 내린 채로 같은 캔버스를 열면 스크립트가 보관되고, 다시 저장한 글자가 처음 것과 같다.
- 가짜 모듈이 등록한 뒤 `Load` 에서 실패해도 두 표에 남는 것이 없다.
- 모르는 컴포넌트 둘(맨 앞·빌트인 사이, 맵·나열·빈 글자·빈 나열을 담은 것)이 읽은 그대로 같은 자리에 되쓰이고, `Package` 쓰기가 거절하며, 오브젝트를 지우면 함께 사라진다.
- 에디터 커맨드로 스크립트를 붙이고, 떼고 되돌리고, 오브젝트를 지우고 되돌려도 값이 돌아온다. 모르는 컴포넌트도 지우기 되돌리기에서 돌아온다. 추가 목록은 스크립트를 빌트인 뒤에 보인다.
- 뮤테이션: 변이 16 개가 모두 잡혔다 - 두 표 거두기(언로드·로드 실패)·DLL 쪽 묶기·두 표 등록·모르는 컴포넌트의 보관·자리·내용·`Package` 거절·오브젝트와 함께 지우기·스냅숏 뜨기와 되살리기·`FindAttachable` 의 스크립트·추가 목록·커맨드·스냅숏 되살리기의 조회·스크립트 떼기. 모두 변이마다 그 자리를 보는 단언의 실패 메시지로 확인했다(시험을 빌드 포함 변이당 4~40 초로 추려 돌렸다)

남은 것:

- 스크립트가 붙은 캔버스의 재생 시작·멈춤을 **실제 에디터**로 재 보지 않았다. 막던 원인(`WriteCanvasText` 실패)은 시험으로 풀린 것을 봤다.
- ~~DLL 을 새로 실은 뒤 이미 열린 캔버스의 보관 컴포넌트를 스크립트로 되살리는 일은 3.5 와 함께 한다.~~ 3.5 에서 섰다(D-268 (8)).
- 스크립트 타입이 `GameScript2D` 를 첫 기반으로 두지 않고 다중 상속하면 필드 접근자가 받는 주소가 어긋날 수 있다(빌트인도 같은 가정이다). 재지 않았다.

### 3.2 진입점을 한 줄로

사용자 DLL 은 `JBRO_SCRIPT_MODULE_2D()` 같은 한 줄로 진입점을 낸다. 컨텍스트 검증·바인딩·해제 목록은 엔진 쪽 한 곳에 두어,
서비스 모듈이 늘어도 사용자 코드가 바뀌지 않게 한다. 스크립트 타입 등록(`RegisterScriptType2D`)이 3.1 의 프로퍼티 표(`PropertyTable`) 등록까지 함께 하게 해서
사용자가 두 번 적지 않게 한다. 파일은 기존 엔진처럼 `.h`/`.cpp` 로 나눈다(2026-09-29 사용자 지시).

- 완료 조건: `Probe.cpp` 가 손으로 하던 바인딩을 이것으로 바꿔도 기존 스크립트 모듈 시험이 모두 통과한다.
- 검증: 바인딩 목록에서 서비스 하나를 빼는 뮤테이션을 시험이 잡는다. 2D 매크로를 3D 프렐류드에서 쓰면 컴파일이 실패한다(음성 시험).
- ~~확인이 필요한 것: 매크로 이름과, 구현을 헤더에 둘지 정적 라이브러리에 둘지.~~ 정했다(D-265).

**진행 (2026-09-29, D-265).** 섰다. 사용자가 쓰는 모양:

```cpp
// Contents/Scripts/Player.h
class Player final : public JBro::GameScript2D
{
    JBRO_SCRIPT_BODY(Player)
public:
    void OnUpdate() override;
    JBRO_FIELD(float, Speed) = 3.0f;
};

// Contents/Scripts/Player.cpp
JBRO_REGISTER_SCRIPT_2D(Player);

// Contents/Scripts/ScriptModule.cpp (프로젝트에 하나, 3.3 에서 에디터가 만든다)
JBRO_SCRIPT_MODULE_2D()
```

- 구현: `Runtime/ScriptRegistry.h` 의 `JBRO_SCRIPT_BODY`·`ScriptTypeRegistration`·`RegisterPendingScriptTypes`, `Framework2D`/`3D` 의 `Scripting/ScriptModule.h`·`.cpp`,
  3D 의 `Scripting/GameScript.h`(`RegisterScriptType3D`, 3D 프렐류드가 include 한다). `Probe.cpp` 가 손으로 하던 진입점·바인딩 약 90 줄을 두 줄로 바꿨다.
- 검증: 시험 DLL 을 호스트가 열었을 때 호스트가 넘긴 컨텍스트 열 개(입력·세이브·로컬라이징·오디오·네트워크의 서비스와 시스템)가 DLL 사본과 바이트로 같다.
  3D API 가 3D 블록 둘을 요구하고, 미뤄 둔 등록이 부를 때 두 표에 들어가며 두 번째 등록은 거절된다(`ScriptModuleEntryTests`). 2D 프로젝트에서 3D 진입점 헤더는 C1083(음성).
- 뮤테이션: 네트워크·로컬라이징 시스템 바인딩 빼기, `Load` 의 등록 빼기, 목록에 걸기 빼기, 등록 거절 무시, 3D 요구 블록 바꾸기, `JBRO_SCRIPT_BODY` 가 모든 타입에 같은 이름을 주기가 잡혔다.
  오디오·입력·세이브 **서비스** 컨텍스트와 2D 서비스 컨텍스트 바인딩 빼기는 살아남았다 - 그 서비스 객체들에는 멤버가 없어(헤더로 확인) 묶든 안 묶든 DLL 쪽 바이트가 같다.
  같은 동작의 변이로 보고, 상태를 드는 시스템 컨텍스트 쪽 검사로 갈음한다.
- 남은 것: 3D 는 시험 DLL 이 없어 `Load` 를 실제로 부르지 않았다(호스트 프로세스에서 부르면 공통 컨텍스트를 비운다). 3D 게임 스크립트를 쓰기 시작할 때 3D 시험 DLL 을 둔다.

### 3.3 사용자 스크립트 프로젝트

**소스 자리는 기존 엔진과 같다**(2026-09-29 사용자 지시): `<프로젝트>/Contents/GameScript.sln`·`GameScript.vcxproj`, 스크립트는 `Contents/Scripts/`,
산출물은 `x64/Debug/GameScript.dll`. `.jproject` 의 두 키가 이미 이 기본값이다. 새 프로젝트를 만들 때 이것을 **한 번만** 만든다.
설정은 엔진이 주는 `.props` 하나로 가져와서, 프로젝트 파일을 다시 쓰지 않아도 엔진 쪽 경로가 바뀌면 따라가게 한다(C2).
반대 차원의 include 경로는 넣지 않는다(D-15). 기존 엔진의 생성 레지스트리(`GeneratedScriptRegistry.*`)·`GameModule.cpp` 같은 생성 파일은 두지 않는다(C1·C3).

- 완료 조건: 빈 프로젝트를 만들고 스크립트 하나를 더해 빌드하면 에디터가 그 DLL 을 싣고 스크립트를 컴포넌트로 붙일 수 있다.
- **만드는 곳은 에디터다**(2026-09-29 사용자 지시: "에디터에서 할거야. 스크립트를 만들려면 어떤 프로젝트에서 만들지 알아야하니까"). 런처는 만들지 않는다.
- 검증: 만들어진 프로젝트의 `.cpp` 에 선언 모양을 바꾼 스크립트(예: 여러 줄에 걸친 클래스 머리, 매크로로 감싼 선언)를 넣어도 빌드와 등록이 된다(C3 의 음성 시험).

**진행 (2026-09-29, D-266).** 섰다.

- 엔진: `source/JBroEngine/JBro.GameScript.props`(Common.props 를 들이고 OutDir·IntDir 을 프로젝트 안으로), `JBro.GameScript.targets`(스크립트 include·정의·링크 목록,
  엔진 라이브러리가 없을 때의 안내 오류).
- 에디터: `ScriptProject`(글자 만들기·이름 검사·`EnsureProject`·`RefreshEngineProps`·`FindEngineRoot`·`CreateScript`), `EditorApplication` 의
  `GetScriptRoot`·`CreateScript`·`CheckScriptName`·`CollectScriptFiles`·`OpenScriptFile`·`OpenNewScriptPopup`, 프로젝트를 열 때의 `JBroEngine.props` 갱신,
  `NewScriptPopup`(이름 + 접힌 `고급 옵션` 의 필드 목록), 에셋 브라우저의 `스크립트` 뿌리와 오른쪽 칸(폴더·소스 파일, 두 번 눌러 열기, 우클릭 `새 스크립트`·탐색기에서 보기·새로 고침).
- 팝업 관리자는 **이미 있다**(`EditorPopup`·`EditorApplication::OpenPopup`, 기존 `ImPopupDesc` 를 옮긴 것). 임시 구현은 하지 않았다.
- 검증(`ScriptProjectTests`): 만들어지는 글자(훅 다섯·필드 기본값·2D/3D·`Vector3` 의 include), 이름 검사(빈 것·식별자·예약어·로드된 스크립트·그 폴더의 파일, 겹치면 아무것도 안 씀),
  프로젝트를 두 번 불러도 사용자가 고친 vcxproj·진입점·`.gitignore` 를 덮지 않고 GUID 가 어긋나는 솔루션을 쓰지 않음, `JBroEngine.props` 는 같으면 쓰지 않고 옮기면 고침,
  개발 빌드가 엔진 폴더를 찾음, 에디터가 스크립트 프로젝트 없는 프로젝트에 아무것도 쓰지 않다가 처음 스크립트를 만들 때 전부 세움, 새 스크립트 창이 에디터 프레임에서 뜸.
  뮤테이션 11 개가 모두 잡혔다. MSBuild 실측은 D-266 (6).

**남은 것 (담당자에게 넘길 것).**

- 스크립트 자리의 파일 다루기: 이름 바꾸기·지우기·옮기기·새 폴더가 없다. 이름을 바꾸면 클래스 이름·`JBRO_SCRIPT_BODY`·등록 줄까지 바꿔야 하고,
  캔버스 파일의 타입 이름이 따라가지 않으면 그 스크립트는 모르는 컴포넌트가 된다(D-264) - 그 정책부터 정한다.
- 스크립트 폴더 감시: 에셋 폴더와 달리 감시하지 않는다. VS 에서 더한 파일은 `스크립트` 뿌리를 누르거나 `새로 고침` 해야 보인다.
- 스크립트 자리의 아이콘 보기·정렬·검색 칸 밖의 필터: 에셋 쪽 기능을 따르지 않았다(목록 한 가지뿐이다).
- 필드 타입 넓히기: 배열·표·enum·컴포넌트 참조(`Ref<T>` 의 리플렉션 설명자가 먼저다).
- Visual Studio 솔루션 탐색기에서 파일을 더하면 VS 가 vcxproj 의 와일드카드를 풀어 쓸 수 있다고 알려져 있다. **재지 않았다.** 풀어 쓰면 파일 목록이 박혀
  에디터가 만든 새 스크립트가 빌드에서 빠진다.
- 엔진 설치본의 SDK 모양(헤더·`.lib`·`JBro.GameScript.props` 를 설치 폴더에 두는 것). 지금은 개발 빌드에서만 스크립트를 만들 수 있다.

### 3.4 에디터에서 빌드

에디터가 MSBuild 를 찾아(Visual Studio 설치 위치) 스크립트 프로젝트를 빌드하고, 진단을 에디터 로그에 파일·줄과 함께 낸다. 새 스크립트 파일을 만드는 일은
3.3 에서 섰다(vcxproj 는 와일드카드라 파일 목록을 더하지 않는다). 빌드는 워커가 아니라 자식 프로세스이고, 에디터는 끝을 기다리지 않고 프레임을 계속 돈다.

- 완료 조건: 에디터 메뉴로 빌드하면 성공과 실패가 로그에 보이고, 실패한 줄로 이동할 수 있다.

**진행 (2026-09-29, D-267).** 섰다. 진단을 보일 곳은 새 `빌드 결과` 패널이고, 여는 편집기는 에디터 설정에서 고른다(두 가지 모두 사용자가 골랐다).

- 플랫폼: `IPlatform` 의 `StartProcess`·`PollProcess`·`CloseProcess`(Job 으로 묶어 닫으면 띄운 것까지 끝낸다)·`LaunchProcess`(띄우고 잊는다)·`ReadEnvironmentVariable`,
  Windows 구현은 `WindowsProcess.cpp`.
- 에디터: `ScriptBuild`(MSBuild·Visual Studio·VS Code 찾기, 빌드 명령, 로그 읽기, 줄을 여는 명령), `EditorApplication` 의 `BuildScripts`·`PollScriptBuild`(`Tick` 안)·
  `OpenScriptDiagnostic`·`Get`/`SetScriptEditor`, 프로젝트를 닫으면 도는 빌드를 끝냄, 파일 메뉴 `스크립트 빌드`, `BuildResultsPanel`, 에디터 설정의 `스크립트` 쪽.
- 검증(`ScriptBuildTests`): 자식 프로세스의 종료 코드·출력 파일·한글 작업 폴더·못 띄우는 경우, 도는 것을 닫으면 자식까지 끝나고 스스로 끝난 것을 닫으면 남긴 것이 산다,
  환경 변수, 로그 줄 모양(한글 경로·칸 없는 줄·`fatal error`·링커·`.obj`·진단이 아닌 줄·BOM·중복), 명령의 따옴표와 편집기별 인자, 편집기 선택이 설정 파일에 남고 다시 읽힘,
  그리고 에디터가 오류를 넣은 스크립트를 실제 MSBuild 로 빌드해 그 줄을 찾고 고치면 성공하는 길(MSBuild 가 없으면 건너뛴다). 뮤테이션 21 개가 모두 잡혔다.

**남은 것.**

- Visual Studio(`/Edit` + `Edit.GoTo`)와 VS Code(`-g`)가 실제로 그 줄로 가는지 **재지 않았다**. 사람이 한 번 눌러 봐야 한다.
- 빌드 중에 로그를 읽어 진행을 보이지 않는다. 끝나야 진단이 온다.
- 첫 빌드의 `vswhere` 는 메인 스레드에서 끝을 기다린다(상한 10 초).
- 소스를 저장하면 저절로 빌드하는 것은 하지 않았다(3.5 에서 정한다).

### 3.5 핫 리로드

빌드가 끝나면 `ScriptDLLLoader::Reload` 로 갈아 끼운다. 스크립트 필드 값은 리플렉션(`JBRO_FIELD`)으로 떠 두었다가 되살리고, 가리키는 것은
`InstanceId` 와 타입 이름이다. 소스 감시로 저절로 빌드하지 않는다. 재생 중에는 갈아 끼우지 않고 재생이 끝난 뒤로 미룬다(2026-09-29 사용자 결정).

- 완료 조건: 필드를 바꾼 스크립트를 다시 빌드하면 에디터를 끄지 않고 새 동작이 돌고, 인스펙터 값과 `Ref<T>` 가 이어진다(ProjectRule §6 의 핫 리로드 MUST).

**진행 (2026-09-29, D-268).** 섰다. 고른 것 셋(시점·재생 중·필드가 바뀌었을 때)은 모두 권장안이다.

- 캔버스: `KeepScriptsAsText`·`ResolveKeptComponents`·`ComponentResolveNote`(`CanvasFile.h`), `Canvas::ReleaseModuleScripts`·`IsModuleScript`·`AttachScript`(번호를 정해 붙이기)·
  `ReplaceUnresolvedComponents`·`SetFileObjectOrder`, `UnresolvedComponent::componentId`.
- 호스트: `IFramework::ReleaseModuleScripts`(2D·3D), `EngineInstance::ReloadScriptModule`·`GetScriptModulePath`, 닫을 때 DLL 을 내리기 전에 스크립트를 뗌.
  플랫폼: `IPlatform::ReadFileWriteTime`.
- 에디터: `ReloadScripts`·`IsScriptReloadPending`·`GetScriptModule`, 빌드가 성공하면 싣기, DLL 파일 확인(`PollScriptModuleFile`), 재생을 멈추면 미룬 것 싣기, 필드 모양이 바뀌면 되돌리기 기록 비우기.
- 검증(`ScriptHotReloadTests`, `ScriptBuildTests` 의 끝에서 끝): 한 스크립트의 두 판(필드를 지우고·타입을 바꾸고·더한 판)으로 자리·번호·값·`Ref<T>`·오브젝트 참조·꺼 둔 것이
  이어지고 빌트인은 건드리지 않으며 이어지지 못한 값을 알리는 것, 새 판이 모르는 타입은 값을 든 채 기다렸다 돌아오는 것, 뜨지 못하는 스크립트가 있으면 아무것도 떼지 않는 것,
  파일에서 몰랐던 스크립트가 뒤에 옳은 오브젝트를 가리키는 것. 에디터에서는 시험 DLL 두 판으로 리로드·되돌리기 기록 유지·재생 중 미루기·파일 변경 감지(처음 본 확인에서는 싣지 않음)·
  뜨기 실패 때 DLL 유지·싣기 실패 뒤 되살림·스크립트를 붙인 채 닫기. 끝에서 끝으로는 실제 MSBuild 로 필드를 더해 다시 빌드한다(D-268 (9)).
  뮤테이션 23 개 중 22 개가 잡혔다. 남은 하나(뜨는 동안 오브젝트 참조 문맥을 비우는 줄을 뺀 것)는 동등하다 - 그 자리에서는 원래 문맥이 없다.

**남은 것.**

- 캔버스 파일을 읽을 때는 여전히 코드에 없는 필드에서 멈춘다. **에디터를 끈 채 필드를 지우고 빌드하면 그 스크립트가 든 캔버스가 열리지 않는다**(§4 `[열림]`).
- 파일에서 온 모르는 컴포넌트는 오브젝트 참조를 그 파일의 번호로 든다. 그 타입을 모르는 채로 오브젝트 차례를 바꾸고 다시 저장하면 다른 오브젝트를 가리킬 수 있다(D-264 부터, §4 `[열림]`).
  같은 까닭으로 재생 스냅숏을 되읽은 뒤에도 모르는 컴포넌트의 참조는 처음 읽은 파일의 차례를 쓴다(코드로 읽었고 재지 않았다).
- 핫 리로드가 뜬 스크립트가 새 DLL 에서도 모르는 채로 저장되면 오브젝트 참조가 `@번호` 로 적혀, 다시 열면 빈 참조가 된다(코드로 읽었고 재지 않았다).
- 실제 에디터 창으로 재 보지 않았다(시험이 에디터를 창 없이 돌렸다). Visual Studio 로 빌드했을 때 링커가 DLL 을 쓰는 동안 싣기를 시도하는지는 0.5 초 간격 두 번 확인으로만 막았다 - 재지 않았다.

### 3.6 스크립트 API 의 구멍

- 오브젝트 만들기·찾기: `PrefabSpawner` 를 구현하고 스크립트가 얻는 자리를 정한다. **새 서비스라 확인 뒤에 한다.**
- `ReadOnly` 필드: 월드 캐시를 스크립트가 쓰지 못하게 한다(getter 뒤로 옮기거나 쓰기를 컴파일 에러로 만든다).

### 3.7 3D

`GameScript3D` 와 충돌 훅. 3D 물리가 없으므로 [todo-3d.md](./todo-3d.md) 의 순서를 따른다.

## 4. 열림

- ~~`[열림]` 3.1 의 모르는 스크립트를 들고 있는 모양~~ 캔버스 쪽 자료다(D-264).
- ~~`[열림]` 3.2 의 매크로 이름과 구현 자리~~ 정했다(D-265).
- ~~`[열림]` 3.3 의 프로젝트를 만드는 곳~~ 에디터다(2026-09-29). 폴더 자리도 정했다(기존 엔진과 같다).
- ~~`[열림]` 스크립트 한 개를 파일 하나로 쓰는 방법~~ 기존 엔진처럼 `.h`/`.cpp` 로 나눈다(2026-09-29 사용자 지시).
- ~~`[열림]` §2.2 의 엔진 위치를 스크립트 프로젝트에 알리는 방법~~ `JBroEngine.props`(D-266).
- `[열림]` 게임 빌드의 스크립트 DLL 구성(Debug/Release).
- `[열림]` 엔진 설치본의 스크립트 SDK 모양(헤더·`.lib`·props 를 설치 폴더에 두는 방법).
- ~~`[열림]` 새 스크립트를 여는 편집기~~ 에디터 설정에서 고른다(Visual Studio·VS Code·기본 앱, D-267).
- ~~`[열림]` 3.5 의 재생 중 리로드 정책~~ 멈출 때까지 미룬다(D-268).
- `[열림]` 캔버스 파일 읽기의 "코드에 없는 필드는 실패" 를 스크립트에도 그대로 둘 것인가. 핫 리로드는 경고하고 잇지만 파일 읽기는 멈춘다(D-268 (2)).
- `[열림]` 모르는 컴포넌트의 오브젝트 참조를 저장·재생 스냅숏을 넘어 바르게 들고 가는 방법(D-264·D-268).
- `[열림]` 3.6 의 오브젝트 만들기·찾기 서비스의 모양.

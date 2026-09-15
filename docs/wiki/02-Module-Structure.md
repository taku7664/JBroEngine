# 2. 모듈 구조

모듈은 **개념 경계**이고 DLL 은 **런타임 이음매**다. 둘을 같은 것으로 보지 않는다.
모듈은 열일곱 개이고 전부 정적 링크다. DLL 은 게임 스크립트 하나뿐이다.

## 규칙 세 줄

1. 모듈마다 vcxproj 가 하나다. 폴더만 나눈 것은 모듈이 아니다.
2. 공개 헤더는 `Modules/<모듈>/Include/JBro/<이름>/` 아래에 두고 `#include <JBro/<이름>/...>` 로만 참조한다.
3. 모듈은 자기가 **의존 선언한 모듈의 Include 경로만** 받는다. 선언하지 않은 모듈의 헤더를 include 하면 C1083 으로 컴파일이 실패한다.
   역방향 include 와 순환 의존은 만들지 않는다.

3번이 이 구조의 핵심이다. "스크립트는 `Canvas` 를 못 본다" 같은 말은 전부 이 include 경로 규칙 위에 서 있다.
지켜졌다는 것만 확인하지 않고, 어겼을 때 실제로 컴파일이 실패하는지를 `Tests/NegativeIncludeProbe` 가 확인한다.

외부 라이브러리는 이 규칙의 예외다(D-60). `ThirdParty/` 에 소스째로 두고 그 라이브러리가 정한 이름(`<imgui.h>`)으로 include 한다.
래핑하지 않는다. 다만 어느 모듈이 그것을 보는지는 똑같이 vcxproj 의 include 경로 선언으로 통제된다.

## 모듈 목록

| 모듈 | 층 | 빌드 | 역할 |
|---|---|---|---|
| `JBroCore` | S | 정적 | 값 타입·컨테이너·할당기·`StableTypeId`·`InstanceIdGenerator`·리플렉션 표 형식·YAML 부분집합 |
| `JBroRuntime` | S | 정적 | `ComponentBase`·`GameObject`·`GameObjectHandle`·`Ref<T>`·`GameScriptBase`·Context 둘·`ScriptModule`·`ScriptRegistry`·`Internal/InstanceRegistry` |
| `JBroAssetTypes` | S | 헤더 전용 | `AssetId`·`AssetHandle`·`AssetMetadata`·`Asset::*` |
| `JBroFramework2D` | S | 정적 | 2D 컴포넌트 다섯, `Physics2DService`, `GameScript2D`, `Layer2D`, `Math2D`, 2D `ScriptAPI.h` |
| `JBroFramework3D` | S | 헤더 전용 | 3D 컴포넌트 다섯, `Math3D`, 3D `ScriptAPI.h` |
| `JBroCanvas` | E | 정적 | `Canvas`·`Layer`·`GameSystem`·`SystemScheduler`·`ScriptPool`·`ComponentRegistry`·`CanvasFile`(`.jcanvas`)·`Internal::CanvasAccess` |
| `JBroFramework2DSystem` | E | 정적 | 2D 시스템 다섯, 렌더 추출(`RenderWorld2D`·`RenderBridge2D`), `Framework2D`(IFramework 구현) |
| `JBroFramework3DSystem` | E | 정적 | `Framework3D`(IFramework 구현, 렌더 시스템 없음), 3D 빌트인 타입 등록 |
| `JBroHost` | E | 정적 | `EngineInstance`·`IFramework`·`ScriptDLLLoader`·`ProjectFile`(`.jproject`) |
| `JBroAsset` | E | 정적 | `AssetSystem`(로드·캐시, **스텁**)·`AssetRegistry` |
| `JBroGraphics` | E | 정적 | `Renderer`. 패킷 수집, 정렬, 스프라이트 파이프라인, 셰이더 헤더 |
| `JBroRHI` | E | 정적 | `IRHIDevice`·`IRHICommandContext`·`IRHIModule` 와 핸들·디스크립터 타입 |
| `JBroD3D12RHI` | E | 정적 | D3D12 백엔드. 디버그 레이어·GPU 기반 검증 스위치 포함 |
| `JBroPlatform` | E | 정적 | `IPlatform`(창·이벤트 펌프·입력·동적 라이브러리), Windows 구현, Web·Android 스텁 |
| `JBroEditor` | E | 정적 | `EditorApplication`·패널·커맨드·위젯·로컬라이징·ImGui 백엔드(`EditorUI`) |
| `JBroEditorHost` | E | 실행 파일 | 에디터 진입점. 인자로 프레임 수를 받으면 그만큼 돌고 끝난다 |
| `JBroGameHost` | E | 실행 파일 | 게임 실행 진입점 |

`JBroFramework2D` 는 "정적" 이지만 `JBroFramework3D` 는 아직 `.cpp` 가 없어 헤더 전용(Utility) 이다. 3D 시스템이 생기면 정적 라이브러리가 된다.

## 의존 방향

vcxproj 의 ProjectReference 를 그대로 옮긴 것이다. 화살표는 "왼쪽이 오른쪽을 본다" 다.

```
JBroCore
  ▲
  ├── JBroRuntime ─────────────────────────────────┐
  ├── JBroAssetTypes                                │
  ├── JBroPlatform                                  │
  │     ▲                                           │
  │     └── JBroRHI ◄── JBroD3D12RHI                │
  │           ▲                                     │
  ├── JBroAsset ◄── JBroGraphics (Core·AssetTypes·Asset·RHI)
  │                                                 │
  ├── JBroFramework2D (Core·AssetTypes·Runtime) ────┤   Tier S 끝
  ├── JBroFramework3D (Core·AssetTypes·Runtime) ────┤
  ═════════════════════════════════════════════════╪═══════════════
  ├── JBroCanvas (Core·Runtime)                     │   Tier E 시작
  ├── JBroHost (Core·Runtime·AssetTypes·Asset·RHI·Graphics·Platform)
  ├── JBroFramework2DSystem (위 전부 + Canvas·Host·Framework2D)
  ├── JBroFramework3DSystem (위 전부 + Canvas·Host·Framework3D)
  ├── JBroEditor (위 전부 + 두 FrameworkSystem + imgui + D3D12RHI)
  ├── JBroEditorHost (전부 + Editor)
  └── JBroGameHost (전부, Editor 제외)
```

읽는 요령 몇 가지.

- **`JBroGraphics` 는 `JBroFramework2D` 를 모른다.** 그래서 렌더러가 `RenderWorld2D` 같은 프레임 타입을 받을 수 없고, Framework 쪽이 POD 패킷을 `ArrayView` 로 넘긴다(D-29, D-32).
- **`JBroRHI` 가 `JBroPlatform` 을 본다.** `SurfaceHandle` 이 플랫폼 것이고 스왑체인이 그것을 받기 때문이다. 반대 방향이 아니다(D-22).
- **`JBroCanvas` 가 `JBroRuntime` 을 본다.** `Canvas` 가 `InstanceRegistry` 에 등록·해제하는 쪽이라 Tier E → Tier S 방향이다.
- **`JBroFramework2D` 의 vcxproj 는 `JBroAsset` 도 선언해 두었지만** 실제로 그 헤더를 쓰는 곳은 없다. Tier S 모듈이 Tier E 를 볼 이유가 없으므로 지워도 되는 선언이다.
- 실행 파일 둘이 `JBroD3D12RHI` 를 직접 참조한다. 백엔드 선택이 호스트의 일이기 때문이다. `JBroEditor` 도 참조하는데, 테스트 프로세스에서 디버그 레이어를 켜는 스위치가 거기 있어서다.

## 스크립트가 보는 층, 엔진만 보는 층

같은 표를 Tier 로 다시 보면 이렇다(D-42). 이 분리는 2026-09-12 구조 리팩터링 단계 1에서 완료했다.

| 층 | 모듈 | 스크립트 타깃의 include 경로에 |
|---|---|---|
| Tier S | `JBroCore`, `JBroRuntime`, `JBroAssetTypes`, 고른 차원의 `JBroFramework2D` **또는** `JBroFramework3D` | 들어간다 |
| Tier E | 나머지 열두 개 | 들어가지 않는다 |

스크립트 타깃의 include 경로는 `JBro.Script.props` 가 `$(JBroScriptIncludes)` 하나로 넘긴다.
그 값은 `JBro.Common.props` 에서 Core·Runtime·AssetTypes 에 `JBroScriptDimension` 이 `2D` 면 Framework2D, `3D` 면 Framework3D 를 더한 것이다.
호스트 쪽은 같은 스위치로 `JBroFramework2DSystem` / `JBroFramework3DSystem` 을 고른다.

한 모듈에 두 층을 섞지 않는다. 그래서 컴포넌트 값 타입(`Transform2D`)은 Framework2D 에, 그것을 갱신하는 시스템(`Transform2DSystem`)은 Framework2DSystem 에 있다.
같은 이유로 에셋도 값 타입(`JBroAssetTypes`)과 시스템(`JBroAsset`)으로 갈라졌다(D-50).

기각한 안 둘: 프렐류드 음성 테스트만 늘리는 것(직접 include 를 막지 못한다), 한 모듈에 include 루트를 둘 두는 것(빌드 단위 원칙과 충돌).

## 모듈별로 조금 더

### JBroCore

차원과 무관한 것만 둔다. 공통 계층의 공개 시그니처에 특정 Framework 타입이 나오면 안 된다.

- `Types/`: `Int`·`Float`·`Bool`·`UInt` 강타입, `Color`, `String`(std::string 래퍼), `Array`, `Table`(open addressing, SIMD 그룹 탐색), `ArrayView`, `SafePtr`, `NameTable`, 할당기(`HeapAllocator`, `LinearAllocator`, `JAllocatorRef`)
- `Core/`: `JStringView`·`JArrayView`·`JMemoryContext`·`IModule`, `InstanceId`, `StableTypeId`(FNV 계열 해시로 이름→id), `InstanceIdGenerator`, `ObjectPool`(`TObjectPool<T>`), `Yaml`
- `Reflection/`: `TypeDescriptor`·`PropertyInfo`·`ValueCodec`·`ArrayOps`·`TableOps`·`EnumNames`, `JBRO_FIELD` 매크로(`Field.h`), `PropertyRegistry`, `ReflectedYaml`, 스칼라·컨테이너·문자열·enum 설명자
- `Script/Macros.h`: `JBRO_SCRIPT`

수학 타입은 여기 없다. `Vec2` 는 Framework2D, `Vec3` 는 Framework3D, `Matrix4x4` 는 Graphics 가 갖는다(D-57). 차원이 곧 의미라서 차원 독립 타입이 아니다.
`Color` 처럼 차원 의미가 없는 값 타입만 Core 에 한 번 정의한다(D-38).

### JBroRuntime

스크립트 DLL 이 링크하는 실행 모델의 뼈대다. `GameObject` 정의가 여기 있지만 프렐류드는 그 헤더를 include 하지 않는다.
`Internal/InstanceRegistry` 는 `Ref<T>::Get()` 이 부르기 때문에 이 층에 있어야 한다.

### JBroCanvas

`Canvas` 본체. `Internal::CanvasAccess` 가 `GameObject*`·`T*` 를 돌려주는 유일한 자리다(구 엔진의 `CCanvasRuntimeAccess` 패턴).
시스템이 쓰는 `T*` 조회는 `GetComponent` 라는 이름을 쓰지 않고 `FindComponentRaw<T>` 다.
`CanvasFile` 이 `.jcanvas` 를 읽고 쓰며, `ComponentRegistry` 가 이름으로 컴포넌트를 붙인다.

### JBroFramework2D / JBroFramework2DSystem

값 타입과 시스템의 짝이다. Framework2D 쪽에 `Internal/` 이 둘 있다. `SystemContext.h`(2D 시스템 인터페이스 묶음)와
`ScriptModuleContext.h`(확장 블록 만들고 찾기). `Internal` 이라 프렐류드가 끌어오지 않고, 서비스 `.cpp` 와 DLL 뼈대만 include 한다.

Framework2DSystem 의 `Rendering/` 에 `RenderWorld2D`(추출 결과와 정렬)와 `RenderBridge2D`(`Renderer` 에 제출)가 있다. `Framework2D` 가 `IFramework` 를 구현한다.

### JBroHost

`EngineInstance` 가 프로세스 자원 전부를 소유하고 조립한다. 별도 `EngineContext` 타입은 없다(D-53).
`ScriptDLLLoader` 가 DLL 로드·재로드·언로드와 임시 복사본을 맡고, `ProjectFile` 이 `.jproject` 를 읽는다.

```cpp
class EngineInstance
{
public:
    bool Initialize(const EngineConfig& config, IPlatform& platform, IRHIModule& rhi);
    bool OpenProject(IFramework& framework);
    bool OpenProject(IFramework& framework, const char* scriptModulePath);
    void CloseProject();
    bool SetGameViewTarget(const FrameTarget& target);      // 에디터가 게임 뷰를 텍스처로 받을 때
    bool Tick(float deltaTime);
    void RequestExit();
    void Shutdown();
    AssetSystem* GetAssetSystem();  Renderer* GetRenderer();  LinearAllocator* GetFrameMemory();  IFramework* GetFramework();
    const ScriptDLLLoader& GetScriptModule() const;
    bool IsRunning() const;
    FrameStatus GetLastFrameStatus() const;
};
```

### JBroGraphics / JBroRHI / JBroD3D12RHI / JBroPlatform

[렌더링과 플랫폼](08-Rendering-And-Platform.md)에서 다룬다.

### JBroEditor

[에디터](09-Editor.md)에서 다룬다. `Source/` 는 `Command/`·`Panel/`·`Widget/` 셋으로 나뉘고 `.cpp` 가 28개로 가장 크다.

## 테스트와 도구

| 위치 | 무엇 |
|---|---|
| `Tests/JBroTests.vcxproj` | 테스트 실행 파일 하나. `TestMain.cpp` 가 D3D12 디버그 레이어를 켜고 CRT 단언을 stderr 로 돌린다(D-69) |
| `Tests/*Tests.cpp` | 영역별 테스트 40여 파일. 이름이 곧 영역이다(`ReferenceSafetyTests`, `ScriptSchedulingTests`, `EditorObjectCommandTests` …) |
| `Tests/HeaderSelfContainment` | 모든 공개 헤더를 하나씩 단독 컴파일 |
| `Tests/NegativeIncludeProbe` | Tier 경계를 어기는 include 가 **실패하는지** 확인하는 음성 테스트 |
| `Tests/ScriptModuleProbe` | 실제로 빌드되는 스크립트 DLL 두 벌(V1·V2). 로더와 핫 리로드 테스트가 이것을 로드한다 |
| `Tests/Shaders` | 테스트용 셰이더와 생성 헤더 |
| `tools/mutate.py` | 뮤테이션 테스트. 한 번에 하나씩 코드를 틀리게 고치고 테스트가 잡는지 본다. 되돌리기는 `git checkout` 이다 |
| `Localization/ko-KR.yaml`, `en-US.yaml` | 에디터 문구. 기본 `ko-KR`, 폴백 `en-US` |

빌드 공통 설정은 `JBro.Common.props` 에 있다. 툴셋 `v145`, SDK `10.0.22621.0`, C++20, `/utf-8`, 경고를 오류로 본다.

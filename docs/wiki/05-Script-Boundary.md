# 5. 스크립트 경계

게임 로직은 **별도 DLL** 로 빌드되고 호스트가 실행 중에 로드·언로드·재로드한다. 이 엔진에서 DLL 경계는 이것 하나뿐이다(D-14).
RHI 도 Framework 도 정적 링크다. DLL 경계는 POD 전달·소유권 규칙·수명 순서 같은 비용을 그 API 에 영구히 부과하므로,
"나중에 필요할지도 모른다" 는 이유로 미리 만들지 않는다.

## 스크립트가 보는 것과 보지 못하는 것

모듈은 두 층으로 나뉜다(D-42). 스크립트 타깃은 **Tier S 모듈의 Include 경로만** 받는다.
Tier E 헤더를 include 하면 경로가 없어 컴파일이 실패한다. 이름 규칙이 아니라 **빌드 설정이 강제**한다.

| 층 | 모듈 | 안에 있는 것 |
|---|---|---|
| Tier S | `JBroCore` | 값 타입, `Array`·`Table`·`String`, `StableTypeId`, 리플렉션 표 형식 |
| Tier S | `JBroRuntime` | `ComponentBase`, `GameObject`, `GameObjectHandle`, `Ref<T>`, `GameScriptBase`, Context 둘, `ScriptModule`, `Internal/InstanceRegistry` |
| Tier S | `JBroFramework2D` | 2D 컴포넌트, `Physics2DService`, `GameScript2D`, `Layer2D`, `Math2D`, `ScriptAPI.h` |
| Tier S | `JBroAssetTypes` | `AssetId`, `AssetHandle`, `AssetMetadata`, `Asset::*` |
| Tier E | 나머지 전부 | `Canvas`, 시스템 구현, `EngineInstance`, `Renderer`, RHI, 플랫폼, 에디터 |

의존은 Tier E → Tier S 방향만 허용한다.

`GameObject` 는 Tier S 에 있다. `ComponentBase`·`GameObjectHandle`·`GameScriptBase` 가 그 정의를 필요로 하고 셋 다 DLL 이 링크하기 때문이다.
그런데도 스크립트가 `GameObject` 를 못 만지는 것은 프렐류드가 `GameObject.h` 를 include 하지 않아서다.
직접 include 하려 들면 `GameObject.h` 머리의 `#error` 가 막는다. `JBRO_SCRIPT_TARGET` 이 정의된 빌드에서 `JBRO_SCRIPT_PRELUDE` 없이
그 헤더에 닿으면 컴파일이 멈춘다.

## 스크립트가 include 하는 헤더는 하나다

```cpp
#include <JBro/ScriptAPI.h>
```

이 프렐류드는 각 Framework 모듈의 `Include/JBro/ScriptAPI.h` 에 있다. 경로는 하나인데 프로젝트가 고른 차원의 모듈이 그 경로를 제공하므로
내용은 차원별이다(D-18, D-42). `using namespace JBro;` 를 하고, `SystemContext.h` 는 include 하지 않는다.

3D 프로젝트의 스크립트 타깃에는 Framework2D 의 Include 경로가 없어 `GetFramework2DServices()` 같은 이름 자체가 컴파일되지 않는다.
2D/3D 배타성은 이렇게 **사용자 스크립트 프로젝트와 게임 익스포트**에만 적용되고, 엔진과 에디터는 둘 다 포함한다(D-15).

## 호스트가 DLL 에 넘기는 것

호스트는 프로세스 자원 전부를 알지만 DLL 에 **통째로 넘기지 않는다.** 필요한 부분집합만 POD 로 넘긴다.

```
EngineInstance                      호스트. 프로세스 자원 전부를 소유·조립
    ├─ FrameworkContext                 Framework 에 주는 부분집합 (메모리, AssetSystem*, Renderer*, 고정 스텝 설정)
    ├─ ScriptModuleLoadContext          DLL 로드 시 1회 전달 (64B POD)
    │    ├─ SystemContext                   공통 시스템. DLL 은 받지만 사용자에게는 안 보인다
    │    ├─ ServiceContext                  공통 서비스. 사용자에게 보인다
    │    ├─ Registry                        InstanceRegistry*  — DLL 이 자기 사본에 바인딩 (D-44)
    │    ├─ Names                           NameTable*         — 태그 원문을 되찾기 위해 (D-51)
    │    ├─ Scripts                         ScriptRegistry*    — DLL 이 여기에 자기 타입을 등록 (H5)
    │    └─ Extensions[]                    Framework2DSystemContext / Framework2DServiceContext 블록
    └─ (Tier E 내부)                    Canvas · Renderer · Platform · RHI · AssetSystem
```

### Context 에 무엇을 넣고 무엇을 넣지 않나

- `ServiceContext` 에는 게임플레이가 정당하게 필요로 하는 **서비스만** 넣는다. 서비스는 **값**으로 담는다(D-34). 포인터로 주면 사용자 코드에 null 검사와 수명 혼동이 퍼진다.
- `SystemContext` 에는 **차원 무관 시스템의 인터페이스 포인터**만 넣는다. 구체 시스템 포인터는 프렐류드 경계를 오염시킨다.
- 차원별 시스템·서비스는 공통 Context 에 넣지 않는다(D-36, D-43). 넣으면 3D 게임 바이너리의 공통 Context 에 2D 슬롯이 남고, 공통 계층이 Framework2D 를 참조하게 된다.
  대신 `TypeId + AbiVersion + Size + Data` 로 된 **확장 블록**(`ScriptContextBlock`)으로 넘긴다(D-37).
- **콘텐츠 단위 객체는 넣지 않는다.** `Canvas` 는 장면의 단위이고 `GameObject` 는 액터의 단위다. 상태가 없는 것(Math)도 슬롯을 차지하지 않고 헤더 전용으로 준다.

### 서비스 헤더의 규칙

서비스 헤더는 시스템을 **전방 선언만** 하고 실제 호출은 `.cpp` 에 둔다. 인라인으로 두면 시스템 정의가 프렐류드를 타고 사용자에게 노출된다.
`Physics2DService.cpp` 가 `GetFramework2DSystems().Physics2D` 를 읽는 식이다.

## DLL ABI

DLL 이 내보내는 C 심볼은 **하나**다(D-37).

```cpp
inline constexpr char ScriptModuleEntryPointName[] = "JBroScriptModule_GetApi";

using GetScriptModuleApiFunction = const ScriptModuleApi* (*)(
    std::uint32_t hostAbiVersion,
    std::uint32_t hostApiSize) noexcept;
```

호스트와 DLL 사이에 C++ 가상 객체를 만들거나 넘기지 않는다. 기존 엔진의 C++ 가상 모듈 객체는 컴파일러·CRT·할당자 ABI 를 경계에 고정하므로 버렸고,
Context 별로 심볼을 따로 내보내는 방식은 Framework 가 늘 때마다 로더를 고쳐야 해서 버렸다.

```cpp
struct ScriptModuleApi                        // 40B
{
    std::uint32_t AbiVersion;                 // ScriptModuleAbiVersion = 1
    std::uint32_t StructSize;
    const ScriptContextRequirement* RequiredContexts;   // 이 DLL 이 요구하는 확장 블록 목록
    std::uint32_t RequiredContextCount;
    std::uint32_t Reserved;
    ScriptModuleLoadFunction   Load;          // bool (*)(const ScriptModuleLoadContext*) noexcept
    ScriptModuleUnloadFunction Unload;        // void (*)() noexcept
};

struct ScriptModuleLoadContext                // 64B, ABI 4
{
    std::uint32_t AbiVersion;
    std::uint32_t StructSize;
    const SystemContext*  Systems;
    const ServiceContext* Services;
    Internal::InstanceRegistry* Registry;
    NameTable*      Names;
    ScriptRegistry* Scripts;
    const ScriptContextBlock* Extensions;
    std::uint32_t ExtensionCount;
    std::uint32_t Reserved;
};

struct ScriptContextBlock                     // 24B
{
    ScriptContextTypeId TypeId;               // MakeStableTypeId("JBro.Framework2D.ServiceContext") 등
    std::uint32_t AbiVersion;
    std::uint32_t Size;
    const void*   Data;
};
```

모든 구조체는 standard-layout·trivially-copyable 이고 크기가 `static_assert` 로 고정돼 있다. 크기가 바뀌면 양쪽이 다른 레이아웃을 읽으므로 거기서 멈춘다.

### 로드 절차

1. 호스트가 `JBroScriptModule_GetApi(hostAbiVersion, hostApiSize)` 를 부른다.
2. 돌려받은 `ScriptModuleApi` 의 ABI 버전, 구조체 크기, 함수 포인터, `RequiredContexts` 를 **전부 검사**한다. 하나라도 어긋나면 모듈 코드를 활성화하지 않는다.
3. `Load(&context)` 를 부른다. DLL 안에서 `BindScriptModuleContexts` 가 레지스트리·이름표·스크립트 표·Context 들을 자기 정적 링크 사본에 바인딩하고, `RegisterScriptType<T>()` 로 자기 스크립트 타입을 등록한다.
4. `Load` 가 실패하면 같은 모듈의 `Unload` 를 롤백 훅으로 부른 뒤 라이브러리를 해제한다.

`Load`/`Unload` 는 예외를 경계 밖으로 내보내면 안 된다. 그리고 이 함수 포인터는 **로드·재로드·언로드 때만** 부른다.
매 프레임 시스템·서비스 호출을 이 표로 우회하지 않는다. `SystemContext` ABI 는 "재빌드 강제 + 버전 스탬프 안전망" 조합이다(D-28).
게임 DLL 은 어차피 사용자 프로젝트마다 호스트와 함께 빌드되므로 재빌드가 자연스러운 규약이고, 런타임 함수 테이블은 매 프레임 호출에 오버헤드라 채택하지 않았다.

Context 블록의 `Data` 는 `Load` 가 끝날 때까지만 빌린다. DLL 은 필요한 값을 **복사**해야 하고 포인터를 저장하면 안 된다.

## 스크립트 타입 등록

DLL 은 컴포넌트 저장소를 직접 만들지 않는다. **크기·정렬·제자리 생성·파괴만** 알려 주고 메모리는 호스트의 풀이 잡는다.
기존 엔진처럼 생성 함수가 캔버스를 받는 모양은 쓸 수 없다. `Canvas` 는 Tier E 라 DLL 이 보지 못하기 때문이다.

```cpp
struct ScriptTypeInfo
{
    NameId          name;
    ComponentTypeId typeId;
    std::uint32_t   size;
    std::uint32_t   alignment;
    GameScriptBase* (*Construct)(void* storage) noexcept;   // 호스트가 준 자리에 제자리 생성. 실패하면 nullptr
    void            (*Destruct)(GameScriptBase* script) noexcept;
};

template<typename T> bool RegisterScriptType();            // DLL 의 Load 안에서 부른다
```

`ScriptRegistry` 는 같은 이름이 이미 있으면 거절한다. 조용히 덮으면 어느 DLL 의 타입인지 알 수 없다.
모듈이 내려갈 때 `Clear()` 로 그 모듈이 등록한 것을 전부 지운다. 남겨 두면 사라진 코드의 함수 포인터를 들고 있게 된다.

호스트는 `Canvas::AttachScript(owner, "이름")` 으로 이 표를 보고 스크립트를 붙인다. `.jcanvas` 를 읽을 때 이 길을 탄다.

## 핫 리로드

Windows 는 로드한 DLL 파일을 잠근다. 컴파일러가 출력 DLL 을 덮어쓸 수 있게 호스트는 **원본과 같은 디렉터리에 유일한 임시 복사본**
(`원본경로.jbro.PID.순번.dll`)을 만들어 그것을 로드한다(D-39). 다른 임시 디렉터리로 옮기지 않는 이유는 의존 DLL 탐색 기준을 유지하기 위해서다.
로드 실패나 언로드 뒤에는 임시 파일과 네이티브 핸들 래퍼를 모두 정리한다.

정상 언로드는 스크립트 인스턴스와 모듈 Context 를 먼저 정리한 뒤 운영체제 핸들을 해제한다. 재바인딩은 `SystemContext`·`ServiceContext`·Framework Context 를 전부 다시 한다.

**모듈 세대**는 활성 모듈의 정체가 바뀌거나 사라질 때 증가한다. 재로드가 실패해도 이전 DLL 이 이미 내려갔다면 세대를 올려서 이전 스크립트 슬롯 캐시가 재사용되지 않게 한다.
별도로 빌드한 V1·V2 DLL 을 교체해 내보낸 revision 이 1→2 로 바뀌는 것까지 실측했다.

## 메모리와 문자열

- 경계를 넘는 데이터는 **POD** 다. `std::string`·`std::vector` 같은 소유권 있는 STL 타입을 넘기지 않는다.
- DLL 에서 만든 객체는 같은 DLL 에서 파괴한다.
- 스크립트 필드의 `Array`/`Table`/`String` 은 호스트가 C++ 컨테이너 연산으로 직접 재할당·해제하지 않는다. DLL 이 제공하는 `ArrayOps`/`TableOps` 함수 포인터를 거친다(D-51).
  그래서 스크립트 필드에 owning `Array`/`Table` 을 둘 수 있다. 기존 엔진은 이것을 허용하지 못했다.
- `String` 은 POD Context·패킷·`Ref`·핸들·컴포넌트 공개 필드에 두지 않는다. 이름·태그는 `NameId` 정수로 두고 원문은 `NameTable` 이 보관한다.

## 어디에 시험이 있나

| 테스트 | 무엇을 붙잡나 |
|---|---|
| `ScriptDLLLoaderTests` | 실제 DLL 로드·재로드·언로드, 임시 복사본 정리, 세대 증가 |
| `ScriptModuleProbe` | 별도로 빌드되는 진짜 스크립트 DLL. `JBro.Script.props` 로 Tier S 경로만 받는다 |
| `NegativeIncludeProbe` | Tier E 헤더를 include 하면 **컴파일이 실패하는지** 확인하는 음성 테스트 |
| `ScriptApiPreludeTests` | 프렐류드가 끌어오는 이름의 집합. `Canvas`·`SystemContext` 가 보이면 실패 |
| `ContextBoundaryTests` | Context 구조체의 POD 성질과 크기 |
| `HeaderSelfContainment` | 모든 공개 헤더가 혼자 컴파일되는지 |

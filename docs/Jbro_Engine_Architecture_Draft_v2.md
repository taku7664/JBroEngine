# Jbro Engine Architecture Draft

> **역사 초안 — 현재 계약이나 구현 지시로 사용하지 않는다.** 이 문서에는 폐기된 ECS/World,
> GraphicsSystem, 전 모듈 DLL 설계가 표시되지 않은 채 남아 있다. 부분적인 `[대체됨]` 표시는 문서
> 전체의 현행성을 보장하지 않는다. 현재 계약은 `ProjectRule.md`, 변경 근거는 `tasks/todo.md`의
> Decisions를 사용한다. 아래 본문은 설계 변천을 보존하기 위한 자료다.

> **이 문서의 어느 절도 현재 구현 계약으로 사용하지 않는다.** 개별 `[대체됨]` 표시는 당시 수정
> 흔적일 뿐이며, 표시가 없는 절도 현행이라는 뜻이 아니다. 현재 계약은
> [ProjectRule.md](./ProjectRule.md), 변경 근거와 미결정 항목은 `tasks/todo.md`를 사용한다.

## 1. 목표

Jbro 엔진의 초기 아키텍처 목표는 다음과 같다.

- 에디터는 Windows 전용
- 지원 플랫폼
  - Windows
  - Android
  - Web
- 프로젝트는 반드시 **2D 또는 3D 중 하나만 선택**
- 2D와 3D Framework는 완전히 분리
- 2D 프로젝트에서 3D API를 사용하면 컴파일 에러
- 3D 프로젝트에서 2D API를 사용하면 컴파일 에러
- Windows Editor에서는 Framework / RHI를 DLL로 분리
- 여러 `JbroEditor.exe` 프로세스가 동일 DLL 코드 이미지를 활용
- Android / Web에서는 필요한 모듈만 정적 링크
- Windows에서는 필요하면 여러 RHI를 포함하고 런타임 선택 가능
- 필요 없는 Framework / RHI / Platform 코드는 빌드 대상에서 제외

---

# 2. 핵심 설계 원칙

## 2.1 Module과 DLL은 분리된 개념이다

`Framework2D`, `Framework3D`, `D3D12RHI`, `VulkanRHI` 등은 **논리적 Module**이다.

DLL은 Windows에서 Module을 배치하는 방법 중 하나일 뿐이다.

```text
Framework2D
    ├─ Windows Editor -> Dynamic DLL
    ├─ Windows Game   -> Static / Dynamic 선택
    ├─ Android        -> Static
    └─ Web            -> Static
```

즉:

```text
Module
    = 엔진 기능 단위

Linkage
    = Static / Dynamic
```

---

# 3. 2D / 3D Framework 정책

2D와 3D는 동시에 사용할 수 없다.

```text
Project
    ├─ Framework2D
    └─ Framework3D

둘 중 정확히 하나만 선택
```

허용:

```text
2D Project
    -> Framework2D

3D Project
    -> Framework3D
```

금지:

```text
Framework2D + Framework3D
```

또한 두 Framework 사이에는 의존성을 만들지 않는다.

```text
Framework2D  -X-> Framework3D
Framework3D  -X-> Framework2D
```

---

# 4. 공통 모듈 기준

공통 모듈에는 **2D / 3D 차원 개념과 무관한 기능만** 둔다.

예:

```text
JbroCore
    Memory
    Math Base
    Container
    Job
    Module System

JbroRuntime
    Entity
    Scene
    Lifecycle
    NameComponent
    ActiveComponent
    HierarchyComponent

JbroAsset
    AssetId
    AssetHandle
    AssetRegistry
    AssetManager
```

반대로 차원에 종속되는 타입은 각 Framework가 소유한다.

```text
Framework2D
    Transform2D
    Camera2D
    SpriteRenderer
    Rigidbody2D
    Collider2D
    TileMap
    Animation2D

Framework3D
    Transform3D
    Camera3D
    MeshRenderer
    Rigidbody3D
    Collider3D
    Skeleton
    Animation3D
```

공통 코드를 판단하는 기준:

> 2D와 3D Framework가 없어도 그 개념이 독립적으로 성립하는가?

성립하면 공통 모듈 후보.

성립하지 않으면 해당 Framework가 소유한다.

---

# 5. Asset 구조

Asset 시스템 자체는 공통이다.

```text
JbroAsset
    AssetId
    AssetHandle
    AssetManager
    AssetRegistry
    Asset Metadata
    Dependency
```

실제 Asset Type은 의미를 소유하는 모듈에 둔다.

```text
JbroGraphics
    TextureAsset
    ShaderAsset

JbroAudio
    AudioClipAsset

Framework2D
    SpriteAsset
    TileMapAsset
    Animation2DAsset

Framework3D
    MeshAsset
    SkeletonAsset
    Animation3DAsset
```

따라서 2D 프로젝트에서는 3D Asset Type 자체가 빌드에 포함되지 않는다.

```text
2D Project

사용 가능:
    Texture
    Shader
    Sprite
    TileMap
    Animation2D

사용 불가:
    Mesh
    Skeleton
    Animation3D
```

---

# 6. Component 구조

ECS 자체는 공통 시스템이다.

```text
JbroCore / ECS
    Entity
    ComponentTypeId
    ComponentStorage
    Query
    ComponentRegistry
```

ECS는 실제 Component 의미를 모른다.

각 모듈이 자기 Component를 등록한다.

공통 Component:

```text
JbroRuntime
    NameComponent
    ActiveComponent
    HierarchyComponent
```

2D Component:

```text
Framework2D
    Transform2DComponent
    Camera2DComponent
    SpriteRendererComponent
    Rigidbody2DComponent
    Collider2DComponent
```

3D Component:

```text
Framework3D
    Transform3DComponent
    Camera3DComponent
    MeshRendererComponent
    Rigidbody3DComponent
    Collider3DComponent
```

`Transform`을 공통으로 합치지 않는다.

```text
Transform2D
    Vec2 position
    float rotation
    Vec2 scale

Transform3D
    Vec3 position
    Quaternion rotation
    Vec3 scale
```

---

# 7. 컴파일 단계에서 2D / 3D 배타성 강제

> **[대체됨]** 엔진 빌드 타깃을 2D판/3D판으로 나누지 않는다.
> 엔진과 에디터는 두 Framework를 모두 포함하며, 배타성은 **사용자 스크립트 프로젝트의
> include 경로**와 **게임 익스포트의 링크 대상**에만 적용한다.
> 이유: 제품 플로우가 "엔진 1회 설치 → 새 프로젝트 → 2D/3D 선택 → 생성"이라
> 엔진을 나누면 사용자가 두 벌을 받아야 한다. 또한 3D 프로젝트가 HUD·스프라이트 이펙트에
> Framework2D를 쓸 수 없게 되어 2D 렌더 경로를 3D 쪽에 중복 구현해야 한다.
> 아래 `JB_FRAMEWORK_2D` / `JB_FRAMEWORK_3D` 매크로 검증은 도입하지 않는다.
> 자세한 정책은 ProjectRule §4 참고.

빌드 타깃은 하나만 정의한다.

2D:

```cpp
#define JB_FRAMEWORK_2D 1
```

3D:

```cpp
#define JB_FRAMEWORK_3D 1
```

공통 헤더에서 검증한다.

```cpp
#if defined(JB_FRAMEWORK_2D) && defined(JB_FRAMEWORK_3D)
#error "2D and 3D frameworks cannot be enabled together."
#endif

#if !defined(JB_FRAMEWORK_2D) && !defined(JB_FRAMEWORK_3D)
#error "A framework type must be selected."
#endif
```

---

# 8. Include 경로도 분리

> **[일부 대체됨]** include 경로로 경계를 강제한다는 방향은 유지하고 실제로 구현했다.
> 다만 적용 대상이 다르다. 이 절은 **엔진 타깃**을 2D/3D로 나눠 include 경로를 자르지만,
> 확정 규칙은 **모듈별로** 의존 선언한 모듈의 Include 경로만 주고
> **사용자 스크립트 프로젝트**에서만 반대 Framework를 잘라낸다.
> 엔진·에디터 빌드는 두 Framework의 경로를 모두 받는다.
>
> 실제 경로 형태도 아래 예시와 다르다. 공개 헤더는
> `Modules/<모듈>/Include/Jbro/<이름>/` 에 두고 `#include <Jbro/<이름>/...>` 로 참조한다.
> 이 규칙이 지켜지는지는 위반 시 컴파일이 실패하는 음성 테스트로 확인한다.

전처리 검증보다 더 강하게 **반대 Framework의 헤더를 include path에 넣지 않는다.**

2D Target:

```text
Include Path
    Jbro/Core
    Jbro/Runtime
    Jbro/Asset
    Jbro/Graphics
    Jbro/Framework2D
```

3D Target:

```text
Include Path
    Jbro/Core
    Jbro/Runtime
    Jbro/Asset
    Jbro/Graphics
    Jbro/Framework3D
```

따라서 2D 프로젝트에서:

```cpp
#include <Jbro/Framework3D/MeshComponent.h>
```

를 쓰면 바로 컴파일 실패해야 한다.

이 방식으로 2D / 3D 경계를 런타임이 아니라 **빌드 시스템이 강제**한다.

---

# 9. 전체 모듈 구조

```text
JbroCore
    Memory
    Math
    ECS
    Module
    Common Utility

JbroRuntime
    Scene
    Entity Lifecycle
    Dimension-independent Components

JbroAsset
    Asset Infrastructure

JbroGraphics
    Common Graphics Resources

JbroPlatform
    Windows
    Android
    Web

JbroRHI
    Graphics API Abstraction

JbroD3D12RHI
JbroVulkanRHI
JbroWebGPURHI

JbroFramework2D
JbroFramework3D

JbroEditor
```

의존 관계:

```text
                   JbroEditor
                       |
                   JbroRuntime
                       |
             +---------+---------+
             |                   |
      Framework2D           Framework3D
             |                   |
             +---------X---------+
                       |
                  JbroGraphics
                       |
                    JbroRHI
                       ^
          +------------+------------+
          |            |            |
       D3D12         Vulkan       WebGPU
```

2D / 3D는 동시에 링크되지 않는다.

---

# 10. Platform과 Renderer 관계

Platform이 Renderer를 소유하지 않는다.

금지:

```text
WindowsPlatform
    -> D3D12Renderer
```

추천:

```text
Engine
    ├─ PlatformSystem
    └─ GraphicsSystem
```

Platform은:

- Native Window
- Surface 생성 정보
- Input
- File System
- OS 기능

을 제공한다.

GraphicsSystem은:

- RHI Module
- RHI Device
- Renderer / Framework

를 사용한다.

Platform은 어떤 RHI가 가능한지 제한할 뿐이다.

---

# 11. RHI 구조

Framework2D / Framework3D는 실제 Graphics API를 몰라야 한다.

```text
Framework2D / Framework3D
            |
          JbroRHI
            |
    +-------+-------+
    |       |       |
  D3D12   Vulkan  WebGPU
```

상위 코드에 다음 타입을 노출하지 않는다.

```text
ID3D12Resource*
VkBuffer
VkImage
WGPUBuffer
```

대신 Handle / 공통 Interface를 사용한다.

```cpp
struct BufferHandle
{
    uint32_t index;
    uint32_t generation;
};

struct TextureHandle
{
    uint32_t index;
    uint32_t generation;
};
```

---

# 12. Windows Editor 실행 구조

Editor는 멀티 프로세스 방식이다.

```text
JbroEditor.exe --project=GameA
JbroEditor.exe --project=GameB
JbroEditor.exe --project=GameC
```

한 프로세스는 하나의 프로젝트만 담당한다.

```text
1 Process
1 Project
1 Engine Instance
1 Framework
1 GraphicsSystem
1 Active RHI
```

---

# 13. Windows Editor DLL 구조

> **[대체됨]** 전 모듈 DLL화는 채택하지 않는다. DLL은 게임 스크립트 하나뿐이고 나머지는 정적 링크한다.
>
> 이 절이 근거로 드는 "동일 DLL의 읽기 전용 코드 페이지를 여러 프로세스가 공유"는
> 문장 자체는 맞지만 DLL 분리를 정당화하지 못한다. **Windows는 동일 EXE 이미지도
> 같은 방식으로 코드 페이지를 공유한다.** 따라서 같은 exe를 여러 개 띄우는 §12 시나리오에서는
> 정적 링크와 DLL의 물리 메모리 사용이 다르지 않다.
> 서로 다른 exe가 같은 모듈을 쓸 때는 실제 이득이 있으나 규모가 작다(코드 수 MB 수준).
>
> DLL 경계는 POD 전달·소유권 규칙·수명 순서(§22~§26)를 그 API에 영구히 부과한다.
> 그 값을 치를 만한 이유는 **핫 리로드**와 **런타임 백엔드 교체**이며,
> 지금 필요한 것은 핫 리로드다. RHI는 두 번째 Windows 백엔드가 생길 때 DLL로 승격한다.
> 자세한 정책은 ProjectRule §3 참고.

예:

```text
JbroEditor.exe

JbroCore.dll
JbroRuntime.dll
JbroAsset.dll
JbroGraphics.dll

JbroFramework2D.dll
JbroFramework3D.dll

JbroD3D12RHI.dll
JbroVulkanRHI.dll
```

2D 프로젝트 두 개:

```text
Editor A
    ├─ JbroFramework2D.dll
    └─ JbroD3D12RHI.dll

Editor B
    ├─ JbroFramework2D.dll
    └─ JbroD3D12RHI.dll
```

동일 DLL의 읽기 전용 코드 페이지는 Windows에서 여러 프로세스가 공유할 수 있다.

공유 가능:

```text
Framework2D::Update() 코드
SpriteRenderer::Draw() 코드
Physics2D::Step() 코드
```

공유되지 않음:

```text
Scene 상태
Framework 인스턴스
AssetManager 상태
Physics World
Heap
Writable Global Data
```

2D 프로젝트에서는 `Framework3D.dll`을 로드하지 않는다.

---

# 14. Framework Dynamic / Static 빌드

DLL 바이너리를 정적으로 링크하는 것이 아니다.

같은 구현 소스를 다른 방식으로 다시 빌드한다.

```text
Framework2D/
├─ Include/
├─ Source/
│   ├─ Framework2D.cpp
│   ├─ SpriteSystem.cpp
│   └─ ...
│
└─ Entry/
    ├─ DynamicEntry.cpp
    └─ StaticEntry.cpp
```

실제 구현은 DLL을 몰라야 한다.

```cpp
class Framework2D final : public IFramework
{
public:
    bool Initialize();
    void Update(float deltaTime);
    void Shutdown();
};
```

구현 코드에는 다음이 없어야 한다.

```text
LoadLibrary
GetProcAddress
HMODULE
dlopen
```

---

# 15. Dynamic Build

Windows Editor:

```text
Framework2D.cpp
SpriteSystem.cpp
DynamicEntry.cpp

        ↓

JbroFramework2D.dll
```

Dynamic Entry:

```cpp
extern "C"
{
    JBRO_EXPORT IFramework* JbroCreateFramework();
    JBRO_EXPORT void JbroDestroyFramework(IFramework*);
}
```

---

# 16. Static Build

Android / Web / 필요 시 Windows Game:

```text
Framework2D.cpp
SpriteSystem.cpp
StaticEntry.cpp

        ↓

JbroFramework2D.lib / .a
```

Static Entry:

```cpp
IFramework* CreateFramework2D();
void DestroyFramework2D(IFramework*);
```

Android:

```text
JbroCore.a
JbroRuntime.a
JbroFramework2D.a
JbroVulkanRHI.a
Game Objects

        ↓

libGame.so
```

Web:

```text
JbroCore.a
JbroRuntime.a
JbroFramework2D.a
JbroWebGPURHI.a

        ↓

Game.wasm
```

---

# 17. RHI도 동일한 방식

Windows Editor:

```text
JbroD3D12RHI.dll
JbroVulkanRHI.dll
```

Android:

```text
JbroVulkanRHI.a
    ↓
libGame.so
```

Web:

```text
JbroWebGPURHI.a
    ↓
Game.wasm
```

RHI 구현 자체는 Dynamic / Static 여부를 몰라야 한다.

---

# 18. Windows Static / Dynamic 선택

> **[대체됨]** Module 상태를 `Disabled / Static / Dynamic` 세 값으로 프로파일마다 고르는 구조는 만들지 않는다.
> 현재 링크 방식은 고정이다. 게임 스크립트만 Dynamic, 나머지는 Static이다.
> 또한 아래 예시의 `Framework2D = Disabled` / `Framework3D = Disabled` 처럼
> 엔진 빌드에서 한쪽 Framework를 통째로 끄는 프로파일은 두지 않는다(§7 대체 참고).
> 익스포트한 게임에서만 선택된 Framework를 링크한다.

Windows에서도 Build Profile에 따라 선택 가능하다.

예:

```text
Editor-Windows

Framework2D = Dynamic
Framework3D = Dynamic
D3D12RHI    = Dynamic
VulkanRHI   = Dynamic
```

2D Game:

```text
Game-Windows-2D

Framework2D = Static
Framework3D = Disabled

D3D12RHI    = Static
VulkanRHI   = Disabled
```

Multi-RHI 3D Game:

```text
Game-Windows-3D-MultiRHI

Framework2D = Disabled
Framework3D = Static

D3D12RHI    = Dynamic
VulkanRHI   = Dynamic
```

Module 상태:

```text
Disabled
Static
Dynamic
```

---

# 19. Build 선택과 Runtime 선택

Build Time:

```text
어떤 Module / RHI가 존재하는가?
```

Runtime:

```text
포함된 RHI 중 무엇을 사용할 것인가?
```

예:

```text
Build
    D3D12
    Vulkan

Runtime
    D3D12
```

---

# 20. Windows Multi-RHI

Steam 실행 옵션 같은 형태 지원 가능.

```text
MyGame.exe --rhi=d3d12
MyGame.exe --rhi=vulkan
```

배포:

```text
MyGame.exe
JbroD3D12RHI.dll
JbroVulkanRHI.dll
```

두 DLL을 모두 배포하면 설치 용량은 둘 다 차지한다.

DLL의 장점은:

- 실행 시 필요한 Backend만 로드
- 불필요한 Backend 코드 미로드
- 교체 가능
- 패치 단위 분리 가능

---

# 21. Android / Web 정책

기본 정책:

```text
Android
    Framework = Static
    RHI       = Static

Web
    Framework = Static
    RHI       = Static
```

2D Android:

```text
Core
Runtime
Asset
Graphics
Framework2D
VulkanRHI
    ↓
libGame.so
```

3D Android:

```text
Core
Runtime
Asset
Graphics
Framework3D
VulkanRHI
    ↓
libGame.so
```

반대 Framework는 컴파일 대상 자체에서 제외한다.

---

# 22. DLL 메모리 경계

DLL 내부에서는 STL을 사용할 수 있다.

```cpp
class AssetCache
{
    std::vector<Asset> assets;
    std::unordered_map<AssetId, Asset> cache;
    std::string path;
};
```

하지만 DLL 경계를 넘는 Public API / Context에서는 owning STL 타입을 피한다.

금지 권장:

```text
std::string
std::vector
std::unordered_map
std::shared_ptr
std::function
```

권장:

```text
POD
Handle
View
Span
Interface Pointer
Allocator
Fixed-size Value Type
```

예:

```cpp
struct JStringView
{
    const char* data;
    uint32_t size;
};

template<typename T>
struct JArrayView
{
    const T* data;
    uint32_t size;
};
```

---

# 23. Memory / Allocator 구조

MemorySystem은 Core가 소유한다.

```text
JbroCore
└─ MemorySystem
   └─ RootAllocator
      ├─ Editor
      ├─ Framework2D
      ├─ Framework3D
      ├─ D3D12RHI
      └─ VulkanRHI
```

실제 Heap을 모듈마다 따로 만들 필요는 없다.

공용 allocator 위에 Tag / Tracking 계층을 둔다.

```text
RootAllocator
    ↓
Tracking / Tag
    ↓
Module
```

DLL 경계에는 단순한 function table 형태를 사용한다.

```cpp
struct JAllocator
{
    void* userData;

    void* (*allocate)(
        void* userData,
        size_t size,
        size_t alignment);

    void (*free)(
        void* userData,
        void* memory);

    void* (*reallocate)(
        void* userData,
        void* memory,
        size_t newSize,
        size_t alignment);
};
```

---

# 24. Memory Context

Module 초기화 시 Context로 전달한다.

```cpp
struct JMemoryContext
{
    JAllocator persistent;
    JAllocator frame;
    JAllocator scratch;
};
```

용도:

```text
persistent
    Scene
    Asset
    Component
    Resource

frame
    한 프레임 임시 메모리

scratch
    로딩 / 변환 / 계산용 임시 메모리
```

---

# 25. DLL 메모리 기본 규칙

1. Core가 MemorySystem을 소유
2. Module은 Context로 Allocator를 전달받음
3. DLL 내부 STL 사용 가능
4. DLL 경계를 넘는 owning STL 타입은 피함
5. DLL이 생성한 객체는 기본적으로 같은 DLL에서 Destroy
6. 외부에서 직접 `delete`하지 않음
7. 전역 `#define new` 방식은 사용하지 않음
8. 필요하면 `JNew / JDelete` helper 사용
9. Module별 메모리는 Tag / Arena로 추적
10. DLL Unload 전에 해당 DLL이 만든 객체를 모두 제거

---

# 26. DLL Lifetime

생성 순서:

```text
DLL Load
    ↓
Module
    ↓
Device / Framework
    ↓
Resources
```

종료 순서:

```text
Resources
    ↓
Device / Framework
    ↓
Module
    ↓
DLL Unload
```

DLL을 먼저 Unload하면 안 된다.

---

# 27. Project 설정

예:

```json
{
    "name": "GameA",
    "engineVersion": "0.1.0",
    "projectGuid": "...",

    "framework": "2D",

    "graphics": {
        "rhi": "D3D12"
    }
}
```

`framework`는 정확히 하나만 허용한다.

```text
2D
or
3D
```

---

# 28. Editor 실행 모델

별도 Hub는 초기에는 만들지 않는다.

```text
JbroEditor.exe
```

두 실행 모드:

## Project Browser

```text
JbroEditor.exe
```

## Project Editor

```text
JbroEditor.exe --project="GameA.jbro"
```

프로젝트를 열면 새 프로세스를 생성한다.

```text
Project Browser
    ├─ Editor[GameA]
    ├─ Editor[GameB]
    └─ Editor[GameC]
```

---

# 29. 동일 프로젝트 중복 실행

기본 정책:

```text
GameA + GameB -> 허용
GameA + GameA -> 금지
```

Project GUID 기반 Lock 사용을 권장한다.

---

# 30. 엔진 설치 구조

프로젝트별로 Framework DLL을 복사하지 않는다.

```text
Jbro/
└─ Engine/
   ├─ 0.1.0/
   │   └─ Bin/
   │       ├─ JbroEditor.exe
   │       ├─ JbroCore.dll
   │       ├─ JbroRuntime.dll
   │       ├─ JbroFramework2D.dll
   │       ├─ JbroFramework3D.dll
   │       ├─ JbroD3D12RHI.dll
   │       └─ JbroVulkanRHI.dll
   │
   └─ 0.2.0/
       └─ ...
```

동일 엔진 버전의 Editor 프로세스들은 동일 DLL 파일을 사용한다.

---

# 31. 최종 권장 정책 요약

> **[일부 대체됨]** 아래 요약 중 다음 항목은 갱신되었다.
>
> - "반대 Framework header / library 접근 불가" → 사용자 스크립트 프로젝트와 게임 익스포트에만 적용한다.
>   엔진과 에디터는 두 Framework를 모두 포함한다.
> - "Windows Editor: Framework2D = DLL, Framework3D = DLL, RHI = DLL" → 전부 정적 링크한다.
>   DLL은 게임 스크립트 하나뿐이다.
>
> 유지되는 항목: 공통 모듈은 차원 개념을 모른다, Framework 간 직접 의존 금지,
> Android/Web은 선택된 Framework와 RHI를 Static 링크한다, 동일 구현 소스를 링크 방식만 달리 빌드한다.
> 확정 정책은 [ProjectRule.md](./ProjectRule.md) §3·§4 를 본다.

```text
JbroEditor
    Windows only
    Multi-process

Framework
    2D / 3D 중 정확히 하나
    서로 직접 의존 금지
    반대 Framework header / library 접근 불가

Common
    차원 독립 기능만 허용

Windows Editor
    Framework2D = DLL
    Framework3D = DLL
    RHI         = DLL

Windows Game
    Static / Dynamic 선택 가능

Android Game
    선택된 Framework만 Static
    VulkanRHI Static

Web Game
    선택된 Framework만 Static
    WebGPURHI Static
```

핵심 원칙:

> 공통 모듈은 2D / 3D 차원 개념을 몰라야 한다.
>
> 프로젝트는 Framework2D 또는 Framework3D 중 정확히 하나만 선택한다.
>
> 반대 Framework는 include path와 link target에서 제거하여 컴파일 단계에서 접근을 차단한다.
>
> Framework와 RHI는 논리적 Module이며, Windows에서는 DLL, Android/Web에서는 동일 구현 소스를 Static Library로 빌드한다.

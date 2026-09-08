# W-host · 합성 루트 · 컨텍스트 · 스크립트 로더 · 프렐류드

**브랜치**: `work/host` (경로: `../JBro-host`)
**병합 순서**: 5번 (마지막. **W-ref · W-platform 병합 후에만 가능**)

## 목표

호스트 프로세스의 조립 루트를 완성한다.

1. **`EngineContext`** 조립 (모든 서비스 · 시스템 · 인프라 포인터 보유).
2. **`SystemContext`** · **`ServiceContext`** — 게임 DLL 로 넘기는 부분집합.
3. **`EngineInstance`** — 컨텍스트를 소유하는 유일한 오너 (`OwnerPtr`).
4. **`ScriptAPI.h` 프렐류드** — 게임 스크립트가 include 하는 유일한 헤더.
5. **`JBRO_SCRIPT` 매크로** — 스크립트 클래스 선언.
6. **스크립트 DLL 로드/언로드/재로드**.
7. **`Service::` 서비스들** — TimeService · InputService · AudioService · AssetService ·
   GameObjectService · CanvasService · Physics2DService.
8. **에디터 애플리케이션** (`JBroEditor`).

## 소유 파일

### 기존 편집

- `source/JBroEngine/Modules/JBroRuntime/Include/JBro/Runtime/EngineContext.h` (분리 · D-27)
- `source/JBroEngine/Modules/JBroRuntime/Include/JBro/Runtime/SystemContext.h` (분리 · D-27)
- `source/JBroEngine/Modules/JBroRuntime/Include/JBro/Runtime/ServiceContext.h` (분리 · D-27)
- `source/JBroEngine/Modules/JBroRuntime/Source/Context.cpp` (신규 · 3구조체 · 바인딩)
- `source/JBroEngine/Modules/JBroRuntime/Include/JBro/Runtime/EngineInstance.h`
- `source/JBroEngine/Modules/JBroRuntime/Source/EngineInstance.cpp`
- `source/JBroEngine/Modules/JBroRuntime/Include/JBro/Runtime/IFramework.h`
- `source/JBroEngine/Modules/JBroEditor/**`

### 신규 (프렐류드 · 매크로 · 스크립트 로더)

- `source/JBroEngine/Modules/JBroCore/Include/JBro/ScriptAPI.h` (**최상위 프렐류드**)
- `source/JBroEngine/Modules/JBroCore/Include/JBro/Script/Macros.h` (`JBRO_SCRIPT` 등)
- `source/JBroEngine/Modules/JBroRuntime/Include/JBro/Runtime/ScriptDLLLoader.h`
- `source/JBroEngine/Modules/JBroRuntime/Source/ScriptDLLLoader.cpp`

### 서비스 헤더 (W-framework 와 위치 조율)

`Service::` 는 도메인별 배치:

- 차원-무관 (Time / Input / Audio / Asset / GameObject / Canvas) → `JBroRuntime`
  (`Include/JBro/Runtime/Service/*.h`)
- 차원-종속 (Physics2D) → `JBroFramework2D`
  (`Include/JBro/Framework2D/Service/Physics2DService.h`)
  - 이 파일은 W-framework 폴더 안이지만 **W-host 가 소유**. W-framework 는 System 만.

**충돌 방지**: Service 헤더 파일 자체는 W-host 가 커밋한다. W-framework 는 이 파일을 편집하지
않는다. worktree-plan §4 "자기 소유 파일만" 규칙의 예외로 명시.

## 배경

- 다이어그램 HOST 레인의 계약:
  - EditorApplication → EngineInstance → EngineContext (Platform/RHI/Renderer/Asset/Time/Input/
    Audio/Reflection).
  - **EngineContext 는 포인터만 보유. 수명은 EngineInstance 하나가 `OwnerPtr` 로.**
  - **조립은 전부, 전달은 좁게** — 모듈별 필요한 부분집합 (FrameworkContext / SystemContext /
    ServiceContext) 만 넘긴다.
- 다이어그램 SCRIPT LAYER 규칙:
  - 게임 스크립트는 `#include <JBro/ScriptAPI.h>` 한 줄만.
  - 프렐류드가 `using namespace JBro` 를 함.
  - `JBRO_SCRIPT` 매크로로 스크립트 클래스 선언 (에디터 코드 생성기가 grep).
  - `BindSystemContext()` / `BindServiceContext()` — 로드 시 1회, 핫 리로드 때 재바인딩.

### `JBRO_SCRIPT` 매크로 계약

CLAUDE.md · memory 에 이미 명시:
> 게임 스크립트 클래스는 `class` 가 아니라 `JBRO_SCRIPT` 로 선언한다. 매크로 확장 결과가 `class`
> 라 어느 프로젝트에서든 그대로 컴파일되지만, 에디터 코드 생성기는 이 마커를 grep 해서 스크립트를
> 자동 등록한다.

매크로 정의:
```cpp
#define JBRO_SCRIPT(ClassName)  class ClassName
```

간단하지만 이 마커가 있어야 에디터의 자동 등록기가 인식한다.

## 작업 항목

### H1. `EngineContext` / `SystemContext` / `ServiceContext` — 3파일로 분리

**Why**: B0 에서 한 파일에 셋을 모아뒀다. 그러면 `ScriptAPI.h` 프렐류드가 `Context.h` 하나만
include 해도 `SystemContext` 정의가 사용자 TU 에 노출된다. **파일을 분리해서 include 트리로
노출 범위를 강제** (D-27).

**How**:

**`Runtime/EngineContext.h`** — 호스트 전용. 프렐류드 include 트리에 없음.
```cpp
namespace JBro
{
    class IPlatform;
    class IRHIModule;
    class Renderer;
    class AssetManager;

    struct EngineContext
    {
        IPlatform*    Platform  = nullptr;
        IRHIModule*   RHI       = nullptr;
        Renderer*     Renderer  = nullptr;
        AssetManager* Assets    = nullptr;
        // ... 시스템 · 서비스 포인터 모음.
    };
}
```

**`Runtime/SystemContext.h`** — 게임 DLL 은 받지만 프렐류드에는 들어가지 않음.
```cpp
namespace JBro
{
    namespace System
    {
        class Transform2DSystem;
        class SpriteRender2DSystem;
        class Camera2DSystem;
        class Physics2DSystem;
        class ScriptSystem;
    }

    struct SystemContext
    {
        std::uint32_t                 AbiVersion  = 1;  // D-28 · 안전망
        System::Transform2DSystem*    Transform2D = nullptr;
        System::SpriteRender2DSystem* SpriteRender2D = nullptr;
        System::Camera2DSystem*       Camera2D    = nullptr;
        System::Physics2DSystem*      Physics2D   = nullptr;
        System::ScriptSystem*         Script      = nullptr;
    };

    void BindSystemContext(const SystemContext& context);
}
```
`AbiVersion` 은 호스트/DLL 이 다른 버전이면 로드 거부용 안전망. 필드 순서가 바뀌면 버전 증가.
게임 DLL 은 사용자 프로젝트마다 함께 빌드되므로 (D-28) 재빌드 강제가 자연 규약.

**`Runtime/ServiceContext.h`** — `ScriptAPI.h` 가 include. 사용자에게 노출됨.
```cpp
namespace JBro
{
    namespace Service
    {
        class TimeService;
        class InputService;
        class AudioService;
        class AssetService;
        class GameObjectService;
        class CanvasService;
        class Physics2DService;
    }

    struct ServiceContext
    {
        std::uint32_t                 AbiVersion       = 1;
        Service::TimeService*         Time             = nullptr;
        Service::InputService*        Input            = nullptr;
        Service::AudioService*        Audio            = nullptr;
        Service::AssetService*        Asset            = nullptr;
        Service::GameObjectService*   GameObject       = nullptr;
        Service::CanvasService*       Canvas           = nullptr;
        Service::Physics2DService*    Physics2D        = nullptr;
    };

    void BindServiceContext(const ServiceContext& context);
}
```

### H2. 서비스는 값, 시스템은 전방 선언 + 비인라인 구현

**Why**: 서비스 헤더가 시스템 정의를 인라인으로 include 하면 프렐류드 타고 사용자 TU 에 시스템이
노출된다.

**How** (Service::Physics2DService 예):

```cpp
// JBro/Framework2D/Service/Physics2DService.h
namespace JBro
{
    namespace System { class Physics2DSystem; }
    namespace Service
    {
        class Physics2DService
        {
        public:
            bool Raycast(Vec2 origin, Vec2 dir, float dist, Collision2D& hit) const;
            void OverlapBox(const Rect& area, Array<GameObjectHandle>& out) const;

        private:
            // 전방 선언만 — 이 헤더는 Physics2DSystem 정의를 include 하지 않는다.
            System::Physics2DSystem* m_system = nullptr;
            friend class ::JBro::EngineInstance;
        };
    }
}
```

```cpp
// JBro/Framework2D/Service/Physics2DService.cpp — 이 TU 만 시스템 정의를 include.
#include <JBro/Framework2D/Service/Physics2DService.h>
#include <JBro/Framework2D/System/Physics2DSystem.h>

namespace JBro::Service
{
    bool Physics2DService::Raycast(...) const { return m_system->Raycast(...); }
    void Physics2DService::OverlapBox(...) const { m_system->OverlapBox(...); }
}
```

### H3. `BindSystemContext` / `BindServiceContext` 구현

**Why**: 게임 DLL 로드 후, DLL 안의 전역 컨텍스트 인스턴스를 호스트 것으로 동기화.

**How** (`Runtime/Context.cpp` 신규):

```cpp
namespace JBro
{
    namespace { SystemContext  g_system; ServiceContext g_service; }

    void BindSystemContext(const SystemContext& context)   { g_system  = context; }
    void BindServiceContext(const ServiceContext& context) { g_service = context; }

    // 스크립트가 이 함수로 서비스에 접근:
    const ServiceContext& GetServiceContext() { return g_service; }
    const SystemContext&  GetSystemContext()  { return g_system;  }
}
```

**주의**: 이 두 전역은 **모듈마다 자기 사본** (Engine.lib 이 호스트와 DLL 각각에 정적 링크).
그래서 호스트가 자기 g_service 를 채우고, DLL 이 자기 g_service 를 채우려면 호스트가 DLL 에
`BindServiceContext(hostServiceCopy)` 를 호출해서 값 복사 시켜야 한다.
이게 매 프레임이 아니라 로드 시 1회 (핫 리로드 때 재바인딩).

### H4. 스크립트 DLL 로드 / 언로드 / 재로드

**Why**: 스크립트 코드 반복 컴파일 + 재로드 (Live Compile).

**How** (`Runtime/ScriptDLLLoader.h/cpp` 신규):

```cpp
namespace JBro
{
    class ScriptDLLLoader
    {
    public:
        bool Load(const char* dllPath, IPlatform& platform);
        void Unload(IPlatform& platform);
        bool Reload(IPlatform& platform);  // Unload + Load + 캐시 무효화 신호

        // 로드된 DLL 안의 심볼 조회.
        void* GetSymbol(const char* name) const;

    private:
        IPlatform::DynamicLibrary m_lib;
        String                    m_path;
        uint64                    m_generation = 0;  // 재로드마다 증가.
    };
}
```

`Load` 흐름:
1. `platform.LoadDynamicLibrary(dllPath)`.
2. DLL 안의 `JBroScriptModule_Register` 함수 (표준 진입점, 사용자가 정의) 를 `GetSymbol` 로.
3. 호출 — DLL 이 자기 안의 스크립트 클래스들을 리플렉션 레지스트리에 등록.
4. `BindSystemContext` / `BindServiceContext` 로 호스트 컨텍스트 전달.

`Reload` 흐름:
1. `Unload` — DLL 안의 스크립트 인스턴스 파괴 (안전 훅으로).
2. `platform.UnloadDynamicLibrary(m_lib)`.
3. 새 DLL 파일 경로로 `Load`.
4. `m_generation++` — 이 시그널로 `Ref<T>` 캐시들이 자신을 무효화한다 (H5).

### H5. 핫 리로드 시 슬롯 캐시 무효화

**Why**: 스크립트 DLL 이 재로드되면 스크립트 오브젝트들의 슬롯이 바뀔 수 있다. `Ref<T>` 의
`Cached` 필드를 그대로 쓰면 잘못된 대상 지목.

**How**:

1. `ScriptDLLLoader::Reload` 후 `Canvas` 에 신호를 보낸다:
   `canvas.InvalidateScriptRefCaches(m_generation)`.
2. Canvas 는 스크립트 카테고리의 `Ref<T>` 를 순회 (리플렉션 필요 — 리플렉션 붙기 전까진 계약만).
3. 각 Ref 의 `Cached` 를 `{0, 0}` 으로 리셋. 다음 `Get()` 이 InstanceId 로 재해석.

### H6. 최소 스크립트로 재로드 실측

**Why**: 규약이 탁상공론이 되지 않으려면 실제 동작 확인.

**How**:

1. `Samples/HotReloadProbe/` 신규:
   - `PlayerScript.h`:
     ```cpp
     #include <JBro/ScriptAPI.h>
     JBRO_SCRIPT(Player) {
     public:
         void OnUpdate(float dt) {
             m_timer += dt;
             if (m_timer > 1.0f) { Log("tick"); m_timer = 0; }
         }
     private:
         float m_timer = 0;
     };
     ```
2. `Player.dll` 을 빌드하고 호스트가 로드.
3. `Log("tick")` 문자열을 `"tack"` 으로 바꾸고 재빌드 → 재로드.
4. 호스트 콘솔에서 `"tack"` 이 뜨는지 확인. 호스트 크래시 없어야.

### H7. `ScriptAPI.h` 프렐류드 헤더

**Why**: 스크립트 DLL 이 include 하는 **유일한 헤더**. 이 하나로 필요한 모든 이름이 온다.

**How** (`JBroCore/Include/JBro/ScriptAPI.h` 신규):

```cpp
#pragma once

// 게임 스크립트 프렐류드. 게임 스크립트 DLL 은 이 헤더 하나만 include 한다.
// using namespace JBro 를 하므로 GameObject / Ref / Canvas 등이 접두 없이 보인다.
// 1 뎁스 네임스페이스 (Component::Transform2D, Service::TimeService 등) 는 유지.

#include <JBro/Types/Types.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/ServiceContext.h>          // 사용자 공개
// SystemContext.h 는 여기서 include 하지 않는다 (D-27) — 사용자에게 시스템 노출 금지.
// EngineContext.h 는 호스트 전용이라 프렐류드 트리에 없다.
#include <JBro/Script/Macros.h>

// 서비스 프렐류드 (도메인별. 게임 스크립트가 접근할 표면만).
#include <JBro/Runtime/Service/TimeService.h>
#include <JBro/Runtime/Service/InputService.h>
#include <JBro/Runtime/Service/AudioService.h>
#include <JBro/Runtime/Service/AssetService.h>
#include <JBro/Runtime/Service/GameObjectService.h>
#include <JBro/Runtime/Service/CanvasService.h>

// 컴포넌트 · 프레임워크는 사용자 선택 (2D 혹은 3D). 여기서 미리 include 하지 않고,
// 사용자가 자기 파일에서 필요한 것만 include 한다.
// - #include <JBro/Framework2D/Component/Transform2D.h>
// - #include <JBro/Framework2D/Service/Physics2DService.h>

// SystemContext 는 여기서 include 하지 않는다 — 사용자가 시스템에 접근하면 안 되므로.
// 컨텍스트 헤더 (Context.h) 는 SystemContext 를 전방 선언만 하고 정의는 host 쪽 TU 에.

using namespace JBro;
```

**주의**: `SystemContext.h` 는 프렐류드 include 트리에 없어야 한다. 게임 DLL 이 `BindSystemContext`
를 호출하려면 그 함수 선언만 필요한데, 그 선언은 `SystemContext.h` 안에 있으므로 DLL 은 자기 TU
안에서만 `SystemContext.h` 를 include (프렐류드 밖). 사용자 스크립트 파일은 SystemContext 존재
자체를 모른다.

### H8. `JBRO_SCRIPT` 매크로

**How** (`JBroCore/Include/JBro/Script/Macros.h` 신규):

```cpp
#pragma once

// 스크립트 클래스 선언 매크로. 에디터 코드 생성기가 이 마커를 grep 해서 자동 등록한다.
// class 로 직접 적으면 컴파일은 되지만 에디터 목록에 안 뜬다 — 사고 방지용 매크로.
#define JBRO_SCRIPT(ClassName)  class ClassName
```

### H9. `EngineInstance` 조립 완성

**Why**: D4. 지금은 스텁. EngineContext 를 조립하는 유일한 지점.

**How** (`EngineInstance.cpp`):

```cpp
bool EngineInstance::Initialize(const EngineConfig& config, IPlatform& platform, IRHIModule& rhi)
{
    m_platform = &platform;
    m_rhi      = &rhi;

    // Renderer 조립 (W-platform 이 제공).
    m_renderer = MakeOwnerPtr<Renderer>();
    RendererConfig rc{ .api = config.graphicsApi, .surface = mainWindow.Surface };
    m_renderer->Initialize(rhi, rc);

    // 서비스 조립 (모든 Service 인스턴스 생성).
    m_time      = MakeOwnerPtr<Service::TimeService>();
    m_input     = MakeOwnerPtr<Service::InputService>(&platform);
    // ... 등

    // 컨텍스트 조립.
    m_engineContext.Platform  = m_platform;
    m_engineContext.RHI       = m_rhi;
    m_engineContext.Renderer  = m_renderer.get();
    // ... 등

    m_serviceContext.Time  = m_time.get();
    m_serviceContext.Input = m_input.get();
    // ... 등

    // 스크립트 DLL 로드 (있으면).
    if (config.scriptDLLPath[0] != '\0') {
        m_scriptLoader.Load(config.scriptDLLPath, platform);
    }
    BindSystemContext(m_systemContext);
    BindServiceContext(m_serviceContext);

    m_running = true;
    return true;
}
```

### H10. `JBroEditor` 최소 골격

**Why**: `EditorApplication` 클래스가 있지만 스텁. 에디터 UI 는 이 워크트리 범위 밖이지만,
`EditorApplication::Initialize` 가 `EngineInstance` 를 만들고 초기화하는 흐름은 완성해야.

**How**:

1. `EditorApplication::Initialize` — 플랫폼 생성 → RHI 모듈 생성 → EngineInstance 초기화.
2. `EditorApplication::Tick` — 프레임 시작 → 이벤트 처리 → `EngineInstance::Tick(dt)` → 프레임 끝.
3. UI 는 스텁 (Dear ImGui 통합 등은 별도 작업).

## 검증

- [ ] Debug / Release x64 빌드 통과
- [ ] `JBroTests` 통과
- [ ] 실제 스크립트 스텁 DLL 로 Load/Unload/Reload 왕복 성공 (한글 경로 포함)
- [ ] H6 실측 통과 (재로드 후 새 로그 문자열 확인)
- [ ] 프렐류드 자립: 사용자가 `#include <JBro/ScriptAPI.h>` 만 한 파일이 컴파일됨
- [ ] `SystemContext` 정의는 프렐류드 include 트리에 없음 (`ScriptAPI.h` include 후 사용자 TU 에서 `System::` 이름 못 씀)
- [ ] `EngineContext` 정의는 프렐류드 include 트리에 없음 (호스트 전용)
- [ ] `AbiVersion` 필드 불일치 시 로드 거부

## 다른 워크트리와의 인터페이스

- **W-ref 의존**: `Ref<T>`, `GameObjectHandle`, `Canvas`, `SafePtr` 실 구현이 있어야 컨텍스트가
  의미를 가짐. W-ref 병합 후 rebase.
- **W-platform 의존**: `IPlatform::LoadDynamicLibrary`, `Renderer`. W-platform 병합 후 rebase.
- **W-framework 협조**: Service 헤더 위치. 차원-종속 서비스는 Framework2D/3D 폴더 안이지만
  W-host 가 커밋.

## 병합

- **모든 다른 워크트리 병합 후에만** 이 워크트리 병합.
- 자체 검증 통과 → main 병합 → 최종.

## 2026-09-08 후속: 승인된 창·프레임 수명 구현

Updates: 위 H9 중 Renderer 조립과 프레임 루프. Context/서비스/DLL 전체 완료를 뜻하지 않는다.
독자: 현재 리팩터링을 이어받는 구현자. 단일 main 브랜치에서 순차 작업한다.

### 승인 및 적용 범위

- 사용자가 Framework·GPU 정리 후 창 파괴, 리사이즈 전달, 최소화 중 렌더링만 생략하는 안을 승인했다.
- `WM_CLOSE`는 닫기 요청만 기록해야 한다(MUST). 명시적 `ClosePlatformWindow` 전까지 HWND를 유지한다.
  `WM_QUIT`도 요청으로 처리한다. OS의 강제 프로세스 종료나 외부 HWND 파괴까지 지연시키는 계약은 아니다.
- `IPlatform::GetWindowState`는 메인 스레드에서 클라이언트 영역 크기와 최소화 여부를 조회한다.
  Windows는 구현했고, 기존 Web/Android 창 스텁은 상태 조회도 false로 명시한다. Web 기능 동등성 완료는 아니다.
- EngineInstance는 Renderer와 기존 AssetManager를 OwnerPtr로 소유한다. 주 창도 생성·파괴한다.
  전달받은 Platform/RHI 모듈과 IFramework 객체 자체는 빌리지 소유하지 않는다. 호출자는 이 객체들을
  EngineInstance보다 오래 유지해야 한다(MUST). Framework는 초기화되지 않은 상태로 전달해야 한다(MUST).
- FrameworkContext에 필요한 값만 전달한다. EngineContext/SystemContext/ServiceContext의 조립은 아직 없다.
- 마지막 소비자가 Renderer로 전환되어 임시 GraphicsSystem 헤더·소스는 삭제했다.

### 실제 흐름과 선택 이유

1. 설정 검증 → 창 생성/크기 조회 → Renderer → 기존 AssetManager → Framework 초기화.
2. Tick: 이벤트 처리 → 종료/치명적 디바이스 손실/dt 검사 → Framework Update → 창 상태 조회.
3. 최소화 또는 0 크기라면 GPU 프레임을 열지 않는다. 시뮬레이션은 계속 진행한다.
4. 유효한 크기가 이전과 다를 때만 ResizeSurface한다. 이벤트마다 즉시 리사이즈하는 방식은
   연속 크기 변경에 따른 불필요한 GPU 대기를 만들므로 사용하지 않는다.
5. BeginFrame이 Ready일 때만 Framework Render → EndFrame. Skipped는 계속 실행한다.
   나머지 오류나 제출 실패는 정리 후 Tick false로 전달한다. 디바이스 자동 복구는 추가하지 않았다.
6. 종료: 열린 프레임 Abort → Framework Shutdown → AssetManager Shutdown → Renderer Shutdown
   (WaitIdle/리소스/Swapchain/Device) → 창 파괴. Shutdown은 반복해도 안전하다.
7. 콜백 안에서 RequestExit 또는 Shutdown을 요청하면 해당 콜백이 반환한 뒤 정리한다.
   초기화·업데이트·렌더 중 예외는 정리 후 전파한다(초기화 bad_alloc은 false).
   Framework 종료 훅은 예외를 던지지 않아야 한다(MUST). 위반 시 오류를 기록하고 GPU/창 정리를 이어간다.

`EngineConfig.window`의 문자열 뷰는 Initialize 호출 중에만 사용하고 보관하지 않는다.
일반 프레임에서 새 저장 공간을 만들지 않는다. 리사이즈/종료의 GPU 대기는 일반 렌더 경로와 구분한다.

### 검증과 실제로 발견한 실패

- TDD: WM_CLOSE 후 HWND 유지 테스트가 기존 구현에서 실패하는 것을 확인한 뒤 플랫폼을 수정했다.
- Host 테스트를 먼저 추가해 미구현 EngineConfig/window 계약으로 컴파일 실패함을 확인한 뒤 구현했다.
- Debug/Release x64 전체 Rebuild: 경고 0, 오류 0. 양쪽 JBroTests 전체 통과.
  SDK는 기존 검증과 같은 WindowsTargetPlatformVersion=10.0.22621.0을 명시했다.
- 가짜 RHI: Framework→GPU→창 정리 순서, 중복 초기화 거부, 반복 Shutdown, 실패 후 재초기화,
  최소화/0 크기/복원, 동일 크기 Resize 생략, Skipped, 리사이즈·획득·Present 실패,
  최소화 중 DeviceLost, 콜백 종료 요청과 Update 예외를 검증했다.
- Debug CRT 할당 감시: 가짜 Platform/RHI를 사용한 정상 EngineInstance Tick 3회에서 할당 0.
  실제 OS/GPU 드라이버의 모든 할당을 측정한 수치나 구 엔진 대비 벤치마크는 아니다.
- 실제 Windows+D3D12+Framework2D: 숨김 창에서 스프라이트를 포함한 6프레임, 160×120 클라이언트 영역
  리사이즈 후 제출, WM_CLOSE 시 HWND 유지와 다음 Tick에서 Canvas/Renderer/HWND 정리를 확인했다.
- 새 smoke 구성에서 CreateObject 반환형을 잘못 사용한 컴파일 오류와 WorldTransform2D/주 카메라 누락에
  따른 spriteCount=0 실패가 발생했다. 현재 Canvas 선언과 기존 렌더 테스트 구성을 대조해 테스트만 수정했다.
- draw.io 6페이지에 초기화·프레임·역순 정리와 남은 범위를 추가했고 XML 파싱 및 연결 참조를 검사했다.
  이번 수정본은 브라우저 제어 연결 오류로 실제 draw.io 화면 렌더 확인을 완료하지 못했다.

### 남은 작업

- EditorApplication/GameHost 진입점은 아직 EngineInstance 루프를 호출하지 않는다.
- H1~H7의 Context/서비스/스크립트 DLL/프렐류드, ScriptSystem 자동 훅 호출은 별도 단계다.
- PixelPerfect 정의는 사용자 확인이 필요하다. Shader Graph·후처리·에셋/Layer 합성은 이 변경 범위 밖이다.
- 실제 최소화 OS 이벤트의 렌더 제출 측정, 화면 픽셀 정확성, 구 엔진 대비 성능 수치는 아직 검증하지 않았다.

# W-platform · 하드웨어 인접 계층

**브랜치**: `work/platform` (경로: `../JBro-platform`)
**병합 순서**: 2번 (독립. W-build 뒤 언제든)

## 목표

플랫폼 · RHI · 그래픽스 · 에셋 로더 — 하드웨어에 붙는 4개 모듈을 완성한다.
지금은 스텁이거나 껍데기만 있다.

**핵심**: `Renderer` 를 새로 도입해 `GraphicsSystem` 을 폐기한다. Renderer 가 `IRHIDevice` 를
부착 흡수한다. 상위 계층에는 네이티브 GPU 타입을 노출하지 않는다.

## 소유 파일

- `source/JBroEngine/Modules/JBroPlatform/**`
- `source/JBroEngine/Modules/JBroRHI/**`
- `source/JBroEngine/Modules/JBroD3D12RHI/**`
- `source/JBroEngine/Modules/JBroGraphics/**`
- `source/JBroEngine/Modules/JBroAsset/**`

이 5개 모듈 밖은 건드리지 않는다.

## 배경 — 이 워크트리에 처음 온 사람을 위한 설명

- 신규 리포의 하드웨어 4개 모듈은 헤더만 있고 몸통은 대부분 스텁이다.
- 기존 엔진 (`C:\Users\박주형\source\repos\JBroEngine\Engine\`) 에 실제 D3D11 구현이 있다.
  이번 워크트리는 그것을 D3D12 로 신규 구현하되, 기존 엔진의 아키텍처 (Renderer/RHIDevice/
  SurfaceHandle 관계) 를 참고한다.
- 다이어그램 (`docs/JBroEngine.drawio.xml` GRAPHICS/BACKEND 레인) 이 최종 형태.

### 의존 방향 (변경 후)

```
Framework2D/3D → Runtime → Graphics(Renderer) → RHI → Platform → Core
                                     ↓
                                   Asset
```

Platform 은 RHI 위가 아니라 아래. Graphics 가 Asset 을 소비 (텍스처/셰이더/메시).

## 작업 항목

### D2. `GraphicsApi` 는 RHI, `SurfaceHandle` 은 Platform (Platform → RHI 의존 없이)

**Why**: 이전 안은 `GraphicsApi` / `SurfaceHandle` 을 Core 로 옮기자는 것이었지만 철회했다.
그래픽스 개념은 RHI, 플랫폼 개념은 Platform. 그러면 의존은 자연스럽게 **RHI → Platform** 방향.

**How** (대부분 이미 되어 있음. 확인 위주):

1. `JBroRHI/Include/JBro/RHI/RHI.h` 에 `GraphicsApi` enum, `BufferHandle`, `TextureHandle`,
   `IRHIModule`, `IRHIDevice` 확인.
2. `JBroPlatform/Include/JBro/Platform/Platform.h` 에 `SurfaceHandle`, `WindowHandle`,
   `IPlatform` 확인.
3. `JBroRHI.vcxproj` 는 `JBroPlatform` 을 프로젝트 참조 — 이미 그런 방향인지 확인.
4. `JBroPlatform.vcxproj` 는 `JBroRHI` 를 참조하지 **않는지** 확인.

### D3. `IPlatform::CreateWindow / DestroyWindow` 개명

**Why**: Windows API 매크로 `CreateWindow` / `DestroyWindow` 가 include 순서에 따라 우리
멤버함수를 매크로 치환해 버려서 의도치 않은 컴파일 오류가 난다.

**How**:

1. `IPlatform` 의 `CreateWindow` → `OpenPlatformWindow`, `DestroyWindow` → `ClosePlatformWindow`
   로 개명.
2. `WindowsPlatform.cpp` / `WebPlatform.cpp` / `AndroidPlatform.cpp` 도 맞춤.
3. `WindowHandle` 타입은 유지.

### D4-a. 동적 라이브러리 로딩 API (H4 지원)

**Why**: 게임 스크립트 DLL 을 로드하려면 `LoadLibrary` / `GetProcAddress` / `FreeLibrary` 를
플랫폼별로 추상화해야 한다. 나중에 웹/안드로이드에서도 같은 API 로 다른 백엔드 (WebAssembly
동적 모듈, Android JNI) 를 쓴다.

**How**:

1. `IPlatform` 에 세 함수를 추가한다:
   ```cpp
   struct DynamicLibrary { void* opaque = nullptr; };
   virtual DynamicLibrary LoadDynamicLibrary(const char* path)             = 0;
   virtual void*          GetSymbol(DynamicLibrary lib, const char* name)  = 0;
   virtual void           UnloadDynamicLibrary(DynamicLibrary lib)         = 0;
   ```
2. `WindowsPlatform` 구현: `LoadLibraryA` / `GetProcAddress` / `FreeLibrary` 감싼다.
3. `WebPlatform` 구현: 지금은 스텁 (nullptr 반환). 웹 스크립트 DLL 은 나중에.
4. `AndroidPlatform` 구현: 지금은 스텁.

**W-host 가 이 API 를 소비**. 이 API 가 W-platform 에 들어가야 W-host 병합이 가능하다.

### C6 + D-17. Renderer 도입, GraphicsSystem 폐기

**Why**: 현재 `JBroGraphics::GraphicsSystem` 은 이름만 있고 사용자가 접근할 이유가 없다. Renderer
가 RHI 디바이스를 부착 흡수하고, 상위는 Renderer 인터페이스만 본다. 디바이스 로스트는 이 계층
안에서만 다룬다.

**How**:

1. **삭제**: `JBroGraphics::GraphicsSystem` 클래스 (헤더/소스 전체).
2. **신규**: `JBroGraphics/Include/JBro/Graphics/Renderer.h`
   ```cpp
   namespace JBro
   {
       class IRHIModule; class IRHIDevice;

       struct RendererConfig
       {
           GraphicsApi   api        = GraphicsApi::D3D12;
           SurfaceHandle surface;
           bool          validation = false;
       };

       class Renderer final
       {
       public:
           bool Initialize(IRHIModule& rhi, const RendererConfig& config);
           void Shutdown();

           void BeginFrame();
           void EndFrame();

           // 프레임 데이터 (RenderWorld2D 로부터 온) 를 처리하는 진입점.
           // 실제 시그니처는 W-framework 의 RenderWorld2D 계약이 확정된 뒤 결정.
           class RenderWorld2D;
           void Render(const RenderWorld2D& world);

           // 디바이스 로스트는 치명적 오류로 종료. 런타임 복구는 하지 않는다.
           bool IsDeviceLost() const;

       private:
           IRHIDevice* m_device = nullptr;
           // 세부 필드는 구현 단계에서 채운다.
       };
   }
   ```
3. `Renderer.cpp` 는 최소 스텁 (Initialize 는 `IRHIModule::CreateDevice` 호출까지만).
4. `JBroGraphics.vcxproj` 는 `JBroRHI` + `JBroAsset` 참조 확인.

**W-framework 가 `Renderer::Render(...)` 를 통해 자기 프레임 데이터를 넘긴다.** 시그니처 확정은
공유 헤더 편집이라 main 병합 → rebase 순서로 한다.

### C6-b. RHI 디바이스 포인터 노출 금지

**Why**: 상위 계층이 IRHIDevice 를 직접 만지면 디바이스 로스트 복구 지점이 여러 곳으로 흩어진다.
Renderer 안에 가둔다.

**How**:

1. `EngineInstance` (W-host 소관) 나 `Framework2D` 가 `IRHIDevice*` 를 멤버로 들지 않게 한다.
   이건 W-host 가 EngineInstance 리팩터링 시 지킬 규칙. W-platform 은 Renderer 인터페이스에서
   `IRHIDevice*` 를 반환하지 않도록만 확인.

### 에셋 — JBroAsset 완성

**Why**: 현재 `JBroAsset/Include/JBro/Asset/Asset.h` 는 `AssetHandle`, `AssetId`,
`AssetManager` 스텁만 있다. 텍스처/메시/스프라이트 로딩이 필요하다.

**How**:

1. `Asset.h` 에 다음 정의를 추가:
   ```cpp
   namespace JBro::Asset
   {
       struct TextureAsset { /* ... */ };
       struct SpriteAsset  { /* ... */ };
       struct MeshAsset    { /* ... */ };
       struct MaterialAsset { /* ... */ };
       struct ShaderAsset  { /* ... */ };
   }
   ```
2. `AssetManager` 에 `LoadTexture / LoadSprite / LoadMesh` 등의 API 를 추가하되, 실제 파일
   로딩은 이 워크트리 범위 밖. 인터페이스만 확정하고 몸통은 placeholder.

### D3D12 RHI 구현

**Why**: 신규 리포는 D3D11 을 안 가져오고 D3D12 로 시작한다. 지금 `JBroD3D12RHI` 는 클래스
껍데기만 있다.

**How**:

1. `IRHIModule::CreateDevice` — DXGI 팩토리 만들고 D3D12 디바이스 생성.
2. `IRHIDevice::CreateBuffer / CreateTexture / CreateSwapchain` 등 최소 API 정의.
3. **주의**: 이 태스크는 매우 크다. 스코프를 "1개 화면 클리어 + 스프라이트 하나 그리기" 까지로
   제한하고 나머지는 별도 추후 작업.

## 검증

- [ ] Debug / Release x64 클린 빌드 통과
- [ ] `dumpbin /dependents JBroPlatform.lib` 에 `JBroRHI` 가 없어야 (역방향 없음)
- [ ] `JBroTests` 통과 (Platform · Renderer 스켈레톤이 인스턴스화만 되어도 통과)
- [ ] D3 개명 후 `CreateWindow` / `DestroyWindow` grep 하면 매크로 아닌 사용처 0 건
- [ ] `IPlatform::LoadDynamicLibrary` 로 실제 dll (예: `Kernel32.dll`) 로드/언로드 왕복 테스트

## 다른 워크트리와의 인터페이스

- **W-host 는 `IPlatform::LoadDynamicLibrary` 를 소비**한다. 이 함수 시그니처가 확정된 뒤 W-host
  가 스크립트 로더를 짠다.
- **W-framework 는 `Renderer::Render(...)` 를 호출**하고 자기 `RenderWorld2D` 를 넘긴다. 시그니처
  협의 필요.
- `IRHIDevice*` 는 어느 워크트리에서도 상위로 노출하지 않는다 (Renderer 안에서만 만짐).

## 병합

- 자체 검증 통과 후 main 병합.
- `IPlatform` / `Renderer` 헤더가 바뀌므로 이 병합 후 W-framework · W-host 는 rebase 필수.

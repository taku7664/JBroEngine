# 8. 렌더링과 플랫폼

## 층이 셋이다

```
Framework2DSystem      RenderWorld2D 가 스프라이트를 추출·정렬하고, RenderBridge2D 가 POD 패킷으로 제출한다
       │  SpriteSubmit / MeshSubmit (ArrayView)
JBroGraphics           Renderer. 패킷을 모았다가 프레임 끝에 커맨드를 기록한다. Framework 타입을 모른다
       │  IRHIDevice / IRHICommandContext
JBroRHI                그래픽스 API 추상화. 핸들과 디스크립터만 있다
       │
JBroD3D12RHI           D3D12 구현. 디버그 레이어, GPU 기반 검증 스위치
```

플랫폼별 그래픽스 API 의존은 전부 RHI 뒤에 있다. 게임플레이·프레임워크·스크립트는 디바이스를 직접 잡지 않는다.
디바이스 로스트는 지금 **치명 오류로 보고 종료**한다(D-16). 나중에 복구를 넣을 때 영향이 국소적이도록 디바이스 포인터를 그래픽스 계층 밖으로 내보내지 않는 것이 규칙이고,
`Renderer::GetDevice()` 는 에디터가 게임 뷰 텍스처와 UI 파이프라인을 만들기 위한 **리소스 생성·파기 전용** 예외다(D-63).

## Renderer

`Submit*` 은 **즉시 그리지 않는다.** 프레임 패킷을 모으고, `EndFrame` 에서 컬링·정렬·배칭·커맨드 기록을 한다(D-32).
D-29 에서 "저수준 즉시 드로우" 로 정했다가 D-32 에서 보정한 것이다.

```cpp
class Renderer final
{
public:
    bool Initialize(IRHIModule& rhi, const RendererConfig& config);
    void Shutdown();

    FrameStatus BeginFrame(const FrameTarget& target = {});   // 비우면 백버퍼, 텍스처를 주면 거기에
    bool BeginView(const CameraParams& camera);
    bool SubmitSprite (const SpriteSubmit& item);              // 단건은 같은 수집 경로의 편의 함수
    bool SubmitSprites(JArrayView<SpriteSubmit> items);
    bool SubmitMesh   (const MeshSubmit& item);
    bool SubmitMeshes (JArrayView<MeshSubmit> items);
    bool EndView();
    FrameStatus EndFrame();
    void AbortFrame();

    bool ResizeSurface(const Extent2D& extent);
    RendererFrameStats GetLastFrameStats() const;
    bool SetFrameOverlay(FrameOverlay overlay, void* user);   // 에디터 UI 가 백버퍼에 얹는 자리
    IRHIDevice* GetDevice() const;                            // 리소스 생성·파기 전용
    bool ReadBackBuffer(std::byte* destination, std::size_t size, TextureReadback& result);   // 진단·테스트 전용
};
```

### 패킷

패킷은 D-32 ABI 다. 필드를 바꾸려면 Decisions 를 거친다.

```cpp
struct SpriteTransform2D { float linear[4]; float translation[2]; float depth; };   // 28B. 아핀 6 + 깊이 1
struct SpriteSubmit      { SpriteTransform2D world; AssetHandle sprite; AssetHandle material; float tint[4]; };
struct MeshSubmit        { Matrix4x4 world; AssetHandle mesh; AssetHandle material; };
struct CameraParams      { Matrix4x4 view, projection; float clearColor[4]; Viewport viewport; AssetHandle postProcessProfile; };
struct FrameTarget       { TextureHandle texture; Extent2D extent; };   // 텍스처를 줄 때는 크기도 준다
```

스프라이트 변환이 `Matrix4x4` 가 아닌 이유는 D-54 다. 4x4 로 넘길 때 사라지던 것은 항상 같던 z 행과 w 행뿐이고, 인스턴스가 80B 에서 44B 로 줄었다.
버텍스 셰이더는 `float4x4` 를 조립하지 않고 두 내적으로 위치를 만든다. `depth` 는 깊이 버퍼가 붙기 전까지 항상 0 이고, 자리를 비워 둔 것은 그때 ABI 를 다시 깨지 않기 위해서다.

GPU 인스턴스 레이아웃과 정점 속성 오프셋은 손으로 적지 않는다. `offsetof` 로 끌어오고 `static_assert` 로 고정한다.

### 정렬

정렬은 렌더러가 아니라 **Framework 가 제출 전에** 끝낸다(D-53). `RenderWorld2D` 가 `(uint64 key, uint32 index)` 배열을 정렬하고 아이템은 제자리에 둔다.
키는 `(layerOrder, renderOrder, sourceId)` 패킹이다. 레이어 순서가 최상위, 그 아래 부호를 옮긴 `renderOrder`, 같은 키일 때만 `sourceId` 로 안정화한다. 비가시 레이어는 추출 단계에서 건너뛴다.

### 게임 뷰는 렌더 타깃 하나 차이다

에디터에서 보는 화면과 실행해서 보는 화면이 달라지면 안 된다. 그래서 렌더 경로를 둘 만들지 않고 `BeginFrame` 의 **인자**로 타깃을 준다(D-63).
게임 실행은 백버퍼로, 에디터는 프로젝트 해상도의 텍스처로 부른다. 에디터 UI 는 뷰를 다 기록한 뒤 `FrameOverlay` 로 같은 프레임 안에서 백버퍼에 얹는다.
오버레이가 `false` 를 돌려주면 프레임을 버린다. 반쯤 그려진 UI 를 내보내지 않는다.

정상 렌더 프레임 경로는 힙 할당·문자열 생성·`WaitIdle` 을 하지 않는다. 텍스처 업로드와 되읽기는 GPU 를 기다리므로 **프레임 안에서는 거절**한다.

### 셰이더

`.hlsl` 원본과 컴파일된 DXIL 헤더(`*.generated.h`)를 함께 커밋한다. 빌드는 HLSL 을 컴파일하지 않는다. 클론에 셰이더 컴파일러가 없어도 빌드되기 위해서다.
`.hlsl` 을 고치면 `Modules/JBroGraphics/Shaders/Compile.ps1` 로 헤더를 다시 만들어 함께 커밋한다.

사용자 커스텀 포스트프로세스는 Shader Graph → Shader/Material 에셋 → `PostProcessProfile` 흐름으로 제공할 계획이다(D-33). 게임 스크립트에 Renderer·RHI·임의 GPU 콜백은 노출하지 않는다.

## RHI

`IRHIDevice` 가 리소스와 프레임을, `IRHICommandContext` 가 기록을 맡는다. 전부 핸들(`BufferHandle`, `TextureHandle`, `SamplerHandle`, `GraphicsPipelineHandle`, `SwapchainHandle`)이다.

```cpp
class IRHIDevice
{
public:
    virtual BufferHandle  CreateBuffer(const BufferDesc&) = 0;        virtual void DestroyBuffer(BufferHandle) = 0;
    virtual bool WriteBuffer(...) = 0;                                 // 프레임 밖에서만
    virtual TextureHandle CreateTexture(const TextureDesc&) = 0;      virtual void DestroyTexture(TextureHandle) = 0;
    virtual bool WriteTexture(...) = 0;                                // 프레임 밖에서만. GPU 를 기다린다
    virtual SamplerHandle CreateSampler(const SamplerDesc&) = 0;      virtual void DestroySampler(SamplerHandle) = 0;
    virtual GraphicsPipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc&) = 0;
    virtual SwapchainHandle CreateSwapchain(const SwapchainDesc&) = 0; virtual bool ResizeSwapchain(SwapchainHandle, const Extent2D&) = 0;
    virtual BeginFrameResult BeginFrame(SwapchainHandle) = 0;
    virtual FrameStatus EndFrame(const FrameContext&) = 0;
    virtual void AbortFrame(const FrameContext&) = 0;
    virtual void WaitIdle() = 0;
    virtual bool ReadTexture(...) = 0;                                 // 진단·테스트 전용. 프레임 중이면 실패
    virtual std::uint32_t GetFramesInFlight() const;
    virtual std::uint32_t GetValidationErrorCount() const;             // D3D12 는 info queue 를 읽는다
};
```

- **텍스처와 샘플러는 슬롯에 직접 묶는다**(D-61). 바인드 그룹도 바인들리스도 아니다. `SetTexture(slot, texture)` / `SetSampler(slot, sampler)` 이고 슬롯 번호가 곧 셰이더의 `t`/`s` 레지스터다.
  파이프라인이 개수를 미리 선언하고(`sampledTextureCount`, `samplerCount`), 선언한 자리를 비운 채 그리면 거절한다. 통과시키면 셰이더가 남의 디스크립터를 읽는다.
- 디스크립터 링과 매 프레임 덮어쓰는 버퍼는 **프레임 슬롯마다 갈라 둔다**(D-61, D-66). 겹쳐 도는 프레임이 아직 읽는 자리를 덮지 않기 위해서다. 슬롯 수는 `GetFramesInFlight()` 에 묻는다.
- RHI 는 정적 링크로 시작한다. 두 번째 Windows 백엔드가 실제로 생기면 DLL 로 올린다.

## 검증 레이어를 증거로 쓴다

그래픽 테스트는 픽셀만 보지 않고 **검증 레이어가 조용한지**까지 본다(D-64). 하드웨어가 잘못된 호출을 조용히 주워 담아 그 기계에서만 맞게 나오는 일이 두 번 있었다.

- D3D12 디버그 레이어는 **프로세스 단위이고 첫 디바이스 생성 전에 켜야 한다**(`EnableD3D12ValidationForProcess()`, `TestMain` 맨 앞). 늦게 켜면 아무 말 없이 무시되고, 아무것도 안 세는 0 을 증거로 믿게 된다.
- WARNING 까지 센다. 인덱스 버퍼 초과가 WARNING 으로 나온다. 최적 클리어 값 권고(ID 820) 하나만 제외한다.
- GPU 기반 검증도 켠다. 기본 레이어는 그릴 때 디스크립터 테이블의 리소스 상태를 보지 않는다.
- **손잡이가 도는지 자체를 재는 테스트**가 있다. 일부러 틀린 호출 하나로 숫자가 올라가야 한다. 그것이 없으면 다른 테스트의 "조용했다" 가 전부 공허해진다.
- 검증 레이어가 조용해도 GPU 는 죽을 수 있다(D-75). 프레임 밖에서 명령 할당자를 되감기 전에 그것을 쓰던 프레임이 끝났는지 기다린다. API 호출은 흠이 없어 레이어가 아무 말도 하지 않고, 프레임이 밀려 있을 때만 죽는다.

## 플랫폼

```cpp
class IPlatform : public IModule
{
public:
    virtual WindowHandle  OpenPlatformWindow(const WindowDesc&) = 0;
    virtual void          ClosePlatformWindow(WindowHandle) = 0;
    virtual SurfaceHandle CreateSurface(WindowHandle) = 0;
    virtual void PumpEvents() = 0;
    virtual JArrayView<InputEvent> GetInputEvents() const = 0;   // 지난 PumpEvents 가 모은 것. 다음 PumpEvents 가 비운다
    virtual void WaitForEvents(std::uint32_t timeoutMilliseconds) = 0;
    virtual bool ShouldClose(WindowHandle) const = 0;
    virtual bool GetWindowState(WindowHandle, WindowState&) const = 0;
    virtual DynamicLibrary LoadDynamicLibrary(const char* utf8Path) = 0;
    virtual void* GetSymbol(DynamicLibrary, const char* name) = 0;
    virtual void UnloadDynamicLibrary(DynamicLibrary) = 0;
};
```

### 입력은 이벤트다

매 프레임 읽어 가는 키 상태 배열이 아니라 **플랫폼이 이벤트로 모아 준다**(D-62). 폴링식은 한 프레임 안에 눌렀다 뗀 키와 글자 입력 순서가 사라지는데, 에디터의 텍스트 필드가 바로 그것을 필요로 한다.

```cpp
enum class InputEventKind : std::uint8_t { KeyDown, KeyUp, Text, MouseMove, MouseButtonDown, MouseButtonUp, MouseWheel, FocusGained, FocusLost };

struct InputEvent                  // 20B POD. 게임 DLL 경계를 넘는다
{
    InputEventKind kind;
    Key            key;            // 물리 키. Key::A 는 QWERTY 의 A 자리다
    MouseButton    button;
    KeyModifiers   modifiers;
    bool           repeat;         // 자동 반복으로 다시 온 KeyDown
    float          x, y;           // MouseMove 는 픽셀 좌표, MouseWheel 은 칸 수
    std::uint32_t  codePoint;      // Text 의 유니코드 코드포인트
};
```

키는 **물리 키**다. 글자는 `Text` 이벤트로 따로 온다. 같은 키가 배열과 조합에 따라 다른 글자를 내기 때문이다.
한 프레임 상한은 4096개이고 넘치면 버린다. Win32 에서는 좌우 Shift 와 키패드 Enter 를 스캔코드로 갈라 내고, BMP 밖 글자는 UTF-16 반쪽 둘을 합친다.

### 게임 호스트의 대기

`GameHost` 는 정상 렌더 프레임(`Ready`) 뒤에 인위적인 대기를 넣지 않는다. 렌더를 생략한 프레임(`Skipped`) 뒤에는 `WaitForEvents` 로 최대 약 16ms 기다리되 OS 메시지가 오면 즉시 깬다(D-35).
고정 `Sleep` 은 창 이벤트 응답을 늦추므로 쓰지 않는다.

## 에셋

`JBroAssetTypes`(값 타입, Tier S)와 `JBroAsset`(시스템, Tier E)로 나뉜다(D-50). `AssetManager` 라는 이름은 쓰지 않는다.

```cpp
class AssetSystem final : public IModule     // 프로젝트 수명. 로드·캐시를 소유한다
{
public:
    AssetHandle Load(AssetId id);            // [스텁]
    AssetHandle LoadTexture / LoadSprite / LoadMesh / LoadMaterial / LoadShader(AssetId id);
    bool IsLoaded(AssetHandle handle) const;
};
class AssetRegistry;                          // 메타데이터. 로드 소유를 합치지 않는다
```

**로드는 아직 스텁이다.** 스프라이트 렌더러의 `spriteId` 는 저장되지만 `sprite` 핸들을 채우는 해석 패스가 없어 화면에는 틴트만 나온다. 로드가 생기면 씬 로드 뒤와 인스펙터 변경 시에 한 번씩 돌면 되고, 렌더 추출은 지금도 앞으로도 핸들만 읽는다.

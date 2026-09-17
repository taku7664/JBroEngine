# Framework3D · 백엔드 · 기즈모 계획

> 계약은 `docs/ProjectRule.md`, 결정은 `tasks/todo.md` Decisions 다. 이 문서는 그 둘을 향해 가는
> 순서와 상태를 적는다. 상태는 항목마다 `[완료]` `[진행]` `[가정]` `[열림]` 으로 붙인다.
> `[가정]` 은 사용자 확인 없이 정한 기본값이다 - 방향이 틀리면 그 항목만 뒤집을 수 있게 좁게 잡았다.

## 0. 시작 시점의 실측 (2026-09-18)

- **Framework3D 는 골격이다.** `Transform3D`(position·rotation·scale, 월드 캐시 없음), `Camera3D`(FOV 하나),
  `MeshRenderer3D`(meshId·materialId 와 해석되지 않는 핸들), `Rigidbody3D`·`Collider3D`. 시스템이 하나도 없고
  `Framework3D::Render` 는 `NothingToSubmit` 을 돌려준다. 2D 에는 `Transform2DSystem`·`Camera2DSystem`·
  `SpriteRender2DSystem`·`Physics2DSystem`·`ScriptSystem` 과 `RenderWorld2D`·`RenderBridge2D` 가 있다.
- **렌더러는 메시를 세기만 한다.** `SubmitMesh` 가 패킷을 모으지만 `RecordViews` 는 스프라이트 파이프라인만
  그린다. 메시 지오메트리를 GPU 에 올리는 길이 없고, `AssetSystem::Load` 는 늘 빈 핸들을 돌려준다.
  깊이 버퍼가 없다 - `D3D12CommandContext::BeginRenderPass` 는 `depthStencilAttachment` 가 있으면 거절한다.
  텍스처 쪽(DSV 생성)과 파이프라인 쪽(`depthFormat`)은 이미 D32Float 를 안다.
- **RHI 는 D3D12 하나다.** `GraphicsApi` 에 `D3D12`·`Vulkan`·`WebGPU` 가 있고 D3D11 은 없다. 기존 엔진
  `Engine/Core/RHI/` 에 D3D11(1,690줄)·Vulkan(2,683줄)·WebGPU 백엔드가 **다른 인터페이스**(객체형
  `IRHIBuffer`·`IRHITexture`)로 있다. 이 기계에 Vulkan SDK 1.4.350.0 과 Windows SDK 10.0.22621 의 `dxc` 가 있다.
- **기즈모는 없다.** 기존 엔진에도 없다. 게임 뷰 패널은 텍스처를 붙이기만 하고 마우스를 받지 않는다.
- 셰이더는 HLSL 을 `dxc` 로 DXIL 헤더로 구워 커밋한다(`Shaders/Compile.ps1`). Vulkan 은 SPIR-V 가 필요하다.

## 1. 순서

| 단계 | 내용 | 상태 |
|---|---|---|
| 1 | Framework3D 시스템(Transform·Camera·MeshRender)과 `RenderWorld3D`·`RenderBridge3D`, 렌더러 메시 파이프라인과 깊이 버퍼, D3D12 깊이 첨부, 빌트인 정육면체 | `[완료]` D-106 |
| 2 | `JBroD3D11RHI` - `IRHIDevice` 를 즉시 컨텍스트로 채움, `GraphicsApi::D3D11`, 같은 픽셀 테스트를 두 백엔드에 | `[완료]` D-107 |
| 3 | `JBroVulkanRHI` - Vulkan 1.3 동적 렌더링으로 `IRHIDevice` 를 채움, SPIR-V 헤더 생성(Vulkan SDK `dxc -spirv`), 렌더러·에디터 UI 가 API 별 바이트코드를 고름 | `[완료]` D-108 |
| 4 | 기즈모 - 게임 뷰 위 ImGui 오버레이로 이동·회전·크기 손잡이, 마우스 집기, `SetPropertyCommand` 로 편집. 3D(`Transform3D`)와 2D(`Transform2D`)가 같은 뼈대 | `[완료]` D-109 |

각 단계는 테스트·뮤테이션·문서·커밋을 끝내고 다음으로 간다. 단계 안에서 막힌 것은 `[열림]` 으로 적고
다음 항목으로 넘어간다 - 사용자 지시("모르는 부분이 있으면 유연하게 넘어가고 보고").

## 2. 1단계 설계

### 2.1 컴포넌트

- `Transform3D` 에 월드 캐시를 더한다: `worldPosition`(Vec3)·`worldRotation`(Quaternion)·`worldScale`(Vec3)·
  `worldValid`, 전부 `NoSerialize | ReadOnly | Category("World cache")`(2D 와 같은 모양).
  `[가정]` 월드 캐시는 **분해된 값**이고 행렬이 아니다. `Matrix4x4` 는 `JBroGraphics` 소유라(§10.1) 컴포넌트
  라이브러리 `JBroFramework3D` 가 들 수 없고, 스크립트 프렐류드에 렌더러 타입이 새어 나가면 안 된다(§5).
  대가: 비균등 스케일 아래의 회전이 만드는 전단(shear)은 자식에게 전해지지 않는다 - Unity 와 같은 근사다.
  행렬은 `Framework3DSystem` 이 렌더 월드를 뜰 때 만든다.
- `Camera3D`: `projection`(Perspective·Orthographic), `verticalFieldOfView`, `orthographicSize`, `nearPlane` 0.1,
  `farPlane` 1000, `clearColor`, `primary`. 2D 와 같은 이름을 쓴다.
- `MeshRenderer3D`: `tint`(Color)·`visible` 을 더한다. `meshId` → `mesh` 핸들 해석은 시스템이 매 프레임 빈 핸들만
  채운다.

### 2.2 수학

`Math3D.h` 에 hot-path 용 inline 함수만 둔다: `Vec3` 덧셈·뺄셈·배·내적·외적·길이·정규화, `Quaternion` 곱·
정규화·벡터 회전·축각·오일러(ZXY)·역. 행렬은 `Framework3DSystem` 안의 `Math3DMatrix.h`(Graphics 의 `Matrix4x4`
를 받아 TRS·원근·직교·뷰 역행렬을 만든다). 규약은 렌더러와 같다: 열 벡터, `values[row*4+col]`, 깊이 0..1,
오른손 좌표, 카메라는 -Z 를 본다. `[가정]`

### 2.3 시스템과 렌더 월드

- `Transform3DSystem`(순서 100): 2D 와 같이 뿌리에서 내려가며 월드 캐시를 채운다.
- `Camera3DSystem`(300): 주 카메라 하나를 `RenderCamera3D` 로 뜬다(월드 위치·회전·투영 값·클리어 색).
- `MeshRender3DSystem`(400): 보이는 `MeshRenderer3D` 를 `MeshRenderItem`(월드 TRS·핸들·tint)으로 뜬다.
  `meshId` 가 있고 `mesh` 핸들이 비었으면 `MeshLibrary` 에서 해석한다.
- `RenderWorld3D`: 카메라 하나 + 메시 항목 배열. `RenderBridge3D::SubmitRenderWorld3D` 가 `CameraParams`(뷰 =
  카메라 TRS 의 역, 투영 = 원근/직교)와 `MeshSubmit`(TRS 행렬)로 렌더러에 넘긴다.
- `MeshLibrary`(Framework3DSystem): `AssetId → AssetHandle`. 초기화 때 빌트인 정육면체를 렌더러에 등록하고
  `builtin/cube` 라는 `AssetId`(`MakeStableTypeId` 해시)로 판다. `[가정]` `AssetSystem` 이 실제로 로드하게
  되면 이 표는 그쪽으로 옮긴다.

### 2.4 렌더러

- `Renderer::RegisterMesh(vertices, indices) -> AssetHandle` / `UnregisterMesh`. 정점은 `MeshVertex{position, normal}`.
  `[가정]` 핸들 모양을 `AssetHandle` 로 쓰는 것은 `MeshSubmit::mesh` 가 그 타입이기 때문이다. 발급자가
  렌더러라는 것은 `MeshLibrary` 만 안다.
- 메시 파이프라인: 인스턴스 버퍼(월드 4x4 + tint), 푸시 상수 = 뷰·투영, 램버트 조명 하나(고정 방향), 깊이 쓰기·
  비교 `LESS_EQUAL`, 뒷면 컬링. 셰이더 `BuiltinMesh.hlsl` 을 `Compile.ps1` 에 더한다.
- 깊이 텍스처: 프레임 타깃의 크기마다 하나(`D32Float`, 백버퍼 크기·게임 뷰 크기). 크기가 바뀌면 다시 만든다.
  뷰에 메시가 있을 때만 깊이 첨부를 단다 - 스프라이트만 있는 2D 프레임은 지금과 한 바이트도 다르지 않아야 한다.
- D3D12: `BeginRenderPass` 가 깊이 첨부를 받는다(DSV 해석, `DEPTH_WRITE` 전이, 클리어, 네이티브 렌더 패스의
  `D3D12_RENDER_PASS_DEPTH_STENCIL_DESC`).

### 2.5 검증

- 수학: 축각 회전·사원수 곱·역·TRS·원근 투영의 손계산 값.
- 시스템: 계층 두 단의 월드 캐시, 주 카메라 하나만 뜨기, 보이지 않는 메시 제외, 핸들 해석.
- 픽셀: 정육면체 하나를 텍스처에 그려 가운데가 클리어 색이 아니고 모서리는 클리어 색인지, 가까운 정육면체가
  먼 것을 가리는지(깊이) 본다. 스프라이트 픽셀 테스트는 그대로 통과해야 한다(깊이 첨부가 2D 를 건드리지 않음).
- 뮤테이션은 단계마다 돌린다.

## 2.6 1단계 실측 (2026-09-18)

- 정육면체 하나가 텍스처 가운데에 tint 비율(1 : 0.5 : 0.25)을 지킨 채 조명을 받아 그려지고 모서리는 클리어
  색이다. 가까운 붉은 상자를 먼저, 먼 초록 상자를 나중에 제출해도 가운데는 붉다 - 깊이 버퍼가 순서를
  대신한다. 같은 렌더러로 다음 프레임을 백버퍼에 그려도 된다(깊이 텍스처가 타깃마다 따로다). D3D12 디버그
  레이어는 조용했다. 스프라이트 픽셀 테스트는 그대로 통과한다.
- **디바이스는 프레임 안에서 자원을 만들지 않는다.** 처음엔 `RecordViews` 에서 깊이 텍스처를 만들려 했고
  `CreateTexture` 가 `m_frameActive` 로 거절해 프레임이 실패했다. 지금은 `Renderer::BeginFrame` 이 타깃
  크기로 미리 확보한다 - 2D 프레임이 내는 값은 크기가 바뀔 때의 텍스처 하나뿐이다.
- **GPU 를 기다리지 않는다.** 깊이 텍스처 크기가 바뀔 때와 메시를 내릴 때 `WaitIdle` 을 넣었다가 렌더러 계약
  테스트("보통 프레임은 GPU 를 기다리지 않는다")가 잡았다. D3D12 디바이스가 은퇴 펜스로 파기를 미루므로
  기다릴 이유가 없다.
- 테스트가 늘어난 것: `Framework3DSystemTests`(사원수·행렬 규약, 계층 캐시, 주 카메라 하나, 메시 추출과 핸들
  해석, 렌더러 없는 프레임워크), `MeshPixelTests`(정육면체, 깊이). 고친 것: 3D 필드 개수 단언, 셧다운이 지우는
  파이프라인 수(2).

## 2.7 2단계 실측 (2026-09-18)

- **기존 엔진 코드는 참고만 했다.** 옛 D3D11 백엔드는 객체형 인터페이스(`OwnerPtr<IRHIBuffer>`)라 새 핸들·세대
  계약에 그대로 옮길 것이 거의 없었다. 디바이스 생성 플래그·스왑체인·상태 객체 만드는 법만 같다.
- **플립 모델은 제시한 버퍼를 다시 읽을 수 없다.** 첫 D3D11 스모크 테스트가 "백버퍼가 클리어 색이어야 한다" 에서
  졌다 - `Present` 뒤 버퍼 0 은 다음 프레임의 것이다. 제시 직전에 사본(`presentedCopy`)을 뜨고 되읽기는 사본을
  읽는다. 프레임마다 복사 하나가 든다 - 되읽기는 진단 전용(§2)이지만 언제 부를지 모르니 늘 뜬다. `[가정]`
- **셰이더 파일은 ASCII 로 쓴다.** fxc 는 UTF-8 BOM 을 `Illegal character` 로 거절하고, BOM 을 떼면 dxc 가
  한글 주석에서 `0x80070459`(유니코드 변환 없음)로 죽는다. `EditorUI.hlsl` 의 한글 주석을 영어로 옮겼다.
  `.hlsl` 은 §14 의 BOM 규칙에서 빠진다(세 컴파일러가 같은 파일을 읽는다).
- **MSBuild 는 `%TEMP%` 아래의 헤더 읽기를 기록하지 않는다.** 스크래치 워크트리가 `F:\AI\Temp` 아래에 있어
  `CL.read.1.tlog` 에 JBro 헤더가 하나도 없었고, 헤더만 고친 뮤테이션은 다시 컴파일되지 않고 "살아남았다".
  1단계 뮤테이션의 헤더 변이 둘(뷰 행렬의 평행이동 회전, 원근 깊이 오프셋)이 그것이다 - 워크트리를
  `F:\AI\wt` 로 옮긴 뒤 다시 돌린 결과가 아래 표다. **이 함정은 사람 빌드에도 있다**: TEMP 아래에서 빌드하면
  헤더를 고쳐도 옛 오브젝트가 남는다. 이전 세션의 편집기 작업은 워크트리에서 헤더만 고친 회차가 없었다
  (헤더를 고칠 때마다 .cpp 도 함께 바뀌었거나 HEAD 가 바뀌어 전체 빌드였다).
- **D3D11 이 D3D12 와 다르게 행동하는 자리**: 프레임 슬롯 하나(`GetFramesInFlight() == 1`) - 오버레이 테스트의
  "슬롯이 바뀐다" 단언은 슬롯이 둘 이상일 때만 본다. 푸시 상수는 파이프라인마다 상수 버퍼 하나(b0)다.
  검증 오류는 `ID3D11InfoQueue` 의 ERROR·CORRUPTION 메시지 수다.

## 2.8 3단계 실측 (2026-09-18)

- **기존 엔진 코드는 쓰지 않았다.** 옛 Vulkan 백엔드는 렌더 패스·프레임버퍼 객체와 객체형 인터페이스 위에 있었다.
  새 모듈은 Vulkan 1.3 의 동적 렌더링(`vkCmdBeginRendering`)과 synchronization2 로 렌더 패스 객체 없이 선다.
  1.3 미만 디바이스는 거절한다 - 이 기계(RTX 2080, 1.4.341)와 Vulkan SDK 1.4.350 에서 실측했다.
- **`vulkan-1.dll` 은 실행 시간에 연다.** 함수 표(`VulkanLoader.h`, X 매크로)를 `vkGetInstanceProcAddr` 로 채운다.
  모듈을 링크하는 실행 파일 넷(에디터·두 호스트·테스트)은 Vulkan SDK 의 가져오기 라이브러리를 몰라도 되고,
  드라이버 없는 기계에서는 `VulkanRHIModule::Initialize` 가 false 라 테스트가 건너뛴다. API 헤더는 SDK 가 아니라
  `ThirdParty/Vulkan-Headers`(1.4.350.0, 헤더 넷 + `vk_video/`)에서 읽는다 - 클론이 SDK 없이 빌드된다.
- **SPIR-V 는 Vulkan SDK 의 dxc 가 굽는다.** Windows SDK 의 dxc 는 SPIR-V 코드젠이 빠져 있다. 세 `Compile.ps1`
  (Graphics·Editor·Tests)이 `-spirv -D JBRO_SPIRV=1 -fvk-t-shift 8 0 -fvk-s-shift 16 0` 으로 `*_SPV.generated.h` 를
  더 만든다. 푸시 상수 b0 은 `[[vk::push_constant]] ConstantBuffer<T>` 라야 하는데 fxc 가 그 문법을 모르므로
  `#if defined(JBRO_SPIRV)` 로 두 표기를 나눴다 - DXIL·DXBC 헤더는 바이트 하나도 바뀌지 않았다(실측).
  텍스처는 set 0 binding 8+, 샘플러는 16+ 다. `ATTRIBUTEn` 의미소는 location n 이 된다.
- **좌표 규약은 뷰포트 뒤집기로 맞췄다.** Vulkan 의 NDC 는 +y 가 아래다. 높이를 음수로 준 `VkViewport`(1.1 부터
  허용)로 뒤집어 D3D 와 같은 투영 행렬을 그대로 쓴다. 그러면 화면에서 시계 방향이 앞면이라 `frontFace` 는
  `CLOCKWISE` 다(D3D 의 `FrontCounterClockwise=FALSE` 와 같은 뜻). 깊이는 두 API 모두 0..1 이다. 스프라이트·메시
  픽셀 테스트가 세 백엔드에서 같은 픽셀을 읽는 것으로 확인했다.
- **스왑체인 크기는 표면이 정한다.** DXGI 는 요청한 크기(64x64)로 만들어 창에 늘려 주지만, Vulkan 의
  `currentExtent` 는 창의 클라이언트 크기고 스왑체인은 그것과 같아야 한다. 창의 최소 너비 때문에 64 짝 시험 창의
  표면이 120x64 였고 첫 되읽기가 여기서 졌다. 계약의 크기(`SwapchainDesc::extent`)는 요청값으로 남기고 이미지는
  표면 크기로 만들어 왼쪽 위만 그리고 되읽는다. 요청이 표면보다 크면 만들 수 없다. `[가정]`
- **파기는 프레임 펜스 뒤로 미룬다.** `Destroy*` 는 은퇴 목록(`m_retired`, 4096 칸)에 넣고, 슬롯 펜스를 기다린
  `BeginFrame` 이 그 프레임까지의 것을 지운다. 목록이 넘치면 한 번 `vkDeviceWaitIdle` 한다. 자원마다
  `VkDeviceMemory` 하나다 - 자원 상한(버퍼 1024·텍스처 512)이 드라이버의 할당 상한(4096) 아래다. `[가정]`
- **레이아웃 전이는 패스 밖에서만.** 동적 렌더링 안에는 배리어를 넣을 수 없다. 색 첨부는 `BeginRenderPass` 앞에서
  `COLOR_ATTACHMENT_OPTIMAL` 로, Sampled 로 만든 첨부는 `EndRenderPass` 뒤에서 `SHADER_READ_ONLY_OPTIMAL` 로 간다.
  `SetTexture` 는 그 레이아웃이 아닌 텍스처를 거절한다(그린 적도 올린 적도 없는 것).
- **되읽기는 D3D11 과 같은 사본이다.** 제시 직전에 `vkCmdCopyImage` 로 `presentedCopy` 를 뜬다. `AbortFrame` 은
  얻어 온 이미지를 사본 없이 그대로 제시한다 - 돌려주지 않으면 다음 획득이 막힌다. `[가정]`
- **검증은 `VK_LAYER_KHRONOS_validation` + `VK_EXT_debug_utils`.** ERROR 심각도 메시지를 센다. 레이어의 눈은 CPU
  쪽이라 "인덱스 버퍼 밖을 그리는" D3D12 식 실수는 못 본다 - 텍스처 바인딩 테스트의 고의 실수는 Vulkan 에서 너비 0
  뷰포트(VUID-VkViewport-width-01770)로 바꿨고 레이어가 2건을 잡았다.
- **테스트**: 스프라이트·메시 픽셀, 텍스처 바인딩(6종, D3D12·Vulkan), 에디터 화면(세 백엔드), Vulkan 스모크.
  텍스처 바인딩 테스트는 D3D11 에서 돌지 않는다 - 그 셰이더의 SM 5.0 헤더를 굽지 않았다. `[가정]`
- **뮤테이션 15 개 중 14 개가 죽었다.** 첫 회차에 살아남은 셋이 실제 결함 하나와 테스트 구멍 둘을 드러냈다:
  - `front-face-counter-clockwise` 가 살아남아 메시 테스트에 앞면 밝기 단언(램버트 0.587)을 더했더니 **D3D12 에서도
    졌다** - 1단계의 정육면체가 반시계로 감겨 바깥 면이 전부 컬링되고 안쪽 면(앰비언트 0.25)이 보이고 있었다.
    세 백엔드가 모두 화면에서 시계 방향을 앞면으로 보므로 인덱스를 시계 방향으로 바꿨다. D-106 의 "CCW 가 앞면" 은
    틀린 기록이었다. 뮤테이션이 없었으면 색 비율만 보는 테스트가 계속 통과했을 것이다.
  - `alpha-blend-off` 는 D3D11 에서도 살아남았던 것이다. 반투명 스프라이트를 그리는 픽셀 테스트가 없었다 - 검은 바탕 위
    반투명 흰색이 회색으로 읽히는 단언을 더해 세 백엔드에서 죽였다.
  - `retire-destroys-immediately`(파기를 미루지 않고 바로 지움)는 살아남았다. 검증 레이어는 "제출된 명령이 쓰는 중"인
    객체의 파기를 잡지만, 테스트의 프레임이 짧아 파기 시점에 GPU 가 이미 끝나 있다. 관측할 손잡이가 없다 - `[열림]`.
- **첫 뮤테이션 회차가 통째로 가짜였다.** 워크트리를 `git checkout` 으로 되돌리며 `tasks/*.md` 가 CRLF 로 돌아가
  컴파일러 규칙 테스트가 첫 줄에서 죽었고, 도구는 그것을 "죽었다" 로 셌다(15/15, 1분). 뮤테이션 결과는 **한 회차의
  시간**과 대조한다 - 회차 하나가 빌드 시간보다 짧으면 결과가 아니라 실패다. `wt-test.sh` 가 LF 원본을 복사하는 이유가
  이것이고, 뮤테이션 도구를 돌리기 전에도 같은 복사가 필요하다.

## 2.9 4단계 실측 (2026-09-18)

- **수학은 ImGui 없이 섰다.** `GizmoModel`(JBroEditor 공개 헤더)은 뷰-투영 행렬과 화면 사각형만 받아 손잡이 픽셀
  좌표·집기·끌기를 계산한다. 직교 2D 카메라와 원근 3D 카메라에서 같은 코드다 - 광선은 역행렬로 NDC 깊이 0 과 1 을
  풀어 만들고, 축 이동은 축 직선과 광선의 최근접점, 자유 이동은 화면 평면과의 교점, 회전은 화면 각도, 크기는 축 위치의
  비율이다. 회전 부호는 축을 0.1 라디안 돌린 점을 투영해 화면에서 어느 쪽으로 돌았는지 잰다 - 카메라가 축의 뒤에 있으면
  부호가 뒤집히는 것을 테스트가 확인했다.
- **손잡이는 픽셀 크기다.** 축 70px, 고리 반지름 60px, 가운데 7px. 카메라 거리에 무관하다.
- **2D 는 평면인 3D 다.** `GizmoSubject::planar` 로 Z 축 손잡이가 빠지고 회전은 Z 고리 하나다. `Transform2D` 의 각도(도)는
  Z 사원수로 바꿔 넣고, 결과의 Z 사원수 각을 도로 도(度)로 돌린다.
- **카메라는 렌더러의 지난 프레임 것이다.** UI 가 엔진 프레임보다 먼저 만들어지므로 `Renderer::GetLastViewCamera` 가
  마지막으로 기록된 프레임의 첫 뷰 카메라를 준다. 한 프레임 늦지만 기즈모를 끄는 동안 카메라가 움직이는 일은 드물다. `[가정]`
  뷰포트는 게임 텍스처 픽셀이라 그림이 패널에 붙은 크기(레터박스)로 옮긴다.
- **편집은 인스펙터의 드래그와 같은 길이다.** 끄는 동안 위젯처럼 필드에 직접 쓰고, 놓으면 편집 전 값으로 되돌린 뒤
  `SetPropertyCommand` 묶음 하나를 실행한다 - 되돌리기 한 번이 끌기 하나다. 대상은 고른 것 중 맨 위 것들이고 주된 것의
  월드 델타를 각자의 부모 좌표계로 돌려 로컬에 얹는다(이동은 부모 회전의 역과 스케일 나눔, 회전은 `q_p^-1 Δq q_p q`,
  크기는 비율). 여럿을 회전하면 각자 제 중심을 돈다(피벗 없음). `[가정]`
- **손잡이마다 ImGui Id 가 있다**(`##gizmo_x` 등). 마우스 아래 손잡이를 `SetHoveredID` 로 알리고 끄는 동안 `SetActiveID`
  로 잡는다. 그래서 (1) 창이 함께 끌리지 않고, (2) 테스트가 화면을 훑어 손잡이를 찾는다. 대신 hovered Id 가 있으면
  ImGui 가 클릭으로 창에 포커스를 주지 않으므로 잡을 때 `FocusWindow` 를 직접 부른다 - 안 부르면 W·E·R 이 안 먹는다(실측).
- **모드는 단추 줄과 W·E·R.** 라벨은 로컬라이징 키(`gizmo.translate` 이동·`gizmo.rotate` 회전·`gizmo.scale` 크기)고
  Id 꼬리(`##gizmo_translate` 등)는 고정이다. 핫키는 창에 포커스가 있거나 마우스가 그 위에 있고 글자 입력 중이 아닐 때.
- **테스트**: `GizmoModelTests`(투영·집기·이동·회전 부호·크기·원근 자기일관성)와 에디터 테스트 하나(X 손잡이 끌기 →
  +x 만 움직임·되돌리기 한 단계·E 로 고리·끌면 회전·R 로 x 상자·끌면 x 만 커짐). 3D 오브젝트의 기즈모는 수학 테스트가
  원근 카메라로 덮고, 에디터 안에서는 2D 프로젝트로만 돌렸다. `[가정]`
- **뮤테이션 11 개 중 9 개가 죽었다.** 첫 회차 7/11 에서 살아남은 넷 중 셋이 테스트 구멍이었다: 부모가 돈 자식의
  델타 변환(90도 부모 아래 자식을 위로 끌면 로컬 +x), 잡았을 때의 포커스(마우스를 창 밖으로 뺀 뒤 E), 빈 커맨드
  실행. 앞의 둘은 단언을 더해 죽였고, 빈 커맨드 쪽은 `CompoundCommand::Execute` 가 빈 묶음을 이미 거절하는 것을
  확인해 기즈모의 중복 검사를 뺐다(동치 변이). 남은 하나 `commit-keeps-widget-value`(놓을 때 편집 전 값으로 되돌리지
  않음)는 커맨드가 같은 값을 다시 쓰므로 결과가 같은 동치 변이다 - 인스펙터와 같은 길을 지키는 규율의 문제다.

## 3. 열린 것

- `[열림]` 물리 3D 시스템은 이 계획에 없다. `Rigidbody3D`·`Collider3D` 는 골격으로 남는다.
- `[열림]` 재질(`materialId`)은 해석하지 않는다. 메시 파이프라인 하나가 전부다.
- `[열림]` 스크립트 3D API(`ScriptAPI.h`)는 컴포넌트 헤더만 노출한다. 3D 서비스(레이캐스트 등)는 없다.
- `[열림]` `ProjectRule.md` §3 은 "두 번째 Windows 백엔드가 실제로 생기면 RHI 를 DLL 로 승격한다(SHOULD)" 고 적었다.
  이제 셋이다. 승격은 방향 판단이라 하지 않았다 - 사용자 확인 뒤 한다.
- `[열림]` Vulkan 의 버퍼는 전부 호스트에서 보이는 메모리다(`MemoryType::Device` 도 BAR 힙을 먼저 찾을 뿐이다).
  정점·인덱스 버퍼가 큰 게임에서는 디바이스 로컬 업로드 경로가 필요하다. 지금 자원 규모에서는 문제가 아니다.
- `[열림]` Vulkan 의 `PresentMode::Immediate` 는 IMMEDIATE, 없으면 MAILBOX, 없으면 FIFO 다. 어느 것이 잡혔는지
  읽는 길이 없다 - 검증할 손잡이가 없어 뮤테이션 대상에서 뺐다.
- `[열림]` Vulkan 의 미룬 파기(`m_retired`)는 테스트로 관측되지 않는다(§2.8). 바로 지우는 변이가 살아남는다.
- `[열림]` Vulkan 코드 리뷰(2026-09-18)에서 고치지 않고 남긴 것: (1) 스왑체인을 지울 때 `vkDeviceWaitIdle` 은 제시
  엔진을 기다리지 않아 `renderFinished` 세마포어가 아직 제시에 잡혀 있을 수 있다 - 바른 해법은
  `VK_KHR_swapchain_maintenance1` 의 제시 펜스다. 검증 레이어는 이 기계에서 조용했다. (2) 디스크립터 풀은 슬롯마다
  set 4096 개다. 한 프레임에 텍스처를 바꾸는 드로우가 그보다 많으면 나머지 드로우가 조용히 빠진다 - 넘침 풀을 두는
  것이 해법이다. (3) 세 RHI 모듈의 `CreateDevice` 는 `new (std::nothrow)`/`delete` 를 쓴다(D3D12 부터 이어진 모양).
  `MakeOwnerPtr` 로 바꾸는 것은 셋을 함께 고칠 일이다. 고친 것은 커밋 메시지에 있다(WaitIdle 완료 번호, 제출 실패
  경로, 크기 바꾸기 실패, 슬롯 수 변경, 은퇴 목록 넘침, 부분 로드 종료).
- `[열림]` 기즈모는 대상의 로컬 축만 쓴다(월드 축 전환 없음). 스냅(격자·각도)도 없다. 여럿을 고르고 회전하면 각자 제
  중심을 돈다 - 공통 피벗을 도는 것은 방향 판단이라 두지 않았다.
- `[열림]` 기즈모의 3D 편집은 에디터 안에서 시험하지 않았다(수학 테스트의 원근 카메라만). 3D 프로젝트를 여는 에디터
  테스트가 서면 같은 손짓을 `Transform3D` 에도 돌려야 한다.
- `[열림]` `GameViewPanel` 은 그림을 붙이는 데 ImGui 를 직접 부른다(`Image`·`GetContentRegionAvail`, D-63 부터). §11.1
  의 잔여 직접 호출이다 - 기즈모는 위젯(`Widget::Gizmo`)을 거친다.
- `[열림]` 게임 호스트·에디터 호스트는 설정(`EngineConfig::graphicsApi`)으로 백엔드를 고르지만 그 값을 명령줄이나
  프로젝트 파일에서 읽는 길은 없다. 지금은 코드 기본값 D3D12 다.

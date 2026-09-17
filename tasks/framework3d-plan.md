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
| 3 | `JBroVulkanRHI` - 기존 엔진 Vulkan 백엔드 이식, SPIR-V 헤더 생성(`dxc -spirv`), 파이프라인 설명자가 API 별 바이트코드를 받음 | 시작 전 |
| 4 | 기즈모 - 게임 뷰 위 ImGui 오버레이로 이동·회전·크기 손잡이, 마우스 집기, `SetPropertyCommand` 로 편집. 3D(`Transform3D`)와 2D(`Transform2D`)가 같은 뼈대 | 시작 전 |

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

## 3. 열린 것

- `[열림]` 물리 3D 시스템은 이 계획에 없다. `Rigidbody3D`·`Collider3D` 는 골격으로 남는다.
- `[열림]` 재질(`materialId`)은 해석하지 않는다. 메시 파이프라인 하나가 전부다.
- `[열림]` 스크립트 3D API(`ScriptAPI.h`)는 컴포넌트 헤더만 노출한다. 3D 서비스(레이캐스트 등)는 없다.
- `[열림]` `ProjectRule.md` §3 은 "두 번째 Windows 백엔드가 실제로 생기면 RHI 를 DLL 로 승격한다(SHOULD)" 고 적었다.
  이제 둘이다. 승격은 방향 판단이라 하지 않았다 - 사용자 확인 뒤 한다.
- `[열림]` 게임 호스트·에디터 호스트는 설정(`EngineConfig::graphicsApi`)으로 백엔드를 고르지만 그 값을 명령줄이나
  프로젝트 파일에서 읽는 길은 없다. 지금은 코드 기본값 D3D12 다.

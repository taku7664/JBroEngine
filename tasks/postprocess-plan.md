# 포스트프로세스 계획

후처리를 얹을 바탕을 먼저 깐다. 2026-10-07 사용자 지시는 "확장성 우선, 지금 쓰지 않으니 나중을 위한 초석을 다 깔아 둔다" 이다.
그래서 이 계획의 앞 단계는 효과가 아니라 RHI·렌더 그래프·효과 등록·프로필 에셋·셰이더 에셋이고, 빌트인 효과는 그 초석이 맞는지 재는 시험대로만 둔다.
계약은 `ProjectRule.md` §2(Shader Graph → Shader/Material → PostProcessProfile, 게임 스크립트에 GPU 콜백 없음)와 D-33·D-111 이고, 이 계획의 결정은 D-322 이다.

## 1. 지금 있는 것 (2026-10-07 코드로 확인)

### 기존 엔진 (`C:\Users\박주형\source\repos\JBroEngine`)

- 후처리는 Reinhard 톤맵 하나뿐이다(`Engine/Core/Renderer/Forward2DRenderer.cpp:677` `TONEMAP_SHADER_SOURCE_HLSL`, `Render2DPipeline.cpp:697` `Tonemap` 패스).
  블룸·컬러 그레이딩·프로필 에셋은 없다. 저장소 전체에서 `bloom`·`postprocess` 를 찾아 이 두 파일만 나왔다.
- 렌더 패스 그래프(`RenderWeave`, `RWPassDesc{Reads, Write, Execute}`)는 있었다. 읽기·쓰기 선언 위에 라이팅·합성·톤맵이 섰다.
- 아팠던 것: P1. Vulkan 은 톤맵 셰이더가 없어 blit 로 조용히 떨어진다(`Forward2DRenderer.cpp:2165`). P2. 노출 없는 Reinhard 라 빛 1 아래의 흰색이 0.5 가 된다(라이팅 계획의 L4).

### 우리 렌더러 (`Modules/JBroGraphics`)

- `CameraParams::postProcessProfile`(`Renderer.h:104`)은 필드만 있고 읽는 곳이 없다.
- 렌더 그래프가 없다. `Renderer::RecordViews`(`Renderer.cpp:1197`) 하나가 뷰마다 라이트맵 → 메인 패스 → 레이어 묶음 합성 → 뷰 합성 → 선택 외곽선을 곧바로 기록한다. 기록 순서가 곧 의존이다.
- 중간 텍스처는 역할·크기로 찾는 고정 풀 둘이다 - `m_layerTargets[8]`(`Layer`·`Backdrop`·`LightMap`·`ShadowMask`)과 `m_depthTargets[4]`. 이번 프레임에 바란 크기를 적고 다음 `BeginFrame` 에 만들며, 300 프레임 쓰지 않으면 지운다(`Renderer.cpp:1775`·`:1800`).
- 장면 타깃은 백버퍼 포맷(`BGRA8Unorm`)이다. 장면 파이프라인의 색 포맷이 모두 `m_config.backBufferFormat` 이다. `RGBA16Float` 는 라이트맵·그림자 마스크에만 쓴다.
- 전체 화면 패스는 ±0.5 쿼드를 VS 가 두 배로 펼치고, PS 가 `Texture.Load(SV_POSITION)` 으로 읽는다. 입력과 출력이 같은 크기여야 한다.
- 셰이더는 `Shaders/Compile.ps1` 을 손으로 돌려 세 바이트코드(DXIL·DXBC·SPIR-V)를 `*.generated.h` 로 커밋한다. 셰이더 하나를 더하면 스크립트의 배열 셋과 `Renderer.cpp` 의 include 블록 셋을 함께 고친다.
- 에디터 기즈모·격자는 렌더러를 거치지 않고 ImGui 로 캔버스 이미지 위에 그린다. 렌더러가 장면 위에 덧그리는 에디터 요소는 선택 외곽선뿐이다.

### RHI (`Modules/JBroRHI` 와 세 백엔드)

| 항목 | 지금 | 후처리에 필요한 것 |
|---|---|---|
| 상수 | 푸시 상수 하나(Vulkan 128 B, D3D12·D3D11 256 B) | 슬롯 여러 개의 상수 버퍼와 프레임 업로드 링 |
| 상수 버퍼 | `BufferUsage::Constant` 로 만들 수는 있으나 묶는 API 가 없다 | `SetConstantBuffer(slot, buffer, offset, size)` |
| 텍스처·샘플러 | 픽셀 셰이더만, 8 장·4 개 고정 | 단계(VS·PS·CS)별 바인딩 레이아웃 |
| 컴퓨트 | 셰이더 단계·파이프라인·`Dispatch` 모두 없음 | 컴퓨트 파이프라인과 `Dispatch` |
| 스토리지 | 텍스처 `Storage` 플래그만 있다. D3D11 은 무시, Vulkan 은 거절, D3D12 는 UAV 를 만들지 않는다 | UAV 텍스처·구조화 버퍼 |
| mip | 렌더 타깃은 mip 0 만 | mip 단위 첨부·읽기·UAV |
| 상태 | 텍스처 하나에 상태 하나, 패스 끝에 PS 읽기로 되돌린다 | 서브리소스 단위, 쓰기 직전에 전이, 컴퓨트 단계 |
| 깊이 읽기 | 세 백엔드 모두 막혔다(원인이 백엔드마다 다르다) | 깊이를 텍스처로 읽기 |
| 포맷 | RGBA8·BGRA8(+sRGB)·RGBA16F·D32F | R11G11B10F·R16F·R32F·RG16F·RGBA32F |
| 그리기 | 인덱스 드로우만 | 비인덱스 `Draw`(전체 화면 삼각형) |
| 텍스처 차원 | 2D 만 | 3D 텍스처(컬러 그레이딩 LUT) |

### 에셋·리플렉션

- `MaterialAsset`·`ShaderAsset`(`AssetTypes.h:218-226`)은 `AssetId` 만 든 빈 껍데기이고 아무도 참조하지 않는다. 확장자 `.jmat`·`.hlsl` 만 등록되어 있다.
- 에셋 핸들의 위 4 비트가 타입이다(`Asset.h:242` `TypeShift = 28`). 타입은 열여섯까지이고 **세 자리 남았다**. 프로필·셰이더 그래프·볼륨 같은 것을 더하면 곧 찬다.
- 리플렉션에 다형 목록이 없다. 배열 원소 타입이 하나다. 가장 가까운 선례는 컴포넌트 목록(`CanvasFile.cpp:46-90`, `Type:` 키 + `PropertyRegistry::Lookup`)이다.
- 인스펙터의 목록 편집 커밋(`InspectorPanel.cpp:1800-1812`)은 컴포넌트 전용이다. 에셋에서 목록을 고치면 커밋되지 않는다.
- 에셋 인스펙터의 블록 구분이 `AssetEditScope` 의 불 플래그(`spriteBlock`·`fontFamilyBlock`)다. 타입이 늘 때마다 플래그가 는다.

## 2. 설계

계층은 아래에서 위로 다섯이다. 아래 계층은 위 계층을 모른다.

```text
5. 에디터          프로필 인스펙터 · 볼륨 기즈모 · 그래프 보기
4. 적용            카메라 · 레이어 · 볼륨 → 뷰마다 풀린 프로필
3. 데이터          PostProcessProfile 에셋 · Shader 에셋 · Material 에셋
2. 효과            효과 종류 표 · 주입 지점 · 파라미터 블록
1. 렌더 그래프     패스 · 리소스 선언 · 수명 · 배리어 · transient 풀
0. RHI             바인딩 레이아웃 · 상수 버퍼 · 컴퓨트 · 서브리소스 · 포맷
```

### 2.1 RHI

- **바인딩 레이아웃.** 파이프라인 설명에 고정 개수 대신 슬롯 표를 둔다 - 상수 버퍼·읽기 텍스처·UAV·샘플러마다 개수와 보이는 단계(VS·PS·CS). 상한은 장치 능력(`DeviceCaps`)으로 묻는다.
  Vulkan 은 지금 비어 있는 binding 0~7 에 상수 버퍼를 넣는다(`-fvk-b-shift`).
- **상수 버퍼.** `SetConstantBuffer(slot, buffer, offset, size)`. 프레임마다 쓰는 값은 프레임 슬롯마다 하나인 업로드 링에 쌓고 오프셋으로 묶는다 - 프레임 경로에 할당이 없다.
  D3D11 은 부분 묶기에 11.1 의 `*SetConstantBuffers1` 을 쓴다. 푸시 상수는 그대로 두고 128 B 상한을 RHI 계약으로 적는다.
- **컴퓨트.** `ShaderStage::Compute`·`ComputePipelineHandle`·`CreateComputePipeline`·`SetComputePipeline`·`Dispatch`. 렌더 패스 밖에서만 부른다.
- **스토리지.** UAV 텍스처와 구조화 버퍼(`BufferUsage::Storage`, stride).
- **서브리소스.** 첨부와 읽기와 UAV 가 mip·배열 조각을 고른다(`ColorAttachmentDesc.mipLevel` 과 텍스처 뷰).
- **상태.** 서브리소스마다 상태를 들고, 쓰기 직전에 필요한 상태로 옮긴다. 배리어는 렌더 그래프가 미리 계산해 RHI 에 넘길 수 있게 `Barrier` 호출을 연다 - RHI 의 자동 전이는 그래프 밖 경로(에디터·시험)를 위해 남긴다.
- **깊이 읽기·포맷·비인덱스 그리기·3D 텍스처.** 표의 오른쪽 열 그대로다.

### 2.2 렌더 그래프

`Modules/JBroGraphics` 안에만 산다. 게임 스크립트와 프레임워크는 그래프를 보지 않는다(D-33).

- **패스.** 종류는 래스터·컴퓨트·복사 셋이다. 패스는 읽는 리소스와 쓰는 리소스를 선언하고 실행 함수를 든다. 실행 함수는 함수 포인터 + 사용자 자료 포인터다 - `std::function` 을 쓰지 않는다(프레임 경로 할당).
- **리소스.** transient(뷰 크기 비례 또는 고정 크기·포맷·mip 수), import(백버퍼·뷰 타깃·에디터 텍스처), persistent(프레임을 넘는 히스토리 - 눈 적응·TAA) 셋이다.
- **컴파일.** 선언에서 실행 순서·수명·배리어를 낸다. 결과에 닿지 않는 패스는 버린다. 수명이 겹치지 않는 transient 는 같은 텍스처를 나눠 쓴다.
- **할당.** 그래프는 프레임마다 처음부터 짓고, 저장소는 프레임 시작에 비우는 고정 용량 아레나다. 실제 텍스처는 지금처럼 "이번 프레임에 바라고 다음 프레임에 만든다" 를 따른다 - 없는 프레임의 패스는 건너뛰고, 건너뛴 이유를 그래프가 안다.
- **옮기기.** 지금의 라이트맵·그림자·레이어 묶음·뷰 합성·외곽선을 그래프 패스로 옮긴다. 둘을 함께 두면 transient 풀이 둘이 된다.
- 공용 todo 의 "선형 구조, 완전한 DAG 는 과하다" 를 이 절이 대신한다(D-322 (1), 사용자 확인 2026-10-07).

### 2.3 효과

- **효과 종류 표.** 이름 → { 파라미터 블록의 리플렉션 표, 기본값, 블록 크기, 그래프에 패스를 더하는 함수, 필요한 것(HDR·깊이·히스토리) }. 빌트인 효과와 나중의 셰이더 그래프 효과가 **같은 모양으로** 등록된다.
  셰이더 그래프 효과는 "Material 에셋 하나를 전체 화면으로 그리는" 범용 종류 하나이고, 다중 패스는 컴파일된 그래프가 든 패스 목록이다(D-33).
- **주입 지점.** 효과가 들어가는 자리를 이름으로 고정한다.

  | 지점 | 입력 | 쓰임 |
  |---|---|---|
  | `AfterOpaque` | HDR 장면·깊이 | 3D 의 SSAO·포그 |
  | `AfterLayer` | 레이어 텍스처 | 레이어마다 프로필(블러·외곽선·그림자) |
  | `BeforeTonemap` | HDR 장면 | 블룸·DoF·노출 |
  | `Tonemap` | HDR → 출력 포맷 | 톤맵 하나 |
  | `AfterTonemap` | 출력 포맷 | 컬러 그레이딩·비네트·FXAA·픽셀화 |
  | `AfterScreenLayers` | 화면 레이어까지 얹은 출력 | 화면 전체에 거는 것(CRT) |

  선택 외곽선과 에디터 표시는 모든 지점 뒤다.
- **HDR.** 뷰의 장면 색 포맷은 그래프가 뷰마다 정한다. 프로필이 HDR 효과를 하나라도 쓰면 `RGBA16Float` 로 그리고 `Tonemap` 지점에서 출력 포맷으로 내린다.
  톤맵의 기본값은 `None`(자르기만)이다 - 라이팅이 정한 "빛 1 아래 흰색은 흰색"(L4)을 지킨다.
- **파라미터.** 효과마다 POD 블록이고 상수 버퍼로 올린다. 푸시 상수에 넣지 않는다(128 B 상한).

### 2.4 데이터

- **`PostProcessProfile` 에셋**(`.jpost`, YAML 본문). 항목 목록이고 항목은 `Type`·`Enabled`·필드마다 덮어쓰기 표시·필드 값이다. 컴포넌트 목록과 같은 `Type:` 판별자를 쓴다.
- **Shader 에셋.** 백엔드마다 바이트코드 + 바인딩 레이아웃 + 파라미터 블록 레이아웃(셰이더 리플렉션에서 뽑는다). 빌트인 셰이더도 Shader 에셋이다(D-111).
  셰이더 목록은 한 곳(매니페스트)에 두고 세 바이트코드와 메타를 만든다 - 배열 셋·include 셋을 따로 고치는 지금 방식을 걷는다.
  에디터는 dxc·d3dcompiler 로 그 자리에서 굽고(셰이더 그래프의 실시간 편집), 게임은 `.jpak` 에 미리 구운 것만 싣는다(D-322 (3)).
- **Material 에셋**(`.jmat`). `{ Shader 에셋, 파라미터 블록, 텍스처 슬롯 }`(D-111). 셰이더 그래프의 결과물이 이것이다.
- **에셋 타입 자리.** 핸들의 타입 비트를 4 에서 8 로 넓혔다(`TypeShift` 28 → 24, D-322 (2), 2026-10-07 섰다). 타입 256, 타입마다 슬롯 1,600 만이다.

### 2.5 적용

- **카메라.** `Camera2D`·`Camera3D` 의 `postProcessProfileId`. 이것이 기준 프로필이다.
- **레이어.** `Layer` 의 프로필. 그 레이어를 제 텍스처에 그린 뒤 `AfterLayer` 효과를 걸고 합성한다(레이어 계획 §5).
- **볼륨.** 새 컴포넌트 `PostProcessVolume2D`·`3D` - 전역 또는 모양(같은 오브젝트의 콜라이더), 우선순위, 섞는 거리, 무게, 프로필.
  카메라 위치에서 무게를 셈해 항목의 필드를 우선순위 순으로 섞는다. 섞기는 리플렉션의 필드 종류로 정한다(`Float`·`Color` 는 보간, `Bool`·열거는 절반에서 바꿈).
- **풀기.** 카메라 + 볼륨을 프레임워크 시스템이 풀어 뷰마다 "풀린 프로필"(효과 종류·블록 오프셋 목록 + 바이트 블록)로 렌더러에 넘긴다. `CameraParams` 에는 에셋 핸들 대신 이 풀린 프로필의 자리를 둔다.
- **스크립트.** 후처리 서비스는 볼륨의 무게와 프로필 바꾸기, 효과 필드 덮어쓰기만 연다. 렌더러·RHI·GPU 콜백은 열지 않는다.

### 2.6 에디터

- 프로필 인스펙터: 효과 추가(종류 표에서 고른다)·지우기·순서·켜고 끄기·필드마다 덮어쓰기. 에셋 범위의 목록 편집 커밋을 먼저 연다. 블록 플래그를 에셋 타입별 편집기 표로 바꾼다.
- 캔버스 뷰와 시뮬레이션 뷰 모두 후처리를 건다. 캔버스 뷰에는 끄는 토글을 둔다.
- 볼륨의 모양과 섞는 거리를 기즈모로 그린다.
- 렌더 그래프 보기(패스·리소스·수명). 디버그용이고 출시 빌드에서는 뺀다.

## 3. 단계

각 단계는 빌드·시험·뮤테이션 뒤 커밋한다. 픽셀 시험은 세 백엔드(D3D12·D3D11·Vulkan)에서 **디버그 레이어를 켜고** 잰다.

1. RHI 바인딩 레이아웃·상수 버퍼·업로드 링·비인덱스 `Draw`·포맷. **섰다(D-325, 2026-10-14) - Vulkan 의 그리기 시험만 남았다(§3.1).**
2. RHI 서브리소스(mip 첨부·읽기)·서브리소스 상태·깊이 읽기·3D 텍스처. **서브리소스·상태·깊이 읽기가 섰다(D-326, 2026-10-15). 3D 텍스처가 남았다(§3.2).**
3. RHI 컴퓨트·UAV·구조화 버퍼·`Barrier`.
4. 셰이더 매니페스트와 Shader 에셋(빌트인 셰이더를 옮긴다).
5. 렌더 그래프와 지금 패스 옮기기. 그림이 바뀌지 않는 것을 기존 픽셀 시험이 잰다.
6. 효과 종류 표·주입 지점·HDR 장면 색·톤맵. 시험대로 노출과 톤맵.
7. 블룸(mip 체인, 컴퓨트 또는 래스터)·LUT 컬러 그레이딩 - 2·3 단계를 실제로 쓰는 시험대.
8. `PostProcessProfile` 에셋·카메라의 프로필·에디터 인스펙터.
9. 레이어 프로필과 `AfterLayer`.
10. 볼륨과 섞기·스크립트 서비스.
11. Material 에셋과 범용 머티리얼 효과(셰이더 그래프 앞의 마지막 칸).

### 3.1 1 단계 - 바인딩 레이아웃·상수 버퍼·업로드 링·비인덱스 그리기·포맷 (D-325)

무엇이 섰나(계약의 원문은 `RHI.h`·`ConstantRing.h` 의 주석이다):

| 것 | 모양 |
|---|---|
| 자리표 | `GraphicsPipelineDesc::bindings` = `BindingLayoutDesc{constantBuffers, sampledTextures, samplers}`. 종류마다 `BindingRange{count, stages}`. 옛 `sampledTextureCount`·`samplerCount` 를 대신한다. 검사는 공용 `IsBindingLayoutValid` 하나 |
| 슬롯 | 상수 버퍼 슬롯 번호 = `b` 레지스터 번호. **b0 은 푸시 상수 자리라 비우고 b1..b7** 을 쓴다. SPIR-V 는 b-shift 없이 binding 1..7 - 비어 있던 0..7 안이다 |
| 상한 | `IRHIDevice::GetCaps()` → `DeviceCaps`. 상수 버퍼 7·텍스처 8·샘플러 4·푸시 상수 128 B·정렬 256 B·한 번에 묶는 크기 64 KiB(Vulkan 은 `maxUniformBufferRange` 와 작은 쪽)·`constantBufferOffsets` |
| 푸시 상수 | 세 백엔드 모두 128 B(`MaxPushConstantBytes`). D3D12·D3D11 의 256 B 를 내렸다. 지금 가장 큰 사용처는 80 B(`Renderer.cpp` 그림자 마스크) |
| 묶기 | `SetConstantBuffer(slot, buffer, offset, size)`. 오프셋·크기는 256 B 배수, 구간은 만들 때 받은 크기 안. 텍스처처럼 파이프라인을 바꾸면 버려지고, 선언한 슬롯이 비면 그리지 않는다 |
| 링 | `ConstantRing`(JBroRHI). 프레임 슬롯마다 `Upload` 상수 버퍼 하나, `BeginFrame(slot)` 으로 처음부터, `Push` 는 정렬된 다음 자리에 memcpy. 차면 빈 구간(되감지 않는다) |
| 그리기 | `DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance)`. 정점 버퍼 없는 파이프라인이 된다(전체 화면 삼각형) |
| 포맷 | `R11G11B10Float`·`R16Float`·`R32Float`·`RG16Float`·`RGBA32Float` 를 enum 끝에 더했다. 텍셀 크기는 공용 `GetTexelSize` 하나 |

백엔드마다 다른 것:

| | D3D12 | D3D11 | Vulkan |
|---|---|---|---|
| 상수 버퍼 | 슬롯마다 루트 CBV. 루트는 푸시 32 + CBV 7×2 + 표 2 = 48 DWORD | `*SetConstantBuffers1`(11.1) 로 구간을 묶는다. 없으면 오프셋 0 만 | `UNIFORM_BUFFER_DYNAMIC` + 동적 오프셋. set 캐시 키는 버퍼·크기이고 오프셋은 키가 아니다 |
| 링의 버퍼 | 업로드 힙, 슬롯 3 | `DYNAMIC` 버퍼 하나(슬롯 1). 오프셋 0 쓰기가 `DISCARD`, 그 뒤는 `NO_OVERWRITE` | 호스트 일관 메모리, 스왑체인 슬롯 수 |
| 단계 | 루트 파라미터 가시성 | 단계마다 `VS*`·`PS*` 를 따로 건다. 선언하지 않은 텍스처·샘플러 슬롯을 이제 거절한다(전에는 8·4 안이면 받았다) | 디스크립터 `stageFlags` |
| 포맷 | 모두 필수 지원 | 모두 필수 지원 | 쓰임마다 `vkGetPhysicalDeviceFormatProperties` 로 묻고 없으면 만들지 않는다(R11G11B10 의 색 첨부는 명세가 보장하지 않는다) |

시험(`ConstantBufferTests.cpp`·`RenderTargetFormatTests.cpp`, 시험 셰이더 `Tests/Shaders/ConstantProbe.hlsl`):

- 상한이 공통 바닥인지, 자리표 검사가 넘는 수·단계 없는 종류·모르는 단계를 거절하는지.
- 링이 정렬해 쌓고, 차면 거절하고, 프레임마다 처음부터 쓰고, 슬롯마다 다른 버퍼인지. 그리지 않으므로 세 백엔드에서 돈다.
- 정점 버퍼 없는 삼각형 둘: 첫째는 화면 전체 빨강, 둘째는 b1(정점 단계)로 왼쪽 위 사분면에만 초록. 둘째 값이 첫째 자리를 덮으면 전부 초록이 된다.
  b0·선언 밖 슬롯·어긋난 정렬·버퍼 밖 구간·상수 버퍼가 아닌 버퍼를 거절하고, 빈 슬롯과 파이프라인을 다시 건 뒤의 그리기를 거절한다.
- 같은 링으로 8 프레임을 그려 마지막 색이 남는지(D3D11 의 DISCARD 길과 슬롯 되감기).
- 다섯 포맷에 (2, 0.5, 0.25, 1) 을 그려 되읽으면 정확히 그 값인지 - 1 보다 큰 값이 UNORM 이면 잘려 드러난다.

뮤테이션(2026-10-14, D3D12·D3D11, 8 개 모두 죽었다):

| 망가뜨린 것 | 죽인 검사 |
|---|---|
| D3D12 루트 CBV 가 오프셋을 버린다 | 둘째 드로우의 왼쪽 위가 초록 |
| D3D11 링이 늘 `DISCARD` 로 쓴다 | 같음 |
| D3D11 이 `*SetConstantBuffers1` 대신 버퍼 통째로 묶는다 | 같음 |
| D3D12 `R16Float` 를 `R16_UNORM` 으로 | 1 보다 큰 값을 담는다 |
| D3D11 `R11G11B10Float` 를 `R10G10B10A2_UNORM` 으로 | 같음 |
| 링이 크기를 정렬로 올리지 않는다 | 첫 구간이 슬롯 처음부터 정렬 크기 |
| D3D12 상수 버퍼 가시성을 픽셀만 | 파이프라인 생성(정점 셰이더가 b1 을 읽는다) |
| D3D12 가 빈 상수 버퍼 슬롯으로 그린다 | 빈 슬롯의 그리기는 거절 |

실측 환경: 이 기계에는 VS 2026(v145)이 없어 `-p:PlatformToolset=v143` 로 빌드했다(파일은 고치지 않았다). 이 단계 전의 기준선에서
`PlatformContract`(COM 끄기 개수)·`TextureBinding`(Vulkan 검증 레이어 없음)·`EditorApplication`(팝업이 화면 가운데)이 이미 실패했다.
이 단계 뒤에 묶음 96 개를 하나씩 돌려 92 개가 통과했다. 실패 넷 중 `TextureBinding`·`EditorApplication` 은 위의 기준선 그대로이고,
`InputGamepad` 는 다시 돌리면 통과했으며(흔들림), `EditorLocalization` 은 아무도 쓰지 않는 키 `node_graph.add_node` 때문이다 - 노드 그래프 작업에서
남은 것이라 이 단계와 상관없고 따로 고친다. `PlatformContract` 는 이번에는 통과했다.

남은 것:

- **Vulkan 의 그리기 시험.** 이 기계에는 Vulkan SDK 의 dxc 가 없어 `ConstantProbe*_SPV.generated.h` 를 굽지 못했다(사용자 결정 "Vulkan은 나중에", 받지 않는다).
  Vulkan SDK 가 있는 기계에서 `pwsh Tests\Shaders\Compile.ps1` 을 돌리고, 두 시험 파일의 `HasProbeShaders`·API 검사에 Vulkan 을 넣는다.
  그때까지 Vulkan 의 상수 버퍼 묶기·`vkCmdDraw`·새 포맷은 **컴파일만 됐다**. 상한·링 시험은 Vulkan 에서도 돈다.
- 이 기계의 Vulkan 에는 검증 레이어가 없다. 그래서 `TextureBinding` 의 "검증 손잡이가 실제로 말하는지" 가 Vulkan 에서 실패한다 - 이 단계 전부터다(§3.1 실측).

### 3.2 2 단계 - 서브리소스·서브리소스 상태·깊이 읽기 (D-326)

무엇이 섰나:

| 것 | 모양 |
|---|---|
| mip 에 그리기 | `ColorAttachmentDesc::mipLevel`. 패스의 렌더 영역·기본 뷰포트는 그 mip 의 크기다. 백버퍼는 0 만 |
| mip 하나 읽기 | `SetTextureMip(slot, texture, mip)`. 셰이더에게는 mip 이 하나뿐인 텍스처다(`SampleLevel(..., 0)` 이 그 mip). `SetTexture` 는 전처럼 모든 mip |
| 뷰 | 백엔드가 텍스처를 만들 때 mip 마다 RTV·SRV(Vulkan 은 이미지 뷰)를 미리 만든다 - 그리기 경로에서 뷰를 만들지 않는다. 그래서 mip 수에 상한이 있다(`MaxTextureMipLevels` 16, `DeviceCaps::maxTextureMipLevels`) |
| mip 수 | `IsMipCountValid` - 가장 긴 변이 1 이 되는 mip 까지. 세 백엔드가 같은 검사를 쓴다. 깊이는 mip 1 개만 |
| 상태 | D3D12 는 mip 마다 상태, Vulkan 은 mip 마다 레이아웃을 든다. 패스는 그리는 mip 하나만 옮기고, 끝나면 그 mip 만 읽기 상태로 돌린다 |
| 깊이 읽기 | `DepthStencil | Sampled` 깊이를 다음 패스가 `SetTexture` 로 읽는다(R32 로 보인다) |
| 겹침 거절 | 지금 패스가 그리는 mip·깊이를 같은 패스에서 `SetTexture`·`SetTextureMip` 하면 거절한다(`RenderPassTargets`, 세 컨텍스트가 같은 검사) |
| 복사 | `CopyTexture` 는 mip 이 하나인 것만 받는다(통째 복사, 상태를 mip 0 으로 옮긴다) |
| 배열 | 세 백엔드 모두 `depthOrLayers == 1` 만 받는다. D3D12 만 배열을 반쯤 받고 있었고 쓰는 곳이 없었다. 3D 텍스처(§3 의 2 단계 남은 것)가 `depthOrLayers` 를 깊이로 쓴다 |

깊이 읽기가 막혔던 까닭(§1 의 표, 백엔드마다 달랐다):

| D3D12 | D3D11 | Vulkan |
|---|---|---|
| 자원은 typeless 로 잘 만들지만 패스 뒤에 `DEPTH_WRITE` 그대로 남아, 읽으면 상태가 어긋난다 | `D32_FLOAT` 그대로 `SHADER_RESOURCE` 를 붙여 텍스처 생성 자체가 실패한다. `R32_TYPELESS` + DSV `D32`·SRV `R32` 로 만든다 | 셰이더 읽기는 `SHADER_READ_ONLY` 레이아웃만 받는데, 깊이는 패스 뒤에 그 레이아웃으로 돌아가지 않았다 |

함께 고친 것:

- D3D12 의 읽기 상태가 `PIXEL_SHADER_RESOURCE` 하나뿐이라, 1 단계에서 연 정점 단계 텍스처 읽기가 검증 오류였다. `PIXEL | NON_PIXEL` 로 둔다(`ShaderReadState`).
- Vulkan 의 셰이더 읽기 전이가 프래그먼트 단계만 기다렸다. 정점 단계도 기다린다.
- Vulkan 은 mip 이 여럿인 텍스처를 색 첨부로 쓸 때 모든 mip 의 뷰를 넘기고 있었다(명세 위반, 첨부 뷰는 mip 하나).

시험(`SubresourceTests.cpp`, 셰이더 `Tests/Shaders/SubresourceProbe.hlsl`):

- mip 사슬 끝까지는 만들고 하나 더는 거절, 깊이는 mip 하나만, 읽는 깊이는 만들어진다 - 그리지 않으므로 세 백엔드에서 돈다.
- mip 0 빨강 → mip 0 을 읽어 mip 1 에 그림 → mip 2 초록 → 왼쪽 절반은 mip 1, 오른쪽 절반은 mip 2 를 읽어 되읽는다. 그리는 mip·텍스처 전체·없는 mip 의 읽기는 거절한다.
  D3D12 검증 레이어가 조용해야 한다.
- 왼쪽 절반에만 깊이 0.25 를 쓰고, 그 깊이를 그리는 패스에서는 읽기를 거절하고, 다음 패스가 R32Float 에 옮겨 되읽으면 왼쪽 0.25·오른쪽 1.0 이다.

뮤테이션(2026-10-15, 9 개 모두 죽었다):

| 망가뜨린 것 | 죽인 검사 |
|---|---|
| D3D12 패스 시작 배리어를 텍스처 전체로(예전 모양) | 검증 레이어 |
| D3D12 깊이를 패스 뒤에 읽기 상태로 돌리지 않음 | 검증 레이어 |
| D3D11 깊이를 typeless 로 만들지 않음 | 읽는 깊이 만들기 |
| D3D12 mip 하나 뷰 대신 모든 mip 뷰 | mip 2 의 초록 |
| D3D12·D3D11 이 그리는 mip·깊이의 읽기를 받음 | 그리는 mip 은 못 읽는다 |
| D3D11·D3D12 RTV 가 늘 mip 0 | mip 1 의 빨강 |
| mip 수 검사가 하나 더 받음 | 64 너비는 mip 일곱 |

남은 것:

- **3D 텍스처**(컬러 그레이딩 LUT)와 샘플러의 W 축. 다음 커밋이다.
- Vulkan 의 그리기 시험(§3.1 과 같은 까닭). Vulkan 은 mip·깊이 만들기와 거절만 돌았다.

## 4. 정한 것과 미정

정한 것(2026-10-07 사용자 확인, D-322):

| 물음 | 고른 것 |
|---|---|
| 렌더 그래프 | DAG. 지금 패스까지 옮긴다 |
| 에셋 타입 비트 | 8 비트 |
| 셰이더 빌드 | 매니페스트 + 에디터 런타임 컴파일 |
| 적용 단위 | 카메라·레이어·볼륨 모두 |

미정:

- 셰이더 그래프 편집기 자체는 이 계획 밖이다.
- 블룸을 컴퓨트로 할지 래스터로 할지는 7 단계에서 세 백엔드를 재고 정한다.
- 장면 HDR 포맷을 `RGBA16Float` 로 둘지 `R11G11B10Float` 로 둘지는 6 단계에서 정한다(알파가 필요한 레이어 합성과 맞물린다).

이 문서를 읽고 나면 어느 계층부터 손대야 하는지와, 단계마다 무엇이 서면 끝인지 알 수 있어야 한다.

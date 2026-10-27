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
  → D-339 로 렌더 그래프의 패스가 되었다(§3.6).
- 중간 텍스처는 역할·크기로 찾는 고정 풀 둘이다 - `m_layerTargets[8]`(`Layer`·`Backdrop`·`LightMap`·`ShadowMask`)과 `m_depthTargets[4]`. 이번 프레임에 바란 크기를 적고 다음 `BeginFrame` 에 만들며, 300 프레임 쓰지 않으면 지운다(`Renderer.cpp:1775`·`:1800`).
  → D-339 로 모양(크기·포맷·쓰임)으로 찾는 풀 하나(`RenderTargetPool`, 12 칸)가 되었다(§3.6).
- 장면 타깃은 백버퍼 포맷(`BGRA8Unorm`)이다. 장면 파이프라인의 색 포맷이 모두 `m_config.backBufferFormat` 이다. `RGBA16Float` 는 라이트맵·그림자 마스크에만 쓴다.
- 전체 화면 패스는 ±0.5 쿼드를 VS 가 두 배로 펼치고, PS 가 `Texture.Load(SV_POSITION)` 으로 읽는다. 입력과 출력이 같은 크기여야 한다.
- 셰이더는 `Shaders/Compile.ps1` 을 손으로 돌려 세 바이트코드(DXIL·DXBC·SPIR-V)를 `*.generated.h` 로 커밋한다. 셰이더 하나를 더하면 스크립트의 배열 셋과 `Renderer.cpp` 의 include 블록 셋을 함께 고친다. → D-329 로 매니페스트 한 곳이 되었다(§3.5).
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
- **상태.** 서브리소스마다 상태를 들고, 쓰기 직전에 필요한 상태로 옮긴다. 배리어는 렌더 그래프가 미리 계산해 RHI 에 넘길 수 있게 `Barrier` 호출을 연다(5 단계, D-328) - RHI 의 자동 전이는 그래프 밖 경로(에디터·시험)를 위해 남긴다.
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
- **볼륨.** 새 컴포넌트 `PostProcessVolume2D`·`3D` - 전역 또는 모양, 우선순위, 섞는 거리, 무게, 프로필. ~~모양은 같은 오브젝트의 콜라이더~~ → 모양은 볼륨이 직접 든다(D-346, 사용자 결정, §3.13).
  카메라 위치에서 무게를 셈해 항목의 필드를 우선순위 순으로 섞는다. 섞기는 리플렉션의 필드 종류로 정한다(`Float`·`Color` 는 보간, `Bool`·열거는 절반에서 바꿈).
- **풀기.** 카메라 + 볼륨을 프레임워크 시스템이 풀어 뷰마다 "풀린 프로필"(효과 종류·블록 오프셋 목록 + 바이트 블록)로 렌더러에 넘긴다. `CameraParams` 에는 에셋 핸들 대신 이 풀린 프로필의 자리를 둔다.
- **스크립트.** 후처리 서비스는 볼륨의 무게와 프로필 바꾸기, 효과 필드 덮어쓰기만 연다. 렌더러·RHI·GPU 콜백은 열지 않는다.

### 2.6 에디터

- 프로필 인스펙터: 효과 추가(종류 표에서 고른다)·지우기·순서·켜고 끄기·필드마다 덮어쓰기. 에셋 범위의 목록 편집 커밋을 먼저 연다. 블록 플래그를 에셋 타입별 편집기 표로 바꾼다.
- 캔버스 뷰와 시뮬레이션 뷰 모두 후처리를 건다. 캔버스 뷰에는 끄는 토글을 둔다.
- 볼륨의 모양과 섞는 거리를 기즈모로 그린다.
- 렌더 그래프 보기(패스·리소스·수명). 디버그용이고 출시 빌드에서는 뺀다. → D-340 으로 디버그 메뉴의 `렌더 그래프` 창이 섰다(§3.7).

## 3. 단계

각 단계는 빌드·시험·뮤테이션 뒤 커밋한다. 픽셀 시험은 세 백엔드(D3D12·D3D11·Vulkan)에서 **디버그 레이어를 켜고** 잰다.

1. RHI 바인딩 레이아웃·상수 버퍼·업로드 링·비인덱스 `Draw`·포맷. **섰다(D-325, 2026-10-14). Vulkan 의 그리기 시험도 D-340 으로 섰다(§3.7).**
2. RHI 서브리소스(mip 첨부·읽기)·서브리소스 상태·깊이 읽기·3D 텍스처. **섰다(D-326·D-327, 2026-10-15). Vulkan 의 그리기 시험도 D-340 으로 섰다(§3.7).**
3. RHI 컴퓨트·UAV·구조화 버퍼·`Barrier`. **컴퓨트·UAV·구조화 버퍼·자동 배리어가 섰다(D-328, 2026-10-15). 명시 `Barrier` API 는 5 단계로 옮겼다(사용자 결정).**
4. 셰이더 매니페스트와 Shader 에셋(빌트인 셰이더를 옮긴다). **매니페스트와 셰이더 자료형 `ShaderData` 가 섰다(D-329, 2026-10-15, §3.5). `.jshader` 에셋 파일과 에디터의 즉석 굽기는 11 단계로 옮겼다(사용자 결정).**
5. 렌더 그래프와 지금 패스 옮기기. 그림이 바뀌지 않는 것을 기존 픽셀 시험이 잰다. **명시 `Barrier` API 도 여기서 연다(D-328).**
   **섰다(D-339, 2026-10-26, §3.6). 그래프 보기와 Vulkan 의 복사 읽기 그리기 시험도 D-340 으로 섰다(§3.7).**
6. 효과 종류 표·주입 지점·HDR 장면 색·톤맵. 시험대로 노출과 톤맵. **섰다(D-342, 2026-10-27, §3.9).**
7. 블룸(mip 체인, 컴퓨트 또는 래스터)·LUT 컬러 그레이딩 - 2·3 단계를 실제로 쓰는 시험대. **섰다(D-343, 2026-10-27, §3.10) - 래스터 밉 체인, 기본 조정, `.cube` LUT.**
8. `PostProcessProfile` 에셋·카메라의 프로필·에디터 인스펙터. **섰다(D-344, 2026-10-27, §3.11).**
9. 레이어 프로필과 `AfterLayer`. **섰다(D-345, 2026-10-27, §3.12) - 블러·외곽선·그림자와 색 효과(기본 조정·LUT)가 레이어에 걸린다.**
10. 볼륨과 섞기·스크립트 서비스. **섰다(D-346, 2026-10-27, §3.13) - `PostProcessVolume2D`·`3D`(모양은 볼륨이 든다), 리플렉션 종류로 섞기, 공통 서비스 `PostProcess`.**
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

- **3D 텍스처**(컬러 그레이딩 LUT)와 샘플러의 W 축은 §3.3 에서 섰다.

### 3.3 2 단계의 끝 - 3D 텍스처·샘플러 W 축 (D-327)

| 것 | 모양 |
|---|---|
| 차원 | `TextureDesc::dimension`(`Texture2D`·`Texture3D`). 3D 는 `depthOrLayers` 가 깊이다 |
| 규칙 | 공용 `IsTextureDimensionValid` 하나 - 3D 는 셰이더가 읽기만 한다(`Sampled`·`CopySource`·`CopyDestination`), mip 하나, 세 변 모두 `DeviceCaps::maxTexture3DSize`(D3D 2048, Vulkan 은 `maxImageDimension3D`) 까지, 깊이 포맷은 안 된다. 2D 는 배열 없이 |
| 올리기 | `WriteTexture` 한 번에 조각 전부(깊이 0 부터 빽빽하게). `WriteTextureRegion`·`ReadTexture` 는 3D 를 거절한다 |
| 읽기 | `SetTexture`(mip 이 하나라 `SetTextureMip(…, 0)` 도 같다). 셰이더는 `Texture3D` |
| 샘플러 | `SamplerDesc::addressW`. 전에는 세 백엔드 모두 W 를 늘 Clamp 로 두었다 |

백엔드: D3D12 는 `TEXTURE3D` 자원과 SRV, 올리기는 사각형 올리기를 상자(`UploadTextureBox`)로 넓혔다 - 발자국 안에서 조각은 `RowPitch × 행 수` 간격이다.
D3D11 은 `ID3D11Texture3D` 를 따로 들고(`volume`) `UpdateSubresource` 에 조각 간격을 준다. Vulkan 은 `VK_IMAGE_TYPE_3D`·`VIEW_TYPE_3D`, 복사 상자의 깊이.

시험(`SubresourceTests.cpp` 의 `TestAVolumeIsReadByDepth`, 셰이더 `Tests/Shaders/VolumeProbe.hlsl`): 조각마다 한 색인 2×2×4 를 올리고 사분면마다 w 를 달리 읽는다 -
w 0.125 는 조각 0, 0.625 는 조각 2, 1.375 는 Repeat 로 조각 1·Clamp 로 마지막 조각. 모자란 올리기·사각형 올리기·되읽기·그리기 쓰임·mip 둘·상한 넘는 깊이·2D 배열은 거절한다.
만들기·올리기·거절은 Vulkan 에서도 돈다.

뮤테이션(2026-10-15, 7 개 모두 죽었다): D3D12·D3D11 의 W 를 늘 Clamp 로(Repeat 사분면), D3D12 조각 간격 무시(조각 0), D3D11 조각 간격 0(조각 2),
3D 쓰임 제한 없앰(그리기 쓰임 거절), Vulkan 올리기 크기에서 깊이 뺌(조각 전부 올리기). D3D12 볼륨을 2D SRV 로 보게 한 것은 그림 비교가 아니라 앞단의 올리기 검사에서
죽었다 - 잘못된 뷰를 만들며 장치가 망가진 것으로 보이고(추정), 원인을 따로 캐지는 않았다.

### 3.4 3 단계 - 컴퓨트·UAV 텍스처·구조화 버퍼·자동 배리어 (D-328)

| 것 | 모양 |
|---|---|
| 파이프라인 | `CreateComputePipeline(ComputePipelineDesc{computeShader, bindings})`·`SetComputePipeline`·`Dispatch(x, y, z)`. 렌더 패스 밖에서만 받는다. 자리표의 단계는 `ShaderStage::Compute` 하나 |
| 묶기 | 상수 버퍼·텍스처·샘플러는 그래픽과 같은 함수(`SetConstantBuffer`·`SetTexture`·`SetTextureMip`·`SetSampler`)다 - 걸린 파이프라인의 자리표로 묶인다 |
| UAV 텍스처 | `TextureUsage::Storage` 2D, mip 마다 UAV. `SetStorageTexture(u슬롯, texture, mip)` - u0..u3. 타입 있는 UAV 를 장치가 받는 포맷만(D3D 는 포맷 지원을, Vulkan 은 `STORAGE_IMAGE` 기능을 묻는다). sRGB·깊이는 안 된다 |
| 구조화 버퍼 | `BufferUsage::Storage` + `BufferDesc::stride`(4 의 배수, 크기를 나눠떨어지게), `Device` 메모리만. `SetStorageBuffer(u슬롯, buffer)` - u4..u7(텍스처가 몇이든 u4 부터) |
| 상한 | `DeviceCaps::maxStorageTextures`·`maxStorageBuffers` 4. 그래픽 파이프라인의 자리표는 storage 를 받지 않는다(`IsBindingLayoutValid(…, compute)`) |
| SPIR-V | u# 는 binding 24 부터(`-fvk-u-shift 24 0`) - 텍스처 u0..u3 은 24..27, 버퍼 u4..u7 은 28..31. 컴퓨트 진입점 이름은 `CSMain` |
| 배리어 | `Dispatch` 가 건다: 돌기 전에 UAV 대상을 쓰기 상태로 옮기고 이미 쓰기 상태인 것은 UAV 배리어(Vulkan 은 컴퓨트→컴퓨트 메모리 배리어), 돈 뒤에 `Sampled` 로도 만든 텍스처를 읽기 상태로 되돌린다. "패스 밖의 텍스처는 늘 읽기 상태" 가 그대로 선다. 버퍼는 쓰기 상태에 남는다 |

백엔드: D3D12 는 루트 시그니처에 u# 표 하나(u0.. 텍스처 범위 + u4.. 버퍼 범위)를 두고 `SetCompute*` 로 건다. UAV 스테이징 힙(텍스처 mip 마다, 버퍼마다)에서 보이는 링으로 복사한다.
D3D11 은 `CSSet*` 이고, UAV 는 디스패치 때 걸고 바로 뗀다(남겨 두면 다음 패스가 그 텍스처를 읽거나 그릴 때 D3D11 이 경고하며 떼어 낸다). 컴퓨트의 SRV 는 `BeginRenderPass` 에서 뗀다.
Vulkan 은 `VK_PIPELINE_BIND_POINT_COMPUTE`, `STORAGE_IMAGE`(GENERAL 레이아웃)·`STORAGE_BUFFER` 디스크립터. 거절한 디스패치는 레이아웃을 옮기기 전에 거절한다.

명시 `Barrier` API 는 이 단계에서 열지 않았다(2026-10-15 사용자 결정 "5단계로 미룸") - 쓰는 쪽인 렌더 그래프가 5 단계에서 서고, 그때 그래프가 실제로 필요로 하는 모양을 본다.

시험(`ComputeTests.cpp`, 셰이더 `Tests/Shaders/ComputeProbe.hlsl` 의 네 커널):

- 상한, 자리표(그래픽은 storage 를 받지 않고 컴퓨트는 `Compute` 단계만), 구조화 버퍼 규칙(원소 크기·온전한 원소·`Device`), 읽고 쓰는 텍스처 만들기와 sRGB 거절 - 세 백엔드.
- 칠하기(64×64 를 한 색으로) → 되읽기. 버퍼 64 칸을 쓰는 디스패치 다음에 그 버퍼를 열마다 옮기는 디스패치 → 열 0·21·63 의 값.
  2 mip 텍스처의 mip 0 을 칠하고 mip 0 을 읽어 반으로 mip 1 에 쓴 뒤, 다음 렌더 패스가 mip 1 을 `SetTextureMip` 으로 읽는다. 잘못된 슬롯·모르는 텍스처·없는 mip·
  빈 자리의 디스패치·파이프라인 없는 디스패치·렌더 패스 안의 컴퓨트를 거절한다. D3D12 검증 레이어가 조용해야 한다.

뮤테이션(2026-10-15, 10 개 중 9 개 죽었다):

| 망가뜨린 것 | 결과 |
|---|---|
| D3D12 디스패치 뒤 읽기 상태로 되돌리지 않음 | 검증 레이어 |
| D3D12 디스패치 앞 UAV 로 옮기지 않음 | 검증 레이어 |
| D3D12 버퍼 범위를 u0 에 | 파이프라인 만들기 |
| 그래픽 자리표가 storage 를 받음 | 그래픽 자리표 거절(처음엔 살아남았다 - 시험이 그래픽 자리표에 `Compute` 단계를 넣어 단계 검사에서 걸러졌다. 그래픽 단계로 다시 잰다) |
| Vulkan 구조화 버퍼를 업로드 메모리에 | 구조화 버퍼는 `Device` |
| D3D11 컴퓨트 상수 버퍼·텍스처를 CS 에 걸지 않음 | 칠한 색·mip 1 의 반 빨강 |
| D3D12 구조화 버퍼 원소 크기 두 배 | 뒤의 만들기 검사에서 죽었다(잘못된 UAV 로 장치가 망가진 것으로 보인다, 추정) |
| **D3D12 디스패치 사이의 UAV 배리어를 뺌** | **살아남았다.** 이 GPU 는 잇단 디스패치를 어차피 차례로 돌리는 것으로 보이고(추정), 검증 레이어와 GPU 기반 검증도 빠진 UAV 배리어를 말하지 않는다. 시험으로 잴 수 없는 자리라 코드 읽기로 지킨다 |

남은 것: Vulkan 의 디스패치 시험(SPIR-V), 명시 `Barrier`(5 단계), 그래픽 단계의 UAV(픽셀 셰이더의 쓰기)와 3D·배열 UAV, 읽기 전용 구조화 버퍼(`StructuredBuffer`, t#), 같은 디스패치에서 같은 mip 을
읽고 쓰는 묶음의 거절(지금은 부르는 쪽의 몫이다), D3D12 프레임 폐기 때 버퍼 상태 되돌리기(텍스처만 되돌린다).
- Vulkan 의 그리기 시험(§3.1 과 같은 까닭). Vulkan 은 mip·깊이 만들기와 거절만 돌았다.

### 3.5 4 단계 - 셰이더 매니페스트와 셰이더 자료형 (D-329)

범위는 사용자가 골랐다(2026-10-15, "매니페스트+자료형"): 빌트인 셰이더를 매니페스트 한 곳으로 옮기고 셰이더 자료형을 세운다.
에디터가 그 자리에서 굽는 것과 `.jshader` 에셋 파일·로더는 쓰는 쪽(Material, 11 단계)과 함께 한다.

| 것 | 모양 |
|---|---|
| 매니페스트 | `Modules/JBroGraphics/Shaders/ShaderManifest.psd1`. `Stages`(심볼·파일·진입점·단계) 20 개와 `Programs`(셰이더 둘·푸시 상수 크기와 단계·상수 버퍼·텍스처·샘플러 개수와 단계) 13 개. 텍스처·샘플러 단계의 기본값은 `Pixel` |
| 굽기 | `Compile.ps1` 이 매니페스트를 읽어 단계마다 DXIL·DXBC·SPIR-V 헤더를 굽고(전과 같은 이름·같은 바이트), 프로그램 표 `BuiltinShaders.generated.h/.cpp` 를 쓴다. `-SkipSpirv` 는 SPIR-V 헤더를 건드리지 않는다(Vulkan SDK 없는 기계) |
| 자료형 | `JBroAsset` 의 `ShaderData`(`<JBro/Asset/ShaderData.h>`): 이름, `[단계][백엔드]` 바이트코드, 푸시 상수 크기와 단계, 자리표 다섯(`ShaderResourceRange{count, stageMask}`). 단계 비트는 RHI 의 `ShaderStage` 와 같다(`static_assert`). 파라미터 블록 레이아웃은 효과가 서는 6 단계에서 더한다 |
| 렌더러 | include 60 줄과 `PickShader` 가 `GetBuiltinShader(BuiltinShader::X)` 하나로 줄었다. `ApplyShader` 가 셰이더 둘·푸시 상수·자리표를 파이프라인 서술자에 옮기고, 그 API 의 바이트코드가 없거나 C++ 의 푸시 구조체 크기가 매니페스트와 다르면 거짓이다 |
| 시험 | `BuiltinShaderTests.cpp`: 프로그램마다 DXBC 를 `D3DReflect` 로 열어 쓰인 b·t·s 자리가 선언과 정확히 같은지, 읽는 단계가 자리표 단계 안인지, b0 크기와 단계가 푸시 상수와 같은지, 세 백엔드 코드가 다 있는지 잰다 |

셰이더 하나를 더하는 일은 매니페스트 한 줄(단계)과 한 줄(프로그램), `Compile.ps1` 한 번이다 - 전에는 스크립트의 배열 셋과 `Renderer.cpp` 의 include 블록 셋을 고쳤다(§1).
에디터 UI 셰이더(`Modules/JBroEditor/Shaders`)는 렌더러 밖이라 옮기지 않았다.

반사 시험이 처음 돌 때 매니페스트(곧 옛 손 자리표)와 셰이더가 어긋난 곳 다섯을 찾았다. 모두 셰이더가 읽지 않는 자리를 선언하던 것이다:

- `OutlineGrow` 는 텍스처 둘과 샘플러를 선언했지만 t0 하나를 `Load` 로만 읽는다. 렌더러의 외곽선 첫 패스가 t1 에 마스크를 묶던 것을 뺐다.
- `OutlineComposite`·`LayerComposite`·`LayerBackdrop`·`Light2DShadowed` 는 샘플러를 선언했지만 모두 `Load` 로 읽는다. 렌더러의 `SetSampler(0, …)` 셋을 뺐다.

그림은 바뀌지 않았다(SpritePixel·Light2DPixel·MeshPixel·TextRender·DebugDraw·EditorUI·세 백엔드 스모크가 그대로 지난다). D3D12 는 빈 자리를 선언해도 거절하지 않지만 묶지 않은 자리로 그리면 거절하는 RHI 규칙(D-325)과 맞물려, 쓰지 않는 자리를 선언하면 부르는 쪽이 쓸데없이 묶어야 했다.

뮤테이션(2026-10-15, 11 개 중 10 개 죽었다):

| 망가뜨린 것 | 결과 |
|---|---|
| 스프라이트의 텍스처 1 → 2 | 반사 시험("쓰지 않는 자리를 선언") |
| 스프라이트의 샘플러를 뺌 | 반사 시험("선언하지 않은 자리를 씀") |
| 스프라이트의 텍스처 단계를 정점으로 | 반사 시험(읽는 단계) |
| 레이어 합성에 샘플러를 다시 선언 | 반사 시험 |
| 스프라이트의 푸시 상수 64 → 80 | 반사 시험(b0 크기) |
| 메시의 푸시 상수 단계를 픽셀로 | 반사 시험(푸시 단계) |
| **`ApplyShader` 의 푸시 크기 검사를 뺌** | **살아남았다.** 지금 크기가 모두 맞아 검사가 거짓을 낼 일이 없다. 매니페스트는 반사 시험이 셰이더에, 이 검사는 C++ 구조체에 묶는다 - 구조체만 바뀌는 경우의 그물이다 |
| D3D11 에 DXIL 을 줌 | 렌더러 초기화(파이프라인 만들기) |
| 자리표의 단계를 무시하고 정점으로 | 렌더러 초기화 |
| 텍스처 자리표를 옮기지 않음 | 렌더러 초기화 |
| 그림자 빛에 마스크를 묶지 않음 | 빛 프레임(묶지 않은 자리로 그린 것을 RHI 가 거절한 것으로 본다, 추정) |

남은 것: SPIR-V 헤더를 매니페스트로 다시 굽기(Vulkan SDK 가 있는 기계에서 `Compile.ps1` 을 `-SkipSpirv` 없이), 컴퓨트 프로그램을 매니페스트에 받기(첫 효과가 6·7 단계에서),
파라미터 블록 레이아웃(6 단계), `.jshader` 에셋 파일과 에디터의 즉석 굽기(11 단계).

### 3.6 5 단계 - 렌더 그래프와 지금 패스 옮기기 (D-339)

배리어의 모양은 사용자가 골랐다(2026-10-26, "계약 유지 + 일괄 전이"): "패스 밖의 텍스처는 늘 읽기 상태" 라는 RHI 계약은 그대로 두고, 그래프는 패스 앞에서
그 패스의 텍스처를 한 번에 옮긴다. 그래프가 상태를 들고 패스 끝의 되돌리기를 끄는 길(레이어 묶음마다 RT↔읽기 왕복이 사라지는 대신 세 백엔드의 패스 끝 처리와 계약이 바뀐다)은 고르지 않았다.

| 것 | 모양 |
|---|---|
| 그래프 | `<JBro/Graphics/RenderGraph.h>`(`JBroGraphics` 안). 텍스처는 들인 것(`ImportTexture`·`ImportBackBuffer`, 같은 핸들은 같은 텍스처)과 만든 것(`CreateTexture`, 크기·포맷·쓰임). 패스는 래스터·복사·컴퓨트이고 실행할 순서대로 더하며, 막 더한 패스에 색·깊이 첨부·읽기·복사·조건(`RunIfAvailable`·`RunIfMissing`)·자료(`SetPassData`)를 붙인다. 실행 함수는 함수 포인터 + 자료 포인터, 자료는 그래프의 프레임 저장소에 복사된다 |
| 컴파일 | ① 모든 패스로 수명을 재고 ② 풀에서 같은 모양이고 수명이 겹치지 않는 텍스처를 고른다(없으면 가상 자리로 모자란 수를 세어 풀에 바람으로 남긴다) ③ 앞에서 뒤로 조건·없는 텍스처·앞이 돌지 않은 입력으로 가리고 ④ 뒤에서 앞으로 결과에 닿지 않는 패스를 가리며(`Load` 와 선택 읽기도 읽기다) ⑤ 살아남은 패스마다 그래프가 만든 텍스처의 배리어를 낸다. 실행 순서는 더한 순서다 - 패스는 앞에서 쓴 것만 읽으므로 그것이 위상 순서다. 가린 까닭은 `RenderGraphSkip`(`MissingTexture`·`TexturePresent`·`InputNotWritten`·`Unused`)으로 남는다 |
| 풀 | `RenderTargetPool`, 12 칸(옛 레이어 8 + 깊이 4). 모양(크기·포맷·쓰임)으로 찾고, 컴파일이 바란 수를 다음 `BeginFrame` 의 `Prepare` 가 만들며 300 프레임 쓰지 않으면 놓는다. 프레임 타깃 크기의 깊이는 `Ensure` 로 미리 세운다(전과 같다) |
| 배리어 | `IRHICommandContext::Barrier(JArrayView<TextureBarrier>)`. 렌더 패스 밖에서만, 백버퍼는 받지 않고, 쓰임 규칙은 공용 `IsTextureBarrierValid` 하나다(하나라도 틀리면 통째로 거절). 이미 그 상태인 mip 은 건너뛰고, 한 묶음에서 같은 mip 이 다시 나오면 앞 배리어의 도착만 고친다 - D3D12 는 같은 서브리소스가 둘이면 경고(1008)하고, Vulkan 은 한 배리어 안의 전이에 차례가 없다. D3D11 은 검사만 한다. 그래프는 들인 텍스처를 옮기지 않는다(쓰임을 모른다 - RHI 가 첨부·복사에서 맞춘다) |
| 렌더러 | `RecordViews` 가 그래프를 짓고 컴파일해 기록한다. 뷰마다 라이트맵(`LightMap`, 그림자 라이트마다 `ShadowMask`·`ShadowedLight`·대신 `Light`), 본 패스(`View`), 레이어 묶음마다 `Layer`·대신 `LayerDirect`·`Backdrop`(복사)·`Composite`, 묶음 뒤 구간마다 `View`, 통째 얹기면 `ViewClear`·`ViewLayer`·대신 `ViewDirect`·`Backdrop`·`Composite`, 외곽선이면 `OutlineGrow`·`Outline`. 깊이는 선택 첨부, 라이트맵은 선택 읽기다. 통계는 실행 함수가 센다 |

그림이 바뀌지 않는 것은 기존 픽셀 시험이 그대로 지나는 것으로 쟀다(SpritePixel·Light2DPixel·MeshPixel·TextRender·DebugDraw·Light2DFramework·Framework2D/3DSystem·RendererContract, 세 백엔드).
행동이 바뀐 곳은 셋이고, 모두 처음 보는 크기의 첫 프레임들이다.

- **그림자 마스크를 라이트맵과 같은 프레임에 바란다.** 전에는 마스크를 라이트맵이 선 다음 프레임에야 바라서 셋째 프레임부터 그림자였다. 지금은 둘째 프레임이다.
  `Light2DPixel` 의 기대를 고쳤고(마스크만 없는 프레임은 라이트맵을 먼저 세운 뒤 그림자를 켜서 만든다), 둘째 프레임 시험을 더했다.
- **레이어 묶음을 보지 않는 뷰는 내용으로 정한다.** 전에는 "깊이가 실제로 달렸는가·레이어 텍스처가 실제로 있는가" 로 정해, 처음 보는 크기의 3D 뷰나 통째 얹기 뷰의 첫 프레임에는 묶음을 얹었다.
  지금은 "메시·월드 텍스트가 있는 뷰"·"통째로 얹는 뷰" 로 정한다 - `Renderer.h` 의 계약 문장(깊이가 달린 뷰는 묶음을 보지 않는다, 통째 얹기는 뷰 안의 묶음을 보지 않는다)과 같다.
- **본 패스가 레이어 묶음에서 끊긴 뒤의 구간은 제 패스다.** 전에는 얹는 패스를 연 채로 다음 구간을 이어 그렸다. 묶음이 대신 그리기로 갈 때 얹는 패스가 빠지므로 나눴다. `Load` 라 그림은 같고 패스가 하나 는다.

옮기다 찾은 것: **D3D12 의 `CopyTexture` 가 사본을 `COPY_DEST` 로 남겼다.** `CopyTexture` 의 계약은 `Sampled` 대상을 다음 패스가 곧바로 읽는 것인데, 렌더 패스 안에서는
배리어를 걸 수 없어 아래 그림을 읽는 블렌드(오버레이 등)의 사본을 `COPY_DEST` 로 읽고 있었다(GPU 검증 942). 그 픽셀 시험은 검증 레이어를 켜지 않아 몰랐다.
복사 뒤에 `Sampled` 원본·대상을 읽기 상태로 돌리게 고쳤고(Vulkan 은 대상만 돌려 왔다 - 원본도 돌린다), `SpritePixel` 의 아래 그림 블렌드 시험에 검증 레이어를 켰다.

시험(`RenderGraphTests.cpp`, 묶음 `RenderGraph`): 배리어의 거절 규칙(세 백엔드), 복사한 사본을 다음 패스가 읽기(D3D12·D3D11, 배리어 없이와 있이 - 검증 레이어 0),
없는 텍스처의 대신 그리기와 다음 프레임, 수명이 겹치지 않는 같은 모양의 나눠 쓰기와 겹칠 때의 바람 둘, 결과에 닿지 않는 패스·쓰이지 않은 입력·선택 깊이·선택 읽기,
복사 대상과 읽기의 배리어 차례, 상한 넘기, 프레임 하나가 힙을 쓰지 않음.

전체 시험(Debug, 2026-10-26, `e1129837`): 73 묶음이 지난 뒤(EditorApplication 포함) `ScriptSummaryPanel` 에서 크래시(종료 코드 139)로 멈췄다. 그 묶음은 혼자 돌리면 지나고,
**main `33445212` 의 Debug 에서도 `JBRO_TESTS=EditorApplication,ScriptSummaryPanel` 로 같은 자리에서 죽는다** - 이 단계와 상관없이 원래 있던 결함이다(그 묶음은 ImGui 만 쓰고 렌더러를 쓰지 않는다,
원인은 보지 않았다). 그 뒤의 49 묶음은 따로 돌려 모두 지났다. 에디터 화면(`JBRO_EDITOR_SHOT`)의 선택 외곽선은 그래프의 외곽선 패스로 그대로 그려졌다.

Codex 리뷰(`codex review --commit 8ec09d84`)가 둘을 찾았고 코드로 확인해 고쳤다. ① 가득 찬 풀이 한 모양을 하나 더 만들 때 모든 칸이 같은 만큼 쉬었으면 첫 칸을 내주는데,
그것이 같은 모양이면 수가 그대로라 매 프레임 같은 바람과 대신 그리기가 이어졌다 - 같은 모양은 내주지 않는다. ② 조건(`RunIfAvailable`·`RunIfMissing`)으로만 가리키는 텍스처는
할당이 보지 않아 영영 없는 것이었다 - 수명과 할당이 조건도 본다. 지금 렌더러는 조건 텍스처를 늘 다른 패스가 첨부로도 써서 ② 에 걸리지 않았다. 둘 다 시험을 더했다.

뮤테이션(2026-10-26, 9 개 중 8 개 죽었다 - 변이마다 빌드하고 그 묶음만 돌려 실패 문장을 보았다):

| 망가뜨린 것 | 결과 |
|---|---|
| D3D12 복사가 대상을 돌리지 않음 | 복사 읽기 시험(검증 942) |
| 그래프가 배리어를 내지 않음 | 깊이 배리어 차례 시험 |
| 대신 그리기 조건(`RunIfMissing`)을 무시 | "the fallback stands down" |
| 할당이 수명을 보지 않음 | "textures alive at the same time get their own" |
| 결과에 닿지 않는 패스를 가리지 않음 | "a pass nobody reads is dropped" |
| 레이어 대신 그리기를 세지 않음 | SpritePixel 첫 프레임의 얹지 못한 묶음 수 |
| 풀이 같은 모양을 하나 더 만들 때 그 모양을 내줌(리뷰 뒤) | "the pool gave up another shape, not the one it was growing" |
| 조건으로만 가리키는 텍스처를 고르지 않음(리뷰 뒤) | "a texture named only by a condition is wanted" |
| **Vulkan 배리어 묶음에서 같은 mip 합치기를 뺌** | **살아남았다.** 이 기계에는 Vulkan 검증 레이어(SDK 1.4.350)가 있지만 한 배리어 안의 같은 서브리소스 전이 둘을 오류로 보지 않는다. 명세상 한 배리어 안의 전이는 차례가 없어 넣은 방어이고, 잡는 그물은 없다 |

벤치마크(`JBRO_BENCH=1`, Debug, 2026-10-26, 그래프 전 = main `33445212` 의 Debug 시험 실행 파일 - 렌더러는 10-15 뒤로 바뀌지 않았다, 차례를 바꿔 두 번):

| 장면 | 전 (ms) | 뒤 (ms) |
|---|---|---|
| D3D12 빈 프레임 | 0.241 / 0.247 | 0.279 / 0.305 |
| D3D11 빈 프레임 | 0.070 / 0.070 | 0.084 / 0.071 |
| Vulkan 빈 프레임 | 0.205 / 0.202 | 0.179 / 0.192 |
| D3D12 스프라이트 6 만 | 26.05 / 25.76 | 26.81 / 27.01 |
| D3D11 스프라이트 6 만 | 26.23 / 25.73 | 26.63 / 26.87 |
| Vulkan 스프라이트 6 만 | 25.50 / 26.47 | 26.89 / 26.48 |

D3D12 빈 프레임의 0.04~0.06 ms 는 그래프를 짓고 컴파일하는 값으로 본다(Debug, 뷰 하나). 스프라이트 장면은 대개 0~1.4 ms 느리게 나왔지만, GPU 에 묶인 그리기 하나짜리 장면이라
그래프가 GPU 에 더하는 일이 없고 최댓값이 35~50 ms 로 흔들린다 - 두 번으로는 그래프 탓으로 가르지 못했다. 레이어 묶음·라이트가 많은 장면의 벤치마크는 없다.

남은 것: 컴퓨트 패스를 실제로 쓰는 것(7 단계의 블룸), Release 와 레이어·라이트가 많은 장면의 벤치마크. 렌더 그래프 보기와 Vulkan 의 복사 읽기 그리기 시험은 §3.7 에서 섰다.

### 3.7 렌더 그래프 창과 Vulkan 의 그리기 시험 (D-340)

사용자가 고른 것(2026-10-26): 창의 자리는 "디버그 메뉴의 새 패널", 보일 것은 "패스 목록과 건너뛴 까닭, 텍스처와 수명 막대, 패스마다 배리어, 풀 상태 - 있어야 할 만한 것 모두, 사용자 시나리오대로".

**Vulkan 의 그리기 시험.** 이 기계에 Vulkan SDK(1.4.350)가 들어와 `Tests/Shaders/Compile.ps1` 을 `-SkipSpirv` 없이 돌렸다. 상수·서브리소스·3D·컴퓨트 탐침의 SPIR-V 헤더 11 개가 새로 생겼고
DXIL·DXBC 헤더는 바이트 하나 바뀌지 않았다. `ConstantBuffer`·`Subresource`·`Compute`·`RenderGraph`(복사 읽기) 시험이 Vulkan 을 건너뛰지 않고 검증 레이어를 켠 채 그린다.
뮤테이션 둘(Vulkan 복사가 사본을 읽기 레이아웃으로 돌리지 않음, 패스 끝에 그린 mip 을 돌리지 않음)이 Vulkan 실행에서 죽었다.

**렌더 그래프 창**(`디버그` → `렌더 그래프`, 패널 종류 `RenderGraph`, 아래 도크, 처음에는 닫혀 있다). 쓰임새부터 정했다 - 레이어 블렌드가 한 프레임 늦게 걸린다, 그림자가 안 나온다,
풀이 매 프레임 텍스처를 다시 만든다 같은 때 연다.

| 자리 | 보이는 것 |
|---|---|
| 도구 막대 | 멈춤(켜면 칠해진다 - 떠 둔 그래프를 그대로 두고 게임은 돈다), 한 프레임 받기(멈췄을 때만), 건너뛴 패스 보이기, 패스·텍스처 이름 찾기, 요약(프레임 번호·돈 패스/전체·텍스처·실제 텍스처·배리어) |
| 왼쪽 | 뷰마다 묶은 패스 표. 뷰 머리 줄은 어느 화면인지(캔버스 뷰·시뮬레이션 뷰·게임 화면·편집용 그림)와 크기. 패스마다 번호(실행 차례)·이름·종류·상태·배리어 수. 건너뛴 패스는 흐리고, 텍스처가 없어 못 돈 것은 주황이며 툴팁이 까닭을 문장으로 말한다. 고른 텍스처를 쓰는 패스는 강조 색 |
| `패스` 탭 | 고른 패스의 뷰·상태와 까닭, 조건(`있을 때만`·`없을 때만(대신 그리기)`), 쓰는 텍스처(쓰임·지우기/이어 그리기·없어도 되는지·실제 텍스처 번호), 그 앞의 배리어(텍스처·옮길 상태) |
| `텍스처` 탭 | 그래프 텍스처마다 이름과 처음 쓰는 뷰·모양·출처(그래프/가져옴/백버퍼)·실제 텍스처 번호·수명 막대. 같은 색 막대는 실제 텍스처 하나를 나눠 쓰고, 고른 패스 자리에 세로선, 고른 패스가 쓰는 텍스처는 진하게 |
| `풀` 탭 | 12 칸마다 모양·이번 프레임에 썼는지·안 쓴 프레임 수, 다음 프레임 전에 만들 모양과 수 |

- 렌더러는 지난 프레임의 그래프·뷰·풀을 읽기 전용으로 내준다(`GetLastRenderGraph`·`GetLastRenderGraphSerial`·`GetLastRenderGraphView`·`GetRenderTargetPool`).
  에디터는 UI 를 엔진 프레임보다 먼저 지으므로 그때 읽으면 지난 프레임의 것이고, 풀의 바람도 다음 `BeginFrame` 전이라 남아 있다.
- 그래프는 패스마다 묶음 번호(렌더러가 뷰 번호를 넣는다)와 텍스처마다 진단 이름(`LightMap`·`Layer`·`Backdrop`·`ShadowMask`·`Depth`·`ViewTarget` 등)을 든다. 살펴보기 API 가 늘었다(`GetPassKind`·`GetPassAccess`·`GetTextureLifetime` 등).
- 창은 열려 있을 때만 매 프레임 그래프를 제 저장소로 떠 온다. 수명 막대는 공용 위젯 `Widget::SpanBar` 다.
- 기존 엔진에 렌더 그래프 창은 없었다. 가까운 것은 `GpuProfilerWindow`(왼쪽 목록 + 오른쪽 상세)이고 그 짜임을 따랐다. 도구 막대는 지금 에디터의 `로그` 창과 같은 관례(아이콘 단추·칸 옆 글자·찾기 칸)다.

시험(`EditorApplicationTests` 의 `TestTheRenderGraphPanelShowsTheLastFrame`): 오버레이 레이어 둘과 라이트가 있는 장면에서 첫 프레임은 `Layer` 가 텍스처 없음·`LayerDirect` 가 돎,
다음부터는 `Layer`·`Backdrop`·`Composite`·`LightMap` 이 돌고 두 레이어 텍스처가 실제 텍스처 하나를 나눠 쓴다. 멈춤 단추를 누르면 프레임이 지나도 그래프가 그대로이고,
한 프레임 받기 단추가 새 그래프 하나만 받는다. 아래 도크 경계를 끌어 올린 뒤 세 탭을 찍었고(`editor_render_graph_pass`·`_textures`·`_pool`), 스크린숏으로 열 잘림과 같은 이름 구분을 보고 고쳤다.
Codex 리뷰(`4fbb56dd`)가 하나를 찾았고 코드로 확인해 고쳤다 - 텍스처 이름으로 찾을 때 그 텍스처를 조건으로만 가리키는 패스(`ShadowMask` 가 없을 때만 도는 `Light`)가 빠졌다.
시험 장면에 그림자를 드리우는 라이트를 더해 이 경우를 잰다. 뮤테이션 셋(찾기가 조건을 보지 않음·멈춤을 무시함·나눠 쓰기를 묶지 않음)이 모두 이 시험에서 죽었다.
전체 시험(Debug, `cc0ddad8`): EditorApplication 까지 74 묶음이 지난 뒤 main 에 원래 있던 `ScriptSummaryPanel` 크래시(§3.6)에서 멈췄고, 그 뒤 50 묶음은 따로 돌려 모두 지났다. 솔루션 전체가 빌드된다.

~~남은 것: 그래프 텍스처의 내용 미리보기(텍스처를 나눠 쓰므로 프레임 끝의 내용은 마지막으로 쓴 패스의 것이라 그대로 보이면 잘못 읽힌다 - 패스 하나를 고르면 그 자리에서 떠 두는 길이 필요하다),
GPU 시간(타임스탬프 쿼리가 RHI 에 없다).~~ → §3.8(D-341)

### 3.8 패스별 GPU 시간·패스 직후 미리보기·창을 열 때 도크 넓히기 (D-341)

사용자 지시(2026-10-26) "남은거 해", 고른 범위: "실제 에디터 확인·도크", "텍스처 미리보기·GPU 시간". `ScriptSummaryPanel` 크래시와 6 단계는 고르지 않았다.

**GPU 시각(RHI).** `IRHICommandContext::WriteTimestamp(자리)` 와 `IRHIDevice::ReadFrameTimestamps` 를 더했다. 자리는 프레임마다 0 부터 빈틈없이 쓰고(`MaxTimestampsPerFrame` = 512,
`DeviceCaps::maxTimestampsPerFrame`), 렌더 패스 밖에서만 적는다. 읽기는 기다리지 않는다 - `BeginFrame` 이 슬롯을 다시 쓸 때 그 슬롯의 앞 프레임이 끝났으므로 그때 받아 둔 것을 준다.
그래서 시각은 떠 있는 프레임 수만큼 늦게 온다.

| 백엔드 | 적기 | 받기 |
|---|---|---|
| D3D12 | 슬롯마다 쿼리 힙 구간 + 읽기 버퍼, `EndQuery(TIMESTAMP)` | `EndFrame` 에서 `ResolveQueryData`, 다음에 그 슬롯을 여는 `BeginFrame` 이 펜스 뒤에 복사 |
| D3D11 | 시각 묶음 셋을 돈다(묶음마다 `TIMESTAMP_DISJOINT` 하나) | `GetData(DONOTFLUSH)` 로 끝난 묶음만 모은다. 주파수가 흔들린 묶음은 버린다 |
| Vulkan | 쿼리 풀, 프레임 앞에서 `vkCmdResetQueryPool`, `vkCmdWriteTimestamp2(ALL_COMMANDS)` | 펜스 뒤 `vkGetQueryPoolResults`. 큐 패밀리의 `timestampValidBits` 가 0 이면 능력 0 |

**패스별 GPU 시간(그래프·렌더러).** `RenderGraph::SetTimestampCapacity` 가 0 이 아니면 `Execute` 가 살아남은 패스 앞(배리어 앞)과 뒤에 시각을 적는다. 자리가 모자라거나 RHI 가 거절하면 그 뒤 패스는 재지 않는다.
렌더러는 창이 켠 동안만(`SetRenderGraphTimingEnabled`) 재고, 프레임마다 잰 패스의 이름 포인터·뷰·시각 자리를 RHI 프레임 번호와 함께 고리(4 칸)에 적어 둔다. 장치 `BeginFrame` 뒤에 그 프레임의 시각이 오면 ms 로 바꾼다.
프레임 경로에서 글자를 만들거나 견주지 않는다 - 이름은 그래프의 정적 이름 포인터 그대로다.

**패스 직후 미리보기.** 텍스처를 나눠 쓰므로 프레임 끝의 내용은 마지막으로 쓴 패스의 것이다. 그래서 창에서 고른 패스의 접근 하나를 그 패스가 끝난 직후에 사본으로 뜬다(`RenderGraph::SetCapture`).
그래프는 프레임마다 다시 지으므로 요청은 패스 번호가 아니라 (뷰, 이름 포인터, 그 뷰에서 그 이름의 몇 번째, 접근 번호)이고, 렌더러가 컴파일 뒤에 그 프레임의 패스로 찾는다.
사본 텍스처는 렌더러가 들고 처음이거나 크기·포맷이 바뀌면 다음 장치 프레임 전에 만든다(장치는 프레임 안에서 자원을 만들지 않는다). 들여온 텍스처(뷰 타깃)는 뷰 크기·백버퍼 포맷이고, 깊이는 뜨지 않는다(`Unsupported`).
상태(`RenderGraphCaptureStatus`)는 `None`·`Waiting`(사본이 아직 없다)·`Captured`·`NotFound`(이번 그래프에 없거나 돌지 않았다)·`Unsupported`(깊이)다.

**창.** 패스 표에 `GPU ms` 열, 요약에 그 프레임의 GPU 전체(시각이 오기 전에는 `GPU 시간 재는 중`, 장치가 못 적으면 `GPU 시간 없음` 과 툴팁).
멈춘 그래프에도 시간이 붙는다 - 같은 프레임이면 패스 번호로, 아니면 (뷰, 이름, 몇 번째)로 짝짓는다. `패스` 탭의 텍스처 줄을 누르면 그 패스 직후의 내용이 아래 `미리보기` 에 나오고(다시 누르면 끈다),
머리 줄이 어느 프레임·패스·텍스처·크기인지 말한다. 지우기만 한 타깃은 창 바탕과 거의 같은 색이라 그림 둘레에 1 픽셀 테두리를 둔다(실제 에디터에서 보고 더했다).

**도크 넓히기.** 아래 도크는 160 픽셀쯤으로 서서 표 셋이 읽히지 않았다. 패널이 `GetPreferredDockHeight`(이 창은 420)를 말하면, 메뉴로 열 때(`DrawPanelMenuItem` → `RequestRoom`)
에디터가 그 창이 붙은 위아래 나눔의 `SizeRef` 를 그만큼 넓힌다(나뉜 높이의 60% 까지, 줄이지는 않는다). 칸 경계를 끄는 것과 같은 값이라 그 뒤에는 사용자가 끌어 바꿀 수 있다.

시험:
- `RenderGraph`: `TestTimestampsArriveAfterTheFrame`(세 백엔드 - 끝난 프레임에서 두 시각을 읽고 뒤가 앞보다 늦지 않다, 패스 안·범위 밖 자리는 거절, 검증 레이어 조용),
  `TestTheCaptureHoldsTheTextureRightAfterItsPass`(세 백엔드 - 빨강으로 지운 패스 뒤에 뜨고 다음 패스가 파랑으로 지우면 사본은 빨강·타깃은 파랑, 검증 레이어 조용).
- `EditorApplication` 의 `TestTheRenderGraphPanelShowsTheLastFrame`: 끌기 대신 메뉴와 같은 `RequestRoom` 으로 도크가 400 픽셀 넘게 넓어지고, 멈춘 그래프의 `Composite` 에 GPU 시간이 붙고,
  뷰 타깃(들여온 것)과 레이어(그래프가 만든 것)가 `Composite` 직후로 떠진다. 다른 텍스처를 고른 직후는 `Waiting` 이어야 한다 - 앞 사본을 새 이름으로 보이지 않는다.
- 실제 에디터(`JBroEditorHost`, 빈 프로젝트, PostMessage 로 조작): `디버그` → `렌더 그래프` 로 열자 아래 도크가 약 160 → 470 픽셀로 넓어졌고, `GPU ms` 가 나오고, 패스를 골라 `ViewTarget` 줄을 누르니 미리보기가 떴다.
- 뮤테이션: 넷 - 사본을 다음 패스 뒤에 뜸(`RenderGraph` 의 빨강 검사), 메뉴로 열어도 도크를 넓히지 않음, 창이 시간을 붙이지 않음, 요청이 바뀌어도 상태를 그대로 둠이 모두 각자 맞는 검사에서 죽었다.
- 리뷰(Claude, `46c9de0b`): 같은 모양의 다른 텍스처를 고르면 상태가 `Captured` 로 남아 한 프레임 동안 앞 사본을 새 이름으로 보였고, 시험의 두 번째 미리보기 검사도 그 상태로 지나고 있었다. 요청이 바뀌면 `Waiting` 으로 돌리고(`3f9af54e`) 위의 검사를 더했다.
- 전체 시험(Debug, `a4be58cf`, Codex 가 돌림): main 에 원래 있던 `ScriptSummaryPanel` 크래시를 빼고 나머지를 이어 돌렸다. 8 묶음(`PlatformContract`·`SourceDebugger`·`NestedScriptType`·`ScriptProject`·`ScriptBuild`·`DebugAdapter`·`ScriptBreak`·`UserScenario`)이
  Codex 샌드박스 안에서 파이프·디버거 붙기·MSVC 빌드 도구 찾기로 실패했고, 샌드박스 밖에서 그 8 묶음을 다시 돌리니 모두 지났다. 나머지는 모두 지났다. 솔루션 전체가 빌드된다.

남은 것: 시각은 패스 단위다(패스 안의 그리기별은 없다). 미리보기는 한 번에 하나이고 깊이·HDR 의 값 범위 조절(노출·채널 고르기)이 없다.

### 3.9 6 단계 - 효과 종류 표·주입 지점·HDR 장면·노출과 톤맵 (D-342)

사용자 지시(2026-10-26) "후처리 6 단계 ㄱㄱ". 사용자 결정: 장면 HDR 포맷은 "RGBA16Float", 톤맵은 "추천방안대로. 고점 높은거로" - 곡선 넷(None·Reinhard·ACES·AgX)을 모두 두고 기본은 계획대로 `None` 이다.
셰이더 분기 하나라 곡선을 더 두는 값이 없고, AgX 가 품질이 가장 높으며, Reinhard 는 기존 엔진에 있던 것이다.

| 것 | 모양 |
|---|---|
| 효과 종류 표 | `<JBro/Graphics/PostProcess.h>` 의 `PostProcessEffectRegistry`. 프로세스에 하나, 메인 스레드 전용, 64 칸. 종류는 이름·주입 지점·바라는 것(`Hdr`·`Depth`·`History`)·파라미터 블록의 리플렉션 표·블록 크기·기본값·패스 함수다. 빌트인 `Exposure`(0 번)·`Tonemap`(1 번)이 표가 처음 열릴 때 든다. 사용자 효과도 `Register` 로 같은 모양으로 든다(시험이 `AfterTonemap` 효과 하나를 들여 돌린다) |
| 주입 지점 | `PostProcessStage` 여섯(§2.3 의 표). 지금 받는 것은 `AfterOpaque`·`BeforeTonemap`·`Tonemap`·`AfterTonemap` 이다. `AfterOpaque` 는 장면을 다 그린 뒤라 `BeforeTonemap` 과 같은 자리다. `AfterLayer`(9 단계)·`AfterScreenLayers`(화면 레이어가 카메라 뒤의 다른 뷰라 아직 자리가 없다)의 효과는 버리고 센다 |
| 뷰에 내기 | `CameraParams::postProcess`(`PostProcessStack` - 효과 목록과 블록 바이트). 쓰는 곳이 없던 `postProcessProfile` 핸들을 대신한다(§2.5 의 "풀린 프로필의 자리"). `BeginView` 가 프레임 저장소로 복사한다(`maxPostProcessEntries` 64·`postProcessBlockBytes` 4096, 뷰마다 32). 모르는 종류·배열 밖의 블록·상한을 넘는 것은 버리고 센다. 꺼진 효과는 옮기지 않는다 |
| 차례 | 지점 차례로, 같은 지점 안에서는 낸 차례로 돈다. 톤맵은 앞의 하나만 쓰고 뒤의 것은 버리고 센다. HDR 효과가 있는데 톤맵이 없으면 `None` 톤맵을 `AfterTonemap` 효과 앞에 넣는다 |
| HDR 장면 | 효과가 있는 뷰는 장면을 `SceneColor` 에 그린다. HDR 을 바라는 효과가 하나라도 있으면 `RGBA16Float` 이고 레이어 텍스처·아래 그림 사본도 같은 포맷이다(알파가 필요하다). 톤맵 앞의 효과는 HDR 중간 텍스처에, 톤맵과 그 뒤는 백버퍼 포맷에 쓰고 마지막 효과가 타깃의 뷰포트 안에 쓴다. 처음 그리는 타깃이면 뷰포트 밖은 지우는 색이고, 앞 뷰가 이미 그린 타깃이면 그것을 장면에 옮겨(`SceneLoad`, HDR 이면 사본을 떠서 `SceneConvert`) 이어 그리고 뷰포트 밖은 그대로다. 선택 외곽선은 후처리 뒤다 |
| 파이프라인 | 장면에 그리는 파이프라인(스프라이트·SDF 텍스트·빛을 받는 둘·깊이 위 쌍둥이·월드 텍스트·메시·레이어 얹기 넷·아래 그림)을 포맷 하나의 묶음(`ScenePipelines`)으로 모았다. 백버퍼 묶음은 처음부터, HDR 묶음과 전체 화면 프로그램(`PostCopy`·`PostExposure`·`PostTonemap`, 정점 번호로 덮는 삼각형 하나, 상수는 b1)·상수 링은 처음 바란 프레임의 다음 `BeginFrame` 에 선다 |
| 첫 프레임 | 파이프라인이나 장면·중간 텍스처가 없으면 바라 두고(`RenderTargetPool::Request`) 그 프레임은 후처리 없이 그린다(`postProcessWaitingViewCount`). 장치가 그래프를 짓기 전에 길을 골라야 해서 컴파일의 대신 그리기가 아니라 미리 묻는다. 둘째 프레임부터 후처리다 |
| 통째로 얹는 뷰 | 3D 레이어처럼 뷰를 제 텍스처에 그려 얹는 뷰는 아직 후처리를 받지 않는다(`unpostprocessedCompositeViewCount`, 9 단계) |

**색 공간.** 엔진은 처음부터 화면 값(텍스처·틴트가 UNORM, sRGB 가 아니다)으로 그려 왔으므로 장면은 화면 값이고, HDR 장면은 1 을 넘는 화면 값이다. 노출과 곡선은 빛의 세기(선형)에서 잰다 -
노출은 화면 값을 2^(EV / 2.2) 배 해 빛을 2^EV 배 하고, 곡선은 2.2 로 풀어 곡선에 넣은 뒤 다시 싼다(AgX 는 제 곡선이 화면 값을 낸다). 그래서 `None` 은 1 까지를 그대로 두고 그 위만 자른다(라이팅 계획 L4).
선형 작업 흐름(sRGB 텍스처·선형 블렌드)으로 바꾸는 것은 이 단계가 아니다 - 그림이 모두 바뀌는 방향 판단이다.

시험(`PostProcessPixelTests.cpp`, 묶음 `PostProcessPixel`, D3D12·D3D11·Vulkan, 검증 레이어를 켜고):
- 빛 1.6 을 받은 0.8 회색이 HDR 장면에서 1.28 이고 노출 -2.2 EV 뒤 0.64 다(백버퍼 포맷이었다면 0.5). 첫 프레임은 기다림으로 센다.
- 회색 넷에 노출 +4 EV 를 건 뒤 곡선 넷이 각 정의(Reinhard, Narkowicz 의 ACES, Wrensch 의 최소 AgX)를 시험이 따로 적은 식과 0.02 안에서 맞는다.
- 톤맵을 노출보다 먼저 내도 노출이 먼저 걸리고, 둘째 톤맵은 버려진다.
- 처음 그리는 타깃의 왼쪽 반 뷰포트는 오른쪽 반을 지우는 색으로 남기고, 앞 뷰가 그린 타깃 위의 후처리 뷰는 그 빨강을 장면에 옮겨 노출한다(뷰포트 밖은 그대로).
- HDR 장면의 표준 반투명 레이어와 오버레이 레이어가 제 텍스처로 얹힌다. 메시가 있는 3D 뷰도 깊이와 함께 HDR 로 그린다.
- 사용자 효과(`AfterTonemap`)가 넣어 준 `None` 톤맵 뒤에 돌고, HDR 효과가 없는 뷰는 백버퍼 포맷 장면으로 돈다.
- 모르는 종류·배열 밖의 블록·`AfterLayer` 효과는 버리고 세며 꺼진 효과는 걸지 않는다. 통째로 얹는 뷰는 센다.
- 후처리 프레임 하나가 CRT 힙을 잡지 않는다(Debug).
- `BuiltinShader` 의 반사 시험이 새 프로그램 셋의 자리표를 셰이더와 맞춘다(13 → 16).

뮤테이션 다섯(2026-10-27, 변이마다 빌드하고 그 묶음만 돌려 실패 문장을 보았다)이 모두 해당 검사에서 죽었다: 톤맵이 곡선을 무시함(곡선 검사), 노출을 화면 값으로 2^EV 배 함(HDR 검사),
지점 차례로 늘어놓지 않음(차례 검사), 앞 뷰의 그림을 장면에 옮기지 않음(겹친 뷰 검사), HDR 을 바라도 백버퍼 포맷으로 그림(HDR 검사).
관련 묶음(TextRender·RendererContract·TextureBinding·RenderGraph·BuiltinShader·RenderTargetFormat·SpritePixel·Light2DPixel·Light2DFramework·Framework2D/3DSystem·MeshPixel·DebugDraw·PublicHeaderComposition)이 그대로 지난다 -
효과가 없는 뷰는 전과 같은 길이다. 솔루션 전체가 빌드되고 실제 에디터(빈 프로젝트)가 전처럼 뜬다. 에디터에서 효과를 거는 길은 8 단계(프로필 에셋·카메라의 프로필)에서 선다.

전체 시험(Debug, `fecd5152`, Codex 가 돌림): main 에 원래 있던 `ScriptSummaryPanel` 크래시를 빼고 이어 돌려 110 묶음이 지났다. 8 묶음(`PlatformContract`·`SourceDebugger`·`NestedScriptType`·`ScriptProject`·`ScriptBuild`·`DebugAdapter`·`ScriptBreak`·`UserScenario`)은
D-341 때와 같이 Codex 샌드박스 안에서만 실패했고, 밖에서 다시 돌리니 모두 지났다.

리뷰(Claude)가 하나를 찾아 고쳤다: 후처리 자원을 만들다 실패한 장치는 그 뒤로도 매 프레임 장면 텍스처를 바라 두기만 했다 - 실패한 뒤에는 바라지 않고 그대로 그린다.

남은 것: `AfterLayer`·`AfterScreenLayers` 의 자리, 통째로 얹는 뷰의 후처리, 깊이·히스토리를 바라는 효과의 입력(지금은 HDR 장면 하나만 넘긴다), ~~에디터에서 걸기(8 단계)~~ → §3.11.

### 3.10 7 단계 - 블룸·기본 조정·`.cube` LUT (D-343)

사용자 지시(2026-10-27) "7, 8 단계 ㄱㄱ". 사용자 결정: 블룸은 "래스터 밉 체인", LUT 는 "내장 값 + .cube 파일", 그레이딩 값은 "기본 조정".
§4 의 미정(컴퓨트냐 래스터냐)은 래스터로 닫는다 - 세 백엔드에 다 있는 길이고 나중의 웹(WebGL 에는 컴퓨트가 없다)에도 그대로 간다. 컴퓨트 판은 만들지 않았고 재지도 않았다.

| 효과 | 지점 | 모양 |
|---|---|---|
| `Bloom` | `BeforeTonemap`, HDR | 절반 크기에서 시작하는 밉 체인(`iterations`, 1..8, 화면이 작으면 그보다 적다). 거르며 줄이기(선형 세기의 문턱, 무릎은 문턱의 절반, `clamp` 로 상한) → Jimenez 의 13 탭 줄이기 → 9 탭 텐트로 키우며 `scatter` 만큼 섞기 → 장면에 `intensity`·`tint` 로 더하고 화면 값으로 다시 싼다(1 위는 1 위로 남는다). 거를 때 좌표를 뷰포트 안으로 묶는다 - 장면의 뷰포트 밖은 다른 뷰이거나 비었다 |
| `ColorAdjustments` | `AfterTonemap` | 화면 값에서 대비(가운데 회색 둘레) → 컬러 필터 → HSV 색조 돌리기 → Rec.709 밝기 쪽으로 채도. 범위는 Unity 의 Color Adjustments 와 같다(대비·채도 -100..100, 색조 -180..180 도). 노출 보정은 `Exposure` 효과의 일이라 두지 않았다 |
| `ColorLookup` | `AfterTonemap` | `.cube` 에셋(`colorLutId`)의 3D LUT 를 삼선형으로 읽어 `contribution` 만큼 섞는다. 정의역은 파일의 `DOMAIN_MIN`·`DOMAIN_MAX` 다. LUT 가 없으면 그대로 옮긴다 |

- **빌더가 넓어졌다.** `PostProcessPassDesc` 로 그래프 입력 둘·그래프 밖의 텍스처 하나(LUT)·효과가 만든 텍스처 전체를 쓰는 패스를 낸다. 효과는 `CreateTexture` 로 제 텍스처(밉 칸)를 만든다.
- **효과가 만들 텍스처를 먼저 센다.** 렌더러는 뷰에 효과를 걸기 전에 패스 함수를 셈으로만 한 번 돌려(그래프를 건드리지 않는다) 만들 텍스처의 모양과 수를 얻고, 풀에 모자라면 바라 두고 그 프레임은 후처리 없이 그린다.
  6 단계의 "중간 텍스처 포맷마다 둘" 셈이 이 길로 합쳐졌다. 패스 함수는 같은 파라미터에 같은 텍스처를 같은 차례로 만들어야 한다. 밉 칸 때문에 풀은 12 칸에서 48 칸이 되었다.
- 렌더러가 `RGBA16Float` 3D 텍스처를 올린다(`RegisterVolumeTexture`). 2D 용 `UpdateTexture`·`UpdateTextureRegion` 은 3D 를 받지 않는다.
- **`.cube` 에셋**(`AssetType::ColorLut`, 새 에셋 종류). `TITLE`·`LUT_3D_SIZE`(2..256)·`DOMAIN_MIN`·`DOMAIN_MAX` 를 받고 `#` 뒤는 주석, 모르는 키는 버린다. 1D LUT·칸 수가 맞지 않음·뒤집힌 정의역·숫자가 아닌 줄은 줄 번호와 함께 거절한다.
  반정밀 실수 텍셀로 옮기는 것(`PackColorLutRgba16`)은 가장 가까운 짝수로 반올림하고 범위 밖은 무한대다. 같은 판에서 에셋 시스템이 `.jpost` 를 글자로 싣는다(8 단계가 푼다).
- 스크립트 API 목록(`Definitions.cpp`)은 원본 헤더의 줄로 위치를 적는다 - `AssetTypes.h` 의 열거에 다섯 줄이 늘어 그 아래의 위치를 다섯 줄씩 내렸다(`ScriptApiCatalog` 가 지난다).

시험:
- `PostProcessPixel`(세 백엔드, 검증 레이어): 노출로 1 위로 올린 4x4 흰 사각형의 3 픽셀 밖이 블룸으로 밝고(세기 0 이면 검고, 문턱이 빛보다 높으면 번지지 않는다), 10 픽셀보다 밝고, 상하좌우가 같다.
  기본 조정 여섯 경우가 시험이 따로 적은 식(대비 → 필터 → HSV 색조 → 채도)과 맞는다. 같음·뒤집기 LUT, 반 섞기, 정의역 0..2, LUT 없음.
- `ColorLut`: 받는 모양(BOM·주석·`TITLE` 따옴표·정의역·`\r\n`·차례)과 거절하는 열 가지, 반정밀 실수(1·0.5·-2·범위 밖·아주 작음·1/3), 확장자와 `colorLutId`·`postProcessProfileId` 의 에셋 종류.
- `BuiltinShader` 의 반사 시험이 새 프로그램 여섯의 자리표를 맞춘다(16 → 22).

뮤테이션 셋(2026-10-27)이 모두 죽었다: 블룸을 더하지 않음(번짐 검사), 색조를 거꾸로 돌림(네 값 함께 검사), LUT 정의역을 무시함(정의역 검사).

### 3.11 8 단계 - 프로필 에셋·카메라의 프로필·인스펙터 (D-344)

사용자 결정: 프로필은 "인스펙터" 에서 고친다(사운드 이펙트처럼 자기 창을 여는 길은 고르지 않았다). 고치면 바로 파일에 적고, 되돌리기는 에디터의 것이다.

| 것 | 모양 |
|---|---|
| 본문(`.jpost`) | `Effects:` 목록. 항목마다 `Type`·`Enabled`·`Parameters`(덮어쓴 필드만). **적히지 않은 필드는 그 종류의 기본값이다** - 필드를 덮어쓰는 것이 곧 그 필드를 파일에 적는 것이고, 볼륨이 섞을 때(10 단계) 덮어쓴 필드만 섞는다. 효과 종류 표에 없는 종류는 걸지 않고 읽은 그대로(스칼라 값만) 되쓴다. 아는 종류의 모르는 필드·읽히지 않는 값은 경고로 버린다. 깨진 YAML 은 거절한다. 읽고 쓰기는 효과 종류 표가 있는 그래픽 모듈(`PostProcessProfile.h`)에 있다 - 에셋 모듈은 글자만 든다 |
| 풀기(`PostProcessLibrary`) | 프로필 에셋을 뷰의 효과 목록으로 푼다. 처음 볼 때와 프로필·LUT 의 `dataGeneration` 이 바뀔 때만 다시 풀고, 효과 블록 안의 `xxxId` 는 에셋 해석 패스(`BindComponentAssets`)로 싣고, `.cube` 는 3D 텍스처로 올린다. 렌더러 프레임 밖(프레임워크의 Update)에서 부른다 |
| 카메라 | `Camera2D`·`Camera3D` 의 `postProcessProfileId`. 2D 는 게임 화면의 월드 뷰에 건다(화면 레이어는 뒤의 다른 뷰라 후처리 뒤다). 3D 는 레이어마다 뷰라 뷰마다 걸면 노출·톤맵이 겹친다 - 레이어를 다 얹은 뒤 그릴 것 없는 뷰 하나가 얹힌 그림을 장면으로 옮겨 한 번 건다. 그래서 3D 의 장면은 백버퍼 포맷까지만 밝다 |
| 캔버스 뷰 | 게임 카메라의 프로필로 보인다. 도구 막대의 후처리 단추로 끈다(처음에는 켜짐). UI 보기는 늘 후처리가 없다. 선택 외곽선과 ImGui 로 그리는 격자·기즈모는 후처리 뒤다 |
| 인스펙터 | `.jpost` 를 고르면 `후처리 효과` 머리 아래 효과마다 접는 머리(종류 이름, 번역하지 않는다), `사용`, 필드마다 앞의 덮어쓰기 체크(끈 필드는 흐리고 기본값). `효과 추가` 는 효과 종류 표를 늘어놓는다(사용자 효과도 나온다). 머리를 끌어 순서를 바꾸고(컴포넌트 머리와 같은 손짓, D-307), 머리 메뉴에서 지운다. 모든 편집은 새 커맨드 `SetAssetSourceCommand` 로 파일의 글자 전체를 바꾸고 실린 에셋을 제자리에서 다시 읽는다 |
| 그 밖 | 에셋 브라우저의 `새 후처리 프로필`(빈 프로필을 만들고 고른다), 에셋 아이콘(`PostProcess`·`ColorLut`), 게임 빌드의 패키지 모으기가 프로필 본문에서 아이디를 찾는다(LUT 가 따라 싸인다) |

시험:
- `PostProcessProfile`: 덮어쓴 필드만 적히고 되읽으면 값·기본값·덮어쓰기가 돌아오며 쓴 글자가 같다. 덮어쓰기를 끄면 기본값이다. 숨긴 필드는 덮어쓸 수 없다. 모르는 종류는 되쓰고 모르는 필드는 버리며 경고가 셋이다. 깨진 YAML 은 거절, 빈 파일은 빈 프로필.
- `EditorApplication` 의 `TestThePostProcessProfileIsEditedInTheInspector`(사용자 시나리오): 빈 프로필을 만들어 카메라에 걸고, 인스펙터의 `효과 추가` 단추와 메뉴로 기본 조정을 더하고(파일에 `Parameters` 가 없다),
  채도의 덮어쓰기 체크를 켜고(`saturation: 0`), 슬라이더의 왼쪽을 눌러 낮춘다(파일에 음수, 캔버스 뷰가 회색 쪽으로). 되돌리기·다시 하기가 파일과 그림을 함께 돌리고, 캔버스 뷰의 단추를 끄면 원래 초록이다.
  프로필에 `.cube` 를 건 LUT 효과를 넣으면 라이브러리가 3D 텍스처를 올려 초록이 자홍이 된다.
- 실제 에디터(`JBroEditorHost`, PostMessage): 에셋 브라우저 우클릭 `새 후처리 프로필` → 인스펙터의 `효과 추가` 메뉴에 다섯 종류 → `Bloom` 을 고르면 파일에 `Type: Bloom` 이 적히고 필드 여섯이 덮어쓰기 체크와 함께 선다. 도구 막대에 후처리 단추가 라이팅 단추 뒤에 선다.
  보고 고친 것: 메뉴의 다른 `새 …` 항목에는 아이콘이 없어 뺐다, 블룸의 `clamp`(1..65504)는 슬라이더로는 쓸 수 없어 숫자 칸으로 바꿨다.

뮤테이션 넷(2026-10-27)이 모두 죽었다: 라이브러리가 바뀐 프로필을 다시 풀지 않음(회색 검사), 덮어쓰기와 상관없이 모든 필드를 적음(기본값 필드 검사), 캔버스 뷰 단추를 무시함(단추 검사), LUT 를 올리지 않음(자홍 검사).

전체 시험(Debug, `736c984b`, Codex 가 돌림): main 에 원래 있던 `ScriptSummaryPanel` 크래시를 빼고 이어 돌려 111 묶음이 지났다. 새로 하나가 실패했다 - `BuiltinComponentProperty` 가 `Camera2D`·`Camera3D` 의 필드 수를 7 로 적고 있었다. 프로필 필드 둘(`postProcessProfileId`·숨긴 `postProcessProfile`)을 더했으니 9 로 고쳤다(Claude 가 다시 돌려 지남).
8 묶음(`PlatformContract`·`SourceDebugger`·`NestedScriptType`·`ScriptProject`·`ScriptBuild`·`DebugAdapter`·`ScriptBreak`·`UserScenario`)은 D-342 때와 같이 Codex 샌드박스 안에서만 실패했고, Claude 가 밖에서 다시 돌리니 모두 지났다.

남은 것: ~~레이어 프로필(9 단계)·볼륨과 섞기·스크립트 서비스(10 단계)~~ → §3.12·§3.13. 인스펙터는 실리지 않은 프로필을 고를 때 디스크를 매 프레임 읽는다(에디터 화면이라 권고 수준, D-274). 3D 의 HDR 장면(레이어 합성까지 HDR 로 두는 일)은 `AfterScreenLayers` 와 함께 볼 일이다.

### 3.12 9 단계 - 레이어 프로필과 `AfterLayer` (D-345)

사용자 결정: 레이어에는 "새 레이어 효과 + 색 효과" 를 건다 - 블러·외곽선·그림자를 새로 만들고, 기본 조정·LUT 도 레이어에 걸린다. 노출·블룸·톤맵은 장면 전체를 보는 효과라 레이어에서는 건너뛰고 센다.

| 것 | 모양 |
|---|---|
| 레이어의 프로필 | 캔버스의 `Layer` 가 `PostProcessProfile` 아이디를 든다. 캔버스 파일과 레이어 에셋(`.jlayer`, 레이어의 내용이라)에 `PostProcessProfile` 로 적는다. 레이어 삭제의 되돌리기·레이어 에셋 넣기가 같이 뜬다. 새 커맨드 `SetLayerPostProcessProfileCommand` |
| 풀기 | 프레임워크가 레이어마다 `PostProcessLibrary::FindOrLoad`(처음 보는 아이디만 싣는다)·`Resolve` 로 풀어 렌더 월드의 레이어 차례 자리에 건다(`RenderWorld2D`·`3D::SetLayerPostProcess`) |
| 효과 종류 | `PostProcessEffectType::onLayer` 가 레이어에 걸리는 종류다. 새 `AfterLayer` 효과 셋: `Blur`(가우스, 반경 픽셀), `Outline`(알파 절반 경계 바깥으로 굵기 픽셀, 그다음 한 픽셀에 걸쳐 흐림), `DropShadow`(알파를 옮겨 흐려 밑에 깐다, 옮김의 y 는 위가 +). 셋 다 화면 픽셀이라 캔버스 뷰를 키워도 굵기가 같다. 기본 조정·LUT 은 `onLayer` 다 |
| 곱한 알파 | 레이어 텍스처는 곱한 알파다. 빌더의 `IsPremultiplied` 가 참이면 색 효과가 알파로 나눠 바꾸고 다시 곱한다(셰이더 상수의 표시). 레이어 효과 셋은 처음부터 곱한 알파를 읽고 쓴다 |
| 렌더러 | `BeginLayer` 가 효과 목록을 받아 프레임 저장소에 옮긴다(`CopyPostEntries`, 뷰의 것과 같은 길). 2D 레이어 묶음은 레이어를 그린 뒤 효과를 낸 차례로 걸고 얹는다. 효과가 있으면 `Normal`·1 이어도 묶음을 연다. 효과는 레이어 텍스처 **전체**를 덮는다 - 다음 효과가 뷰포트 둘레를 읽어도 빈 값이다. 걸 자리(파이프라인·중간 텍스처)는 바라 두고 모자라면 그 프레임은 효과 없이 얹는다(`layerPostProcessWaitingCount`) - 그래프는 텍스처가 없는 패스를 건너뛰므로, 미리 다 있어야 레이어가 사라지지 않는다 |
| 3D | 3D 레이어는 레이어마다 뷰라 뷰의 `CameraParams::layerPostProcess` 다. 그 뷰를 제 텍스처에 그려 효과를 건 뒤 얹는다(`Normal`·1 이어도). 썸네일과 후처리를 끈 캔버스 뷰는 걸지 않는다 |
| 에디터 | 레이어 인스펙터의 `후처리 프로필` 칸(프로필 에셋 고르기). 캔버스 뷰의 후처리 단추를 끄면 레이어 효과도 꺼진다. 썸네일·선택 외곽선의 마스크는 효과를 보지 않는다 |
| 셰이더 | `BuiltinLayerEffects.hlsl` 의 다섯 프로그램(블러, 외곽선 거리·얹기, 그림자 흐림·얹기). 빌트인 프로그램이 27 개다 |

시험:
- `PostProcessPixel`(세 백엔드, 디버그 레이어): 외곽선(굵기 2 의 두 픽셀은 빨강, 셋째는 검정, 앞에 반경 0 블러를 둬 효과 사이의 중간 텍스처도 지난다, LDR 과 HDR 장면 모두), 그림자(옮긴 자리만 검다), 블러(가장자리 반쯤·네 픽셀 밖 조금·여섯 픽셀 밖 0), 레이어의 대비가 곱한 알파를 풀어 바꿈(풀지 않았다면 0.25, 맞으면 0.5)과 노출을 버리고 셈, 통째로 얹는 뷰의 `layerPostProcess`. 힙 시험에 레이어 효과가 든 프레임을 더했다.
- `CanvasFile`: 레이어의 프로필이 캔버스 파일과 레이어 에셋을 오간다.
- `EditorApplication` 의 `TestALayerProfileOutlinesItsLayer`(사용자 시나리오): 레이어를 고르고 인스펙터의 `후처리 프로필` 칸에서 빨간 외곽선 프로필을 고르면 캔버스 뷰에 빨간 둘레가 서고, 후처리 단추를 끄면 사라지며, 되돌리기·다시 하기가 따라간다. 스크린숏으로 흰 사각형 둘레의 빨간 선을 봤다.

### 3.13 10 단계 - 볼륨과 섞기·스크립트 서비스 (D-346)

사용자 결정: 볼륨의 모양은 "볼륨이 직접 든다"(물리와 따로 논다 - 3D 콜라이더가 아직 뼈대뿐이다), 스크립트 서비스는 "계획대로 셋"(볼륨의 무게·프로필, 효과 필드 덮어쓰기).

| 것 | 모양 |
|---|---|
| 볼륨 | 새 컴포넌트 `PostProcessVolume2D`(`Global`·`Box`·`Circle`)·`PostProcessVolume3D`(`Global`·`Box`·`Sphere`). 필드: `shape`·`size`·`radius`·`blendDistance`·`priority`·`weight`·`postProcessProfileId`. 오브젝트에 하나다. 모양은 오브젝트의 월드 위치·회전·크기를 따른다(크기는 `size`·`radius` 에 곱한다) |
| 무게 | `weight` x 거리 비율 - 모양 안이면 1, 밖 `blendDistance` 안이면 경계에서 멀어질수록 0 으로, 그 밖은 0. `Global` 은 늘 1. 잰 자리는 게임 카메라의 월드 위치다 - 캔버스 뷰도 게임 카메라 자리의 결과로 보인다(뷰마다 따로 섞지 않는다) |
| 섞기(`PostProcessMixer`) | 카메라의 프로필이 기준이고, 볼륨을 `priority` 가 낮은 것부터(같으면 캔버스의 차례) 섞는다. **필드의 리플렉션 종류로 정한다**: `Float` 보간, `Float` 만 든 구조(`Vector2`·`Vector3`·`Color`) 필드마다 보간, `Int32` 보간해 반올림, 그 밖(`Bool`·열거·에셋)은 무게 절반에서 바꾼다. 볼륨은 덮어쓴 필드만 섞는다(라이브러리가 항목마다 덮어쓴 필드를 준다, `ResolveProfile`). 저장하지 않는 필드(LUT 의 텍스처·정의역)는 그 항목의 에셋 아이디가 바뀔 때 함께 바뀐다. 기준에 없는 종류는 기본값에서 시작하는 항목을 끝에 더한다. 자리는 미리 잡고 넘치면 버리고 센다 |
| 스크립트 | 볼륨의 무게·프로필은 볼륨 핸들(`PostProcessVolume2D::SetWeight`·`SetPostProcessProfile`, 다른 필드도)이다. 효과 필드 덮어쓰기는 공통 서비스 `GetServiceContext().PostProcess`(`FindField`·`SetFloat`·`SetInt`·`SetBool`·`SetColor`·`SetVector2`·`Clear`·`ClearAll`)다 - 섞은 맨 끝에 얹고 지울 때까지 남는다. 필드의 타입이 맞아야 하고 숨긴 값은 덮어쓰지 못한다. 시스템은 공통 `SystemContext::PostProcess`(`IPostProcessSystem`, 판 7), 서비스는 공통 `ServiceContext::PostProcess`(판 5)다. 구현은 호스트의 `PostProcessSystem` 이고 프레임워크가 하나씩 든다 |
| 에디터 | 컴포넌트 추가의 `Rendering` 에 선다. 2D 캔버스 뷰가 볼륨의 모양(진하게)과 섞는 거리의 끝(옅게, 상자는 모서리가 둥글다)을 늘 그린다 |

시험:
- `PostProcessMixer`: 종류마다 섞기(Float 의 4 분의 1, Color, Int32 반올림, 덮어쓰지 않은 필드는 기준, 열거는 절반에서), 기준에 없는 종류를 기본값에서 더함, 뒤에 섞은 볼륨이 위, LUT 아이디가 바뀌면 텍스처·정의역이 따라감, 스크립트의 덮어쓰기가 볼륨 위·타입이 다르면 거절·숨긴 값은 못 찾음·같은 필드는 갈아 끼움, 넘치면 셈. 2D·3D 무게(전역·상자 안팎·섞는 거리·돌린 상자·크기를 곱한 원·구).
- `EditorApplication` 의 `TestAVolumeBlendsAroundTheCamera`(사용자 시나리오): 채도 -100 프로필의 상자 볼륨이 멀면 초록, 카메라 쪽이면 회색, 무게 반이면 반쯤 회색. 스크립트 서비스로 채도를 0 으로 덮어쓰면 볼륨 안에서도 초록이고 지우면 다시 회색이다. 스크린숏으로 상자와 둥근 섞기 경계를 봤다.
- `ContextBoundary`: 공통 시스템 컨텍스트가 포인터 여섯이고 `PostProcess` 슬롯이 `IPostProcessSystem*` 다.

뮤테이션 넷(2026-10-27)이 모두 죽었다: 브리지가 레이어 효과를 보지 않음(캔버스 뷰의 빨간 외곽선 검사), 레이어의 색 효과가 곱한 알파를 풀지 않음(대비 검사), 프레임워크가 스크립트의 덮어쓰기를 얹지 않음(서비스 검사), 무게가 섞는 거리를 보지 않음(상자 밖 반 무게 검사).

전체 시험(Debug, `c0b70a77`, Codex 가 돌림): main 에 원래 있던 `ScriptSummaryPanel` 크래시를 빼고 이어 돌려 112 묶음이 지났다. 새로 하나가 실패했다 - `RendererContract` 의 프레임 힙 검사다.
첫 프레임에 레이어 효과 자리(`RenderWorld2D::SetLayerPostProcessCount`)를 처음 늘리며 할당했다. 프레임워크를 세울 때 자리를 잡게 고쳤고(`ReserveLayerPostProcess`), Claude 가 다시 돌려 지났다.
8 묶음(`PlatformContract`·`SourceDebugger`·`NestedScriptType`·`ScriptProject`·`ScriptBuild`·`DebugAdapter`·`ScriptBreak`·`UserScenario`)은 전과 같이 Codex 샌드박스 안에서만 실패했고, Claude 가 밖에서 다시 돌리니 모두 지났다.
실제 에디터(`JBroEditorHost`, PostMessage): 레이어 인스펙터의 `후처리 프로필` 칸에서 `Outline.jpost` 를 고르고 스프라이트를 붙이니 4 픽셀 빨간 외곽선이 섰고, 파일을 고치면 감시가 다시 읽었다. 컴포넌트 추가의 `렌더링` 에 `PostProcessVolume2D` 가 선다.
보고 안 것: 손으로 쓴 `.jpost` 의 흐름 표기 `[1, 0, 0, 1]` 은 엔진 YAML 이 읽지 않아 그 필드가 기본값이 된다 - 엔진이 쓰는 파일은 블록 표기라 맞다.

남은 것: 3D 캔버스 뷰의 볼륨 기즈모. 3D 프레임워크의 레이어 효과와 볼륨 섞기는 렌더러 픽셀 시험(뷰의 `layerPostProcess`)과 무게 단위 시험으로만 쟀고 3D 에디터 시나리오는 없다. JBroScript 카탈로그(`JBroScriptApiCatalog2D`)에 볼륨 핸들과 후처리 서비스가 없다 - C++ 스크립트는 쓰지만 `.jscript` 의 언어 서버는 모른다. 볼륨을 뷰마다(캔버스 뷰는 편집 카메라 자리) 따로 섞는 일은 하지 않았다. `AfterScreenLayers`(화면 레이어까지 얹은 뒤)는 여전히 자리가 없다.

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
- ~~블룸을 컴퓨트로 할지 래스터로 할지는 7 단계에서 세 백엔드를 재고 정한다.~~ → 래스터 밉 체인(D-343, 사용자 결정). 컴퓨트 판은 재지 않았다.
- ~~장면 HDR 포맷을 `RGBA16Float` 로 둘지 `R11G11B10Float` 로 둘지는 6 단계에서 정한다(알파가 필요한 레이어 합성과 맞물린다).~~ → `RGBA16Float`(D-342, 사용자 결정).

이 문서를 읽고 나면 어느 계층부터 손대야 하는지와, 단계마다 무엇이 서면 끝인지 알 수 있어야 한다.

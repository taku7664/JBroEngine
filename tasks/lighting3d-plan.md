# 3D 조명 계획 (Light3D)

결정은 D-407. 2026-11-19 사용자와 정했다(todo-3d 우선순위 5, "다 추천안대로"). 그림자는 이 계획에 없다 - 그 뒤의 일이다.

> 상태: **완료** (2026-11-19 시작·세 단계 완료, D-408~D-410). 남은 것은 §4.

## 1. 지금 상태와 참고한 것

### 1.1 지금 엔진 (2026-11-19 실측)

- 3D 의 빛은 `BuiltinMesh.hlsl` 의 상수 셋이다: 고정 방향광(`normalize(0.4, 0.8, 0.45)` 쪽으로, 세기 0.75)과 주변광 0.25. `Standard` 재질(D-399)이
  그 빛으로 metallic-roughness PBR 을 셈하고, `Unlit` 은 빛을 받지 않는다.
- 메시 패스는 b0 푸시 상수(뷰-투영·카메라 위치·탄젠트 있음)와 b1 재질 상수 블록을 묶는다. 렌더러는 프레임마다 바뀌는 상수를 올리는 `ConstantRing`(D-325)을
  아직 쓰지 않는다.
- 2D 는 `Light2D`(종류 `Global`·`Point`·`Spot`, 색·세기·반지름 둘·전체 각 둘·그림자)와 렌더러의 `SubmitLight2D` 가 섰다(D-291, `lighting2d-plan.md`).
  라이트는 레이어를 가리지 않고 빛을 받는 모든 레이어를 비추며, 감춘 레이어의 라이트는 꺼진다. 캔버스에 라이트가 없으면 라이팅이 꺼진 장면이다.

### 1.2 상용 엔진과 glTF (2026-11-19 조사)

| | 종류 | 거리 감쇠 | 원뿔 각 | 환경광 | 라이트가 없을 때 |
|---|---|---|---|---|---|
| glTF `KHR_lights_punctual` | directional·point·spot | `clamp(1 - (d/range)^4, 0, 1) / d^2`, `range` 없으면 무한 | 가운데에서 잰 반각, 기본 안 0·밖 π/4 | 없음 | - |
| Unity (URP) | Directional·Point·Spot·Area | Range 로 자름 | Inner/Outer Spot Angle(원뿔 전체) | Lighting 창(라이트가 아님) | 새 씬에 방향광이 들어 있음 |
| Unreal | Directional·Point·Spot·Rect·Sky | 기본 거리 제곱, Attenuation Radius 는 자르기만 | 안/밖 원뿔 | SkyLight 컴포넌트 | - |
| Godot 4 | DirectionalLight3D·OmniLight3D·SpotLight3D | `omni_attenuation` 지수(기본 1, 2 가 물리) | spot_angle·spot_angle_attenuation | `WorldEnvironment` | **에디터만** 미리보기 해·환경을 띄우고, 씬에 방향광을 놓으면 끈다 |

- 물리 단위(루멘·럭스)는 Godot·Unreal 모두 켜는 설정이고 기본은 단위 없는 배율이다.
- 라이트 수: Unity URP 포워드는 오브젝트당 추가 라이트 8, Forward+ 는 데스크톱 카메라당 256.
- 출처: KHR_lights_punctual README(Khronos), Godot 문서(GLTFLight·OmniLight3D·물리 단위·환경), Unreal 문서(Point Light·`bUseInverseSquaredFalloff`),
  Unity 문서(Light 컴포넌트·Forward+·라이트 제한). Filament 문서는 읽지 못했다(리디렉트).

## 2. 설계 (D-407, 2026-11-19 사용자 확인)

### 2.1 정한 것

1. **컴포넌트 `Light3D` 하나에 종류.** `Ambient`·`Directional`·`Point`·`Spot`. 2D 의 `Light2D` 와 같은 모양이고 2D 의 `Global` 이 3D 의 `Ambient` 다.
   세 엔진 모두 환경광은 라이트 종류가 아니지만(Unity 는 설정, Godot 는 `WorldEnvironment`, Unreal 은 따로 된 SkyLight), 2D 와 같은 모양을 앞에 둔다.
   하늘 상자 같은 환경이 오면 그때 다시 본다(§4).
2. **거리 감쇠는 glTF 권장식, 필드는 `range` 하나.** 2D 의 안·밖 반지름과 다르다 - 선형 감쇠는 PBR 과 섞이면 어색하고, glTF 와 같으면 모델의 라이트를 그대로 가져온다.
   `range` 가 0 이면 무한(감쇠는 거리 제곱만)이다. 거리는 0.01 아래로 내려가지 않는다(라이트 바로 곁의 무한대 방지).
3. **라이트가 없는 캔버스는 지금의 고정 빛이다.** "라이트가 없으면 라이트 기능 전과 같다" 는 2D 규칙과 같은 뜻이다. Godot 는 에디터에서만 이렇게 하지만,
   여기는 게임도 같다 - 에디터와 게임이 같은 그림을 내는 것을 시험이 바이트로 본다(D-404). `Light3D` 가 하나라도(종류 무관) 뷰에 오면 놓인 라이트만 빛을 낸다.
   새 3D 프로젝트의 시작 오브젝트(`AddStarterObjects`)에 `Directional` 라이트 하나를 더한다.
4. **포워드, 뷰마다 라이트 목록 하나.** 상수 블록(b2)에 `Ambient` 합과 라이트 최대 64 를 담아 픽셀마다 모두 돈다. 넘치면 버리고 센다
   (`RendererFrameStats`, 2D 의 `maxLights2D` 와 같은 꼴). 클러스터는 재 본 뒤다.
5. **그림자는 하지 않는다.** `castShadows` 필드도 그림자 단계에서 더한다 - 하는 일 없는 필드를 두지 않는다.

### 2.2 컴포넌트 `Component::Light3D`

| 필드 | 타입 | 기본 | 뜻 |
|---|---|---|---|
| `type` | `Light3DType` | `Point` | `Ambient`·`Directional`·`Point`·`Spot` |
| `color` | `Color` | 흰색 | 선형 |
| `intensity` | `Float` Clamp(0, 100) | 1 | 색에 곱한다. `Directional`: 마주 보는 흰 램버트 면이 이 값이 된다. `Point`·`Spot`: 1 유닛 거리에서 그렇다(거리 제곱 감쇠) |
| `range` | `Float` Clamp(0, 1000) | 10 | `Point`·`Spot`. 여기서 0. 0 이면 무한 |
| `innerAngle` | `Degree` Clamp(0, 179) | 30 | `Spot` 원뿔 **전체** 각(2D 와 같다). 안은 다 닿는다 |
| `outerAngle` | `Degree` Clamp(0, 179) | 60 | 여기서 0 |

- 자리와 방향은 오브젝트의 `Transform3D` 월드 값이다. 빛은 로컬 -Z 로 간다(glTF·`AudioListener3D` 와 같다). 크기는 보지 않는다.
- 원뿔 감쇠는 glTF 식: `t = saturate(cos θ · scale + offset)`, `scale = 1 / max(0.001, cos(안/2) − cos(밖/2))`, `offset = −cos(밖/2) · scale`, 빛 × t².
  안 각이 밖 각보다 크면 같다고 본다.
- 감춘 레이어의 라이트는 꺼진다(2D 와 같다). 라이트는 모든 레이어의 메시를 비춘다. `Unlit` 재질과 발광은 영향을 받지 않는다.

### 2.3 렌더러

- `Light3DSubmit`(종류·월드 위치·방향·색×세기·`range`·전체 각 둘)과 `SubmitLight3D(s)` - 뷰 안에서 받는다(2D 와 같은 자리). `Ambient` 는 뷰의 환경광에 더하고
  나머지는 뷰의 목록에 담는다. 상한은 `RendererConfig::maxLights3D`(기본 64, 셰이더 배열 길이와 같다).
- 메시 패스는 뷰마다 라이트 블록을 `ConstantRing` 에 한 번 올려 b2 로 묶는다. 라이트를 받지 않은 뷰는 렌더러가 **기본 빛**(지금의 고정 방향광·주변광)을 그 블록에 채운다 -
  셰이더는 길이 하나다.
- 블록: `float4 ambientCount`(rgb 환경광, w 라이트 수) + 라이트마다 `float4` 넷(자리와 `range`, 방향과 종류, 색, 원뿔 scale·offset) = 16 + 64 × 64 = 4112 바이트.

### 2.4 프레임워크와 스크립트

- `Light3DSystem` 이 `RenderWorld3D` 에 라이트를 담고, 브리지가 뷰마다 같은 목록을 낸다(레이어마다 뷰가 열리므로).
- 스크립트 핸들 `Light3D`(2D 의 `Light2D` 핸들과 같은 멤버).

### 2.5 에디터 (2D 와 같게)

- 컴포넌트 추가 메뉴·인스펙터는 리플렉션 그대로. 끝이 안전장치일 뿐인 값은 끌기다(2D 와 같은 `Clamp`).
- 캔버스 뷰: 라이트 오브젝트에 전구 표시(빛의 색), 고르면 `Point` 는 `range` 구, `Spot` 은 원뿔과 각 손잡이, `Directional` 은 방향 화살표. 손잡이로 `range`·각을 끈다.

## 3. 단계

| 단계 | 내용 | 완료 조건 | 상태 |
|---|---|---|---|
| 1 | 렌더러: `Light3DSubmit`·b2 라이트 블록·셰이더 루프·기본 빛·넘침 세기 | 세 백엔드 픽셀 시험 - 라이트 없음은 전과 같은 그림, 방향광의 방향, 점광의 `range` 밖은 환경광만, 스포트의 안·밖, `Ambient` 만, 넘침 셈. 뮤테이션 | `[완료]` 2026-11-19 · D-408 · `Light3DSubmit`·`SubmitLight3D(s)`·`RendererConfig::maxLights3D`(1024)·`Renderer::MaxLights3DPerView`(64)·`RendererFrameStats::light3DCount`/`droppedLight3DCount`, 메시 셰이더의 라이트 루프와 b2 블록(`ShaderManifest` 의 `Mesh` 상수 버퍼 둘), 뷰마다 `ConstantRing` 에 한 번(앞 뷰와 같은 바이트면 그 구간을 다시 묶는다), 기본 빛 블록 · 시험 `MeshPixel` 의 `TestLightsLightTheMeshes`(세 백엔드) |
| 2 | `Light3D` 컴포넌트·`Light3DSystem`·브리지·스크립트 핸들·시작 오브젝트의 방향광 | 캔버스로 그린 픽셀 시험, 감춘 레이어의 라이트, 캔버스 파일 왕복, 새 3D 프로젝트의 라이트 | `[완료]` 2026-11-19 · D-409 · `Component::Light3D`(`Light3DType`)·`Handle::Light3D`·`Light3DSystem`·`RenderWorld3D` 의 라이트·브리지의 `PushLights`(레이어 뷰마다)·시작 오브젝트 `Light`(방향광 0.75 + 환경광 0.25)·새 스크립트 창의 `Light3D` 필드 · 시험 `MeshPixel` 의 `TestLightComponentsLightTheCanvas`(세 백엔드), `UserScenario` 의 시나리오 D |
| 3 | 에디터: 전구 표시·기즈모(`range` 구·원뿔 손잡이·방향 화살표) | 손잡이를 끌어 값이 바뀌고 되돌리기 한 번, 시험 스크린숏 | `[완료]` 2026-11-19 · D-410 · `LightGizmo3DModel`(화면을 모르는 손잡이 수학)·캔버스 뷰의 `DrawObjectMarkers3D`(라이트·카메라 표시)·`DrawLightEditor3D`(점광 구·스포트 원뿔·방향광 화살표, `range`·바깥 각·안쪽 각 손잡이, 커맨드 한 번) · 시험 `LightGizmoModel` 의 `TestThe3DLightHandles`, `UserScenario` 의 시나리오 L(`TestScenarioLights3D`, 스크린숏 l01~l06) |

## 4. `[열림]`

- 그림자(방향광의 캐스케이드·점광 큐브맵·스포트) - 그 뒤. `castShadows` 는 그때 더한다.
- 환경광의 자리: 하늘 상자·IBL 이 오면 `Ambient` 라이트를 둘지, 캔버스 설정으로 옮길지.
- 모델 파일의 라이트 가져오기(glTF `KHR_lights_punctual`·FBX 라이트, model-import-plan §4 의 "카메라·조명") - 각은 반각이라 두 배로 옮긴다.
- 물리 단위(루멘·럭스)와 노출.
- 64 를 넘는 라이트(클러스터·타일).
- 3 단계에서 남긴 것: 손잡이 끌기는 화면에서 잰다 - 스포트의 `range` 는 원근에서 축을 따라 선형이 아니라 손잡이가 마우스보다 조금 앞서거나 뒤진다(놓을 때 값은 맞는다). 축이 카메라를 곧바로 향하면 각 손잡이가 꼭짓점에 겹친다.
  ~~각 손잡이도 화면에서 잰다~~ → 2026-11-21 D-441: 각은 마우스 광선을 끝 원의 평면에 맞혀 월드에서 잰다(`LightGizmo3DModel::DragAngleOnPlane`, 맞힐 수 없을 때만 화면 셈).
  내려다보는 스포트에서 화면 셈은 두 배 거리로 끌어도 60 도가 65 도밖에 안 됐다 - 시나리오 L 은 그동안 편집 카메라 대신 썸네일의 게임 카메라로 재서 통과했었다. `range` 손잡이는 아직 화면 셈이다.
  전구 표시는 그리기만 시험이 보지 않는다(시나리오 L 의 스크린숏으로 봤다). 표시를 누르면 골라지는 것은 3D 의 자리 집기가 한다.
- 시나리오 L 의 게임 화면은 프로젝트 해상도가 작아(`EnableEditorUi({64, 48})`) 흐리다 - 라이트와 무관하다.

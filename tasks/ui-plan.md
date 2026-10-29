# 화면 공간 UI 계획 (D-237)

> 상태: **진행** (2026-09-27 시작). 사용자 확인(2026-09-27): 화면 레이어 + 앵커 방식, 좌표 단위는 기준 해상도의 픽셀, 레이어의 Space·ScaleMode 는
> 캔버스의 `Layer` 로 옮긴다, 캔버스 뷰는 월드/UI 보기 전환, 범위는 1~3 단계 모두. 동기는 text-plan §5 5 단계가 남긴 "화면 공간 UI 텍스트" 다.

## 1. 기존 엔진의 UI - 무엇이 있었고 무엇이 아팠나

읽은 것: `Engine/GameFramework/Canvas/GameLayer.h`·`Canvas.cpp`(`GetRootAnchorOffset`·`ScreenToUI`)·`Transform/TransformSystem.cpp`,
`Engine/Core/Renderer/ScreenSpaceProjection.h`·`Render2DPipeline.cpp`, `Component/Button2D.h`·`System/Button2DSystem.cpp`,
`Application/Editor/Main/CanvasView/CanvasViewTool.cpp`, 그쪽 문서 `tasks/UISystemDesign.md`·`_TEMP_RendererBaseAndButton.md`.

### 1.1 있었던 것

- **UI 트리가 없다.** 레이어마다 `Space {World, Screen}`·`ScaleMode`·`AnchorToSafeArea` 가 있고, 화면 레이어에 보통의 렌더러(`SpriteRenderer2D`·`Text2D`)를 올렸다.
  RectTransform 식 트리는 일부러 거절했다(UISystemDesign §2 Alt-C).
- **앵커는 `Transform2D.Anchor`**(Y 위, (0,0) 이 왼쪽 아래). `UIAnchor2D` 컴포넌트는 "위치의 출처가 둘이 된다" 며 거절했다. 루트에만 걸고, 앵커 점을
  **부모 행렬로** 넣어 `Position` 을 덮지 않았다. 매 프레임 다시 쟀다(더러움 표시 없음).
- **맞춤 방식** FixedHeight·FixedWidth·Contain·ConstantPixel. 좌표는 유닛(기준 해상도 / 전역 PPU).
- **그리기**: 월드(조명 포함) 뒤의 "Overlay" 패스에서 화면 레이어를 그렸다 - UI 가 광원 맵에 곱해지지 않게.
- **버튼** `Button2D`(누름 사각형·색/스프라이트 바꾸기·스크립트 훅 다섯). 버튼 시스템은 스크립트보다 먼저 돌았다.
- **에디터**: 화면 레이어도 편집 카메라로 그림, 기준 사각형 안내선, "화면에 맞추기", 월드↔화면 바꾸기 커맨드(화면 위치 유지), 화면 레이어에서만 보이는 앵커 칸.

### 1.2 아팠던 것

| 번호 | 문제 | 새 설계의 대응 |
|---|---|---|
| U1 | 그린 뒤에 화면 크기를 알려 주어 **첫 프레임에 앵커가 0** 이었다 | 프레임을 시작할 때 대상 크기를 안다(`GetFrameExtent`·시뮬레이션 뷰 텍스처). 앵커를 그 값으로 먼저 잰다(§2.3) |
| U2 | 앵커·역투영은 화면 전체, 그리기는 뷰포트라 **분할 화면에서 어긋났다** | 화면 영역을 재는 함수 **하나**를 그리기·앵커·역투영이 같이 부른다(§2.2) |
| U3 | 버튼이 장치를 직접 폴링했고, 게임이 `IsPointerOverButton()` 을 손으로 물어야 막혔다 | 입력 레이어 체인의 `"UI"` 레이어가 포인터를 소비한다 - 아래 핸들러와 폴링에서 자동으로 사라진다(D-214, §2.5) |
| U4 | 안전 영역 값을 넣는 플랫폼이 없었다 | 자리만 남긴다(§4) |

## 2. 설계

### 2.1 레이어

- 캔버스의 `Layer`(차원 무관)에 `LayerSpace { World, Screen }` 와 `ScreenScaleMode { FixedHeight, FixedWidth, Contain, ConstantPixel }` 를 둔다.
  캔버스 파일의 `Layers` 항목에 `Space`·`ScaleMode` 로 적는다(기본값이면 적지 않는다 - 옛 파일은 그대로 읽힌다). `Layer2D` 의 죽은 `Space` 는 지운다.
- 3D 는 이 판에서 화면 레이어를 그리지 않는다(값만 같이 쓴다).

### 2.2 좌표와 맞춤

- **화면 레이어의 1 유닛 = 기준 해상도의 1 픽셀.** 기준은 프로젝트의 `ResolutionWidth/Height`(기본 1920 x 1080). 원점은 화면 가운데, y 위.
- 대상 크기 (W, H) 픽셀에서 보이는 반폭·반높이(기준 픽셀):
  FixedHeight = (RH/2 · W/H, RH/2), FixedWidth = (RW/2, RW/2 · H/W), Contain = 기준 사각형이 다 들어가는 쪽(`min(W/RW, H/RH)` 로 나눔),
  ConstantPixel = (W/2, H/2).
- 이 계산은 `ScreenSpace.h` 의 함수 하나다. 앵커(§2.3)·그리기(§2.4)·역투영(§2.5)이 모두 이것을 부른다(U2).
- 화면 레이어의 그림·글자 크기는 PPU 대신 픽셀이다: 스프라이트는 프레임의 픽셀 크기, `Text2D` 의 `fontSize 32` 는 기준 픽셀 32 다.

### 2.3 앵커

- `Transform2D` 에 `anchor`(Vector2, 기본 (0.5, 0.5), Y 위)를 더한다. **화면 레이어의 루트에만** 뜻이 있다: 앵커 점 = (lerp(-반폭, 반폭, x), lerp(-반높이, 반높이, y))
  을 루트의 부모 행렬로 넣는다. `position` 은 앵커에서의 거리로 남는다. 자식은 로컬 그대로다.
- `Transform2DSystem` 은 프레임워크가 프레임마다 넣어 주는 화면 크기(`SetScreenExtent`)로 잰다. 크기는 렌더링 전에 알려져 있다(U1).

### 2.4 그리기

- 렌더 월드의 아이템이 화면 레이어의 것인지 표시를 든다. 브리지는 월드 뷰(월드 레이어만) 다음에 **화면 뷰**(화면 레이어만, §2.2 의 정사영, 카메라 무관)를 연다.
  렌더러는 대상을 첫 뷰에서만 지우므로 화면 뷰가 월드 위에 그려진다. 화면 레이어가 없으면 뷰를 열지 않는다.
- 월드 카메라가 없어도 화면 레이어는 그린다(메뉴만 있는 캔버스).
- 에디터 캔버스 뷰는 **월드 보기 / UI 보기**를 바꾼다: UI 보기는 화면 레이어만 편집 카메라로(기준 픽셀 좌표) 그리고 기준 사각형 안내선을 그린다.
  고른 오브젝트의 레이어가 바뀌면 보기도 따라 바뀐다.

### 2.5 입력 (3 단계)

- 화면 역투영 `ScreenToLayer`(게임 픽셀 → 그 레이어의 좌표)를 스크립트 서비스로 낸다.
- `Button2D`(누름 사각형·`interactable`·색 바꾸기·스크립트 훅)와 `Button2DSystem`(스크립트보다 먼저). 버튼 위의 포인터는 `"UI"` 입력 레이어에서 소비한다.

## 3. 단계

1. **그리기**: 레이어의 Space·ScaleMode 와 저장, `ScreenSpace` 함수, `Transform2D.anchor`, 화면 뷰, 화면 레이어의 픽셀 크기. 완료 조건: 픽셀 시험(맞춤 넷·앵커 모서리·
   카메라 없음·월드 위에 그림), 캔버스 파일 왕복.
2. **에디터**: 레이어 Space·ScaleMode 고르기, 앵커 칸(화면 레이어에서만), 월드/UI 보기 전환과 안내선, 월드↔화면 바꾸기 커맨드. 완료 조건: 에디터 시험, 되돌리기.
3. **입력**: 역투영 서비스, `Button2D`, 포인터 소비. 완료 조건: 눌림·훅·소비 시험.

## 4. 이 계획 밖 (`[열림]`)

- 안전 영역(값을 넣는 플랫폼이 없다), 분할 화면(카메라가 하나다), 레이아웃 컨테이너·스크롤·마스크·텍스트 입력·게임패드 포커스, 3D 의 화면 레이어,
  조명이 서면 화면 레이어가 광원 맵 뒤에 그려지는지(지금은 조명이 없다).

## 5. 진행 기록

1. ~~**그리기**~~ → 완료 2026-09-27 · `JBroCanvas`(`Layer` 의 `LayerSpace`·`ScreenScaleMode`, `ScreenSpace.h`, 캔버스 파일의 `Space`·`ScaleMode`),
   `Transform2D.anchor`, `Transform2DSystem::SetScreenSpace`, `RenderWorld2D`(아이템의 `screenSpace`·`scaleMode`), `RenderBridge2D`(월드 뷰 뒤 화면 뷰),
   `IFramework::SetScreenSpace`·`EngineInstance`(기준 해상도와 이번 프레임의 대상 크기를 `Update` 앞에서), 화면 레이어의 스프라이트·글자는 픽셀 크기 ·
   - **잰 것**(D3D12, 64 x 64): 기준 = 대상일 때 오른쪽 위 앵커의 8 px 사각형이 두 가장자리에서 4 px 안쪽(x 52..59, y 4..11)에 64 픽셀로 선다. 기준을 32 로
     줄이면(FixedHeight) 256 픽셀(16 x 16), ConstantPixel 은 기준과 무관하게 64 픽셀이다. 가운데로 옮기면 월드의 파랑 위에 빨강이다(화면 레이어가 월드
     레이어보다 아래 차례여도). 카메라를 지워도 검정 바탕에 그려진다. 화면 레이어의 40 px `A` 는 PPU 와 무관하게 40 px 이고 경계도 글자 픽셀이다.
     맞춤 넷의 값(1920 x 1080 기준, 1280 x 1024·2560 x 1080 대상), 앵커 점, 파일 이름 왕복과 모르는 이름 거절. 캔버스 파일: 화면 레이어의 공간·맞춤 방식과
     앵커가 오가고, 기본값은 적지 않으며, 모르는 공간은 거절한다.
   - 정한 것: 화면 뷰는 맞춤 방식이 같은 화면 아이템이 이어진 덩어리마다 하나다. 화면 레이어는 레이어 차례와 무관하게 월드 위다(기존 엔진의 Overlay 와 같다).
     에디터 캔버스 뷰는 이 단계에서 화면 레이어를 그리지 않는다(2 단계의 UI 보기).
2. ~~**에디터**~~ → 완료 2026-09-27 · `EditorViewDesc::screenSpace`(편집 뷰가 한 공간만 그린다), `SetLayerSpaceCommand`(공간·맞춤과 루트의 자리를 한 커맨드로,
   옛 자리를 뜨지 못하면 실행하지 않는다), `DeleteLayerCommand` 가 공간·맞춤을 되살린다, `EditorApplication::MakeLayerSpaceCommand`·`GetGameScreenSpace`,
   계층의 레이어 메뉴(화면 레이어로 바꾸기·맞춤 방식, 줄 끝의 `UI` 표시), 인스펙터는 화면 레이어에서만 `anchor` 를 보인다, 캔버스 뷰의 월드/UI 단추와
   기준 사각형 안내선, 고르기·상자 고르기·3D 고르기·선택 테두리가 지금 보기의 공간만 본다, 화면 레이어의 스프라이트 경계는 픽셀이다 ·
   - **잰 것**(에디터 시험, 64 x 48 시뮬레이션 뷰, 카메라 세로 절반 5): 월드 (5, 0) 을 화면 레이어로 바꾸면 (540, 0)(1920 x 1080 FixedHeight)이고, 되돌리기·다시
     하기가 공간과 자리를 함께 옮긴다. 맞춤 방식만 Contain 으로 바꾸면 자리는 540 그대로다. 화면 레이어의 오브젝트를 고르면 캔버스 뷰가 UI 보기로, 월드
     오브젝트를 고르면 월드 보기로 간다. 다시 월드로 바꾸면 3.75 다(Contain 에서 반폭 960 → 0.5625 x 6.667). 지운 화면 레이어를 되살리면 Screen·Contain 이다.
     없는 레이어는 커맨드를 만들지 않는다.
   - 정한 것: 월드↔화면 변환은 **지난 프레임의 게임 카메라**로 한다(렌더 월드의 카메라와 그 주인의 월드 행렬). 카메라가 없으면 자리는 그대로다. UI 보기의
     편집 카메라는 월드 보기와 따로 기억하고, 처음 열면 기준 사각형이 다 보이게 연다(세로 절반 = 기준 높이 x 0.55). 보기는 고른 것이 **바뀐 프레임에만**
     따라간다 - 손으로 바꾼 보기를 매 프레임 되돌리지 않는다. 캔버스 뷰의 최대 확대 한계는 기준 픽셀을 담도록 20000 이다.
3. ~~**입력**~~ → 완료 2026-09-27 · `ScreenSpace.h` 의 역투영(`ScreenPixelToNormalized`·`ScreenPixelToLayer` 와 반대쪽), `Button2D`(JBroFramework2D, 갈래 `UI`),
   `Button2DSystem`(JBroFramework2DSystem, 입력 체인의 시스템 핸들러이자 `IScreen2DSystem`), `ScriptSystem::AddSystemInputHandler`, `Camera2DSystem::SelectCamera`,
   `GameScript2D` 의 훅 다섯, `Screen2DService`(2D 서비스·시스템 컨텍스트 ABI 5) ·
   - **잰 것**(`InputChainTests`, 200 x 100 게임 화면 = 기준, 카메라 세로 절반 50): 가운데로 오면 화면 버튼만 호버이고 밑에 깔린 월드 버튼은 받지 않는다.
     누르면 눌림 색이고 게임의 폴링은 마우스를 보지 못한다. 버튼 위에서 떼면 클릭 한 번이고 `clicked` 는 한 프레임이다. 누른 채 벗어나면 포인터를 쥐고, 벗어나
     떼면 떼기뿐이다. 버튼 밖의 누름은 게임이 받는다. 월드 (60, 0) 의 버튼은 픽셀 (160, 50) 에서 눌린다. 오른쪽 위 앵커의 (-20, -10) 버튼은 픽셀 (180, 10) 에서
     눌린다. 꺼진 버튼은 누르지 못해도 포인터를 가져간다. 손가락도 같다. 서비스: 픽셀 (150, 25) 는 화면 레이어의 (50, 25), (160, 50) 은 월드 (60, 0) 이고 거꾸로도 같다.
   - 정한 것: 한 프레임에 **가장 위 버튼 하나**다(화면 레이어 → 레이어 차례 → 스프라이트 `renderOrder`). 감춘 레이어·꺼진 오브젝트의 버튼은 없는 것이다.
     훅은 입력 체인 안에서(고정 스텝보다 먼저) 불린다. 좌표는 지난 프레임에 그린 화면의 것이다.
   - **뮤테이션 12 개가 모두 죽는다**(`tools/mutations-ui3.txt`): 소비 빼기·월드를 위로·누름 사각형 1/10·아무 데서나 떼도 누름·꺼진 버튼 누르기·`clicked` 남기기·
     쥐기 빼기·카메라 무시·터치 무시·색 빼기·y 뒤집기 빼기·버튼에 화면 기준 안 주기. 처음에는 누름 사각형 1/10 과 "아무 데서나 떼도 누름" 이 살았다 - 시험이
     가운데만 누르고 버튼 밖에서만 뗐다. 가장자리 네 점과 "다른 버튼 위에서 떼기" 를 더해 죽였다.

## 6. 2 부 - 위젯 (D-351)

> 상태: **완료** (2026-10-29 시작, 2026-10-30 끝). 사용자 확인(2026-10-29): 범위는 1~3 단계(버튼 보강과 저작 보조 · 그림 위젯 · 클리핑과 스크롤).
> 사용자 지시(2026-10-30 "나머지 단계 진행")로 4~6 단계(텍스트 입력 · 게임패드 포커스 · 레이아웃 컨테이너, D-352)까지 했다. §4 의 `[열림]` 가운데 안전 영역·분할 화면·3D 의 화면 레이어만 남는다.
> 1 부(§1~§5)가 세운 뼈대(화면 레이어·앵커·`Button2D`·포인터 소비)는 그대로이고, 이 2 부는 그 위에 위젯을 얹는다.

### 6.1 기존 엔진과 지금 엔진에서 없는 것

기존 엔진도 `Button2D` 에서 멈췄다(`tasks/UISystemDesign.md` §6, `_TEMP_RendererBaseAndButton.md` §5-11 이 "안 하는 것" 으로 포커스·스크롤·마스크·
드래그 위젯·텍스트 입력을 미뤘다). 지금 엔진에서 `grep` 으로 확인한 것:

| 항목 | 지금 |
|---|---|
| 버튼의 전이 대상·스프라이트 바꾸기 | 없다. `tintSprite` 로 **같은 오브젝트**의 스프라이트 틴트만 바꾼다. 기존 엔진은 `TargetGraphic` 참조와 `SpriteSwap` 이 있었다 |
| 버튼 저작 보조 | 없다. 캔버스 뷰에 누름 사각형이 보이지 않고, 기존 엔진의 "렌더러에서 사각형 맞추기"(`ButtonRectFit`) 도 없다 |
| 9-슬라이스·타일·채우기 | 없다. `SpriteSizeMode` 는 `FromSprite`·`Custom` 뿐이고 에셋에 테두리 값이 없다 |
| 슬라이더·토글·진행 막대·스크롤·마스크 | 없다 |
| 클리핑 | RHI 세 백엔드에 `SetScissor` 가 있고 렌더러는 **패스마다 뷰포트 시저 한 번**이다(`Renderer.cpp` `ExecuteSprites`). 아이템 단위는 없다 |
| 샘플 | 화면 레이어·버튼을 쓰는 `.jcanvas` 가 저장소에 없다 |

### 6.2 설계

공통 제약은 1 부와 같다. 별도 UI 트리를 두지 않고(D-237 MUST), 위젯은 `JBroFramework2D` 의 컴포넌트와 `JBroFramework2DSystem` 의 시스템이며,
포인터는 `"UI"` 입력 체인의 **시스템 핸들러 하나**(`Button2DSystem`)가 버튼·토글·슬라이더·스크롤 뷰를 모두 본다 - 한 프레임에 포인터를 받는 위젯은
가장 위 **하나**다(화면 레이어 → 레이어 차례 → `renderOrder` → `drawSequence`, 1 부와 같다). 매 프레임 경로에 할당·문자열이 없다.

**1 단계 - 버튼 보강과 저작 보조**

- `Button2D` 에 `transition { None, ColorTint, SpriteSwap }`(기본 `ColorTint`)과 `targetGraphic`(`Handle::SpriteRenderer2D`, 비면 같은 오브젝트의 스프라이트)을
  둔다. `tintSprite` 는 `transition` 으로 바뀐다(옛 파일의 `tintSprite` 는 D-349 의 빌트인 엄격 규칙대로 거절되지 않도록 읽어서 `None`/`ColorTint` 로 옮긴다).
  `SpriteSwap` 은 `hoverSpriteId`·`pressedSpriteId`·`disabledSpriteId`(짝 핸들은 해석 패스가 채운다)로 대상의 `sprite` 핸들을 바꾸고, 보통 상태로 돌아올 때
  **떠 둔 원래 핸들**을 되돌린다(기존 엔진의 normal 래치). 비운 칸은 보통 스프라이트 그대로다. 누름 사각형은 그대로 버튼의 것이고 그림을 보지 않는다(기존 엔진 §5-1).
- 캔버스 뷰는 인스펙터에서 고른 `Button2D` 의 누름 사각형을 그린다(D-304 규칙과 같다 - 고른 컴포넌트만). 네 변의 손잡이로 `size` 를, 가운데 손잡이로 `offset` 을
  끈다. 끄는 동안 값을 바로 보여 주고 놓을 때 `SetPropertyCommand` 하나다(라이트 손잡이와 같은 틀). UI 보기에서도 선다(라이트와 다르다).
- 인스펙터의 `size` 아래에 `스프라이트에 맞추기`(필드 덧줄) - 대상 그림(`targetGraphic`, 비면 같은 오브젝트)의 보이는 크기와 피벗으로 `size` 와
  `offset = (0.5 - pivot) x size` 를 한 커맨드(`CompoundCommand`)로 쓴다. 그림이 없으면 눌리지 않고 까닭을 툴팁으로 적는다.
- 시험 데이터에 화면 레이어·버튼·슬라이더·토글·마스크가 든 캔버스 파일을 두고 왕복한다.

**2 단계 - 그림 위젯**

- 스프라이트 임포트 옵션에 테두리 `borderLeft`·`borderTop`·`borderRight`·`borderBottom`(픽셀, 기본 0)을 더한다. 시트의 모든 칸이 같은 테두리다.
  옛 `.jmeta` 는 키가 없어 0 으로 읽힌다.
- `SpriteRenderer2D.drawMode { Simple, Sliced, Tiled, Filled }` 와 `fillAmount`(0..1)·`fillOrigin { Left, Right, Bottom, Top }`. `Sliced`·`Tiled` 는 `size` 를
  채운다(`sizeMode` 가 `FromSprite` 면 `Simple` 과 같은 그림이다). 테두리는 화면 레이어에서 픽셀, 월드 레이어에서 픽셀 / PPU 다. 렌더 브리지가 아이템 하나를
  조각 여럿(`Sliced` 9, `Tiled` 는 타일 수)의 `SpriteSubmit` 으로 편다 - 렌더 월드의 아이템 수·정렬·선택 마스크는 그대로다. `Filled` 는 쿼드와 uv 를 비례로 자른다.
  **진행 막대는 `Filled` 스프라이트다** - 별도 컴포넌트를 두지 않는다(값 하나가 두 자리에 살지 않게).
  기각: 인스턴스에 테두리를 실어 셰이더에서 uv 를 다시 매핑하기(인스턴스 40 B·`SpriteSubmit` 84 B 단언, 세 백엔드 셰이더, 머티리얼·SDF 파이프라인까지 번진다).
- `Slider2D`: `value`(0..1)·`direction { LeftToRight, RightToLeft, BottomToTop, TopToBottom }`·누름 사각형(`size`·`offset`)·`handle`(`Handle::GameObject`, 자식
  오브젝트를 사각형의 그 축 끝에서 끝으로 옮긴다)·`fill`(`Handle::SpriteRenderer2D`, `Filled` 면 `fillAmount` 를 값으로)·`interactable`·`changed`(한 프레임).
  누르면 그 자리의 값으로 뛰고 끌면 따라오며 떼면 끝난다. 값은 스크립트가 써도 된다 - 손잡이·채우기는 매 프레임 값에서 다시 놓는다.
- `Toggle2D`: `isOn`·`checkmark`(`Handle::SpriteRenderer2D`, `visible` 을 `isOn` 으로)·누름 사각형·`interactable`·`changed`. 누르고 떼면 뒤집힌다.
  같은 오브젝트에 `Button2D` 를 요구하지 않는다(사각형이 둘이 된다).
- 스크립트 훅 `OnValueChanged()` 하나를 슬라이더·토글이 같은 오브젝트의 스크립트에 부른다(인자 없음 - 핸들로 읽는다, 1 부의 다섯과 같은 꼴).

**4 단계 - 텍스트 입력**(D-352)

- `InputField2D`: 누름 사각형(`size`·`offset`)·`interactable`·`text`(`TextId`, `Text2D` 와 같이 호스트 `TextStore` 의 번호이고 파일에는 글자)·`display`(`Handle::Text2D`,
  바뀔 때마다 그 글자를 덮어쓴다)·`caret`(`Handle::SpriteRenderer2D`, `display` 의 자식 - 글자 끝에서 `blinkRate` 초마다 깜빡인다)·`maxLength`(코드포인트 수, 0 은 없음).
  상태 `focused`(스크립트가 써도 된다)·`changed`·`submitted`(한 프레임). 누르면 포커스, 다른 데를 누르거나 Esc 면 잃는다. 포커스가 있는 동안 플랫폼의 글자 이벤트
  (`InputEventKind::Text`, 코드포인트)를 UTF-8 로 붙이고 Backspace 는 마지막 코드포인트를 지우며 Enter 는 `submitted` 와 `OnSubmit` 이다. **키보드는 `"UI"` 체인에서
  소비한다**(게임의 폴링에 보이지 않는다). 커서는 늘 글자 끝이다 - 가운데 편집·선택·IME 조합 표시는 없다(조합이 끝난 글자만 `Text` 이벤트로 온다).

**5 단계 - 게임패드 포커스**(D-352)

- 포커스는 위젯 하나다(`Button2DSystem` 이 든다). 포인터로 누른 위젯이 받고(빈 곳을 누르면 없다), 게임패드의 십자키·왼쪽 스틱(문턱 0.5 를 넘는 순간 한 번)이
  **그 방향의 가장 가까운 위젯**으로 옮긴다 - 화면 픽셀에서 `간 거리 + 옆으로 벗어난 거리 x 2` 가 가장 작은 것. 포커스가 없으면 왼쪽 위(줄이 먼저, 같은 줄은 왼쪽)부터다.
  `South` 가 누름·뗌·누름(버튼은 훅 다섯, 토글은 뒤집기)이고, 슬라이더에 포커스가 있으면 좌우가 값(0.1 씩)이며 상하만 옮긴다. 게임패드로 받은 포커스의 버튼은
  호버처럼 보인다(포인터로 누른 포커스는 포인터가 떠나면 보통이다). 연결된 패드 모두의 합을 보고, UI 가 쓴 프레임에만 게임패드를 소비한다.
  버튼·토글·슬라이더·글자 칸에 `focused` 상태 칸이 는다.

**6 단계 - 레이아웃 컨테이너**(D-352)

- `Layout2D`: `mode { Horizontal, Vertical, Grid }`·`cellSize`·`spacing`·`padding`·`columns`·`pivot`(0..1, 기본 (0, 1) = 왼쪽 위가 원점). 살아 있는 자식(계층 차례, 트랜스폼이
  있는 것)을 칸의 가운데에 놓고 자식의 크기는 보지 않는다(칸은 고정 - 앵커로 대부분 되므로 레이아웃은 늘어놓기만 한다). `Layout2DSystem`(60)이 트랜스폼 전파 앞에
  자식의 `position` 을 매 프레임 쓴다 - 자식의 자리는 레이아웃이 소유한다. 블록의 크기는 `contentWidth`·`contentHeight` 로 읽는다(스크롤 뷰의 `contentSize` 에 넣기 좋다).
  기각: 자식의 선호 크기로 늘리는 Unity 식 레이아웃 그룹(크기를 묻는 계약이 위젯마다 필요하다 - 지금은 칸 하나면 된다).

**3 단계 - 클리핑과 스크롤**

- `Mask2D`(`size`·`offset`): 자손의 스프라이트·글자를 그 사각형 안에서만 그린다. 사각형은 오브젝트 월드 행렬로 놓고 **축 정렬 사각형으로 근사**한다
  (회전한 마스크는 둘러싼 사각형이다 - 스텐실이 아니라 시저). 렌더러에 조명 구간(`SetSpriteLighting`)과 같은 꼴의 **클립 구간**(`SetSpriteClip`)을 두고,
  업로드가 구간을 run 으로 가르며 그릴 때 run 마다 `SetScissor` 를 부른다(패스·파이프라인은 그대로). 좌표는 아이템과 같은 공간(월드 또는 기준 픽셀)이고
  렌더러가 그 뷰의 투영으로 픽셀 사각형을 센다 - 게임 뷰·캔버스 뷰·썸네일이 각자 맞게 잘린다. 포인터 판정도 마스크 밖이면 없는 것이다.
- `ScrollView2D`: 같은 오브젝트의 `Mask2D` 가 뷰포트, `content`(`Handle::GameObject`, 자식)가 움직이는 쪽이다. `contentSize`·`horizontal`·`vertical`·
  `scrollSensitivity`(휠)·`decelerationRate`(관성). 누르고 끌면 내용이 따라오고 놓으면 관성으로 미끄러지다 멈추며, 끝을 넘지 않는다. 끌기가 시작되면 안의
  버튼 누름은 취소된다(뗌만 있고 누름은 없다). 휠은 포인터가 뷰포트 안일 때다.

### 6.3 단계와 완료 조건

1. **버튼 보강과 저작 보조**: 전이 대상·`SpriteSwap`·캔버스 뷰 사각형과 손잡이·`스프라이트에 맞추기`·시험 캔버스. 완료 조건: `InputChainTests` 에 대상 그림의
   틴트·스프라이트 바꾸기와 되돌림, 에디터 시험에 손잡이 끌기(되돌리기 하나)와 맞추기, 캔버스 파일 왕복.
2. **그림 위젯**: 테두리 옵션·`drawMode`·`Slider2D`·`Toggle2D`·`OnValueChanged`. 완료 조건: 픽셀 시험(9-슬라이스의 모서리 보존·타일 수·채우기 비율),
   `InputChainTests` 에 슬라이더 끌기·토글 뒤집기·가장 위 하나, 인스펙터의 새 필드와 옵션 창의 테두리.
3. **클리핑과 스크롤**: `Mask2D`·렌더러 클립 구간·`ScrollView2D`. 완료 조건: 픽셀 시험(마스크 밖 픽셀이 바탕색, 세 백엔드), `InputChainTests` 에 마스크 밖
   버튼 무시·끌어 스크롤·관성·끝 멈춤·끌기가 버튼 누름을 취소함.
4. **텍스트 입력**: `InputField2D`·키보드 소비·`OnSubmit`. 완료 조건: `InputChainTests` 에 포커스·글자 붙이기(UTF-8)·Backspace·Enter·Esc·상한·스크립트 포커스·키보드 소비.
5. **게임패드 포커스**: 포커스 하나·십자키와 스틱 이동·`South` 누름·슬라이더 값. 완료 조건: `InputChainTests` 에 첫 포커스·방향 이동·누름·슬라이더·스틱 한 번.
6. **레이아웃 컨테이너**: `Layout2D`·`Layout2DSystem`. 완료 조건: `Layout2D` 묶음에 세로·가로·격자·피벗·여백·꺼진 자식.

### 6.4 진행 기록 (2 부)

1. ~~**버튼 보강과 저작 보조**~~ → 완료 2026-10-29 · `Button2D` 의 `transition`(`ButtonTransition`)·`targetGraphic`(`Handle::SpriteRenderer2D`)·
   `hoverSpriteId`·`pressedSpriteId`·`disabledSpriteId`(짝 핸들은 해석 패스), `Button2DSystem` 의 전이(틴트는 대상의 `tint`, 바꾸기는 대상의 `sprite` 와
   `normalSprite`·`swapped` 래치), 에디터의 `ButtonRectModel`(화면을 모르는 셈)·`CanvasViewPanel::DrawButtonEditor`(네 변·가운데 손잡이, 끌기 하나 =
   `CompoundCommand` 하나)·`SetButtonRect`(캔버스 뷰와 인스펙터가 함께 쓰는 커맨드)·`ResolveSpriteVisual`(집기 사각형과 맞추기가 같은 셈)·인스펙터 `size`
   덧줄 `스프라이트에 맞추기`(`###fit_sprite`), 캔버스 파일 왕복 시험 ·
   - **잰 것**: `InputChainTests` - 다른 오브젝트의 그림을 가리키면 그쪽 틴트만 바뀌고 같은 오브젝트의 스프라이트는 그대로, `SpriteSwap` 은 호버에 `{7,1}`,
     빈 눌림 칸은 보통 `{3,1}`, 벗어나면 되돌림, 꺼지면 `{9,1}`, 다시 켜면 되돌림, `None` 은 틴트도 스프라이트도 그대로. `CanvasFileTests` - `transition: SpriteSwap` 과
     아이디는 적히고 해석된 핸들은 적히지 않으며, 다른 오브젝트의 그림을 가리키는 핸들이 되읽힌다. `EditorApplicationTests`(1280 x 720) - 트랜스폼을 고른 동안 손잡이
     없음, `Button2D` 를 고르면 `##button_right`·`##button_top`·`##button_left`·`##button_center`, 오른쪽 변을 한 유닛 끌면 크기 2 → 3 과 자리 0 → 0.5(왼쪽 변 그대로)
     이고 되돌리기 하나, 가운데를 끌면 크기 그대로 자리만, 피벗 (0, 0) 의 3 x 1.5 그림에 맞추면 크기 (3, 1.5)·자리 (1.5, 0.75) 이고 되돌리기 하나. 스크린숏 `button_rect`.
     묶음: `InputChain`·`BuiltinComponentProperty`·`CanvasFile`·`ScriptApiCatalog`·`ButtonRectModel`·`EditorLocalization` 통과, `EditorApplication` 472.5 초 통과.
   - 정한 것: 옛 파일의 `tintSprite` 키는 읽지 않는다(빌트인은 엄격하다, D-349) - 저장소에 버튼을 쓴 캔버스가 없어 옮기는 길을 두지 않았다. 변 손잡이는 `size` 와
     `offset` 을 함께 바꾼다(맞은편 변이 그대로). `Button2D` 를 고른 동안 가운데 손잡이가 이동 기즈모의 가운데를 가린다(축 손잡이는 그대로다). API 목록의
     위치표(`Definitions.cpp`)는 헤더를 고칠 때마다 손으로 옮긴다 - 같은 글자의 줄이 여럿이면(`Collision2D::other`·`RaycastHit2D::other`) 가장 가까운 줄로
     잇는 셈이 틀리고, 구간 대응(difflib)으로 이어야 한다(처음에 틀려 `ScriptApiCatalog` 가 떨어졌다).
2. ~~**그림 위젯**~~ → 완료 2026-10-29 · 스프라이트 임포트 옵션의 `borderLeft`·`borderTop`·`borderRight`·`borderBottom`(픽셀, `SpriteLibrary::Resolve` 가 칸 안으로
   줄여 `SpriteFrameView` 에 넘긴다), `SpriteRenderer2D.drawMode`(`SpriteDrawMode`)·`fillAmount`·`fillOrigin`(`SpriteFillOrigin`), 렌더 아이템의 테두리·칸 크기(`size`
   와 같은 단위), **조각 셈 `BuildSpritePieces`**(`Framework2DSystem/Rendering/SpritePieces.h`, 렌더러를 모른다)와 브리지의 조각마다 `SpriteSubmit`(`BuildSprite` 가 조각의
   비율로 기하와 uv 를 줄인다, 상한 256 조각), 스프라이트 뷰어의 테두리 안내선, `Slider2D`(`SliderDirection`·`handle`·`fill`)·`Toggle2D`(`checkmark`)와 핸들,
   `GameScript2D::OnValueChanged`, `Button2DSystem` 이 세 위젯을 모두 본다(`WidgetKind`, 갱신 훅 50 에서 손잡이·채우기·표시를 값에서 놓는다), 캔버스 뷰 사각형
   손잡이가 타입을 가리지 않는다(`RectFieldsOf`) ·
   - **잰 것**: `SpriteDrawMode`(조각 셈) - 4 x 4 칸·테두리 1 을 16 x 16 으로 펴면 9 조각이고 모서리는 1/16 x 1/4, 가운데는 14/16 x 1/2, 테두리 합이 폭을 넘으면
     비례로 줄며 가운데 열이 빠진다(6 조각), 타일 10 x 10 은 한 줄 셋에 마지막이 반 칸, 4000 x 4000 은 상한 256 에서 끊긴다, 채우기는 네 변에서 비율만큼 한 조각이고
     0 이면 없다. **픽셀**(D3D12, 64 x 64 화면 레이어, 4 x 4 PNG 의 아홉 색) - 9-슬라이스 16 x 16 의 네 모서리가 한 픽셀 색 그대로, 변은 제 쪽으로 늘고 가운데는
     희며 밖은 검다. 타일 10 x 10 은 첫 칸이 온전하고 셋째가 2 픽셀, 위의 잘린 칸은 칸의 아래 반을 보인다(타일은 왼쪽 아래부터). 채우기 왼쪽 절반은 x 24..31 만,
     위 절반은 위만. `InputChainTests` - 토글은 누르고 떼면 뒤집히고 `OnValueChanged` 한 번, 벗어나 떼면 그대로, 꺼지면 안 뒤집히되 포인터는 가져가며, 표시
     스프라이트의 `visible` 이 `isOn` 을 따른다. 슬라이더는 픽셀 75 에서 0.25, 끌어 100 에서 0.5(손잡이 0·채우기 0.5), 축 밖으로 가도 값 그대로, 170 은 1 로 붙고,
     스크립트가 쓴 0.3 은 손잡이(-20)·채우기만 옮기고 알리지 않으며, `RightToLeft` 는 같은 자리가 0.75 다. `EditorApplicationTests` - 토글의 사각형도
     `##button_right` 이고 끌면 1 → 1.5. 묶음: `SpriteDrawMode` 0.7 초, `InputChain`·`BuiltinComponentProperty`·`CanvasFile`·`AssetSystem`·`EditorLocalization`
     통과, `EditorApplication` 통과.
   - 정한 것: 타일은 왼쪽 아래(y 위 좌표의 원점 쪽)부터 깐다 - 위가 잘린다. 슬라이더의 손잡이는 **자식 오브젝트**여야 한다(로컬 자리를 쓴다). 토글·슬라이더는
     `OnPointerEnter` 같은 버튼 훅을 부르지 않는다 - 값 훅 하나다. 포인터가 바꾼 값만 알린다. `Button2DSystem` 의 갱신 순서를 150 → 50 으로 옮겼다(트랜스폼 전파보다
     먼저 손잡이를 놓아야 같은 프레임에 보인다). 스프라이트 뷰어의 테두리 안내선은 화면으로 보지 않았다(미확인).
3. ~~**클리핑과 스크롤**~~ → 완료 2026-10-29 · `Mask2D`(`size`·`offset`, 시스템이 채우는 `clip*`)와 `Mask2DSystem`(120 - 트랜스폼 전파 뒤, 추출 앞, 로컬 사각형
   네 꼭짓점의 월드 AABB), **한 함수 `ComputeClipRect2D`**(오브젝트와 조상의 마스크 교집합 - 스프라이트·글자 추출과 위젯 판정이 같이 쓴다), 렌더 아이템의
   `clipped`·`clip`, 브리지의 클립 구간(사각형이 바뀌면 묶음을 끊고 `SetSpriteClip`, 패럴랙스만큼 같이 옮긴다), 렌더러의 `ClipRange`(`LitRange` 와 같은 꼴 -
   업로드가 구간을 run 으로 가르고 `DrawSpriteRuns` 가 run 마다 `SetScissor`, 사각형은 뷰-투영으로 픽셀이 되어 뷰의 시저와 교집합), `ScrollView2D`(`content`·
   `contentSize`·`horizontal`·`vertical`·`scrollSensitivity`·`decelerationRate`, 상태 `dragging`·`velocity`)와 `Button2DSystem` 의 끌기(문턱 8 px, 안의 버튼 누름 취소)·
   관성(갱신 훅)·휠, 캔버스 뷰 사각형 손잡이에 마스크 ·
   - **잰 것**: **픽셀**(D3D12) - 16 x 8 마스크 안의 32 x 32 흰 사각형이 x 24..39·y 28..35 만 희고 밖은 검으며, 마스크를 8 옮기면 잘리는 자리가 따라간다.
     `Light2DPixel`·`TextRender` 가 그대로 지난다(시저 구간이 빛·글자 경로를 깨지 않는다). `InputChainTests`(200 x 100, 창 100 x 60, 내용 100 x 200) - 마스크
     사각형은 y -30..30, 마스크 밖의 버튼은 눌리지 않고 포인터가 게임까지 간다, 안의 버튼은 눌린다, 빈 곳을 누르고 10 px 끌면 끌기가 시작되어 내용이 10 따라오고
     속도는 프레임의 이동(1200/s), 떼는 자리가 버튼 위여도 누름이 아니며, 놓으면 미끄러지다 끝(70)에서 멈추고 속도 0, 휠 한 칸은 감도만큼(40) 내려가고 끝을 넘지
     않는다, 버튼 위에서 누르고 끌면 버튼은 뗌만 받고 스크롤이 쥐며 누름이 아니다, 세로만인 창은 가로 끌기에 움직이지 않는다. 묶음: `SpriteDrawMode`·`InputChain`·
     `BuiltinComponentProperty`·`CanvasFile`·`ScriptApiCatalog`·`Light2DPixel`·`TextRender`·`EditorLocalization` 통과.
   - 정한 것: 포인터 판정의 마스크는 지난 프레임의 사각형이다(체인이 트랜스폼 전파보다 먼저 돈다 - 위젯 판정 전체가 그렇다). 차례가 같으면 뒤에 온 위젯이 위이되
     스크롤 뷰는 제 안의 위젯 밑이다(처음에 스크롤 뷰가 버튼을 가려 시험이 떨어졌다). 내용은 스크롤 뷰의 **자식**이어야 한다(자리를 뷰포트와 같은 로컬로 가둔다).
     관성의 속도는 끌던 마지막 프레임의 이동이고, `decelerationRate` 는 1 초에 남는 비율, 1 미만이면 멈춘다. 돌린 마스크는 둘러싼 사각형이다(스텐실 없음).
     빈 교집합은 넓이 0 의 사각형이라 아무것도 그리지 않는다. 클립 구간이 상한(`maxLayerGroups`)을 넘으면 자르지 않고 센다.
4. ~~**텍스트 입력**~~ · 5. ~~**게임패드 포커스**~~ · 6. ~~**레이아웃 컨테이너**~~ → 완료 2026-10-30 · `InputField2D`(글자는 `TextStore`, `display`·`caret`, `OnSubmit`),
   `Button2DSystem` 의 포커스(`SetFocus`·`NavigateFocus`·`TypeInto`, 위젯 넷의 `focused` 칸, 게임패드·키보드 소비), `Layout2D`·`Layout2DSystem`(60), 캔버스 뷰
   사각형 손잡이에 글자 칸 ·
   - **잰 것**: `InputChainTests` - 글자 칸을 누르면 포커스와 커서, `a`·`한` 이 UTF-8 로 붙고 보이는 텍스트에 옮겨지며 스크립트에 한 번 알리고 W 키가 게임에 안 보인다,
     Backspace 는 마지막 코드포인트, Enter 는 `submitted` 한 프레임과 `OnSubmit`, 상한 2 면 세 번째가 안 붙는다, Esc 로 포커스를 잃고 키보드가 게임에 간다, 스크립트가
     `focused` 를 쓰면 다음 프레임부터 받는다, 빈 곳을 누르면 잃는다. 게임패드 - 포커스가 없으면 오른쪽 누름이 왼쪽 위(글자 칸)를 고르고, 오른쪽이 옆 버튼(호버 색),
     `South` 누름·뗌이 눌림·클릭, 아래가 아래 버튼, 왼쪽이 슬라이더, 슬라이더에서 오른쪽은 값 0.1, 스틱 -1 은 한 번만 0.0, 위가 글자 칸, 포인터 누름이 포커스를 옮긴다.
     `Layout2D` - 세로 (50, -20)·(50, -64)·(50, -108) 블록 100 x 128, 가로 308 x 40, 격자 두 열 204 x 84, 피벗 (0.5, 0.5) 면 (-52, 22), 여백 10 이면 (60, -30) 과 224,
     꺼진 자식은 칸을 비운다. 묶음: `InputChain`·`Layout2D`·`BuiltinComponentProperty`·`ScriptApiCatalog`·`RendererContract`·`CanvasFile`·`EditorLocalization` 통과.
   - 정한 것: 게임패드 시험의 빈 자리는 2 초(120 프레임)에 한 번만 다시 묻는다 - 패드는 처음부터 꽂아 둔다. 버튼의 포커스 표시는 게임패드·키보드로 받은 것만이다
     (처음엔 포인터 포커스도 호버로 보여 1 단계의 `SpriteSwap` 되돌림 시험이 떨어졌다). 글자 칸의 커서 자리는 `Text2DSystem::GetLocalBounds` 의 오른쪽 끝이다 -
     폰트가 없으면 원점이다. 레이아웃은 자식의 크기를 보지 않는다. IME 조합 중 글자 표시와 플랫폼의 `SetTextInputPosition` 호출은 하지 않았다(미확인·미지원).
   - **전체**(2026-10-30, 4~6 단계 뒤, 이 PC 의 main 폴더, Debug): 솔루션 빌드 경고 0, 전체 시험 126 묶음 `all tests passed.`(`EditorApplication` 526.9 초).
   - **전체**(2026-10-30, 1~3 단계 뒤, 이 PC 의 main 폴더, Debug): 솔루션 빌드 경고 0. 전체 시험은 `RendererContract` 의 기본 시스템 수(9 → 10, `Mask2DSystem`)에서 한 번
     멈춰 그 단언을 고친 뒤 멈춘 자리부터 남은 82 묶음을 돌렸다(`EditorApplication` 466.8 초) - 앞의 48 묶음과 합쳐 전부 지났다. 실제 에디터 실행 파일은 띄워 보지
     않았다(미확인) - 화면은 `EditorApplication` 시험의 스크린숏(`button_rect`)으로 봤다.

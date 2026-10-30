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

- ~~안전 영역(값을 넣는 플랫폼이 없다), 분할 화면(카메라가 하나다), 레이아웃 컨테이너·스크롤·마스크·텍스트 입력·게임패드 포커스~~ → 2 부(D-351·D-352)와 3 부(D-358·D-359)에서 섰다.
  3D 의 화면 레이어는 열림(3 부 C). 조명이 서면 화면 레이어가 광원 맵 뒤에 그려지는지 → 픽셀 시험으로 봤다(3 부 C, 희다).

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
> 사용자 지시(2026-10-30 "나머지 단계 진행")로 4~6 단계(텍스트 입력 · 게임패드 포커스 · 레이아웃 컨테이너, D-352)까지 했다. §4 의 `[열림]` 가운데 안전 영역·분할 화면·3D 의 화면 레이어만 남는다 → 3 부(§7, D-355)가 맡는다.
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

### 6.5 리뷰·적대 시나리오·사용성 평가 (D-353)

2026-10-30. 사용자 지시 "1. 코드리뷰(컨벤션, 규칙 준수 등) 2. 적대적 테스트를 포함한 사용자 시나리오 테스트 3. 사용성 평가 - 위 사항을 진행하고 수정해서 반영".
리뷰는 `6c19c8b2..HEAD` 의 `Modules` 67 파일 diff 를 규칙(CLAUDE.md·ProjectRule §7.1·§9·§10.5·§11·§14, D-237/D-351/D-352)에 맞댔다. 지적은 원본 코드에서 다시 확인한 것만 고쳤다.

- **고친 것(리뷰)**:
  - 한 줄 제어문 8 개(`Button2DSystem` 의 십자키·스틱) → 블록으로.
  - 같은 프레임에 포커스가 옮겨지면 뒤의 훅이 옛 오브젝트에 갔다(누름·탐색 뒤 `focusedObject` 를 다시 풀지 않았다) → `resolveFocused` 로 번호에서 다시 푼다.
    십자키 + `South` 를 같은 프레임에 누르면 새 포커스가 눌리고, 빈 곳을 눌러 포커스를 비운 프레임의 `South` 는 아무것도 누르지 않는다.
  - 키보드 훅(`OnValueChanged`·`OnSubmit`)을 `ForEach<InputField2D>` 안에서 불렀다 → 순회 밖에서 `FindComponentRaw` 로 하나를 잡아 부른다.
  - 쥔 위젯이 사라지면 끌기 상태(`m_scrollDragging`·`m_scrollCandidate`)가 남았다 → 비운다.
  - 스크립트가 `InputField2D.focused` 를 거짓으로 써도 듣지 않았고, 둘이 참이면 매 프레임 오갔다 → 거짓이면 놓고, 참은 먼저 도는 하나만 받는다.
  - 짝 없는 서러게이트(0xD800..0xDFFF)를 UTF-8 로 적었다 → 버린다. 제어 글자처럼 글자가 아니다.
  - 렌더러의 시저 셈이 `floor` 결과를 범위 검사 없이 `int32` 로 캐스팅했다 → 유한하지 않으면 뷰 시저, 유한하면 ±1e9 로 가둔 뒤 캐스팅.
  - 클립 구간 상한을 넘으면 브리지가 아이템마다 묶음을 끊고 다시 물었다 → 한 번 거절당하면 그 뷰에서는 더 묻지 않는다(`clipRefused`).
  - `Layout2DSystem` 의 `std::max<std::int32_t>` → `Max(Int32(1), columns)`. 자식이 0 이면 블록은 여백뿐이다(칸 하나로 세던 것).
  - `ClipRectContains` 가 넓이 0 의 사각형(어긋난 마스크의 교집합)의 경계 점을 안으로 봤다 → 밖이다.
  - 커서 깜빡임 시계가 무한히 커졌다 → 한 주기 안으로 `fmod`. `blinkRate` 가 유한하지 않거나 0 이하면 켜 둔다.
  - `South` 를 쥔 중간 프레임을 게임이 봤다 → `m_padPressed` 인 동안 게임패드를 소비한다.
  - `InputField2D::OnDetached` 가 핸들 소스에 있었다 → `Source/InputField2D.cpp`. `InspectorFieldExtras.cpp` 의 중복 include, `NavigateFocus` 첫 걸음의 죽은 코드.
  - 주석: 포인터 판정의 마스크는 지난 프레임의 사각형이다(`Button2DSystem.h`), `fillOrigin` 은 `flip` 과 함께 뒤집힌다(`SpriteRenderer2D.h`).
- **적대 시험이 찾은 것(엔진)** - 새 묶음 `UiScenario`(`Tests/UiScenarioTests.cpp`, 200 x 100 화면, 넷):
  - **스크립트가 하나도 없는 캔버스에서는 위젯이 입력을 받지 못했다.** `ScriptSystem::Rebuild` 가 스크립트가 없으면 입력 체인을 통째로 비워 시스템 핸들러(`Button2DSystem`)까지
    빠졌다. `InputChainTests` 는 늘 폴링 스크립트를 붙여 두어 드러나지 않았다 → 스크립트가 없어도 시스템 핸들러는 체인에 선다.
  - NaN 크기·자리의 버튼이 **어디서나 눌렸다**(밖이라는 비교가 NaN 에 거짓) → 안이라는 쪽을 묻는다(`dx <= half && dy <= half`).
  - 슬라이더 손잡이·스크롤 내용이 자기 자신이나 남을 가리키면 그 오브젝트가 매 프레임 끌려갔다 → **자식**일 때만 놓는다. 커서는 `display` 의 자식일 때만 놓는다.
  - NaN 휠 감도·감속률이 자리를 NaN 으로 만들었다 → 0 으로 본다. 레이아웃은 유한하지 않은 값이 하나라도 있으면 그 프레임을 건너뛴다.
  - **잰 것**(`UiScenario`): 잘못 이은 참조는 제자리(자기 자신·남)이고 자식이면 움직인다, 마스크 없는 스크롤 뷰는 위젯이 아니다, 표시 텍스트가 제 오브젝트의 `Text2D` 여도 된다.
    NaN 값의 슬라이더는 손잡이 처음·채우기 0, 음수 크기는 절댓값, NaN·0 크기의 버튼은 어디에도 없고 포인터가 게임에 간다, 음수 `maxLength` 는 상한 없음, 한 프레임에 40 글자는
    입력 상태의 32 에서 잘린다, 서러게이트 반쪽·탭은 버린다, 상한을 넘긴 글자는 Backspace 가 되고 입력은 안 된다, NaN 깜빡임은 켜 둠, NaN 감도는 안 움직임, NaN 감속은 놓자마자
    멈춤, NaN 내용 크기·열 0·음수 칸·NaN 간격·NaN 피벗·NaN 마스크 크기에 자리는 유한하고 마스크는 자르지 않는다. 누름 훅에서 제 오브젝트를 지우는 버튼은 그 프레임을 끝내고
    사라지며 다음 누름은 게임에 간다(클릭 훅·제출 훅·값 훅도 같다), 쥔 버튼·포커스 칸·끌던 스크롤 뷰·게임패드 포커스의 버튼을 프레임 사이에 지워도 다음 프레임에 상태가 비고 입력이
    게임에 간다, 십자키 + `South` 한 프레임, 포커스 비운 프레임의 `South`, 두 글자 칸의 `focused` 동시 참, 거짓으로 쓰면 놓음.
- **사용자 시나리오 D**(`EditorApplicationTests` 의 `UserScenario` 묶음, `TestScenarioDUiBuilder`, script-user-scenario §3.2): 새 프로젝트 → `컴포넌트 추가` 로 마스크·스크롤 뷰·
  자식 버튼·슬라이더·토글·글자 칸·레이아웃·표시 텍스트 → 캔버스 뷰의 사각형 손잡이 → 일부러 잘못 잇고(스크롤 내용 = 자기 자신, 손잡이 = 슬라이더 자신, 표시 텍스트를 지움)
  재생 → 저장·프로젝트를 닫고 다시 열어 되읽기 → 되돌리기 끝까지. 목록의 `UI` 갈래에 일곱 위젯이 모두 붙일 수 있는 것으로 선다. 스크린숏 `d00`~`d09`. `UserScenario` 55.7 초.
- **사용성 평가**(스크린숏 `d01`~`d09` 를 봤다):
  - `InputField2D` 의 `State` 가 다른 위젯과 달리 접히지 않고 평평하게 보였다(`focused` 가 쓰기 가능이라 읽기 전용 갈래로 접히지 않았다) → 인스펙터에서는 읽기 전용(`ReadOnly`),
    스크립트의 `SetFocused` 는 그대로다. 고친 뒤 `d05` 에서 `▶ State` 로 접힌다.
  - 인스펙터의 필드 차례·위젯(색 칸·핸들 드롭다운·`Range` 슬라이더·`스프라이트에 맞추기` 덧줄)은 기존 컴포넌트와 같은 모양이다. 캔버스 뷰의 손잡이는 실제 프로젝트에서도 선다(`d07`).
  - [의문] 월드 레이어의 오브젝트에 위젯을 붙이면 기본 크기가 픽셀 값(`Button2D` 160 x 48, `Mask2D` 320 x 240)이라 유닛으로는 수십 미터다. UI 는 화면 레이어에 두는 것이
    설계(§2.1)지만, 월드 레이어에서는 레이어에 맞춰 기본값을 줄일지(픽셀 → 유닛, D-184 의 100) 정하지 않았다.
  - [의문] `targetGraphic`·`content`·`handle` 이 비면 `없음` 으로 보인다. 버튼의 `없음` 은 "같은 오브젝트의 스프라이트" 라는 뜻이라 글자가 뜻을 말하지 않는다.
  - [의문] 같은 프로세스에서 앞 시나리오(C)의 알림과 상태 줄(`스크립트 오류로 그 콜백을 멈췄습니다`)이 다음 `EditorApplication` 에도 남아 보였다(`d00`~`d09`). 알림 저장소가
    인스턴스 밖에 있는 것으로 보이나 확인하지 않았다 - UI 와 무관해 손대지 않았다.
- **묻는 것**: 리뷰가 Backspace·십자키·스틱의 **자동 반복이 없다**고 지적했다(§7.1 "자동 반복은 누름이 아니다" 를 그대로 따라, 누르고 있어도 한 글자·한 칸이다). 일반 UI 와
  다르므로 의도인지 확인이 필요하다 - 반복을 넣으려면 시스템이 누른 시간을 들고 초기 지연·간격으로 다시 내는 길이다. → 사용자 지시 "남은거 작업 ㄱ" 으로 §6.6 에서 넣었다(D-354).
- **묶음**: `UiScenario`·`InputChain`·`Layout2D`·`SpriteDrawMode`·`ButtonRectModel`·`CanvasFile`·`BuiltinComponentProperty`·`ScriptApiCatalog`·`RendererContract`·`EditorLocalization`·
  `UserScenario` 통과.
- **전체**(2026-10-30, D-353 뒤, 이 PC 의 main 폴더, Debug, 기준 main 685ff96a): 솔루션 빌드 경고 0, 전체 시험 127 묶음 `all tests passed.`(`EditorApplication` 467.7 초, `UserScenario` 41.5 초).

### 6.6 남은 것 처리 (D-354)

2026-10-30. 사용자 지시 "남은거 작업 ㄱ" - §6.5 가 남긴 것(묻는 것 하나, `[의문]` 셋, 가운데 편집)을 처리했다.

- **자동 반복**(`Button2DSystem`): 입력 상태의 눌림 수는 §7.1 대로 OS 반복을 세지 않는다. 대신 시스템이 쥔 시간을 든다 - `RepeatDelay` 0.4 초 뒤 처음 되풀이하고,
  글자 칸의 Backspace·Delete·←·→ 는 `KeyRepeatInterval` 0.05 초, 십자키·스틱은 `NavigateRepeatInterval` 0.1 초마다다. 시간은 지난 갱신 훅의 델타(`m_frameDelta`)라
  입력 체인이 갱신보다 앞인 차례를 바꾸지 않고, 편집 중(델타 0)에는 반복하지 않는다. 새로 누르거나 방향이 바뀌면 처음부터 센다. 스틱은 문턱을 넘긴 채 두면 같다.
  상수는 시스템의 것이다 - 컴포넌트 필드로 올릴 까닭이 보이면 그때 올린다.
- **가운데 편집**: `InputField2D.caretIndex`(코드포인트 번호, `State` 의 읽기 전용) 가 섰다. 포커스를 받으면 글자 끝이고, ←·→·Home·End 가 옮기며, 글자는 커서 자리에 끼고
  Backspace 는 앞 글자·Delete 는 뒤 글자를 지운다. 고치거나 옮기면 커서가 바로 보인다(`blinkTime` 0). 스크립트가 글자를 통째로 바꾸면 글자 수 안으로 가둔다.
  커서 자리는 `TextLayout::GetCaretPosition`(바이트 위치 → 그 글자의 왼쪽, 공백·탭도 글자, 끝은 마지막 글자의 전진 폭 끝, 세로는 그 줄의 가운데 - `LineInfo.shift` 로
  정렬을 더한다) → `TextBlock::GetCaret` → `Text2DSystem::GetLocalCaret`(PPU 로 나눔) 이고, 레이아웃이 아직 없으면 전처럼 오른쪽 끝이다.
  한 프레임에 글자와 화살표가 같이 오면 **글자가 먼저**다(이벤트 차례는 접힌 상태에 없다).
- **월드 레이어의 위젯 기본 크기**: 리플렉션 어트리뷰트 `Pixels()`(`FieldAttributes.pixels` → `PropertyEditInfo.pixels`, 48 바이트 그대로)를 위젯의 픽셀 값 필드에 붙였다
  (`Button2D`·`Toggle2D`·`Slider2D`·`InputField2D`·`Mask2D` 의 `size`·`offset`, `ScrollView2D` 의 `contentSize`·`scrollSensitivity`, `Layout2D` 의 `cellSize`·`spacing`·`padding`).
  에디터의 `AddComponentCommand` 가 **맨** 컴포넌트를 월드 레이어의 오브젝트에 붙일 때만 그 필드를 `DefaultPixelsPerUnit`(100, D-184)으로 나눈다 - 160 x 48 픽셀 버튼이
  1.6 x 0.48 유닛이다. 값을 들고 붙이는 길(붙여넣기·다시 하기의 값)과 스크립트의 `AttachComponent` 는 그대로다(스크립트가 월드 레이어에 만드는 위젯은 픽셀 값 그대로 - 열림).
  대안으로 본 것: 컴포넌트에 레이어별 기본값(컴포넌트가 레이어를 모른다), 월드 레이어의 UI 를 막기(§2.1 이 허용한다).
- **빈 핸들의 `없음`**: 글자는 두고 툴팁으로 뜻을 적었다 - `targetGraphic`("비면 같은 오브젝트의 SpriteRenderer2D 다"), `handle`·`content`("이 오브젝트의 자식이어야 한다"),
  `fill`·`checkmark`·`display`·`caret`. 글자를 바꾸면 모든 핸들 칸의 `없음` 이 흔들린다.
- **알림 누수**: `EditorApplication::PollScriptErrors` 가 로그를 serial 0 부터 훑어 같은 프로세스의 앞선 에디터(시나리오 C)가 남긴 `JBroScript` 오류를 새 에디터가 다시 알렸다
  → 생성 때 로그의 마지막 serial 로 시작한다. 시나리오 D 가 첫 프레임 뒤 마지막 알림 제목이 스크립트 오류가 아님을 본다.
- **잰 것**: `UiScenario` 에 `TestHeldKeysRepeatAndTheCaretMoves`(←·→·Home·End·Delete·가운데 끼우기·한글 한 글자·범위 밖 커서 가두기, 쥔 Backspace 가 20 프레임 동안 한 글자·30 프레임에
  두세 글자·놓으면 멈춤, 쥔 십자키가 20 프레임 동안 한 칸·28 프레임에 두 칸·34 프레임에 세 칸·새 누름은 바로, 스틱도 같음). `EditorObjectCommand` 에
  `TestPixelDefaultsShrinkOnAWorldLayer`(월드 1.6 x 0.48, 화면 160 x 48, 다시 하기도 같음, 붙여넣기는 그대로). `BuiltinComponentProperty` 의 `InputField2D` 열넷.
  `TextLayout` 의 `TestCaretPositionFollowsTheGlyphs`(A·공백·V 의 전진 폭 608·224·575 로 자리 넷, 오른쪽 정렬의 밀림, 두 줄에서 개행 앞은 첫 줄 끝·뒤는 둘째 줄 머리 -
  개행 글자는 줄 구간 [begin, end) 밖(offset == end)이라 처음엔 둘째 줄로 갔다).
  `InputChain` 의 "holding the stick does not repeat" 는 지연 전이라는 뜻으로 글자만 고쳤다.
- **열림**: IME 조합 표시(플랫폼이 `WM_IME_COMPOSITION` 을 입력 이벤트로 주지 않는다 - 조합이 끝난 글자만 `Text` 로 온다. 조합 창 자리(`SetTextInputPosition`)도 걸지 않았다),
  포인터로 커서 놓기, 선택·복사·붙여넣기, 커서의 스크립트 API(`caretIndex` 는 핸들에 없다), 스크립트가 월드 레이어에 만드는 위젯의 기본값.
- **전체**(2026-10-30, D-354 코드 뒤, 이 PC 의 main 폴더, Debug, 기준 main 94db5045): 솔루션 빌드 경고 0, 전체 시험 127 묶음 `all tests passed.`(`EditorApplication` 1504.8 초 -
  앞 실행 467.7 초보다 느린데 같은 PC 의 다른 작업과 겹친 것으로 보인다, 재지 않았다. `UserScenario` 92.6 초). 스크린숏 `d05` 에서 월드 레이어의 글자 칸이 1.6 x 0.32 로 붙고
  `State` 가 접히며, `d00` 의 상태 줄에 앞 시나리오의 스크립트 오류가 없다. **실제 `JBroEditorHost.exe`** 를 시나리오 D 의 프로젝트(`ui.jcanvas`)로 띄워 PostMessage 로 글자 칸
  오브젝트를 고르고 `caret` 라벨의 툴팁("커서 스프라이트. display 의 자식이어야 한다 - 커서 자리에 놓는다")과 펼친 `State` 의 `caretIndex 0`, 캔버스 뷰의 사각형 손잡이를 눈으로 봤다.
  재생 중 글자 입력·반복은 실제 창에서 보지 않았다(PostMessage 키 입력은 포커스 없는 창에서 믿을 수 없다 - `UiScenario` 가 잰다).

## 7. 3 부 - 남은 것 모두 (D-355)

> 상태: **완료** (2026-10-30 시작, 같은 날 끝 - A·B 1~3 순위·C 모두 섰다, 열림은 §7.5). 사용자 지시 "C까지 ㄱㄱ. 단위작업별 커밋. 큰 기능 단위별 적대적 테스트를 포함한 검증. 사용성 평가 및 수정까지."
> 범위 셋: **A** §6.6 이 열림으로 남긴 글자 칸 마무리와 IME, **B** 새 위젯(1~3 순위), **C** §4 의 오래된 열림(안전 영역·분할 화면·3D 의 화면 레이어·조명 차례).
> 차례는 A → B 1 순위 → B 2 순위 → B 3 순위 → C 다. 단위마다 커밋하고, 묶음(A·B 순위마다·C)마다 적대 시나리오(`UiScenario`)와 사용성 점검(스크린숏)을 넣는다.
> 1·2 부의 뼈대(화면 레이어·앵커·`"UI"` 체인의 시스템 핸들러 하나·포인터는 가장 위 하나)는 그대로다.

### 7.1 A - 글자 칸 마무리

- **포인터로 커서 놓기**: 글자 칸을 누른 자리를 `display` 오브젝트의 로컬로 옮기고 `TextLayout::GetCaretOffsetAt(x, y)`(y 로 줄을, x 로 글자 경계를 - 글자의 가운데를 넘으면
  그 뒤)로 바이트 위치를 얻어 코드포인트 번호로 센다. 길은 `TextBlock::HitCaret` → `Text2DSystem::GetLocalCaretAt`(PPU 를 곱해 레이아웃 단위로). 레이아웃이 없으면 끝이다.
  누른 채 끌면 선택이다.
- **선택**: `selectionAnchor`(코드포인트 번호, -1 은 없음, `State`). Shift+←·→·Home·End 가 늘리고, 포인터 끌기가 고르며, Ctrl+A 는 전부다. 글자가 들어오거나
  Backspace·Delete 가 오면 선택 구간을 지우고 그 자리다. 보이는 것은 `selection`(`Handle::SpriteRenderer2D`, `display` 의 자식) - 시스템이 두 커서 자리 사이로 늘린다
  (`sizeMode = Custom`, 높이는 그 줄의 높이. 여러 줄에 걸치면 둘러싼 사각형이다 - 줄마다 조각은 두지 않는다).
- **복사·잘라내기·붙여넣기**: Ctrl+C/X/V. 클립보드는 **OS 의 것**이다 - `IPlatform::ReadClipboardText`·`WriteClipboardText`(Windows 는 `CF_UNICODETEXT`). 프레임워크는 플랫폼을
  모르므로 `FrameworkContext.textInput`(`ITextInputHost`, 호스트 구현)으로 묻는다. 붙여넣기는 제어 글자를 버리고 `maxLength` 를 지킨다(넘치는 뒤는 버린다). 에디터 ImGui 의
  사설 클립보드와는 별개다.
- **IME 조합 표시**: 플랫폼이 `WM_IME_COMPOSITION` 의 조합 글자(`GCS_COMPSTR`)를 `CompositionBegin`(비움)·`Composition`(코드포인트 하나씩 차례로)·`CompositionEnd` 이벤트로
  준다. 입력 상태의 `KeyboardState.composition`(상한 `MaxTextPerFrame`, **프레임을 넘어 남는다** - 끝 이벤트가 비운다)에 접힌다. 글자 칸은 포커스가 있으면 커서 자리에 조합 글자를
  끼워 `display` 에 보이고(저장 글자 `text` 는 그대로) 커서는 조합 뒤다. 조합이 끝난 글자는 전처럼 `Text` 로 온다.
  **조합 창은 게임이 들고 있을 때 띄우지 않는다**: 글자 칸이 포커스를 가진 프레임마다 시스템이 `ITextInputHost::SetTextInputArea(픽셀 x, y, 줄 높이)` 를 부르고 호스트가 플랫폼의
  `SetTextInputPosition` 과 `SetCompositionInline(window, true)` 로 옮긴다 - 인라인이면 플랫폼은 `WM_IME_COMPOSITION` 을 `DefWindowProc` 에 넘기지 않고(시스템 조합 창 없음)
  결과 글자(`GCS_RESULTSTR`)도 제가 `Text` 로 낸다. 포커스가 없는 프레임은 `ClearTextInput` 으로 돌린다(ImGui 의 글자 칸은 전처럼 시스템 조합 창이다).
- **커서 스크립트 API**: 핸들에 `GetCaretIndex`·`SetCaretIndex`·`GetSelectionAnchor`·`SetSelection(anchor, caret)`·`IsComposing`. 위치표(`Definitions.cpp`)를 함께 옮긴다.
- **스크립트가 월드 레이어에 만드는 위젯**: 픽셀 기본값 나누기를 `JBroCanvas` 의 `ScalePixelFields(table, instance, factor)` 로 옮기고 에디터와 `CanvasObjectSystem::AddComponent`
  (스크립트의 `AddComponent`)가 같이 쓴다. 비율은 프레임워크가 `SetWorldPixelsPerUnit` 으로 준다(캔버스는 에셋 타입의 상수를 모른다).

### 7.2 B - 새 위젯

지금 없는 것(§6.1 뒤 기준)과 우선순위. 모두 같은 틀이다 - 컴포넌트는 `JBroFramework2D`, 시스템은 `Button2DSystem`(포인터·포커스) 또는 제 시스템, 핸들·위치표·인스펙터 손잡이·캔버스
파일 왕복·시험 캔버스가 따라온다.

- **1 순위**: `Selectable2D` 는 **두지 않는다** - 토글·슬라이더·글자 칸의 호버·포커스 표시는 각 위젯에 `transition`·`targetGraphic`(버튼과 같은 꼴)을 더해 푼다(공통 뼈대를 두면
  사각형이 둘이 된다, §6.2 와 같은 까닭). `ToggleGroup2D`(같은 오브젝트나 자손의 토글 가운데 하나만, `allowOff`), `Dropdown2D`(누르면 자식 `list` 오브젝트를 펴고 항목 토글로
  고른다 - `options` 는 글자 목록, `value` 는 번호, `caption` 텍스트에 고른 글자), `Scrollbar2D`(`ScrollView2D` 의 `horizontalBar`·`verticalBar` 가 가리키는 슬라이더 - 내용 비율로
  손잡이 길이를 잰다), `ContentSizeFitter2D`(`Text2D` 의 내용 크기나 `Layout2D` 의 블록 크기로 같은 오브젝트의 스프라이트 `size` 나 마스크를 맞춘다)와 `Layout2D.fitChildren`
  (자식의 보이는 크기로 칸을 잰다).
- **2 순위**: `Tooltip2D`(머문 시간 뒤 자식 오브젝트를 보인다), `Modal2D`(켜져 있으면 제 자손 밖의 위젯이 포인터를 받지 않는다 - 시스템이 가장 위 모달 하나만 본다), 드래그 앤 드롭
  (`Draggable2D`·`DropTarget2D`, 훅 `OnDragBegin`·`OnDrop`), 탭(`TabGroup2D` = 토글 그룹 + 페이지 오브젝트 보이기), 가상화 `ListView2D`(보이는 칸만 자식으로 재사용 - 내용 수는
  스크립트가 준다), UI 트윈(`Tween2D` - 위치·크기·틴트를 곡선으로, 스크립트 서비스 `Tween`). 진행 막대는 §6.2 대로 `Filled` 스프라이트라 없다.
- **3 순위**: `SafeArea2D`(루트의 앵커를 안전 영역으로 - C 의 안전 영역과 함께), `CanvasGroup2D`(자손의 알파·`interactable` 한꺼번에), `AspectRatioFitter2D`, 렌더 텍스처 이미지,
  호버·클릭 사운드(`Button2D` 의 `hoverSound`·`clickSound`), 가상 키보드(화면 버튼 묶음이 포커스 칸에 글자를 넣는 `VirtualKeyboard2D`).
- **기반**: 내비게이션 명시 연결(`navigationUp/Down/Left/Right` 핸들 - 비면 지금의 방향 자동 탐색).

### 7.3 C - §4 의 오래된 열림 `[제안]`

- **안전 영역**: 값은 D-249 로 이미 온다(`ScreenSpaceFrame.safeArea`, 데스크톱 0). 남은 것은 쓰는 쪽 - `Layer.anchorToSafeArea`(화면 레이어, 기본 거짓)가 참이면 루트의 앵커를
  안전 영역의 사각형으로 잰다. 시험은 띠를 넣은 프레임으로 앵커 자리를 본다.
- **분할 화면**: `Camera2D.viewport`(정규화 사각형, 기본 전체)와 `primary` 둘 이상 - 브리지가 켜진 카메라마다 월드 뷰를 연다(뷰포트 차례는 `renderOrder`). 화면 레이어는 그대로 전체
  하나다(U2 - 영역 함수 하나). 포인터 역투영은 픽셀이 든 뷰포트의 카메라다.
- **3D 의 화면 레이어**: `Framework3D` 가 화면 레이어를 그리는지(§2.1 은 "값만 같이 쓴다") 를 보고, 3D 캔버스의 화면 레이어에 `Text3D`·스프라이트가 서는 최소 길을 잡는다.
- **조명 차례**: 조명이 선 지금(D-345 뒤) 화면 레이어가 광원 맵 뒤에 그려지는지 픽셀 시험으로 본다 - 틀리면 화면 뷰를 조명 합성 뒤로.

### 7.4 단계와 완료 조건

1. **A**: 7.1 전부. 완료 조건: `TextLayout` 에 자리 → 바이트 위치, `UiScenario`·`InputChain` 에 누른 자리의 커서·끌어 고르기·Shift 선택·선택 지우기·Ctrl+A/C/X/V(가짜 호스트)·조합
   표시와 끝·`maxLength` 를 넘는 붙여넣기·NaN 자리 누름, `InputSystem` 에 조합 접기, 스크립트 `AddComponent` 의 월드 레이어 나누기, `ScriptApiCatalog` 통과. 실제 창에서 한글 조합이
   칸 안에 보이는지 눈으로 본다.
2. **B 1 순위** → 3. **B 2 순위** → 4. **B 3 순위**: 위젯마다 `InputChain` 또는 제 묶음, `BuiltinComponentProperty` 수, 캔버스 파일 왕복, 인스펙터 스크린숏. 순위마다 `UiScenario` 의
   적대 시나리오(잘못 이음·NaN·도중 삭제)와 사용자 시나리오 D 확장.
5. **C**: 넷 각각 픽셀·입력 시험. 끝에 솔루션 빌드·전체 시험·실제 에디터.

### 7.5 진행 기록 (3 부)

1. ~~**A - 글자 칸 마무리**~~ → 완료 2026-10-30 · `TextLayout::GetCaretOffsetAt`·`GetCaretPosition(…, lineHeight)` → `TextBlock::HitCaret`·`GetCaret` → `Text2DSystem::GetLocalCaretAt`·
   `GetLocalCaret(…, lineHeight)`, `InputField2D` 의 `selection`(스프라이트 핸들)·`selectionAnchor`·`composing`(State)·`mirroredComposition`(Hidden), `Button2DSystem` 의 `PlaceCaretAt`(누름·끌기)·
   `TypeInto`(선택·Shift·Ctrl+A/C/X/V·붙여넣기 UTF-8 풀기)·조합 떠 두기와 끼워 보이기·선택 사각형·`ITextInputHost` 호출, 플랫폼 이벤트 `CompositionBegin`·`Composition`·`CompositionEnd` 와
   `KeyboardState.composition`(프레임을 넘어 남는다), `IPlatform::SetCompositionInline`·`ReadClipboardText`·`WriteClipboardText`(Windows: `WM_IME_*` 처리와 `CF_UNICODETEXT`),
   `FrameworkContext.textInput`(`ITextInputHost`)과 `EngineInstance::TextInputBridge`(게임 픽셀 → 창 픽셀, 포커스 칸이 있는 프레임에만 인라인 조합), 핸들의 `GetCaretIndex`·
   `SetCaretIndex`·`GetSelectionAnchor`·`SetSelection`·`IsComposing`, `JBroCanvas` 의 `ScalePixelFields` 와 `CanvasObjectSystem::SetWorldPixelsPerUnit`·`AddComponent` 의 나누기(에디터도 같은 함수),
   에디터의 `GetSimulationViewMapping` ·
   - **잰 것**: `TextLayout` - `A V` 에서 x 300 은 0, 310 은 1(A 의 가운데 304 를 넘으면 뒤), 공백의 반, 5000 은 끝 3, 첫 줄 위·마지막 줄 아래는 그 줄, 오른쪽 정렬의 밀림, 두 줄에서 첫 줄 끝은
     개행 앞 1 이고 둘째 줄 왼쪽은 2, 두 바이트 글자의 끝은 바이트 3, NaN 점은 거짓. `InputSystem` - 조합이 차례대로 접히고 프레임을 넘어 남으며 새 시작이 바꾸고 끝·포커스 잃음이 비우며
     32 에서 잘린다. `UiScenario`(레이아웃 없는 칸) - 포커스 칸은 호스트에 (50, 80) 픽셀을 알린다, Shift+← 둘이 [4, 6), 글자가 선택을 대신, Ctrl+A·C(제어 글자 0x03 은 안 들어감)·→ 접기·
     V 끼우기·X 자르기, 상한 3 에서 `a\nb\tcd` 붙여넣기는 `abc`, 깨진 바이트 뒤의 `é` 는 들어감, Shift+Home 뒤 Backspace·Shift+End 뒤 Delete 가 선택을 지움, 기준점 99 는 글자 수로·커서와
     같으면 없음, 조합 `ㄱ` 은 보이는 텍스트에만·프레임을 넘어 남고·바뀌면 대신·끝나면 결과가 저장되고·커서 자리에 끼며·Esc 가 지움, 레이아웃 없는 누름은 끝이고 끌어도 선택 없음, NaN 누름은
     유한. 스크립트 `AddComponent` 의 월드 버튼 1.6 x 0.48·화면 160 x 48·레이아웃 칸 1·간격 0.04·피벗 그대로, 핸들의 커서·선택 읽고 쓰기(음수 커서는 0, 음수 기준점은 없음, 99 는 가둠)·조합.
     `EditorApplication`(`UserScenario` 시나리오 E, 1280 x 720, Noto 48 px) - 재생 중 창에서 칸을 눌러 `Hello`, 조합 `한` 이 보이는 텍스트에만(**[봄 e01]**), Shift+← 둘의 선택 사각형(**[봄 e02]**),
     Ctrl+C 가 OS 클립보드에 `o한`, 셋째 글자 경계 + 3 px 누름이 커서 3 이고 끝까지 끌면 [3, 6)(**[봄 e03]**), Ctrl+V 가 선택을 대신해 `Helo한`(**[봄 e04]**), Esc·멈춤. `ScriptApiCatalog`·
     `BuiltinComponentProperty`(글자 칸 열여덟)·`CanvasFile`·`RendererContract`·`EditorObjectCommand`·`InputChain` 통과.
   - 정한 것: 스크롤 뷰 안의 글자 칸은 끌기가 스크롤이라 끌어 고르지 않는다(Shift+화살표로 고른다). 여러 줄에 걸친 선택은 둘러싼 사각형이다. 조합 중에는 선택이 없다. Alt 가 같이 눌린
     Ctrl(AltGr)은 글자다. 붙여넣기는 상한에서 멈추고 제어 글자·깨진 바이트를 버린다. 포커스를 잃으면 선택·조합이 없다. 인라인 조합은 `WM_IME_STARTCOMPOSITION`·`WM_IME_COMPOSITION` 을
     `DefWindowProc` 에 넘기지 않고 `WM_IME_SETCONTEXT` 의 조합 창 비트를 끈다 - 후보 창은 그대로다(`SetTextInputPosition` 이 자리를 준다). 시험은 OS 클립보드를 쓴 뒤 전 글자를
     되돌린다(글자가 아니던 내용은 되돌리지 못한다).
   - **사용성**(스크린숏 e01~e04 를 봤다): 조합 글자가 보통 글자와 같은 모양이라 조합 중임을 알 길이 커서 자리뿐이다 - 밑줄·색은 넣지 않았다(열림). 선택 사각형은 글자 위에 반투명으로
     겹쳐 글자가 읽힌다. 글자의 세로 자리는 `display` 텍스트의 `alignY` 에 달렸다(글자 칸은 표시 텍스트의 자리를 정하지 않는다). 처음 돌린 시험에서 Ctrl+C 직후의 클립보드 읽기가 한 번
     떨어졌다(글자는 쓰여 있었다) - 다른 프로그램이 바뀐 직후 잠깐 쥐는 것으로 보고 `OpenClipboard` 를 몇 번 두드리게 했다(원인은 재지 않았다).
   - 열림: 실제 IME 로 조합하는 모습은 Windows 메시지 길을 코드로만 봤다 - 시험은 이벤트를 플랫폼의 자리에 바로 넣는다(플랫폼 시험은 `WM_IME_COMPOSITION`·`WM_IME_ENDCOMPOSITION` 이 이벤트가
     되는 것과 클립보드 왕복만 본다). 조합 글자의 구분 표시, 더블 클릭 단어 선택, Ctrl+Z 되돌리기, 여러 줄 선택의 줄마다 조각은 없다.
2. ~~**B 1 순위**~~ → 완료 2026-10-30 · **전이**: `Toggle2D`·`Slider2D`·`InputField2D` 에 버튼과 같은 전이 필드(`transition`·`targetGraphic`·틴트 넷·상태 스프라이트 셋·래치)를
   더하고 `Button2DSystem` 의 전이 셈을 템플릿 하나(`ApplyTransition<TWidget>`)로 - 글자 칸은 `hovered`·`pressed` 도 든다. **`ToggleGroup2D`**(`allowOff`·State `value`): 이 오브젝트와
   자손의 토글(`VisitToggles`, 계층 차례) 가운데 하나만 - `FlipToggle` 이 누름을 받고(켜진 것을 다시 누르면 `allowOff` 가 아니면 그대로), 갱신 훅이 둘 이상 켜진 것을 새로 켜진 쪽으로 정리한다.
   **`Dropdown2D`**(버튼과 같은 사각형·전이, `list`(자식)·`caption`(`Text2D`)·`value`, State `open`·`hovered`·`pressed`·`focused`·`changed`): 항목은 `list` 아래의 토글이고 `FlipToggle` 이
   항목이면 뒤집지 않고 고른다(값·접기·`OnValueChanged`). 밖을 누르거나 Esc 면 접히고(`CloseDropdownsOutside`), 게임패드 `South` 가 펴고 접는다. 갱신 훅이 목록의 켜짐·항목의 `isOn`·
   캡션 글자(판번호로 따라감)를 든다. **스크롤바**: `ScrollView2D.horizontalBar`·`verticalBar`(`Slider2D` 가 붙은 오브젝트) - 갱신 훅이 슬라이더 자리 놓기 앞에서 막대가 끌린 프레임은
   내용을, 아니면 막대를 맞추고(값 0 은 위·왼쪽), 손잡이의 `Custom` 스프라이트 길이를 뷰포트/내용 비율(최소 1/10)로 둔다. **`Layout2D.fitChildren`**: 칸을 자식의 보이는 크기(`VisibleExtent` -
   레이아웃 블록 → 글자 → 스프라이트(`Custom` 또는 스프라이트 라이브러리의 칸 크기) → 위젯 사각형 → 마스크)로 - 세로·가로는 제 크기만큼, 격자는 가장 큰 것 하나. **`ContentSizeFitter2D`**
   (`fitWidth`·`fitHeight`·`padding`·`minSize`, State 크기): `Layout2DSystem` 이 레이아웃 뒤에 같은 오브젝트의 블록(없으면 글자 사각형)에 여백을 둘러 스프라이트(`Custom` 으로)·마스크·위젯
   사각형에 쓴다. 핸들 셋(`ToggleGroup2D`·`Dropdown2D`·`ContentSizeFitter2D`)과 기존 핸들의 새 멤버, 카탈로그·위치표, `UI` 갈래 등록(모두 하나), 캔버스 뷰 사각형 손잡이에 드롭다운 ·
   - **잰 것**: `UiScenario` - 토글 호버 0.9·눌림 0.7·꺼짐 알파 0.6·`None` 은 그대로, 그룹에서 둘째를 켜면 첫째가 꺼지고 `value` 1, 켜진 것을 다시 눌러도 그대로(`allowOff` 거짓), 참이면 꺼져 -1,
     스크립트가 둘을 켜면 첫째(전에 켜진 것이 없을 때)·새로 켜진 것이 이긴다. 드롭다운 - 접힌 목록의 항목은 눌리지 않고, 누르면 펴지고, 둘째 항목을 고르면 값 1·접힘·훅 한 번·캡션 `Two`·
     그 항목만 켜짐, 밖 누름·Esc 가 접고, 같은 항목을 다시 골라도 알리지 않으며, 스크립트의 값은 캡션·항목을 옮기고 범위 밖은 캡션이 비며, 라벨 글자가 바뀌면 따라오고, 목록이 자기 자신이면
     손대지 않는다. 스크롤바 - 위가 맞춰진 내용에서 막대 0·손잡이 18(60 x 0.3), 휠 한 칸에 0.2857, 막대 가운데를 누르면 내용 0, 아래 끝은 70, 놓아도 그대로, 내용이 작으면 0·손잡이 60,
     꺼진 축·NaN 내용 크기는 0 과 유한, 내용이 창 자신이면 막대 그대로. `Layout2D` - 맞춘 세로(30 x 10·50 x 20·칸) 자리 -5·-24·-58 과 블록 100 x 78, 가로 15·59·138 과 188 x 40, 격자는
     가장 큰 칸, 풀리지 않은 에셋 크기·NaN `Custom` 은 칸 크기, 맞추기는 110 x 88 을 스프라이트(`Custom`)·마스크에 쓰고 꺼진 축은 그대로·`minSize` 300·NaN 여백은 안 바꿈.
     `BuiltinComponentProperty`(토글 23·슬라이더 25·글자 칸 34·레이아웃 9·스크롤 뷰 10·그룹 2·드롭다운 27·맞추기 6), `ScriptApiCatalog`·`InputChain`·`CanvasFile`·`RendererContract` 통과.
     시나리오 D 가 `UI` 갈래 열 개를 세고 드롭다운·그룹·맞추기를 붙여 저장·되읽는다(**[봄 d06b·d06c]**).
   - 정한 것: 공통 선택 뼈대(`Selectable2D`)는 두지 않았다 - 위젯마다 전이 필드를 든다(사각형이 둘이 되지 않게, §6.2 와 같은 까닭). 전이의 기본은 버튼처럼 `ColorTint` 다 - 전부터 있던
     토글·슬라이더·글자 칸의 대상 스프라이트가 호버에 0.9 로 어두워진다(바꾸려면 `None`). 드롭다운 항목의 `isOn` 과 목록의 켜짐은 시스템의 것이다(스크립트가 써도 다음 갱신에 덮인다).
     스크롤바는 컴포넌트가 아니라 스크롤 뷰의 핸들이다. 글자 크기는 지난 프레임의 레이아웃이라 맞추기가 한 프레임 늦다. `fitChildren` 의 에셋 크기 스프라이트는 스프라이트 라이브러리로
     푼다(처음 보는 텍스처는 거기서 올라간다 - 추출이 하던 일과 같다). 내용 크기 맞추기가 스프라이트의 `sizeMode` 를 `Custom` 으로 바꾼다.
   - **사용성**(스크린숏 d06b·d06c 를 봤다): 월드 레이어의 드롭다운 기본 크기가 1.6 x 0.32 로 나뉘어 붙고, 새 셋이 `UI` 갈래에서 붙는다. 전이 필드 열둘이 위젯마다 인스펙터를 길게
     차지해 `Category("Transition")` 로 묶어 접었다(버튼도 같이) - `State` 와 같은 접는 갈래다. 맞추기의 인스펙터는 넷뿐이라 그대로다.
   - 열림: 드롭다운의 키보드 조작(위·아래로 항목 옮기기)은 게임패드 탐색이 대신한다. 스크롤바의 트랙 누름(손잡이 밖)은 슬라이더의 뜀 그대로다(한 페이지씩 가지 않는다).
3. ~~**B 2 순위**~~ → 완료 2026-10-30 · **`Tooltip2D`**(사각형·`delay`·`target`, State `shown`): `Button2DSystem` 이 누르지 않은 포인터가 사각형에 머문 시간을 세어(지난 갱신 훅의
   델타) `delay` 를 넘기면 대상을 켠다 - 포인터를 잡지 않는다. **`Modal2D`**(`blockGame`): 켜진 모달이 있으면 그 안의 위젯만 포인터·탐색 후보(`InsideModal`)이고, `blockGame` 이면
   마우스·터치·키보드·게임패드를 모두 지운다. **`TabGroup2D`**(`content`, State `page`): 같은 오브젝트의 토글 그룹 값과 짝인 페이지(`content` 의 자식)만 켠다. **`Draggable2D`**
   (사각형·`snapBack`, State `dragging`·`droppedOn`)·**`DropTarget2D`**(사각형, State `hovering`·`dropped`): 누르면 제자리를 떠 두고 `OnDragBegin`, 쥔 동안 레이어 단위로 따라오며
   놓을 자리의 `hovering`, 놓으면 포인터 아래의 첫 자리에 떨어져 한 프레임 서로를 가리키고 `OnDrop`(자리)·`OnDragEnd`(끌던 것), 자리가 없고 `snapBack` 이면 제자리로. 스크롤 뷰 안이어도
   끌기가 가진다. **`Tween2D`**(`target`·`ease`·`duration`·`loop`·`playOnStart`·from/to 셋, State `playing`·`time`·`finished`)와 새 **`Tween2DSystem`**(55): 자리·크기·회전·틴트·알파를
   `EaseKind` 모양으로, `Once`·`Loop`·`PingPong`. `EaseKind` 에 리플렉션(`JBRO_DEFINE_ENUM_TYPE`, Core)을 더했다. **`ListView2D`**(`count`·`itemSize`·`spacing`·`vertical`, State
   `firstVisible`·`visibleCount`·`boundItem`·`boundIndex`): 같은 오브젝트의 스크롤 뷰 내용 아래 사용자가 둔 풀(자식)에 뷰포트에 걸치는 항목만 묶는다(`SlotIndex` - 항목 `i` 는 풀의
   `i % 풀` 번째) - 범위는 내용 자리에서 셈으로 재고(순회 없음), 묶임이 바뀐 칸마다 `OnListItem`. 지난 범위를 셋(`shownFirst`·`shownVisible`·`shownPool`)으로 되세어 배열을 들지
   않는다. 훅 넷(`OnDragBegin`·`OnDragEnd`·`OnDrop`·`OnListItem`)이 `GameScript2D`·콜백 표·위치표에 섰다. 핸들 일곱, 열거형 셋(`TweenTarget`·`TweenLoop`·`EaseKind`)의 카탈로그, `UI`
   갈래 등록(트윈은 여럿), 사각형 손잡이에 툴팁·끌기·자리 ·
   - **잰 것**: `UiScenario` - 툴팁은 12 프레임 전엔 안 보이고 뒤엔 대상이 켜지며 누르면 꺼지고 누름은 게임에 가며 떠나면 안 보이고 대상이 자신이면 그대로, NaN 지연은 바로. 모달이 켜지면
     밖 버튼은 호버·클릭이 없고 게임은 마우스를 못 보며 안 버튼은 눌리고 밖 툴팁은 안 보이며 탐색은 안에만 내리고, `blockGame` 을 끄면 게임이 마우스를 보되 밖 버튼은 그대로, 끄면 풀린다.
     탭은 고른 번호의 페이지만 켜지고 내용이 자신이면 그대로. 끌기는 누르면 시작(`OnDragBegin` 1·게임은 마우스 못 봄), 25 px 끌면 5 에, 자리 위에서 `hovering`, 놓으면 `dropped`·
     `droppedOn`·훅 각 1·한 프레임, `snapBack` 이면 빈 곳에 놓아 제자리, 끝 훅에서 제 오브젝트를 지워도 다음 누름은 게임에. `Layout2D` 묶음 - 목록 20 개·풀 다섯·창 60 에서 넷이 보이고
     190·130 에 서며 훅 넷, 20 내리면 1~5 가 보이고 항목 5 가 풀 0 에·4 가 풀 4 에(훅 여섯), 맨 아래 16~19, 수를 줄이면 내용도, 0 이면 모두 꺼짐, NaN 크기·내용이 창 자신이면 안 묶음.
     트윈 - 첫 갱신에 1/60, 0.5 초에 30, 끝에서 멈추며 `finished` 한 프레임, 핑퐁 1.5 초에 30, 알파 0.5, `QuadIn` 0.75, NaN 길이는 바로 끝, NaN 목표는 그대로. `RendererContract` 기본
     시스템 열둘, `BuiltinComponentProperty`(툴팁 6·모달 1·탭 2·끌기 8·자리 4·트윈 15·목록 11), `ScriptApiCatalog`·`InputChain`·`CanvasFile` 통과.
   - 정한 것: 가상화 목록의 풀은 사용자가 만든다(런타임 복제 없음 - 프리팹은 에셋에서만 뜬다). 항목의 크기·간격은 고정이다. 끌기는 레이어 단위라 부모가 돌거나 늘어나 있으면 포인터와
     어긋난다. 놓을 자리가 겹치면 먼저 찾은 것이다. 모달은 그리기를 손대지 않는다(막은 스프라이트로). 트윈은 같은 오브젝트에 여럿이다. 트윈 서비스는 두지 않았다 - 핸들의 `Play`·`Stop`·
     `Restart` 다. 툴팁의 머문 시간은 지난 갱신 훅의 델타다(편집 중에는 늘지 않는다).
   - 열림: 끌기 중 그림을 맨 위로 올리기(renderOrder 는 손대지 않는다), 목록 항목의 가변 크기, 트윈의 사슬(끝나면 다음), 툴팁 자리 따라가기(대상은 제자리다).
4. ~~**B 3 순위**~~ → 완료 2026-10-30 · **`SafeArea2D`**(`horizontal`·`vertical`): 화면 레이어의 루트에 두면 `Transform2DSystem` 이 앵커를 `ComputeSafeAnchorPoint`(새 `ScreenSpace` 함수 -
   띠를 보이는 영역의 비율로 기준 픽셀로 옮긴다)로 잰다 - §4·§7.3 의 "안전 영역" 이 이것으로 섰다. **`CanvasGroup2D`**(`alpha`·`interactable`): 새 `GroupState2D`(`ComputeGroupAlpha2D`·
   `IsGroupInteractable2D`, 조상을 모두 본다)를 스프라이트·글자 추출(알파 곱)과 위젯 판정·탐색(후보 제외)이 같이 쓴다. **`AspectRatioFitter2D`**(`mode`·`aspect`): `Layout2DSystem` 이 내용
   크기 맞추기 뒤에 같은 오브젝트의 `Custom` 스프라이트·마스크·위젯 사각형의 한 변을 맞춘다. **`VirtualKeyboard2D`**(`target`·`backspace`·`enter`·`space`): 아래의 버튼은 포커스를 가져가지
   않고, 클릭이 대상 칸(없으면 포커스 칸)에 제 `Text2D` 글자를 끼운다(`EditField` - 선택 대신·상한·`OnValueChanged`), 특수 키 셋은 핸들로 가리킨다. **`Navigation2D`**(`up`·`down`·
   `left`·`right`): `NavigateFocus` 가 방향 탐색 앞에 본다 - 가리킨 곳에 켜진 위젯(`WidgetKindOf`)이 없거나 자기 자신·모달·그룹 밖이면 그 방향은 없다. **버튼 소리**: `Button2D.hoverSoundId`·
   `clickSoundId`(핸들은 해석 패스) - 들어올 때·클릭(포인터·게임패드)에 `GetAudioServices().Audio.PlayOneShot` 한 번. 핸들 다섯과 버튼의 새 멤버, 열거형 `AspectMode`, `UI` 갈래 등록 ·
   - **잰 것**: `UiScenario` - 왼쪽 20·위 10 의 띠에서 왼쪽 위 앵커의 `SafeArea2D` 루트는 (-80, 40)·보통 루트는 (-100, 50), 가로를 끄면 (-100, 40), 띠가 없으면 (-100, 50). 그룹 알파는
     0.5 x 0.5 = 0.25 이고 밖은 1, NaN 은 1, 켜진 그룹 아래 버튼은 눌리고 `interactable` 이 거짓이면 호버·눌림이 없고 게임이 마우스를 본다. 가상 키보드 - 포커스 칸에 `A`·`AA`,
     지우기 `A`, 제출, Esc 뒤엔 안 들어가고, `target` 을 주면 포커스 없이 들어가며, 상한이 지켜진다. 연결 - 포커스 없이 맨 위가 먼저, `down` 연결이 방향 탐색 대신 가고, 꺼진 위젯·자기
     자신을 가리키면 그대로. `Layout2D` - 가로세로비 2 에서 100 x 30 → 100 x 50(마스크도), 높이 주인이면 60 x 30, 0·NaN 은 그대로, 에셋 크기 스프라이트는 두고 마스크만.
     `BuiltinComponentProperty`(버튼 25·안전 영역 2·그룹 2·비율 2·키보드 4·연결 4), `RendererContract`·`ScriptApiCatalog`·`InputChain`·`CanvasFile`·`TextRender`·`SpritePixel` 통과.
   - 정한 것: 렌더 텍스처 이미지(3 순위)는 **넣지 않았다** - 텍스처는 호스트·RHI 가 만들고(에디터의 편집 뷰처럼) 프레임워크에는 텍스처 수명과 같은 프레임의 순서를 다루는 자리가 없다.
     렌더러 쪽 일(카메라의 대상 텍스처)로 남긴다. 버튼 소리의 실제 재생은 오디오 없는 시험에서 보지 않았다(서비스는 시스템이 없으면 아무 일도 않는다 - 코드로 확인). 가상 키보드의
     버튼은 게임패드로는 누를 수 없다(포커스를 가져가지 않는 길이 포인터뿐이다). 그룹의 알파는 추출 때 곱한다 - 틴트 값은 그대로다.
   - 열림: 렌더 텍스처 이미지(렌더러), 가상 키보드의 게임패드 조작, 그룹의 `blocksRaycasts` 같은 세분(지금은 `interactable` 하나).
5. ~~**C**~~ → 완료 2026-10-30 · **안전 영역**은 B 3 순위의 `SafeArea2D`(D-358)로 섰다. **분할 화면**(D-359): `Camera2D.viewportOrigin`·`viewportSize`(대상의 0..1, 왼쪽 위 원점)·
   `depth`, `RenderCamera2D` 에 같은 셋, `Camera2DSystem` 이 그릴 카메라 **모두**를 `depth` 차례로 렌더 월드에 든다(`FillCamera`·`RenderWorld2D::AddCamera`; 주 카메라는 전처럼 따로),
   `ApplyCameraViewport`(뷰포트 → 대상 픽셀 사각형, `PixelPerfect` 는 그 안의 레터박스)와 다시 정한 `ApplyCameraArea`(뷰포트가 전체일 때만 레터박스 - 화면 레이어·앵커·에디터는
   대상 전체), 렌더 브리지가 카메라마다 뷰 하나(조명·그림자·패럴랙스는 그 뷰의 카메라, 디버그 선은 첫 뷰, 후처리는 주 카메라의 프로필 하나), 포인터는 그 픽셀이 든 뷰포트의 카메라
   (`SelectCameraAt`, 겹치면 `depth` 큰 것)로 그 뷰포트 안에서 역투영하고 월드 → 픽셀은 주 카메라다. 카메라를 직접 건 시험(렌더 월드의 목록이 빈 경우)은 전처럼 주 카메라 하나다.
   **조명 차례**: 픽셀 시험으로 봤다 - 환경광 0.2 의 월드 위에서 화면 레이어의 흰 사각형은 희다(화면 뷰는 광원 맵 뒤의 다른 뷰다, §2.4·D-237 의 설계 그대로). **3D 의 화면 레이어**는
   넣지 않았다 - `Framework3D` 에 스프라이트 경로가 없다(§2.1 "3D 는 이 판에서 화면 레이어를 그리지 않는다"). 열림 ·
   - **잰 것**: `CameraView2D` - 오른쪽 절반 뷰포트는 200 x 100 에서 (100, 0, 100, 100) 이고 반폭이 그 비율(5), 화면 사각형은 비며, `PixelPerfect` 는 뷰포트 안 (100, 25, 100, 50),
     넘치면 잘리고 0 폭은 그릴 수 없고 NaN 원점은 0, 기본은 전체. **픽셀**(D3D12, 64 x 64) - 왼쪽 카메라의 빨강(환경광으로 51)·오른쪽의 파랑, 서로 보이지 않고, 화면 레이어의 흰
     8 x 8 이 오른쪽 위에 희고 밖은 검다. `UiScenario` - 왼쪽 절반 가운데는 (-10, 0)·오른쪽은 (10, 0)·오른쪽 안의 1/4 은 (35, 25), 월드 버튼은 제 카메라의 절반에서만 눌리고,
     월드 → 픽셀은 주 카메라, 겹치면 `depth` 큰 카메라, 빈 뷰포트는 건너뛴다. `Framework2DSystem`(카메라 직접 걸기)·`Light2DPixel`·`RendererContract`·`BuiltinComponentProperty`
     (카메라 12)·`ScriptApiCatalog`·`InputChain`·`CanvasFile`·`TextRender` 통과.
   - 정한 것: 뷰포트 원점은 왼쪽 위다(마우스 픽셀과 같다). 렌더러는 대상을 첫 뷰에서 지운다 - 둘째 카메라의 `clearColor` 는 쓰이지 않는다(그 뷰포트는 첫 뷰가 지운 색이다).
     HUD 는 늘 대상 전체다(분할 화면마다 HUD 를 두려면 월드 레이어에 둔다). 후처리 프로필은 주 카메라 하나다.
   - 열림: 3D 의 화면 레이어, 카메라마다 후처리 프로필, 둘째 뷰의 지우기.
- **전체**(2026-10-30, C 뒤, 이 PC 의 main 폴더, Debug, 기준 main 3cb7e788): 솔루션 빌드 경고 0, 전체 시험 127 묶음 `all tests passed.`(`EditorApplication` 1503.4 초,
  `UserScenario` 99.6 초 - 시나리오 D 의 위젯 스물둘과 E 의 글자 칸이 더해졌다). 스크린숏(`UserScenario`): e01 조합 `한` 이 칸 끝에 커서 앞으로 보이고, e02 Shift+← 둘의 선택 사각형이
  `o한` 을 덮고, e03 끌어 고른 [3, 6) 이 같은 사각형이고, e04 붙여넣기 뒤 `Helo한` 과 커서. d06b `Dropdown2D` 인스펙터(사각형·전이 틴트 넷·상태 스프라이트 셋·`list`), d06c 피터.
  **실제 에디터**(`JBroEditorHost.exe`, 위젯 일곱이 든 `UI Game` 프로젝트): 오브젝트를 고르고 `컴포넌트 추가` 를 열면 `UI` 갈래에 스물둘이 이름 차례로 서고 검색 칸·스크롤바가 있으며,
  이미 붙은 `Layout2D` 는 회색이다. 열두 줄이 보이고 나머지는 스크롤이다 - 길지만 검색이 있어 그대로 둔다(열림: 갈래를 입력·배치·효과로 나누기).
- 3 부 끝(2026-10-30). 남은 것은 위 항목마다의 "열림" 이다.

### 7.6 4 부 - 3 부의 열림 일곱 (D-360)

> 상태: **완료** (2026-10-30 시작, 같은 날 끝). 사용자 지시 "드롭다운 키보드 항목 이동, 스크롤바 트랙 페이지 넘김, 끌기 중 그리기 순서 올리기, 가변 크기 리스트 항목, 트윈 사슬, 툴팁 따라가기,
> 가상 키보드의 게임패드. ㄱㄱ". 차례는 드롭다운 → 스크롤바 → 끌기와 툴팁 → 가상 키보드 → 목록 → 트윈이다. 단위마다 커밋하고 `UiScenario`·`Layout2D` 에 적대 시나리오를 넣는다.
> 새 컴포넌트·서비스는 없다 - 기존 위젯의 필드와 시스템의 셈이다. 결정은 D-360.

1. ~~**드롭다운의 키보드·게임패드**~~ → 완료 2026-10-30 · `Dropdown2D.highlighted`(State, 펴진 동안만 0 이상), `Button2DSystem` 의 `CountOptions`·`OpenDropdown`(고른 항목, 없으면 첫 항목)·
   `MoveHighlight`(끝에서 멈춤)·`PickHighlight`(값·`changed`·`OnValueChanged`·접기). 키보드: 포커스 드롭다운에 Enter·Space 가 펴고, 펴진 동안 ↑·↓(`Repeats`, `m_dropdownHeld`)·Enter·Esc 를
   소비한다. 게임패드: `South` 가 펴고 고르며, 펴진 동안 십자키·스틱 위아래가 돋움이고 좌우는 아무것도 아니다. 6 단계에서 돋운 항목의 토글은 `hovered` 가 참이다(호버 틴트). 핸들 `GetHighlighted`.
   - **잰 것**: `UiScenario` `TestDropdownsMoveByKeyboardAndGamepad` - 값 없이 펴면 0 이 돋고 그 스프라이트가 0.9, ↓ 로 1·둘째가 0.9 첫째가 1.0, 끝에서 ↓ 는 그대로, Enter 가 값 1·접힘·훅 1 번,
     접히면 호버 없음, 닫힌 채 Enter 가 고른 항목(1)에서 펴고, ↑ 둘은 0 에서 멈춤, Esc 는 값 그대로 접힘, Space 가 펴고 ↓ 를 쥐면 0.5 초 안에 끝(1)까지, `South` 가 펴고 십자키 아래가 1·왼쪽은 그대로
     (펴진 채 포커스 그대로)·`South` 가 고름(훅 2 번), 목록이 없으면 -1 이고 Enter 는 접기만, 값 9 는 첫 항목, 꺼진 드롭다운은 키를 무시. `ScriptApiCatalog`·`BuiltinComponentProperty`(28) 통과.
   - 정한 것: 펴진 동안만 키보드를 소비한다(접힌 드롭다운에 포커스가 있어도 글자 키는 게임에 간다). 돋움은 접히면 -1 이다. 스크립트가 `open` 을 썼으면 돋움은 -1 이고 첫 ↓ 가 0 이다.
2. ~~**스크롤바 트랙의 페이지 넘김**~~ → 완료 2026-10-30 · `Button2DSystem::ScrollbarPage`(이 슬라이더가 어느 스크롤 뷰의 켜진 막대면 한 페이지 = 뷰포트/(내용 - 뷰포트) 과 손잡이 반 길이
   = max(뷰포트/내용, 1/10)/2)·`PageScrollbar`(처음 방향으로 꼭 한 페이지, 손잡이가 포인터에 닿았거나 포인터가 반대쪽이면 그대로). 누를 때 손잡이 밖이면 `m_sliderPaging` 이고 놓을 때까지
   끌기로 바뀌지 않으며, 쥔 동안 `Repeats`(`RepeatDelay` 뒤 `NavigateRepeatInterval`)마다 그때의 포인터로 다시 센다 - 쥔 채 포인터를 더 옮기면 다시 간다.
   - **잰 것**: `UiScenario` `TestScrollbarsFollowTheContent` - 내용 200/뷰포트 60: 가운데(0.5) 누름이 0.2857 → 0.7143(내용 30)이고 지나쳤으니 쥐어도 그대로, 손잡이 위(y 63 → 0.7167) 누름은 끌기,
     위쪽 트랙(0.0833) 누름이 1 → 0.5714(10), 23 프레임은 그대로, 24 프레임째 0.1429(-50), 닿았으니 멈춤, 끝 너머는 1 에서 멈춤, 0.5 를 누르면 1 → 0.5714 에서 덮어 멈추고 쥔 채 y 25 로 옮기면
     0.1429, 반대쪽으로 옮기면 그대로, 막대가 아닌 슬라이더는 전처럼 뜀.
   - 정한 것: 손잡이 길이는 스프라이트가 `Custom` 이 아니어도 같은 셈이다(보이는 손잡이와 다를 수 있다). 트랙 누름 중에는 끌기로 바뀌지 않는다(놓고 손잡이를 다시 누른다).
   - 처음 판은 손잡이가 닿으면 `m_sliderPaging` 을 끄게 했더니 그 프레임부터 끌기가 되어 포인터 자리로 뛰었다 - 놓을 때까지 페이지 모드로 두고 `PageScrollbar` 가 스스로 그대로 있게 바꿨다.
3. ~~**끌리는 동안 맨 위에, 툴팁 따라가기**~~ → 완료 2026-10-30 · `Draggable2D.liftOrder`(기본 100)·`lifted`(Hidden): 끌기 시작에 `Button2DSystem::LiftSubtree` 가 이 오브젝트와 자손의
   `SpriteRenderer2D`·`Text2D` 의 `renderOrder` 에 더하고 `lifted` 에 적으며, `DropDraggable` 이 `lifted` 만큼 뺀다(포인터가 사라져 놓일 때도). `Tooltip2D.follow`·`followOffset`(기본 (12, -12) 픽셀):
   `PlaceWidgets` 의 툴팁 단계가 보이는 동안 대상을 포인터(대상 부모의 로컬, 부모가 없으면 레이어) + 오프셋에 둔다 - 포인터는 입력 체인이 `m_pointer` 에 떠 둔 것이다. 핸들 `GetLiftOrder`·
   `SetLiftOrder`·`GetFollow`·`SetFollow`·`GetFollowOffset`·`SetFollowOffset`.
   - **잰 것**: `UiScenario` `TestTooltipsModalsTabsAndDragging` - 끌면 카드 스프라이트 3 → 103 과 자식 글자 0 → 100 이고 `lifted` 100, 놓으면 3·0·0, `liftOrder` 0 이면 그대로, 끌리는 중에 7 로
     바꿔도 놓을 때 되돌리는 값은 0(그대로 3). 툴팁은 `follow` 없이 (0, 20) 그대로, 켜면 포인터 (100, 50) 에서 (12, -12), (110, 42) 에서 (22, -4), NaN 오프셋은 그 프레임 그대로.
     `BuiltinComponentProperty`(툴팁 8·끌기 10)·`ScriptApiCatalog`·`PublicHeaderComposition` 통과.
   - 정한 것: 올리는 것은 스프라이트·글자의 `renderOrder` 다(`drawSequence` 는 손대지 않는다 - 같은 `renderOrder` 안의 차례는 그대로). 끌리는 동안 붙인 그림은 올리지 않고 뗀 그림은
     되돌리지 않는다(놓을 때 `lifted` 를 빼는 대상은 그때의 자손이다 - 끌리는 동안 붙인 자식 그림은 놓을 때 `lifted` 만큼 **내려간다**, 열림). 툴팁의 자리는 스크립트가 같은 프레임에 써도 시스템이 덮는다.
4. ~~**가상 키보드의 게임패드**~~ → 완료 2026-10-30 · `PressVirtualKey(keyboardOwner, key, fromGamepad)` - 대상 칸은 `target` → 포커스 칸 → (게임패드면) `m_lastField`(`SetFocus` 가 글자 칸에
   포커스를 줄 때마다 적는다). 게임패드 `South` 로 포커스 버튼을 클릭한 뒤 그 버튼이 가상 키보드 안이면 키를 누르고, 포커스가 키보드 안에 있을 때 `East` 가 지우기(`key` 가 nullptr)다.
   포인터로 누른 키는 전처럼 포커스 칸까지다(`fromGamepad` 거짓).
   - **잰 것**: `UiScenario` `TestSafeAreaGroupsKeyboardAndLinks` - 칸을 눌러 포커스(커서 2), 십자키 위·왼쪽 둘로 `keyA` 에 포커스(칸은 포커스를 잃는다), `South` 가 클릭이고 `AA` → `AAA`,
     `East` 가 `AA`, 오른쪽 둘이 `enter` 키이고 `South` 가 제출, 마지막 칸이 꺼져 있으면 `East` 는 아무것도 안 함, 포인터로 누른 키는 포커스 칸이 없으면 그대로.
   - 정한 것: 게임패드로 치는 동안 칸은 포커스가 없다(커서가 보이지 않고 `focused` 거짓) - 글자는 떠 둔 커서 자리에 들어간다. `East` 는 키보드 안에 포커스가 있을 때만 UI 의 것이다.
5. ~~**가변 크기 목록 항목**~~ → 완료 2026-10-30 · `ListView2D.variableSize`·`measuredSize`(State, 훅이 쓴다)·`remeasure`(State, 스크립트가 세운다)·`measuredCount`(Hidden), 스크립트 훅
   `GameScript2D::OnListMeasure`(카탈로그·위치표), 핸들 `GetVariableSize`·`SetVariableSize`·`GetMeasuredSize`·`SetMeasuredSize`·`Remeasure`. `Layout2DSystem` 이 목록마다 `ListCache`(오브젝트 번호와
   끝 오프셋 배열)를 들고 - 처음·`count` 가 다름·`remeasure`·배열 길이가 다름에 항목마다 `boundIndex` 를 두고 훅을 불러 `measuredSize`(수가 아니거나 음수면 `itemSize`)를 누적한다. 걸치는 범위는
   `lower_bound`(end_i >= lo)·`upper_bound`(start_i <= hi) 이고 고정 크기와 같은 경계다. 항목 자리는 (start + end)/2. 이번 갱신에 쓰지 않은 캐시(목록이 사라졌거나 고정으로 돌아감)는 버린다.
   - **잰 것**: `Layout2D` `TestListItemsCanVaryInSize` - 여섯(10·30·…, 끝 10·40·50·80·90·120)은 첫 갱신에 여섯 번 재고 내용 120, 위 맞춤에서 0~3 이 보이며 자리는 55·35·15·-5, 묶임 넷, 가만히
     있으면 더 재지 않고, 아래로 가면 3~5(4·5 는 풀 0·1 에 -25·-45), 개수 4 는 다시 재어 80, `remeasure` 와 5/15 는 40, NaN·음수는 20 씩 80, 훅이 안 쓰면 80, 고정으로 돌리면 재지 않고 80,
     다시 켜면 넷 재고, 0 은 내용 0. `UiScenario`·`BuiltinComponentProperty`(15)·`ScriptApiCatalog`·`PublicHeaderComposition` 통과.
   - 정한 것: 재는 동안의 훅마다 스크립트 목록을 다시 모은다(훅이 스크립트를 떼도 된다). 100 만 항목이면 재는 데 훅 100 만 번이다 - 한 번이고 `count` 를 바꿀 때마다다(열림: 바뀐 뒤만 재기).
     항목의 가로(진행 축과 수직) 길이는 전처럼 `itemSize` 하나다.
6. ~~**트윈 사슬**~~ → 완료 2026-10-30 · `Tween2D.next`(`Handle::GameObject`), 핸들 `GetNext`·`SetNext`. `Tween2DSystem` 이 `Once` 가 끝에 닿은 프레임에 `next` 오브젝트의 `Tween2D` 를
   처음부터(`time` 0·`playing`·`started`) 돌린다 - 자기 자신은 무시, 트윈이 없는 오브젝트는 아무것도 아니다.
   - **잰 것**: `Layout2D` `TestTweensMoveOverTime` 끝 - 다음 트윈은 첫 것이 끝나기 전엔 서 있고(59 프레임), 끝나는 프레임에 처음부터 돌며(`time` ≤ 1/60), 제 길이(0.5 초)를 다 가고,
     되돌아오는 고리(B → A)는 A 를 다시 돌리며(풀 차례에 따라 같은 프레임에 한 번 더 가므로 x 는 1 또는 2), 자기 자신을 `next` 로 둔 트윈은 그냥 서고, 트윈 없는 `next` 는 아무것도 아니다.
     `BuiltinComponentProperty`(16)·`ScriptApiCatalog`·`PublicHeaderComposition`·`UiScenario` 통과.
   - 정한 것: 사슬은 `Once` 만 잇는다(`Loop`·`PingPong` 은 끝이 없다). 같은 프레임에 풀 차례가 뒤인 트윈이 이어지면 그 프레임에 바로 한 걸음 간다(한 프레임 차이, 열림으로 두지 않는다).
   - 시험을 쓰다 두 번 헛짚었다: 풀이 늘어 옛 컴포넌트 포인터가 다른 자리를 가리켰고(새 오브젝트를 만든 뒤 `FindComponentRaw` 로 다시 받는다), 앞 단계가 둔 `QuadIn` 이 남아 첫 프레임 값이 1/3600 이었다.
- **전체**(2026-10-30, 4 부 뒤, 이 PC 의 main 폴더, Debug, 기준 main 27352ae4): 솔루션 빌드 C++ 경고 0, 전체 시험 127 묶음 `all tests passed.`(`EditorApplication` 1505.2 초, `UserScenario` 100.1 초).
  스크린숏 d06b(`Dropdown2D` 인스펙터)는 3 부와 같다 - 새 필드(`highlighted`·`liftOrder`·`follow`·`variableSize`·`next`)는 State 갈래거나 아래쪽이라 화면이 바뀌지 않았다.
- 4 부 끝(2026-10-30). 남은 것: 끌리는 동안 붙인 자식 그림의 내려감(3), 가변 목록은 `count` 가 바뀌면 전부 다시 잼(5), 3 부의 열림 가운데 3D 화면 레이어·실제 IME·렌더 텍스처·카메라마다 후처리.
  → 5 부(§7.7)에서 (3)·(5)·카메라마다 후처리를 세웠다.

### 7.7 5 부 - 남은 열림 (D-361)

> 상태: **완료** (2026-10-30 시작, 같은 날 끝). 사용자 지시 "남은거 해". 3·4 부의 열림 가운데 엔진 안에서 되는 것: 끌기 올리기 보완, 가변 목록 부분 재기, 글자 칸 셋(조합 밑줄·더블 클릭·Ctrl+Z),
> 그룹 `blocksRaycasts`, 카메라마다 후처리, 컴포넌트 갈래 나누기. 남기는 것과 까닭은 D-361 (7). 단위마다 커밋한다.

1. ~~**끌기는 올린 것만 내린다**~~ → 완료 2026-10-30 · `Button2DSystem::m_lifted`(`Array<Handle::GameObject>`, 64 예약)에 `LiftSubtree` 가 올린 오브젝트를 적고 `LowerLifted` 가 그것만 내린다.
   - **잰 것**: `UiScenario` `TestTooltipsModalsTabsAndDragging` - 끌리는 중에 붙인 자식 스프라이트(5)는 그대로 5 이고, 끌리는 중에 부모를 뗀 자식 글자는 놓을 때 100 → 0 으로 돌아오며 카드는 103 → 3.
   - 정한 것: 끌리는 동안 지워진 오브젝트는 핸들이 풀리지 않아 건너뛴다.
2. ~~**가변 목록은 늘어난 항목만 잰다**~~ → 완료 2026-10-30 · `Layout2DSystem` 의 재기 앞에 `known`(= `remeasure` 아님·`measuredCount` 가 끝 배열 길이·0 아님): 줄면 `Resize`, 늘면 끝에서 이어 잰다.
   - **잰 것**: `Layout2D` `TestListItemsCanVaryInSize` - 6 → 4 는 재지 않고 내용 80, 4 → 7 은 셋만 재고(마지막 번호 6) 내용 130, 7 → 4 는 재지 않고 80, `remeasure` 는 넷 모두(13).
3. ~~**글자 칸 셋**~~ → 완료 2026-10-30 · `InputField2D.composition`(스프라이트, `display` 의 자식)은 `PlaceWidgets` 가 조합 구간([커서, 커서 + 조합 바이트))의 아래에 글자 높이의 1/10 두께로 늘린다.
   더블 클릭은 `Button2DSystem` 의 시계(`m_clock`, 지난 델타의 합)로 - 같은 칸을 `DoubleClickTime` 0.3 초 안·`DoubleClickDistance` 4 픽셀 안에서 다시 누르면 `SelectWordAtCaret`(공백이 아닌 글자의
   이어짐; 커서가 공백이면 없음). 되돌리기는 `undoText`·`undoCaret`·`undoValid`(Hidden): `TypeInto`·`EditField` 가 글자를 쓰기 직전에 `KeepUndo` 로 떠 두고, Ctrl+Z 가 떠 둔 글자를 지금 것으로 쓰며
   그때 지금 것이 다시 떠 둔 것이 된다(다시 누르면 다시 하기). 핸들 `GetComposition`·`SetComposition`. `OnDetached` 가 `undoText` 도 지운다.
   - **잰 것**: `UiScenario` `TestSelectionClipboardAndComposition` - 여섯 글자 뒤 떠 둔 것은 빈 글자·커서 0, Ctrl+Z 가 비우고 다시 누르면 `abcdef` 에 커서 6, 레이아웃 없는 밑줄은 숨김,
     `ab cd` 끝을 두 번 누르면 [3, 5), 0.33 초 뒤의 누름은 한 번 누름, 4 픽셀 안은 더블 클릭, 공백에서는 없음, 글자 한 자 뒤의 Ctrl+Z. `UserScenario` 시나리오 E - 조합 `한` 아래에 밑줄이
     늘어난다(폭 > 10, 두께 > 1, 스크린숏 e01 을 봤다 - 글자 아래 주황 선).
   - 정한 것: 되돌리기는 한 단계다(둘째 Ctrl+Z 는 다시 하기). 더블 클릭은 단어까지다(세 번은 없다). 밑줄의 두께는 글자 높이의 1/10 이고 색·그림은 사용자의 스프라이트다.
4. ~~**그룹 `blocksRaycasts`**~~ → 완료 2026-10-30 · `CanvasGroup2D.blocksRaycasts`(기본 참), `IsGroupBlockingRaycasts2D`(GroupState2D). 포인터 후보 판정(`consider` 의 사각형 검사 앞)과 툴팁의 머무름에서만
   본다 - 포커스 탐색·게임패드는 전처럼 `interactable` 만 본다. 핸들 `GetBlocksRaycasts`·`SetBlocksRaycasts`.
   - **잰 것**: `UiScenario` `TestSafeAreaGroupsKeyboardAndLinks` - 거짓이면 누름이 게임에 가고 버튼은 눌리지도 호버되지도 않으며, 십자키가 포커스를 주고 `South` 가 클릭한다. `BuiltinComponentProperty`(3).
   - 처음엔 검사를 후보 판정의 맨 앞에 두었더니 포커스 장부(`focusedObject`)를 적지 못해 "포커스 위젯이 사라졌다" 로 포커스가 지워졌다 - 사각형 검사 앞으로 옮겼다.
5. ~~**카메라마다 후처리 프로필**~~ → 완료 2026-10-30 · `RenderWorld2D::SetCameraPostProcessCount`·`SetCameraPostProcess(index)`·`GetCameraPostProcess(index)`(자리 밖은 `GetPostProcess`),
   `Framework2D::MixPostProcessFor(camera, mixer, overridesFrom)` 로 섞기를 떼어 주 카메라는 `m_postMixer`, 나머지는 `m_cameraMixers`(`MaxCameraMixers` 8, 처음에 잡는다)로 제 프로필·제 자리의
   볼륨을 섞고 스크립트 덮어쓰기는 `PostProcessMixer::CopyOverridesFrom` 으로 주 카메라의 것을 받는다. 브리지의 `drawCamera` 가 카메라 번호의 것을 뷰에 건다.
   - **잰 것**: `SpriteDrawMode` 분할 화면 픽셀 시험 - 오른쪽 카메라에만 노출 -2.2 의 `dark.jpost`(파일을 쓰고 메타를 만들며 다시 스캔) → 오른쪽 파랑 51 → 26, 왼쪽 빨강 51 그대로,
     후처리된 뷰 1. `RendererContract`·`CameraView2D`·`Framework2DSystem`·`UiScenario` 통과.
   - 정한 것: 카메라가 여덟을 넘으면 그 뒤는 주 카메라의 것이다. 메타 없는 파일은 스캔이 등록하지 않는다(시험은 `createMissingMeta`).
6. ~~**컴포넌트 갈래 나누기**~~ → 완료 2026-10-30 · `ComponentCategory::UiLayout`("UiLayout"), 로컬라이징 키 `component_category.UiLayout`(`UI 배치` / `UI Layout`, `LocalizationKeys.h` 의
   `ComponentCategoryUiLayout`). `Layout2D`·`ContentSizeFitter2D`·`ListView2D`·`SafeArea2D`·`AspectRatioFitter2D` 가 간다.
   - **잰 것**: `UserScenario` 시나리오 D - `UI` 열일곱·`UiLayout` 다섯이 모두 붙일 수 있다. `EditorLocalization`(두 언어의 키가 맞고 헤더가 선언한다)·`BuiltinComponentProperty` 통과.
   - **실제 에디터**(`JBroEditorHost.exe`, `UI Game` 프로젝트): `컴포넌트 추가` 에 `UI 배치` 갈래 다섯(붙은 `Layout2D` 는 회색)과 `UI` 열일곱이 서고, 갈래 차례는 이름 순으로 처음 만난 차례라
     `UI 배치` → `오디오` → `UI` 다(스크린숏을 봤다). 갈래 차례를 정하는 것은 열림.
- **전체**(2026-10-30, 5 부 뒤, 이 PC 의 main 폴더, Debug, 기준 main 72a4aaf0): 솔루션 빌드 C++ 경고 0, 전체 시험 127 묶음 `all tests passed.`(`EditorApplication` 1504.9 초,
  `UserScenario` 99.5 초, `PostProcessPixel` 15.1 초).
- 5 부 끝(2026-10-30). 남긴 것과 까닭은 D-361 (7) - 3D 의 화면 레이어, 렌더 텍스처, 둘째 뷰의 지우기, 실제 IME 실측, 여러 줄 선택 조각, 버튼 소리 재생. 더한 열림: 갈래 차례.

### 7.8 6 부 - 남긴 다섯 (D-362)

> 상태: **진행** (2026-10-31 시작). 사용자 지시 "1~5까지 작업 진행. 1번은 제안대로. 모듈을 뗌." 다섯은 3D 의 화면 레이어(새 모듈 `JBroScreenUi`), 렌더 텍스처, 둘째 뷰의 지우기,
> 실제 IME 실측, 여러 줄 선택의 줄마다 조각이다. 차례는 작은 것부터 - 둘째 뷰 → 여러 줄 선택 → IME → 렌더 텍스처 → 3D 화면 레이어. 단위마다 커밋한다.

1. ~~**둘째 뷰의 지우기**~~ → 완료 2026-10-31 · `Camera2D.clearViewport`(기본 참, 핸들 `GetClearViewport`·`SetClearViewport`), `RenderCamera2D.clearViewport`, 렌더 브리지의 `BuildViewFill`(뷰 행렬의
   역행렬로 뷰 공간 [-반폭, 반폭] x [-반높이, 반높이] 를 월드로) - `drawCamera` 가 첫 뷰가 아니면 라이트보다 먼저 낸다.
   - **잰 것**: `SpriteDrawMode` 분할 화면 픽셀 시험 - 오른쪽 카메라의 지우기 색이 초록이면 오른쪽 빈 곳이 (0, 128, 0)(노출 -2.2 를 받아 절반), 왼쪽은 검정·빨강 51 그대로, 오른쪽 파랑 사각형은
     26 그대로(칠한 사각형이 빛을 받지 않는다), 화면 레이어 흰색 그대로, `clearViewport` 를 끄면 오른쪽 빈 곳은 앞 뷰의 검정. `BuiltinComponentProperty`(카메라 13)·`CameraView2D`·`Framework2DSystem`·
     `UiScenario`·`CanvasFile`·`ScriptApiCatalog` 통과.
   - 정한 것: 칠하기는 카메라의 담는 영역이다 - `PixelPerfect` 의 레터박스 띠는 앞 뷰의 것이 남는다(첫 뷰처럼 띠까지 칠하려면 렌더러의 사각형 지우기가 든다, 열림).
2. ~~**여러 줄 선택의 줄마다 조각**~~ → 완료 2026-10-31 · `Text::RangeRect`·`TextLayout::GetRangeRects`(줄마다 [max(begin, 줄 시작), min(end, 줄 끝)) 의 첫 글자 왼쪽 ~ 마지막 글자 끝, 높이는 줄 높이;
   개행만 든 줄·폭 0 은 없음; `capacity` 로 자름), `TextBlock::GetRangeRects`, `Text2D.highlightBegin`·`highlightEnd`·`highlightColor`(기본 반투명 파랑, 핸들 `GetHighlightBegin`·`GetHighlightEnd`·
   `SetHighlight(begin, end)`·`GetHighlightColor`·`SetHighlightColor`). Text2DSystem 은 코드포인트 번호를 `text` 의 바이트로 옮겨(이어지는 바이트를 건너 셈) 줄마다(최대 64) 흰 스프라이트를 글리프보다 먼저 낸다.
   글자 칸은 `selection` 이 비면 선택 구간을 `display` 의 강조 범위로 쓴다(조합 중·포커스 없음이면 0).
   - **잰 것**: `TextLayout` `TestRangeRectsFollowTheLines` - 한 줄 전체는 0 ~ 1407·줄 높이, 공백 하나는 그 전진 폭, 빈·뒤집힌 구간 없음, 끝 너머는 글자 끝, 세 줄은 셋이고 줄 높이만큼 내려가며 용량 2 는 둘,
     개행만은 없고 다음 줄 V 만, 빈 가운데 줄은 없음. `TextRender` `TestHighlightRangesPaintEachLine`(D3D12 64 x 64) - `AA\nAA` 의 [1, 4) 는 윗줄 오른쪽·아랫줄 왼쪽만 파랑(> 20 픽셀)이고 윗줄 왼쪽·
     아랫줄 오른쪽은 4 픽셀 아래(한 사각형이면 생긴다), 글자는 그 위에 검다, 리치 텍스트는 칠하지 않음, 뒤집힌 범위 없음, 범위 밖 번호는 글자 전체로. `UiScenario` - 선택 스프라이트가 있으면 강조 범위는 그대로,
     없으면 Shift+← 둘의 [1, 3) 이 강조 범위이고 접으면 지워진다. `UserScenario`·`BuiltinComponentProperty`(Text2D 26)·`ScriptApiCatalog` 통과.
   - 시험을 쓰다 본 것: 앞 단계가 Esc 로 끊은 조합이 입력 상태에 남아 칸이 "조합 중" 이었다(실제 Windows 에서는 Esc 를 IME 가 먼저 받아 조합을 끝낸다).
3. ~~**실제 IME 실측**~~ → 완료 2026-10-31 · `InputTests` `TestARealImeComposesThroughThePlatform` - 보이는 창에 포커스를 주고 한국어 자판·한글 모드를 켠 뒤 `ImmSetCompositionStringW(SCS_SETSTR, "한")` →
   `CompositionBegin`·`Composition`(U+D55C), `ImmNotifyIME(CPS_COMPLETE)` → `CompositionEnd`·`Text`(U+D55C), `CPS_CANCEL` → 끝만, 인라인 모드(`SetCompositionInline`)도 같다. `UserScenario` 시나리오 E 4b -
   실제 에디터 창에서 같은 IMM 으로 `글` 을 조합하면 칸의 커서 자리에 끼고 저장 글자는 그대로이며 밑줄이 서고(**[봄 e05]**, `Helo한글` 에 주황 밑줄), 확정하면 `Helo한글`·커서 6·밑줄 없음,
   다시 조합하고 취소하면 그대로다.
   - **잰 것**(실제 IME 메시지 차례, 이 PC 의 Microsoft 한국어 IME): 조합 시작은 `WM_IME_COMPOSITION`(lParam 0x1B8 = 조합 글자·속성·커서), 확정은 `WM_IME_ENDCOMPOSITION` **뒤에**
     `WM_IME_COMPOSITION`(0x800 = 결과 글자)이 온다 - 끝 이벤트 뒤에 `Text` 가 와도 입력 상태가 둘 다 받는다(조합은 비고 글자는 들어간다).
   - 찾은 것: 시나리오의 합성 Ctrl+V 가 V 를 뗀 이벤트(Ctrl 붙음)만 넣어 입력 상태에 Ctrl 이 남았고, 그 뒤 실제 확정 글자가 단축키로 읽혀 버려졌다 - 실제 키보드는 Ctrl 을 떼는 이벤트가
     오므로 시험이 Ctrl 뗌을 더 넣었다(엔진은 고치지 않았다).
   - 열림: 후보 창(한자 변환 따위)의 모양과 자리는 사람이 봐야 한다 - `SetTextInputArea` 가 주는 자리는 시험이 숫자로 본다(D-355).


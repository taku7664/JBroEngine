# 캔버스 뷰포트 목록 계획 (D-438)

카메라가 화면 배치를 알던 것(`primary`·뷰포트 사각형·`depth`·`clearViewport`·렌더 텍스처)을 캔버스의 **뷰포트 목록**으로 옮긴다.
카메라는 "월드 속의 눈" 만 남는다. 2026-11-21 사용자 결정 "뷰포트 목록으로" 와 네 갈림길의 답(§3)을 따른다.

## 1. 왜 바꾸나

- **3D 는 `primary` 를 켠 카메라가 없으면 아무것도 그리지 않았다**(`Camera3DSystem`). 카메라를 붙여도 기본값이 꺼짐이라
  시뮬레이션 뷰가 "카메라가 없습니다" 라고 말한다. 2D 는 같은 일을 D-187 에서 "첫 활성 카메라로 떨어진다" 로 막았다.
- `primary` 는 2026-09-02 첫 골격 커밋(`79ac5413`)부터 `bool primary = false;` 로 있었고, 넣은 까닭을 적은 결정은 없다.
  처음 뜻은 "그릴 카메라" 였는데 D-359(분할 화면)에서 켜진 카메라가 **모두** 그리게 되며 그 뜻은 사라졌다.
  남은 쓰임은 화면 레이어의 레터박스 기준(`ApplyCameraArea`)·뷰포트 밖 포인터·스크립트 UI 의 화면↔월드·오디오 리스너가 없을 때의 자리뿐이다.
- D-359·D-362 가 뷰포트 사각형·차례·렌더 텍스처를 **카메라 컴포넌트에** 넣었다. 기존 엔진은 일부러 반대로 했다(§2).

## 2. 기존 엔진 (`taku7664/JBroEngine_old`, 리포에서 직접 읽음)

로컬 소스(`C:\Users\박주형\source\repos\JBroEngine`)는 지워졌다. 리포의 파일:

- `Engine/GameFramework/Component/Camera2D.h` - `Camera2D` 의 필드는 투영·`OrthographicSize`·FOV·near/far 뿐이다. 주석: 화면 어디에 그릴지·무엇을 그릴지·
  어떤 순서로 그릴지는 캔버스의 뷰포트 목록이 정하고 바탕색은 캔버스 속성이다 - "월드의 오브젝트가 화면 배치까지 아는 반쪽 신이 되지 않게".
- `Engine/GameFramework/Canvas/CanvasViewport.h` - `CanvasViewport` = 이름 + 카메라 오브젝트 Guid(비면 첫 활성 카메라) + 출력 사각형(`Layout2D`, 비율 + 픽셀) +
  레이어 필터(레이어 Guid 목록, 비면 전체) + `Active`. 보통 게임은 기본 뷰포트 하나(전체 화면, 카메라 자동)라 사용자는 볼 일이 없다. 분할 화면은 좌우 둘과
  UI 레이어만 그리는 전체 화면 하나, 미니맵은 작은 사각형 + 월드 레이어만.
- `Application/Editor/Main/Inspector/InspectorTool.cpp` `DrawCanvasInspector` → `DrawViewportProperties` - 캔버스 인스펙터가 배경색과 뷰포트 목록을 보인다.
  카메라는 `자동` 또는 캔버스의 카메라를 고르는 콤보, 레이어 필터는 레이어마다 체크. 카메라를 못 찾으면, 또는 자동인데 활성 카메라가 둘 이상이면 경고 한 줄.
- `Application/Editor/Command/EditorCanvasCommands.h` - `CSetViewportPropertyCommand`·`CCreateViewportCommand` 등 커맨드.

## 3. 사용자와 정한 것 (2026-11-21)

| 갈림길 | 답 |
|---|---|
| 스크립트 API | **뷰포트 서비스를 더한다.** 카메라 핸들의 `primary`·뷰포트·`depth`·`clearViewport`·렌더 텍스처 멤버는 지운다 |
| 3D 범위 | **3D 도 여러 뷰까지.** 뷰포트마다 제 사각형에 그린다 |
| 레이어 필터 | **이번에 넣는다** |
| 바탕색 | **카메라의 `clearColor` 그대로.** "제 사각형을 칠할지"(`clearViewport`)만 뷰포트로 간다 |

## 4. 설계

### 4.1 자료 - `JBroCanvas` 의 `CanvasViewport`

차원과 무관하므로 캔버스 모듈에 둔다(`<JBro/Canvas/Viewport.h>`). 캔버스가 목록을 소유한다(레이어·배경색과 같은 자리).

| 칸 | 뜻 |
|---|---|
| `name` | 인스펙터 머리와 스크립트의 `Find` 가 쓰는 이름 |
| `active` | 끄면 그리지 않는다 |
| `camera` | 카메라 오브젝트의 번호(`InstanceId`). 비면 **자동** - 그 차원의 첫 활성 카메라(캔버스 차례) |
| `origin`·`size` | 대상 안의 사각형. 왼쪽 위 원점, 0..1 비율(D-359 의 값 그대로) |
| `layers` | 그릴 레이어(`LayerId`). 비면 모든 레이어 |
| `clearViewport` | 앞 뷰가 그린 대상에서 제 사각형을 카메라의 `clearColor` 로 먼저 칠한다(D-362 의 값 그대로) |
| `renderTextureId` | 있으면 화면 대신 그 렌더 텍스처(`.jrt`)에 그린다. 풀린 핸들 `renderTexture` 는 파일에 적지 않는다 |

- **목록 차례가 그리는 차례다**(카메라의 `depth` 를 대신한다). 렌더 텍스처 뷰포트가 먼저인 것은 그대로다(D-362).
- **새 캔버스는 뷰포트 하나**(`Main`, 전체 화면, 카메라 자동)로 시작한다. 목록이 비면 월드를 그리지 않는다 - 인스펙터가 경고한다. 화면 레이어는 여전히 그린다.
- **주 카메라**는 "화면에 그리는 첫 켜진 뷰포트의 카메라" 다. 화면 레이어의 레터박스 기준·뷰포트 밖 포인터·스크립트 UI 의 화면↔월드·리스너가 없을 때의 오디오 자리가 이것을 쓴다.
- 카메라 번호가 가리키는 오브젝트가 없거나 그 차원의 켜진 카메라가 아니면 그 뷰포트는 그리지 않는다(자동으로 떨어지지 않는다 - 엉뚱한 카메라로 그리면 틀린 것을 알 길이 없다).
  인스펙터가 경고한다. 되돌리기는 뗀 오브젝트를 같은 번호로 되살리므로(D-302) 지웠다 되돌리면 다시 잇는다.
- 레이어 필터는 **월드 레이어**에 건다. 화면 레이어는 지금처럼 대상 전체에 한 번 그린다(D-359) - 기존 엔진의 "UI 만 그리는 뷰포트" 는 우리에게 화면 레이어다.
  렌더 쪽은 레이어 차례 0~255 의 비트 마스크다(렌더러의 묶음 상한 `maxLayerGroups` 256 과 같다). 그 밖의 차례는 늘 그린다.

### 4.2 파일 (`.jcanvas`)

`BackgroundColor` 다음에 `Viewports` 차례. 카메라는 오브젝트 참조처럼 **파일 안 차례**(-1 은 자동), 레이어는 파일의 레이어 `Id` 로 적는다.
`Viewports` 키가 없는 옛 파일은 **카메라에 적혀 있던 값으로 목록을 만든다**(`MigrateLegacyViewports`). 그린 그림이 바뀌지 않게 그때의 규칙을 따른다:
- 2D 는 켜진 카메라가 모두 제 사각형에 `depth` 차례로 그렸다(D-359) - 카메라마다 뷰포트 하나를 `depth` 차례(같으면 파일 차례)로 세우고 `viewportOrigin`·`viewportSize`·`clearViewport`·`renderTextureId` 를 옮긴다.
  그린 차례를 지키느라 `primary` 카메라를 앞으로 당기지 않는다 - 주 카메라가 바뀔 수 있는 것은 화면 레이어의 레터박스 기준뿐이다.
- 3D 는 `primary` 를 켠 첫 카메라 하나만 그렸다 - 그 카메라의 뷰포트 하나다. 없으면 기본(자동)이라 이제는 그려진다.
- 2D 카메라가 하나뿐이고 전체 화면·렌더 텍스처 없음이면 자동 하나로 접는다. 카메라가 없으면 기본 하나다.

옛 카메라 칸은 컴포넌트에 읽지 않고 건너뛴다(`ReadFields` 의 카메라별 건너뛸 키). 코드에서 지운 칸은 원래 "엔진이 모르는 칸" 으로 파일을 거절하는데(`ReadReflectedFields`),
그러면 카메라가 든 옛 캔버스가 모두 열리지 않는다. 버전은 올리지 않는다(키가 없으면 옮긴다). `.jlayer` 의 카메라는 옮기지 않는다(§6).

### 4.3 2D

- `Camera2D` 에서 `primary`·`viewportOrigin`·`viewportSize`·`depth`·`clearViewport`·`renderTextureId`·`renderTexture` 를 뺀다.
- `Camera2DSystem` 이 목록을 돌며 `RenderCamera2D`(이제 "뷰 하나") 를 채운다. 사각형·지우기·렌더 텍스처·레이어 마스크는 뷰포트에서, 투영·지우기 색·후처리는 카메라에서.
  자동은 **그릴 수 있는** 첫 켜진 카메라다 - 값이 잘못된 카메라(크기 0 등)는 D-239 처럼 건너뛰고 센다. 정한 카메라가 그릴 수 없으면 그 뷰포트는 그리지 않고 센다.
- 브리지(`SubmitRenderWorld2D`)는 그대로 뷰마다 그리고, `SpriteFilterRule` 에 레이어 마스크를 더한다. 포인터의 카메라는 그 픽셀을 담은 **목록에서 가장 뒤의** 화면 뷰포트다.
- 렌더 텍스처 핸들은 `BindCanvasAssets` 가 뷰포트의 `renderTextureId` 로 푼다.

### 4.4 3D

- `Camera3D` 에서 `primary` 를 뺀다. `RenderWorld3D` 가 뷰 목록을 든다(2D 와 같은 모양: 사각형·지우기·레이어 마스크).
- `SubmitRenderWorld3D` 가 뷰마다 `SubmitLayerViews` 를 부른다. 투영의 가로세로비는 그 사각형의 것이다. 둘째 뷰부터 `clearViewport` 면 그 사각형을
  카메라의 지우기 색으로 먼저 칠한다(렌더러는 대상을 첫 뷰에서만 지운다). 깊이는 뷰마다 지우므로(D-280) 겹치는 뷰도 서로의 깊이를 보지 않는다.
- 후처리는 뷰마다 그 카메라의 것이다. 3D 렌더 텍스처는 그것을 보일 3D 쪽 소비자(머티리얼의 텍스처 칸)가 없어 `[열림]` 이다 - 뷰포트에 적혀 있으면 그 뷰는 그리지 않고 경고한다.

### 4.5 에디터

- 캔버스 인스펙터(캔버스를 고르면 서는 화면, D-186): 배경색 아래 **뷰포트** 절. 뷰포트마다 접는 머리(이름)와 `사용`·`카메라`(자동 + 캔버스의 카메라 오브젝트)·`위치`·`크기`·
  `칠하기`(clearViewport)·`렌더 텍스처`·`레이어`(레이어마다 체크, 모두 켜면 비운다). 머리 오른쪽 메뉴에 위로·아래로·삭제, 절 끝에 `뷰포트 추가`.
- 경고: 카메라를 못 찾음 / 자동인데 켜진 카메라가 둘 이상 / 목록이 비어 월드를 그리지 않음.
- 편집은 커맨드 하나(`SetCanvasViewportsCommand`) - 목록 전체를 앞뒤로 뜬다. 같은 칸을 잇달아 끄는 것은 합친다(배경색 커맨드와 같다).
- 시뮬레이션 뷰의 "`primary` 카메라가 없어 첫 번째로 그린다" 경고는 위의 "자동인데 둘 이상" 으로 바뀐다.

### 4.6 스크립트 - `ViewportService`

공통 서비스(`GetServiceContext().Viewports`, `JBroRuntime`)와 시스템 인터페이스 `IViewportSystem`(`SystemContext::Viewports`). 구현은 캔버스 모듈의
`CanvasViewportSystem`(오브젝트 서비스의 `CanvasObjectSystem` 과 같은 자리). 차원과 무관하다.

`GetCount`·`Find(name)`·`IsActive`/`SetActive`·`GetOrigin`/`SetOrigin`·`GetSize`/`SetSize`·`GetCamera`/`SetCamera(GameObject)`·`SetAutoCamera`·
`GetClearViewport`/`SetClearViewport`·`SetRenderTexture(AssetHandle)`. 번호가 밖이면 경고 없이 아무것도 하지 않고 기본값을 준다. 레이어 필터는 스크립트에서 바꾸지 않는다(`[열림]`).
`ServiceContextAbiVersion`·`SystemContextAbiVersion` 을 올린다 - 옛 스크립트 DLL 은 다시 빌드해야 한다.

## 5. 단계

1. `[완료]` 자료·파일·옮기기(`JBroCanvas`) + 2D 시스템·브리지·레이어 마스크 + 컴포넌트 필드 빼기.
2. `[완료]` 3D 여러 뷰(`RenderWorld3D` 뷰 목록·뷰마다 후처리·`SubmitRenderWorld3D` 의 사각형과 칠하기).
3. `[완료]` 에디터 캔버스 인스펙터의 뷰포트 절과 `SetCanvasViewportsCommand`, 경고, 시뮬레이션 뷰 경고.
4. `[완료]` `ViewportService`·`IViewportSystem`·`CanvasViewportSystem`, 카메라 핸들과 2D API 표에서 지운 멤버.
5. `[완료]` 문서(`ProjectRule.md`·D-438), 솔루션 빌드, 전체 시험, 캔버스 인스펙터 화면 확인(`JBRO_EDITOR_SHOT`). 도면에는 `primary` 가 없어 고치지 않았다.
   실제 에디터는 사용자가 띄워 3D 프로젝트를 만들었다 - 그때 본 3D 캔버스 뷰의 두 증상(오브젝트 고르기·큐브가 비쳐 보임)은 todo-3d "편집" 의 `[열림]` 이고 원인은 아직 보지 않았다.

## 6. `[열림]`

- 3D 렌더 텍스처(§4.4). 지금은 그 뷰포트를 건너뛰고 한 번 경고한다.
- 스크립트의 레이어 필터·렌더 텍스처 바꾸기. 렌더 텍스처는 전에 `Camera2D.SetRenderTexture` 로 됐다 - 서비스가 `AssetHandle` 을 받으려면 런타임이 에셋 타입을 알아야 한다.
- 2D 스크립트 API 표(`JBroScriptApiCatalog2D`)에 `ViewportService` 가 없다(후처리·머티리얼 서비스도 없다). C++ 스크립트는 쓰고, JBroScript 의 자동 완성에는 안 나온다.
- 뷰포트 사각형의 픽셀 단위 칸(기존 `Layout2D` 의 픽셀 성분) - 지금은 비율만이다.
- 화면 레이어를 뷰포트마다 그리기(분할 화면의 플레이어별 HUD) - 지금은 대상 전체에 한 번이다.
- 레이어 필터의 인스펙터 칸은 레이어 차례의 앞 32 개만 고른다(`LayerMaskField`). 렌더 쪽 마스크는 256 차례다.
- `.jlayer`(레이어 에셋)에 든 옛 카메라의 화면 칸은 뷰포트로 옮기지 않는다 - 건너뛰기만 한다.
- 지운 레이어를 가리키는 레이어 필터는 그대로 남는다(없는 번호는 아무것도 그리지 않는다).

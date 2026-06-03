# TODO — 효과 에디터 독윈도우 + Dock 재활성 버그

## Goal

1. 사운드 효과(.jfx) 편집을 인스펙터에서 전용 독윈도우로 이전.
   - AssetBrowser 에서 .jfx 더블클릭 → Root 자식 DockWindow 생성 + 내부 에디터 패널 + Focus.
   - 같은 .jfx 재오픈 시 새 창 안 만들고 기존 창 Focus.
   - 창 닫으면(X) 자동 삭제.
2. "창" 메뉴에서 윈도우 비활성화 후 재활성이 안 되던 버그 수정.

## Changed

### Dock 재활성 버그 (ImDockWindow.cpp)
- 코드를 읽었고: `OnPostBegin` 의 child 도킹 지정(`DockBuilderDockWindow`)이 레이아웃 재빌드 중(`isBeginDockBuild`)에만 실행.
- 어떻게 생각했고: SetVisible(false)→child Begin 생략→imgui 가 dock node 에서 탭 제거. SetVisible(true) 시 재빌드 트리거가 없어 재도킹 안 됨 → 빈 떠다니는 윈도우로 떠서 "안 켜짐".
- 어떤 반례를 찾았고: 매 프레임 강제 DockBuilderDockWindow 는 사용자가 수동으로 옮긴 도킹을 리셋 → 불가. 1프레임 전이만 처리해야.
- 어떻게 고쳤다: child 가 이번 프레임 hidden→visible 전이(`m_bIsVisible.first && first!=second`)면 재도킹 1회. friend 접근, child Update 전 평가 → 정확히 1프레임 작동.

### 효과 에디터 독윈도우
- `EffectEditorWidget.{h,cpp}` 신규 — 효과 UI(Kind 콤보 + Kind별 슬라이더 + 저장)를 인스턴스 상태(멤버 data/dirty)로 보유. static 캐시 아님 → 여러 창 동시 가능.
- `EffectEditorWindow.{h,cpp}` 신규 — `CEffectEditorDockWindow`(CImDockWindow, Root 자식) + `CEffectEditorPanel`(CImCustomWindow, 위젯 보유, OnRenderStay→widget.Draw).
- `CEffectAssetOpenHandler`(AssetHandler) 신규 — .jfx/AudioEffect 더블클릭 → guid별 unique key 로 DockWindow+Panel 생성, SetTargetGuid, Focus. 이미 열려있으면 Focus만(CreateImWindow 중복 key nullptr + FindImWindow).
- 인스펙터에서 효과 에디터 제거(`DrawEffectEditor`/`SaveEffectData`/`EffectParamSpecs` 등 삭제) → 안내 텍스트만. 위젯으로 이전.
- 생명주기: 창 X → m_bIsAlive=false → ImEditor::Update 가 DestroyImWindow 자동. Dock 부모 닫히면 소멸자가 child Destroy.

## Verified

- Application Debug_Editor|x64 빌드 성공.
- 줄끝 정상(autocrlf 경고만).

## Not Verified (런타임 — 사용자 측)

- .jfx 더블클릭 → 독윈도우 뜨고 Kind/슬라이더 편집/저장 round-trip.
- 같은 .jfx 재더블클릭 시 기존 창 Focus(중복 안 뜸).
- 창 X 로 닫으면 삭제, 다시 더블클릭 시 재생성.
- "창" 메뉴 토글 비활성→재활성 정상 복귀(dock 자리 유지).

## Risks

- DockWindow 가 floating 으로 뜸 — 초기 위치/크기만 지정(420x360). 사용자가 도킹 옮기면 유지.
- 효과 에디터 위젯이 디스크 .jfx 직접 read/write(ifstream/ofstream) — 동시에 같은 파일을 다른 경로로 수정하면 마지막 저장 우선(에디터 단일 창이라 실질 무해).

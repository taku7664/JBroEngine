# TODO — Android Package Staging Foundation

## Goal
모바일 패키지 완료의 첫 단계로 Android/iOS 를 뭉개지 않고, Android Debug APK 생성을 향한 Gradle project staging 계약을 추가한다.

## Assumptions
- Android 를 먼저 진행하고 iOS 는 Xcode/signing/MoltenVK 비용 때문에 명확한 unsupported 상태를 유지한다.
- 현재 repo 에 Android NDK native library target 은 없으므로, Android package path 는 `libJBroGame.so` 가 없으면 실패해야 한다.
- `BuildGame.ps1` 은 기존 Windows/Web package contract 처럼 manifest/asset pack 을 먼저 만들고 Android Gradle project 의 assets 로 staging 한다.
- Partial APK 를 성공처럼 내지 않는다.

## Success Criteria
- `BuildGame.ps1 -Platform Android` 가 unsupported 즉시 throw 를 하지 않고 Android package staging 경로로 들어간다.
- Android package output 에 Gradle project skeleton 과 `app/src/main/assets/Content` 가 생성된다.
- Android staging 은 binary manifest/asset pack smoke 를 재사용한다.
- native `.so` 가 없으면 Gradle build 전 명확한 메시지로 실패한다.
- iOS 는 Android 와 섞이지 않고 명확한 iOS-specific unsupported 메시지를 유지한다.

## Plan
- [x] Android Gradle project template 추가
- [x] BuildGame.ps1 Android staging/render helper 추가
- [x] Android native library 후보 검증과 fail-fast 추가
- [x] MobilePlatformPlan / EngineAuditFollowup 갱신
- [x] parser/smoke 검증 및 커밋

## Verification
- [x] `BuildGame.ps1` parser check
- [x] Android staging command reaches native library fail-fast with package skeleton generated
- [x] iOS remains explicit unsupported
- [x] `git diff --check`

## Review
- 코드를 읽었고: `BuildGame.ps1` 은 Android/iOS 를 인식하지만 공통 scene/pack/manifest 단계 전에 mobile unsupported 로 즉시 실패하고 있었다.
- 생각했고: Android 는 Windows/Web 과 같은 pack/manifest 계약을 타야 하고, iOS 는 Xcode signing/MoltenVK 결정 전까지 Android 와 섞으면 안 된다고 판단했다.
- 반례를 찾았고: native `.so` 없이 Gradle project 만 만들고 성공 처리하면 partial package 를 완성 산출물로 오해하게 된다.
- 고쳤다: Android 는 package staging 을 수행하되 `libJBroGame.so` 가 없으면 Gradle 실행 전에 명확히 실패하고, generated Gradle project 경로와 native library 후보 경로를 출력하게 했다.
- 반례를 찾았고: Android asset staging 이 root `Content` 와 Gradle `assets/Content` 중 하나만 검증하면 package drift 를 놓칠 수 있다.
- 고쳤다: root package 와 Gradle asset path 모두 binary manifest/asset pack 을 검증하고 loose `Assets`, editor artifacts, `GameScript.dll` 을 금지했다.
- 코드를 읽었고: 실제 사용자 프로젝트 경로는 현재 이 세션에서 존재하지 않고, repo 안에는 `SampleProject/Project.Jproject` 가 있었다.
- 생각했고: 사용자의 실제 프로젝트가 없다는 이유로 검증을 생략하지 말고, repo sample 로 package staging 계약을 확인해야 한다고 판단했다.
- 반례를 찾았고: package folder name 은 프로젝트 폴더명이 아니라 `.Jproject` 의 `Build.ProductName` 기반이라 `SampleProject-Android-Debug` 로 보면 잘못된 실패가 난다.
- 고쳤다: smoke assertion 을 실제 생성 폴더 `Project-Android-Debug` 기준으로 다시 확인했고 root `Content` 와 Gradle `assets/Content` 의 manifest/pack 존재를 검증했다.
- 검증했다: PowerShell parser, `git diff --check`, Android native library fail-fast smoke, iOS unsupported smoke 를 통과했다.

---

# TODO — Move Camera Culling Debug To Inspector

## Goal
카메라 컬링 통계 표시는 GameView/SceneView 오버레이가 아니라 선택된 Camera2D Inspector 에 표시하고, 메인카메라/오버레이 흔적을 제거한다.

## Assumptions
- 프로젝트 Debug Mode 는 범용 진단 표시 토글이며 camera culling 전용 설명을 쓰지 않는다.
- Camera2D 별 culling count 는 GameView camera stack 렌더 결과를 camera owner object key 로 저장해 Inspector 에서 조회한다.
- 엔진에는 main camera 개념이 없으므로 `IsMainCamera` 필드는 제거한다.

## Success Criteria
- GameView/SceneView 에 culling overlay 가 표시되지 않는다.
- Project Settings Debug Mode 설명은 tooltip 만 사용하고 범용 문구로 바뀐다.
- Camera2D Inspector 에 Debug Mode enabled 시 해당 카메라 culling stats 가 표시된다.
- `IsMainCamera` property/field/localization 이 제거된다.
- SDK mirror 가 관련 public header 변경을 반영한다.

## Plan
- [x] 기존 overlay/debug string 흔적 제거
- [x] camera owner 별 culling stats 저장 경로 추가
- [x] Inspector Camera2D debug section 추가
- [x] main camera 필드/등록/로컬라이징 제거
- [x] SDK mirror 갱신
- [x] 검증
- [x] 커밋

## Verification
- [x] `Engine` target `Debug_Editor|x64`
- [x] `Application:ClCompile` target `Debug_Editor|x64`
- [x] `git diff --check`

## Review
- 코드를 읽었고: GameView/SceneView 오버레이가 프로젝트 Debug Mode 로 culling stats 를 직접 표시하고 있었다.
- 생각했고: 디버그 정보는 화면 결과를 덮는 오버레이가 아니라 선택된 컴포넌트의 Inspector 진단 섹션으로 가야 한다.
- 고쳤다: GameView/SceneView overlay helper/string 사용처를 제거하고, Camera2D Inspector 에 debug section 을 추가했다.
- 반례를 찾았고: 마지막 renderer stats 는 여러 카메라 중 마지막 카메라 값만 보여주므로 선택된 카메라의 값이라고 볼 수 없다.
- 고쳤다: `GameRenderCameraDesc` 에 owner object key 를 싣고, `RenderGameCameraStack` 이 카메라별 `RenderCullingStats` 를 반환해 `ImEditor` 가 owner key 로 저장하게 했다.
- 코드를 읽었고: `Camera2D::IsMainCamera`, reflection property, localization, debug frustum 색 분기에 main camera 개념이 남아 있었다.
- 고쳤다: main camera field/property/localization/debug color branch 를 제거했다.
- 반례를 찾았고: `SafePtr` 는 bool 과 직접 비교하면 컴파일 에러가 나므로 기존 스타일대로 `IsValid()` 를 써야 한다.
- 고쳤다: Inspector 의 `Editor::ImEditor` 확인을 `IsValid()` 로 바꿨다.
- 검증했다: `Engine` target `Debug_Editor|x64`, `Application:ClCompile` target `Debug_Editor|x64`, `git diff --check` 를 통과했다.
- 주의: 전체 `Application` target 링크는 실행 중인 `Build\Debug_Editor\Application.exe` 잠김으로 `LNK1168` 이 발생했다.

---

# TODO — Camera Sprite Culling Debug Stats

## Goal
2D 카메라별 sprite camera culling 을 renderer 단계에 추가하고, 프로젝트 세팅의 debug mode 에서 camera culling count 를 확인할 수 있게 한다.

## Assumptions
- 카메라 view 는 renderer pass 직전에만 확정되므로 `SpriteRenderSystem` 이 아니라 `Forward2DRenderer` 에서 culling 한다.
- 1차 컬링 대상은 `RenderItem` 의 transform 된 quad bounds 이며, sprite opaque bounds 기반 축소는 다음 단계에서 `RenderItem` 에 bounds metadata 를 싣는 구조가 필요하면 확장한다.
- Debug mode 는 `.Jproject` 에 저장되는 프로젝트 설정으로 둔다.
- 디버그 표시는 에디터 GameView/SceneView 에서 확인 가능해야 하며, 런타임 game build 의 화면 오염은 피한다.

## Success Criteria
- 카메라 view 밖 sprite `RenderItem` 은 draw/batch 에 들어가지 않는다.
- renderer 가 마지막 render pass 의 submitted/drawn/culled count 를 보관한다.
- ProjectSettings 에 Debug category 또는 Debug option 이 추가되고 `.Jproject` 에 저장/로드된다.
- Debug mode enabled 시 GameView/SceneView overlay 에 camera culling count 가 표시된다.
- SDK public mirror 가 필요한 타입 변경을 반영한다.

## Plan
- [x] renderer view culling 위치/API 확인
- [x] RenderItem camera bounds culling 및 stats 추가
- [x] project debug mode 저장/로드/UI 추가
- [x] editor overlay 에 culling stats 표시
- [x] SDK mirror 갱신
- [x] 검증
- [x] 커밋

## Verification
- [x] `Engine` target `Debug_Editor|x64`
- [x] `Application:ClCompile` target `Debug_Editor|x64`
- [x] `Application` target `Debug_Editor|x64`
- [x] `git diff --check`

## Review
- 코드를 읽었고: `SpriteRenderSystem` 은 모든 sprite `RenderItem` 을 제출하지만 카메라 정보를 모르고, 카메라 half extent/rotation 은 `Forward2DRenderer::SetViewCamera*` 이후 렌더 패스에서 확정된다.
- 생각했고: 실제 camera culling 은 제출 단계가 아니라 렌더러의 camera view transform 직전에서 수행해야, SceneView/GameView/빌드 런타임이 같은 렌더 경로를 탄다.
- 반례를 찾았고: 배치 루프 안에서 다음 아이템의 submitted/culled 를 먼저 세면, 배치가 끊긴 아이템이 다음 루프에서 다시 카운트되어 통계가 부풀 수 있다.
- 고쳤다: 카운트는 실제로 소비한 현재 아이템과 성공한 batch 범위에만 더하고, 컬링된 아이템은 current item 이 되었을 때만 기록한다.
- 반례를 찾았고: SceneView focus overlay 의 `RenderFiltered` 나 GameView render pass 가 renderer 의 last stats 를 덮어써서 SceneView 오버레이가 다른 뷰의 통계를 보여줄 수 있다.
- 고쳤다: `ImEditor` 가 SceneView/GameView 렌더 직후 각각의 `RenderCullingStats` 를 저장하고, UI 오버레이는 그 저장값만 읽게 했다.
- 반례를 찾았고: 렌더 요청이 없는 프레임에 이전 통계가 남으면 디버그 오버레이가 stale 값을 보여준다.
- 고쳤다: SceneView/GameView render target 요청이 없거나 생성 실패한 프레임에는 해당 통계를 0으로 초기화한다.
- 코드를 읽었고: ProjectSettings 는 `ProjectManager` 에 편집값을 반영한 뒤 `SaveProject()` 로 YAML `.Jproject` 를 저장한다.
- 고쳤다: `ProjectInfo::DebugModeEnabled` 를 저장/로드하고, ProjectSettings 의 Debug 카테고리에서 Apply 시 저장되게 했다.
- 검증했다: `Engine` target `Debug_Editor|x64`, `Application:ClCompile` target `Debug_Editor|x64`, `Application` target `Debug_Editor|x64`, `git diff --check` 를 통과했다.

---

# TODO — Sprite Opaque Bounds Import Cache

## Goal
Sprite import/load 시 frame별 opaque bounds 와 sprite 전체 maximum bounds 를 계산해 `CSpriteAsset` 에 캐싱하고, SceneView 피킹의 legacy per-pixel/frame alpha bounds scan cache 를 새 asset metadata 기반으로 교체한다.

## Assumptions
- Sprite pixels 는 RGBA8 로 로드/쿠킹된다.
- Alpha opaque 기준은 현재 SceneView legacy 기준과 호환되도록 alpha > 0 을 기본으로 둔다.
- Bounds 는 pixel rect 와 PPU/pivot 적용 local rect 를 모두 보관한다.
- 완전 투명 frame 은 `HasOpaquePixels=false` 로 표시하고, 에디터 선택/box fallback 은 기존처럼 전체 frame bounds 를 사용할 수 있어야 한다.

## Success Criteria
- 각 `SpriteFrame` 이 `OpaqueBoundsPixels`, `LocalBounds`, `LocalOpaqueBounds`, `HasOpaquePixels` 를 가진다.
- `CSpriteAsset` 이 `MaximumLocalBounds`, `MaximumLocalOpaqueBounds`, opaque 존재 여부를 제공한다.
- sprite load/reload/import option 변경 시 bounds 가 재계산된다.
- SceneView 피킹은 더 이상 frame 전체 alpha bounds 를 자체 scan/cache 하지 않고 `SpriteFrame` cached bounds 를 사용한다.
- SDK public mirror 도 동기화된다.

## Plan
- [x] SpriteFrame bounds 구조/API 추가
- [x] SpriteAsset frame build 후 pixel/local opaque bounds 계산
- [x] SceneView legacy alpha bounds cache 제거 및 asset cached bounds 사용
- [x] SDK mirror 갱신
- [x] 빌드/검증 및 커밋

## Verification
- [x] `Engine` target `Debug_Editor|x64` build
- [x] `Application:ClCompile` target `Debug_Editor|x64`
- [x] `git diff --check`

## Review
- 코드를 읽었고: `CSpriteAsset` 는 frame slice 정보만 저장하고 opaque bounds 는 저장하지 않았다.
- 생각했고: 매 프레임/매 박스선택마다 alpha scan 을 반복하는 것보다, sprite load/import option 적용 시 frame별 bounds 를 한 번 계산해 asset metadata 로 들고 있는 편이 맞다고 판단했다.
- 반례를 찾았고: `Rect` 는 `Top <= Bottom` 규칙인데 sprite local 좌표는 y-up 이라 위/아래를 그대로 넣으면 empty bounds 가 된다.
- 고쳤다: frame local bounds 는 `Rect(left, lowerY, right, upperY)` 규칙으로 저장한다.
- 반례를 찾았고: 완전 투명 frame 은 opaque rect 가 없으므로 이를 0 크기 rect 로 저장하면 selection fallback 과 렌더 제외 정책이 섞일 수 있다.
- 고쳤다: `HasOpaquePixels` 와 `OpaqueBoundsPixels` 를 분리하고, `LocalOpaqueBounds` 는 opaque 가 있을 때만 유효하게 했다.
- 반례를 찾았고: `OpaqueX/OpaqueY/...` 식의 흩어진 필드는 이후 API 계약이 불명확해진다.
- 고쳤다: `SpritePixelBounds OpaqueBoundsPixels` 구조체로 frame-local pixel bounds 를 저장한다.
- 코드를 읽었고: SceneView `PickBox()` 는 editor-only 전역 cache 와 frame 전체 alpha scan 을 자체 구현하고 있었다.
- 생각했고: 이제 asset 이 authoritative bounds 를 가지므로 SceneView cache 는 중복 레거시가 된다.
- 고쳤다: `CachedAlphaBounds`, `g_alphaBoundsCache`, `TryGetAlphaBounds()` 를 제거하고 `SpriteFrame::LocalOpaqueBounds` 를 사용하게 했다.
- 주의: 단일 클릭 `Pick()` 의 1-pixel alpha test 는 비용이 작고 클릭 정확도를 위한 별도 기능이라 유지했다.
- 검증했다: `Engine` target `Debug_Editor|x64`, `Application:ClCompile` target `Debug_Editor|x64`, `git diff --check` 를 통과했다.
- 주의: 전체 `Debug_Editor|x64` build 는 `Build\Debug_Editor\Application.exe` 가 실행 중/잠김 상태라 `LNK1104` 로 링크만 실패했다.

---

# TODO — Rebuild yaml-cpp Libraries With Matching PDBs

## Goal
repo 루트 `yaml-cpp-master` 소스를 기준으로 yaml-cpp header/source/lib/pdb를 같은 버전으로 맞추고, Debug/Release Windows link 에서 matching PDB 를 사용할 수 있게 한다.

## Assumptions
- 현재 `Engine/ThirdParty/lib/yaml-cppd.lib` 는 `yaml-cppd.pdb` 를 참조하지만 PDB 가 없다.
- `yaml-cpp-master` 가 사용자가 제공한 새 기준 소스다.
- 엔진 include 경로는 `Engine/ThirdParty/yaml-cpp/yaml.h` 형태를 기대하므로 `yaml-cpp-master/include/yaml-cpp/*` 를 그 위치로 동기화해야 한다.
- yaml-cpp debug/release lib 는 엔진과 같은 MSVC runtime 계열(`/MDd`, `/MD`)로 빌드한다.

## Success Criteria
- `Engine/ThirdParty/lib/yaml-cppd.lib` 와 `yaml-cppd.pdb` 가 같은 build output 에서 나온다.
- `Engine/ThirdParty/lib/yaml-cpp.lib` 와 release PDB 가 필요 시 함께 보관된다.
- `Engine/ThirdParty/yaml-cpp` header/source 가 새 lib 와 같은 `yaml-cpp-master` 기준으로 동기화된다.
- Debug link 에서 yaml-cpp PDB `LNK4099` 가 사라진다.

## Plan
- [x] 기존/신규 yaml-cpp 구조와 빌드 설정 확인
- [x] yaml-cpp Debug/Release static library 빌드
- [x] Engine ThirdParty yaml-cpp header/source/lib/pdb 동기화
- [x] 엔진/툴 빌드로 LNK4099 제거 확인
- [ ] 문서/todo 갱신 및 커밋

## Verification
- [x] `BuildManifestTool Debug_Game|x64` build without yaml-cpp LNK4099
- [x] `BuildManifestTool Release_Game|x64` build
- [x] `Debug_Game|x64` 또는 relevant link check
- [x] `git diff --check`

## Review
- 코드를 읽었고: 기존 `Engine/ThirdParty/lib/yaml-cppd.lib` 는 내부에 `yaml-cppd.pdb` 참조를 가지고 있었지만 matching PDB 파일이 repo/vendor 위치에 없었다.
- 생각했고: PDB만 따로 만들면 header/source/lib 버전이 다시 갈라질 수 있으므로, 사용자가 둔 `yaml-cpp-master` 를 기준으로 header/source/static lib/PDB 를 한 번에 동기화해야 한다고 판단했다.
- 반례를 찾았고: 새 yaml-cpp 는 `fptostring.cpp` 가 추가된 소스 세트라 Windows prebuilt lib만 교체하면 Web 소스 빌드가 빠진 컴파일 단위를 놓칠 수 있었다.
- 고쳤다: `Engine/ThirdParty/yaml-cpp` header/source 를 `yaml-cpp-master` 기준으로 갱신하고, `web_game_sources.txt` 에 `fptostring.cpp` 를 추가했다.
- 반례를 찾았고: CMake 기본 출력 PDB를 build temp 폴더에 두면 lib가 삭제 가능한 temp PDB 경로를 참조한다.
- 고쳤다: yaml-cpp Debug/RelWithDebInfo static lib 를 `Engine/ThirdParty/lib` 로 직접 출력해 `yaml-cppd.lib` -> `yaml-cppd.pdb`, `yaml-cpp.lib` -> `yaml-cpp.pdb` 경로가 vendor 위치를 가리키게 했다.
- 반례를 찾았고: SDK `Lib` 는 gitignore 대상이라 SDK PDB를 직접 커밋하는 방식은 유지보수성이 낮다.
- 고쳤다: `StageSDK.targets` 가 yaml-cpp lib 와 함께 matching PDB 도 SDK 로 스테이징하도록 추가했다. 로컬 SDK 복사본도 최신 vendor 산출물로 갱신했다.
- 반례를 찾았고: `Engine.lib` object 가 구버전 yaml-cpp header ABI 를 들고 있으면 새 yaml-cpp lib 와 `insert_map_pair` 심볼이 불일치한다.
- 고쳤다: 솔루션 `Engine:Rebuild` 로 Debug_Game/Release_Game Engine.lib 를 새 header 기준으로 재생성했고, 직접 project build 를 막던 `Engine/...` 접두 include 를 프로젝트 include-root 규칙에 맞게 정리했다.
- 검증했다: `Engine:Rebuild Debug_Game|x64`, `Engine:Rebuild Release_Game|x64`, `BuildManifestTool Debug_Game|x64`, `BuildManifestTool Release_Game|x64`, `git diff --check` 를 실행했다. Debug tool link 에서 yaml-cpp `LNK4099`/unresolved 가 사라졌다.
- 주의: Release tool link 는 yaml-cpp 문제가 아닌 root `x64\Release_Game\Engine.pdb` 타입 레코드 `LNK4020` 경고가 남는다. 링크는 성공하며, 별도 Engine PDB 생성 정책 이슈로 분리한다.

---

# TODO — Release Package Smoke Tests

## Goal
Windows/Web release package 생성 후 manifest/package 계약을 더 강하게 검증하고, yaml-cpp PDB `LNK4099` 경고 제거 방안을 정리한다.

## Assumptions
- smoke 강화는 packaging script에서 자동으로 실패해야 의미가 있다.
- binary manifest의 hash/startup GUID 검증은 engine loader를 쓰는 `BuildManifestTool`에 맡기는 것이 가장 정확하다.
- yaml-cpp PDB 경고는 이번에 임의 설정 변경하지 않고, 원인과 선택지를 제시한다.
- 기존 dirty build artifacts는 커밋하지 않는다.

## Success Criteria
- `BuildGame.ps1`가 생성된 `build_manifest.jbmanifest`를 tool validate 모드로 다시 읽어 검증한다.
- Release package에는 legacy `build_manifest.yaml/json`, loose `Content/Assets`, editor-only root artifacts가 없어야 한다.
- asset pack header magic/size가 smoke에서 검증된다.
- yaml-cpp PDB `LNK4099` 경고 제거 방안이 문서/보고에 남는다.

## Plan
- [x] BuildManifestTool validate 모드 추가
- [x] BuildGame release package smoke helper 추가
- [x] yaml-cpp PDB 경고 원인/해결 방안 문서화
- [x] 검증 및 문서 갱신
- [x] 커밋

## Verification
- [x] `BuildManifestTool Release_Game|x64` build
- [x] `BuildGame.ps1` parser check
- [x] `BuildGame.ps1` available project package smoke
- [x] `git diff --check`

## Review
- 코드를 읽었고: 기존 `BuildGame.ps1` package 검증은 manifest/pack 파일 존재, loose `Content/Assets`, root `SDK/Localization/Editor` 정도만 확인했다.
- 생각했고: release smoke 는 파일 존재가 아니라 runtime 이 실제로 읽을 binary manifest 계약과 startup scene GUID 를 검증해야 한다고 판단했다.
- 반례를 찾았고: PowerShell 에서 manifest payload 를 별도 파싱하면 runtime loader 와 검증 로직이 갈라질 수 있었다.
- 고쳤다: `BuildManifestTool --validate` 를 추가해 `CBuildManifestLoader::LoadFromFile()` 로 생성 manifest 를 다시 읽고, startup GUID/resolution/platform/script 계약을 확인하게 했다.
- 추가로 고쳤다: `BuildGame.ps1` release smoke 가 manifest magic, manifest validate, asset pack magic, legacy text manifest/index 금지, loose asset/editor-only artifact 금지를 확인하게 했다.
- 코드를 읽었고: yaml-cpp PDB 경고는 `yaml-cppd.lib` 가 `yaml-cppd.pdb` 를 참조하지만 matching PDB 가 package/lib/output 에 없어서 발생한다.
- 생각했고: 전역 `LNK4099` 무시는 다른 third-party symbol 누락을 숨기므로, matching PDB 를 보관하는 방식이 맞다고 판단했다.
- 고쳤다: `tasks/BuildPipelineDesign.md` 에 yaml-cpp PDB 경고 원인과 권장 해결책을 남겼다.
- 검증했다: `BuildManifestTool Release_Game|x64` build, `BuildGame.ps1` parser check, `SampleProject` release package smoke, `git diff --check` 를 통과했다.
- 주의: `C:\Users\박주형\Desktop\Project\Project.Jproject` 는 현재 이 머신에 없어 `SampleProject\Project.Jproject` 로 package smoke 를 대체했다.

---

# TODO — Vulkan RHI Descriptor Cache and Project YAML Emitter Cleanup

## Goal
Vulkan backend의 per-draw descriptor allocation/update 비용을 줄이고, ProjectManager의 `.Jproject` YAML build-settings emitter 스타일을 정리한다.

## Assumptions
- 이번 Vulkan parity 작업은 descriptor reuse/cache에 집중한다.
- D3D11/WebGPU는 이미 각자 backend 방식으로 동작하므로 RHI public API 변경 없이 Vulkan 내부만 보강한다.
- Project YAML 작업은 동작 변경이 아니라 emitter helper/indentation/style 정리다.
- 기존 사용자 dirty build artifact는 건드리지 않는다.

## Success Criteria
- Vulkan draw path가 같은 pipeline layout + bound resources 조합에 대해 descriptor set을 반복 allocate/update하지 않는다.
- frame boundary에서 cache/pool lifetime이 안전하게 리셋된다.
- 새 프로젝트 생성과 `SaveProject()`가 같은 build-settings YAML emit helper를 사용한다.
- `.Jproject` 저장/로드 계약은 유지된다.

## Plan
- [x] Vulkan descriptor bind/update 경로 읽기
- [x] Vulkan frame-local descriptor cache 구현
- [x] ProjectManager YAML build-settings emitter helper화
- [x] 검증 및 문서 갱신
- [x] 커밋

## Verification
- [x] `Debug_Editor|x64` build
- [x] `Debug_Game|x64` build 또는 Vulkan 관련 compile
- [x] `git diff --check`

## Review
- 코드를 읽었고: Vulkan `BindPendingDescriptors()`는 draw마다 descriptor set을 새로 할당하고 `vkUpdateDescriptorSets()`를 호출했다. WebGPU는 이미 bind group cache cursor를 가지고 있었다.
- 생각했고: RHI public API를 바꾸거나 D3D11/WebGPU까지 흔들기보다, Vulkan 내부를 WebGPU와 같은 frame-local binding cache 패턴으로 맞추는 것이 parity에 맞다고 판단했다.
- 반례를 찾았고: Vulkan descriptor pool은 `BeginFrame()`에서 reset되므로 cache가 프레임을 넘기면 descriptor set handle이 무효가 된다.
- 고쳤다: Vulkan command context에 `SafePtr` 기반 descriptor set cache entry와 cursor를 추가하고, frame 시작 시 cache를 비운 뒤 같은 pipeline/buffer/texture/sampler 조합은 재사용하게 했다.
- 코드를 읽었고: ProjectManager의 새 프로젝트 생성과 `SaveProject()`가 build settings YAML map을 각각 직접 emit하고, 새 프로젝트 쪽 일부 `out <<` 들여쓰기가 깨져 있었다.
- 생각했고: 이 항목은 기능 변경이 아니라 emitter/style 정리이며, build settings 저장 이슈 재발을 줄이려면 같은 키 목록을 한 helper에서 쓰는 편이 낫다고 판단했다.
- 반례를 찾았고: 새 프로젝트의 빈 `BuildScenes` 표현이 inline empty sequence에서 일반 sequence emit로 바뀔 수 있지만 YAML 의미와 reader 계약은 유지된다.
- 고쳤다: `EmitBuildSettingsYaml()` helper를 추가하고 생성/저장 양쪽에서 재사용하게 했다.
- 검증했다: `Debug_Editor|x64`, `Debug_Game|x64`, `git diff --check`를 통과했다. `Debug_Game`에는 기존 yaml-cpp PDB `LNK4099` 경고가 남아 있다.

---

# TODO — SceneView Sprite Alpha Bounds Cache

## Goal
SceneView box selection에서 Sprite 불투명 픽셀 tight AABB를 매 후보마다 재스캔하지 않도록 asset/frame 단위 캐시를 추가한다.

## Assumptions
- 단일 click pick은 현재 1픽셀 alpha test라 유지한다.
- hot-path는 box selection의 full frame alpha scan이다.
- 캐시는 editor-only `SceneViewEditContext.cpp` 내부에 두고 runtime asset/runtime renderer 계약은 건드리지 않는다.
- Sprite reload/replace는 `CSpriteAsset::GetPixelGeneration()`과 asset pointer/frame rect 변화로 무효화한다.

## Success Criteria
- `PickBox()`가 같은 sprite/frame alpha bounds를 반복 스캔하지 않는다.
- pixel generation, texture size, frame rect가 바뀌면 bounds를 다시 계산한다.
- 완전 투명 sprite frame은 기존처럼 whole OBB fallback으로 처리한다.
- 기존 선택 결과 의미는 유지한다.

## Plan
- [x] alpha bounds cache key/value 추가
- [x] per-frame scan helper로 기존 scan 이동
- [x] PickBox에서 cache helper 사용
- [x] 검증 및 문서 갱신
- [x] 커밋

## Verification
- [x] `Debug_Editor|x64` build 또는 관련 compile
- [x] `git diff --check`

## Review
- 코드를 읽었고: `CSceneViewEditContext::PickBox()`가 박스 선택 후보마다 sprite frame 전체 alpha를 스캔해 tight AABB를 계산하고 있었다.
- 생각했고: 단일 click pick은 1픽셀 alpha sample이라 유지하고, 반복 비용이 큰 box selection full-frame scan만 캐시하는 것이 가장 좁은 수정이라고 판단했다.
- 반례를 찾았고: sprite reload/import, `ReplacePixels()`, texture size 변경, frame rect 변경 시 캐시가 stale 될 수 있었다.
- 고쳤다: `AssetGuid`별 frame bounds cache를 추가하고 asset pointer, pixel generation, texture size, frame rect가 일치할 때만 재사용하게 했다.
- 추가 반례를 찾았고: GUID당 1개만 저장하면 향후 frameIndex가 들어왔을 때 프레임이 번갈아 재스캔된다.
- 고쳤다: GUID 아래에 frame별 bounds vector를 두고, asset/pixel generation/texture size가 바뀌면 해당 GUID cache를 비우게 했다.
- 검증했다: `JBroEngine.sln /p:Configuration=Debug_Editor /p:Platform=x64 /m` 빌드와 `git diff --check`가 통과했다.

---

# TODO — Cooked Asset Payload Transition Phase 1

## Goal
Release pack의 raw-source 계약을 줄이기 위해 Sprite를 실제 cooked RGBA payload로 전환하고, 나머지 타입도 runtime payload type을 RawSource가 아닌 의도별 타입으로 분류한다.

## Assumptions
- 이번 단계는 Engine/Core/Asset 중심으로 진행하고, 빡대리 작업 중인 GameFramework 변경 파일은 건드리지 않는다.
- Sprite는 stb decode를 통해 RGBA8 cooked payload를 만들 수 있다.
- Scene/Prefab/File/Custom은 아직 loader가 byte payload를 그대로 읽는 구조이므로 payload type만 SerializedScene/SerializedPrefab/BinaryBlob으로 명시한다.
- Audio는 decompressed/streaming cooked 계약이 더 크므로 이번 단계에서는 raw-compatible fallback을 유지하고 후속 작업으로 분리한다.
- `ImportOptionsYaml`은 Sprite cooked payload 안에도 넣되, index에도 호환성 목적으로 아직 유지한다.

## Success Criteria
- C++ pack writer가 Sprite payload를 raw image file bytes가 아니라 cooked RGBA8 payload로 저장한다.
- Sprite loader가 cooked RGBA8 memory payload를 직접 읽어 `CSpriteAsset`을 생성한다.
- C++ pack writer가 Scene/Prefab/BinaryBlob payload type을 타입별로 기록한다.
- SDK public mirror도 같은 asset package/load desc 계약을 가진다.
- BuildGame.ps1 script writer의 payload type 분류도 RawSource 일괄 기록에서 벗어난다.

## Plan
- [x] cooked sprite payload binary format 추가
- [x] C++ pack writer sprite cook 적용
- [x] Sprite loader cooked memory path 추가
- [x] script pack writer payload type 분류 적용
- [x] SDK mirror 동기화 확인
- [x] 문서/todo 갱신
- [x] 검증 및 커밋

## Verification
- [x] `rg`로 C++ writer RawSource 일괄 기록 제거 확인
- [x] PowerShell parser check for `BuildGame.ps1`
- [x] embedded C# pack writer `Add-Type` compile
- [x] `Engine.vcxproj Debug_Game|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: `CAssetPackWriter::Write()`는 모든 entry를 `ReadFileBytes()`로 읽고 `RawSource`로 기록했고, `CSpriteAssetLoader`는 memory payload를 stb로 원본 이미지처럼 디코드했다.
- 생각했고: Sprite는 pack 시점에 RGBA8로 cooking할 수 있어 runtime이 원본 png/jpg bytes를 알 필요가 없지만, Audio streaming은 pack-backed streaming 설계가 먼저 필요하다고 판단했다.
- 반례를 찾았고: Web/script C# pack writer가 cooked texture라고 record만 바꾸면 실제 payload는 raw image라 계약이 거짓이 된다.
- 고쳤다: C++ writer만 Sprite를 실제 cooked RGBA8 payload로 쓰고, Sprite loader는 cooked magic을 먼저 확인한 뒤 raw image fallback을 유지한다.
- 추가로 고쳤다: C++/script writer 모두 Scene/Prefab/BinaryBlob payload type을 기록하되, script writer의 Sprite/Audio는 raw-compatible으로 남겼다.
- 검증했다: `BuildGame.ps1` parser check, embedded C# pack writer `Add-Type` compile, `Engine.vcxproj Debug_Game|x64` build, `git diff --check`를 통과했다.
- 남겼다: Web/script도 cooked Sprite를 만들려면 embedded C# pack writer를 engine-owned `AssetPackTool`로 교체해야 한다.

---

# TODO — WebBuild CleanOnly Contract

## Goal
NMake `Clean`이 안내 echo가 아니라 실제 Web package 폴더만 정리하도록 `BuildGame.ps1` / `BuildWeb.ps1`에 clean-only 경로를 추가한다.

## Assumptions
- 기존 `-Clean`은 "삭제 후 빌드" 의미로 유지한다.
- 새 `-CleanOnly`는 package dir 계산 후 삭제하고 종료한다.
- 삭제는 기존 `Remove-DirectoryInside` guard를 그대로 사용해 output root 밖을 지우지 않는다.
- clean-only는 startup scene, emsdk, engine build 검증이 필요 없다.

## Success Criteria
- `BuildGame.ps1`와 `BuildWeb.ps1`가 `-CleanOnly`를 받는다.
- `BuildGame.ps1 -CleanOnly`는 package dir만 삭제하고 build/validate/package 단계로 가지 않는다.
- `WebBuild.vcxproj` NMake Clean command가 `BuildWeb.ps1 -CleanOnly`를 호출한다.

## Plan
- [x] `BuildGame.ps1` clean-only 추가
- [x] `BuildWeb.ps1` clean-only 전달 추가
- [x] `WebBuild.vcxproj` NMake clean command 교체
- [x] parser 및 clean-only smoke 검증
- [x] todo/review 갱신
- [x] 커밋

## Verification
- [x] PowerShell parser syntax check
- [x] temp OutputRoot 대상 `BuildGame.ps1 -CleanOnly` smoke
- [x] temp OutputRoot 대상 `WebBuild.vcxproj /t:Clean` smoke
- [x] `git diff --check`

## Review
- 코드를 읽었고: `WebBuild.vcxproj` Clean은 echo만 했고, `BuildWeb.ps1 -Clean`은 clean-only가 아니라 삭제 후 빌드였다.
- 생각했고: VS/NMake Clean은 emsdk/startup scene/engine build 없이 package 폴더만 제거해야 하며, 기존 `Remove-DirectoryInside` guard를 재사용하는 것이 가장 좁은 수정이라고 판단했다.
- 반례를 찾았고: clean-only가 startup scene 검증 뒤에 있으면 깨진 프로젝트는 정리도 못 하고, NMake 환경에서는 `powershell`/`powershell.exe`가 PATH에 없었다.
- 고쳤다: `BuildGame.ps1`/`BuildWeb.ps1`에 `-CleanOnly`를 추가하고 package dir 계산 직후 종료하게 했으며, WebBuild Clean은 Windows PowerShell 절대 경로를 사용한다.
- 추가 반례를 찾았고: Makefile project clean target은 `OutDir`/`IntDir`/clean pattern이 비면 package 삭제 후에도 VS clean task에서 실패했다.
- 고쳤다: WebBuild project에 intermediate output, NMakeOutput, clean pattern을 명시했다.
- 검증했다: parser check, temp project 대상 `BuildGame.ps1 -CleanOnly`, `WebBuild.vcxproj /t:Clean`, `git diff --check`를 통과했다.

---

# TODO — WebBuild NMake Argument Parity

## Goal
`BuildScripts/Web/WebBuild.vcxproj`에서 EmsdkRoot와 OutputRoot를 NMake 빌드 인자로 전달할 수 있게 해 `BuildWeb.ps1` 직접 실행과 VS WebBuild 실행의 계약을 맞춘다.

## Assumptions
- `BuildWeb.ps1`는 이미 `-EmsdkRoot`와 `-OutputRoot`를 지원한다.
- `build_web_debug.bat` / `build_web_release.bat`는 optional 인자를 받아 PowerShell로 전달하는 wrapper 역할만 해야 한다.
- Clean target은 현재처럼 빌드 실행을 유발하지 않는 안내로 둔다.

## Success Criteria
- `build_web_debug.bat` / `build_web_release.bat`가 `[EmsdkRoot] [OutputRoot]`를 받는다.
- EmsdkRoot와 OutputRoot가 비어 있으면 기존 동작을 유지한다.
- `WebBuild.vcxproj` NMake build/rebuild command가 `$(JBRO_EMSDK_ROOT)`와 `$(JBRO_OUTPUT_ROOT)`를 전달한다.

## Plan
- [x] batch wrapper optional output root 전달 추가
- [x] WebBuild NMake command property 연결
- [x] syntax/static 검증
- [x] todo/review 갱신
- [x] 커밋

## Verification
- [x] PowerShell parser syntax check for `BuildWeb.ps1`
- [x] batch usage/error path check
- [x] batch command text 검토
- [x] `git diff --check`

## Review
- 코드를 읽었고: `BuildWeb.ps1`는 `-EmsdkRoot`와 `-OutputRoot`를 지원하지만, WebBuild vcxproj는 `JBRO_PROJECT_FILE`만 batch로 넘겼다.
- 생각했고: VS/NMake에서 실행한 웹 빌드가 PowerShell 직접 실행과 다른 인자 계약을 가지면 출력 폴더/emsdk 문제 재현성이 떨어진다고 판단했다.
- 반례를 찾았고: batch에서 optional args를 문자열 변수로 조립하면 Windows 경로 공백/quoting이 깨질 수 있었다.
- 고쳤다: debug/release batch가 `[EmsdkRoot] [OutputRoot]`를 받고 네 가지 분기로 PowerShell 인자를 명시 전달하며, vcxproj는 `$(JBRO_EMSDK_ROOT)`와 `$(JBRO_OUTPUT_ROOT)`를 넘기게 했다.
- 검증했다: `BuildWeb.ps1` parser check, no-arg usage/error path, batch command text, `git diff --check`를 확인했다.

---

# TODO — Build Manifest Tool Staleness Guard

## Goal
`BuildGame.ps1`가 `BuildManifestTool.exe` 존재 여부만 보지 않고 tool source/project 변경 이후에는 자동으로 tool을 재빌드하게 한다.

## Assumptions
- packaging script는 stale helper exe를 사용하면 안 된다.
- tool output은 `Build/Tools/BuildManifestTool/<Config>/BuildManifestTool.exe`에 생성된다.
- tool source root는 `BuildTools/BuildManifestTool`이고, build output은 이 root 밖에 있다.

## Success Criteria
- tool exe가 없으면 빌드한다.
- tool source/project/filter 파일이 exe보다 새로우면 빌드한다.
- tool exe가 최신이면 불필요하게 빌드하지 않는다.

## Plan
- [x] staleness helper 추가
- [x] `Write-JBroBuildManifest` tool build 조건 교체
- [x] script syntax/static 검증
- [x] targeted tool build 검증
- [x] todo/review 갱신
- [x] 커밋

## Verification
- [x] PowerShell parse/syntax check
- [x] `BuildManifestTool Release_Game|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: `Write-JBroBuildManifest`는 `BuildManifestTool.exe`가 존재하면 source/project가 더 최신이어도 재빌드하지 않았다.
- 생각했고: manifest writer를 engine-owned tool로 바꿔도 stale exe를 쓰면 동일한 drift 문제가 다른 형태로 남는다고 판단했다.
- 반례를 찾았고: tool output은 `Build/Tools/...`에 있고 source root는 `BuildTools/BuildManifestTool`이라 source timestamp만 비교하면 build output timestamp 순환 문제는 생기지 않는다.
- 고쳤다: `Test-JBroToolOutdated`를 추가하고 exe 없음/source root 없음/source newer 조건에서 tool을 빌드하게 했다.
- 검증했다: PowerShell parser syntax check, `BuildManifestTool Release_Game|x64` build, `git diff --check`를 통과했다.

---

# TODO — Build Manifest Tool Solution Integration

## Goal
`BuildManifestTool`을 `JBroEngine.sln`에 등록해 manifest writer 도구가 솔루션에서 보이고, game 구성에서 명시적으로 빌드될 수 있게 한다.

## Assumptions
- tool 자체는 Windows host에서 실행되는 packaging helper이므로 `x64` tool project만 유지한다.
- editor solution build가 tool을 항상 빌드할 필요는 없다.
- game/package 검증 경로에서는 `Debug_Game|x64` / `Release_Game|x64` tool build가 중요하다.

## Success Criteria
- `JBroEngine.sln`에 `BuildManifestTool` project가 포함된다.
- solution의 game x64 구성에서 tool project가 build 대상이 된다.
- editor/web/x86 solution 구성은 tool active config만 연결하고 불필요한 build target으로 만들지 않는다.
- 기존 `BuildManifestTool.vcxproj` 단독 빌드는 계속 통과한다.

## Plan
- [x] solution project entry 추가
- [x] solution configuration mapping 추가
- [x] targeted solution/project build 검증
- [x] todo/review 갱신
- [x] 커밋

## Verification
- [x] `BuildManifestTool Debug_Game|x64` build
- [x] `BuildManifestTool Release_Game|x64` build
- [x] solution target `BuildManifestTool` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: `BuildManifestTool`은 `BuildScripts`에서 쓰이지만 `JBroEngine.sln`에는 등록되어 있지 않았다.
- 생각했고: manifest writer는 build/package 계약의 일부라서 솔루션에서 보이고 targeted build가 가능해야 유지보수성이 맞다고 판단했다.
- 반례를 찾았고: tool은 Windows host packaging helper이므로 Web/x86 target으로 억지 빌드하면 플랫폼 계약이 흐려진다.
- 고쳤다: solution project entry를 추가하고, game x64 구성만 Build.0 대상에 넣고 editor/web/x86은 ActiveCfg만 연결했다.
- 검증했다: Debug_Game/Release_Game tool project build, solution target `BuildManifestTool` build, `git diff --check`를 통과했다.

---

# TODO — Build Manifest Writer Unification

## Goal
`BuildGame.ps1`의 embedded C# build manifest writer를 제거하고, Engine의 `CBuildManifestLoader::WriteBinaryFile()`을 호출하는 engine-owned CLI tool을 사용한다.

## Assumptions
- authoritative writer는 `Engine/Core/Build/BuildManifest.cpp`의 C++ writer다.
- 에디터 Windows package는 이미 C++ writer를 직접 사용한다.
- script/web package는 새 `BuildManifestTool`을 빌드 후 실행해 같은 writer를 사용한다.

## Success Criteria
- `BuildScripts/BuildGame.ps1`에서 `JBroBuildManifestWriterV2` embedded C# writer가 제거된다.
- 새 `BuildManifestTool`이 `CBuildManifestLoader::WriteBinaryFile()`으로 manifest를 생성한다.
- `BuildGame.ps1`는 `BuildManifestTool.vcxproj`를 빌드하고 tool을 호출한다.
- tool-generated manifest를 runtime loader가 읽을 수 있다.

## Plan
- [x] `BuildManifestTool` vcxproj/source 추가
- [x] `BuildGame.ps1` manifest writer 호출 경로 교체
- [x] embedded C# manifest writer 제거
- [x] tool build/run 및 manifest read 검증
- [x] 문서/todo 갱신
- [x] 커밋

## Verification
- [x] `BuildManifestTool Debug_Game|x64` build
- [x] `BuildManifestTool Release_Game|x64` build
- [x] tool로 sample manifest 생성
- [x] generated manifest를 runtime loader 경유로 읽기
- [x] `rg JBroBuildManifestWriterV2` 잔존 없음
- [x] `git diff --check`

## Review
- 코드를 읽었고: `BuildGame.ps1`의 script/web manifest writer가 embedded C#이고, editor Windows packaging은 `CBuildManifestLoader::WriteBinaryFile()`을 직접 쓰는 것을 확인했다.
- 생각했고: manifest field가 하나만 추가돼도 C++ writer와 C# writer가 갈라질 수 있으며, 이전 PPU 문제와 같은 drift가 반복될 수 있다고 판단했다.
- 반례를 찾았고: tool이 `ProjectReference`로 Engine을 다시 빌드하면 빡대리 작업 중인 dirty source 때문에 manifest 단계가 불필요하게 실패할 수 있었다.
- 고쳤다: `BuildManifestTool`을 추가해 C++ writer를 호출하게 하고, script는 tool을 빌드/실행하되 기존 `Engine.lib`에 명시 링크하도록 구성했다.
- 검증했다: Debug_Game/Release_Game tool build, sample manifest 생성, tool 내부 loader round-trip, stale C# writer 잔존 검색, `git diff --check`를 통과했다.

---

# TODO — SceneSerializer Side-effect API Cleanup

## Goal
`SceneSerializer::SerializeToText()`가 referenced asset cache를 갱신하는 mutation을 API signature에 명시하고 `const_cast`를 제거한다.

## Assumptions
- scene serialization은 `ReferencedAssets` cache를 갱신하는 side effect가 의도된 동작이다.
- object serialization 자체는 const object로 충분하다.
- SDK public mirror도 같은 signature를 가져야 한다.

## Success Criteria
- `SerializeToText()` / `SaveToFile()`이 `CScene&`를 받는다.
- `SceneSerializer.cpp`의 scene 대상 `const_cast`가 제거된다.
- 현재 호출자는 새 signature로 컴파일된다.

## Plan
- [x] serializer API 호출자 확인
- [x] Engine/SDK header signature 변경
- [x] const scene 순회와 explicit metadata update로 구현 변경
- [x] 문서/todo 갱신
- [ ] 빌드 검증 및 커밋

## Verification
- [x] `rg`로 `SceneSerializer` 대상 `const_cast<CScene&>(scene)` 제거 확인
- [x] `Debug_Game|x64` build
- [x] `Debug_Editor|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: `CScene`에는 const `ForEachObject`가 있고, `Serialization::WriteObject()`도 `const CGameObject&`를 받는 것을 확인했다.
- 생각했고: 오브젝트 순회는 const로 충분하지만 `SetReferencedAssets()`는 의도된 scene metadata mutation이므로 API signature에서 감추면 안 된다고 판단했다.
- 반례를 찾았고: scene save 호출자는 대부분 non-const scene을 가지고 있으며, prefab serialization도 내부 `prefabScene`은 non-const라 signature 변경이 가능했다.
- 고쳤다: `SerializeToText()`/`SaveToFile()`을 `CScene&`로 바꾸고, scene 대상 `const_cast`를 제거했으며 SDK mirror도 맞췄다.

---

# TODO — Editor-only Localization Output Hygiene

## Goal
게임 빌드 중간 출력에 editor-only `Localization` 폴더가 복사되지 않게 한다.

## Assumptions
- editor 구성은 `Debug` / `Release`이고 game 구성은 `Debug_Game` / `Release_Game`이다.
- editor localization sync는 editor 실행 편의를 위해 유지한다.
- package verifier의 `Localization` 금지 검사는 유지한다.

## Success Criteria
- `Application.vcxproj` post-build localization sync가 editor 구성에서만 실행된다.
- `Debug_Game` / `Release_Game` 빌드 로그에 `Sync Localization` post-build가 나오지 않는다.
- package verifier의 forbidden artifact check는 그대로 유지된다.

## Plan
- [x] localization post-build 조건 확인
- [x] editor-only 조건 적용
- [x] 문서/todo 갱신
- [ ] Debug_Game/Release_Game 빌드 검증 및 커밋

## Verification
- [x] `Debug_Game|x64` build
- [x] `Release_Game|x64` build
- [x] build output에서 `Sync Localization` 미출력 확인
- [x] `git diff --check`

## Review
- 코드를 읽었고: `Application.vcxproj`의 localization post-build가 조건 없는 `ItemDefinitionGroup`에 있어 모든 구성에 적용되는 것을 확인했다.
- 생각했고: editor 구성 이름은 `Debug`/`Release`, game 구성 이름은 `Debug_Game`/`Release_Game`로 이미 분리되어 있으므로 별도 스크립트보다 vcxproj 조건이 가장 좁은 수정이라고 판단했다.
- 반례를 찾았고: editor 실행은 cwd `Localization`을 읽으므로 editor post-build sync 자체는 제거하면 안 된다.
- 고쳤다: localization post-build `ItemDefinitionGroup`을 `Debug` 또는 `Release` 구성에서만 실행되도록 제한했다.

---

# TODO — Runtime Manifest and Scene Fallback Tightening

## Goal
Release runtime game에서 legacy YAML manifest와 loose scene path fallback이 패키징 오류를 숨기지 못하게 막는다.

## Assumptions
- Debug game과 editor/development 환경에서는 legacy fallback이 진단/전환용으로 남아도 된다.
- Release game package는 `Content/build_manifest.jbmanifest`와 `StartupSceneGuid`를 필수로 본다.
- 기존 binary manifest writer는 startup scene guid를 이미 요구한다.

## Success Criteria
- Release game runtime의 default manifest 검색은 `build_manifest.yaml`을 후보로 보지 않는다.
- Release game runtime에서 binary magic이 없는 manifest를 YAML로 fallback load하지 않는다.
- Release game runtime에서 startup/build scene을 path fallback으로 로드하지 않는다.

## Plan
- [x] legacy manifest fallback 허용 조건 추가
- [x] default manifest 후보와 YAML parse fallback gate
- [x] runtime scene path fallback gate
- [x] 문서/todo 갱신
- [ ] diff/build 검증 및 커밋

## Verification
- [x] `rg`로 legacy fallback gate 확인
- [x] `git diff --check`
- [x] `Debug_Game|x64` build
- [x] `Release_Game|x64` build

## Review
- 코드를 읽었고: `FindDefaultManifest()`가 YAML 후보를 항상 추가하고, `LoadFromFile()`도 binary magic이 없으면 YAML parse로 넘어가는 구조를 확인했다.
- 생각했고: default 후보만 제거하면 명시 YAML 경로가 release runtime에서 여전히 통과하므로 load 단계도 같이 gate해야 한다고 판단했다.
- 반례를 찾았고: Debug game과 editor는 이전 package 진단/전환에 YAML fallback이 필요할 수 있다.
- 고쳤다: `JBRO_EDITOR` 또는 non-`NDEBUG`에서만 legacy manifest fallback을 허용했다.
- 추가로 읽었고: `LoadRuntimeSceneNodes()`는 guid가 null이면 loose path로 scene을 로드했다.
- 고쳤다: release runtime에서는 scene path fallback을 거부하고 GUID package asset 경로만 허용하게 했다.

---

# TODO — BuildGame Pack Writer Contract Parity

## Goal
`BuildGame.ps1`의 C# asset pack writer를 엔진 C++ pack writer의 runtime index v2 계약과 맞춘다.

## Assumptions
- 웹/스크립트 빌드도 같은 `.jbpack` runtime index 계약을 써야 한다.
- `Read-JBroMeta`의 `Importer`는 asset type 판정에는 계속 필요하지만 pack index에는 쓰지 않는다.
- PowerShell 세션에 이전 C# type이 로드된 반례를 피하기 위해 writer/entry type 이름을 v2로 분리한다.

## Success Criteria
- `BuildGame.ps1` pack writer가 index version 2를 쓴다.
- script-side pack index에 `Importer`, `SourceExtension`이 기록되지 않는다.
- script-side pack writer type name이 stale loaded type과 충돌하지 않는다.

## Plan
- [x] `BuildGame.ps1` embedded C# pack writer 계약 수정
- [x] C++/PowerShell pack index field 순서 재대조
- [x] 문서/todo 갱신
- [ ] diff/build or script-level 검증 및 커밋

## Verification
- [x] `rg`로 script pack writer의 removed fields 확인
- [x] `git diff --check`
- [x] embedded C# pack writer `Add-Type` compile
- [ ] `Debug_Game|x64` build: not rerun for script-only parity change; previous asset pack contract build passed.

## Review
- 코드를 읽었고: `BuildGame.ps1`의 embedded `JBroPackWriter`가 아직 index version 1과 `Importer`/`SourceExtension`을 쓰는 것을 확인했다.
- 생각했고: C++ writer만 v2로 바꾸면 Windows editor package와 script/web package의 `.jbpack` 계약이 갈라진다.
- 반례를 찾았고: PowerShell 장기 세션에 이전 C# type이 로드되어 있으면 같은 type 이름 재정의가 실패하거나 stale type을 계속 쓸 수 있다.
- 고쳤다: script writer/entry type을 `JBroPackWriterV2`/`JBroPackEntryV2`로 바꾸고, index version 2와 C++ writer와 같은 field 순서로 맞췄다.
- 검증했다: removed field 잔존 검색과 embedded C# `Add-Type` compile을 통과했다.

---

# TODO — Asset Pack Release Contract Hardening

## Goal
남은 감사 목록 1번의 첫 단계로 release/runtime pack 계약에서 디스크 materialization과 source/debug metadata 노출을 제거한다.

## Assumptions
- 이번 단계는 package index 계약을 먼저 hardened runtime record로 줄인다.
- cooked payload 전환은 importer별 payload 포맷 계약이 필요하므로 다음 커밋 단위로 진행한다.
- `ImportOptionsYaml`은 현재 Sprite PPU/frame과 Audio mode 생성에 쓰이는 runtime-required metadata라 cooked payload 전환 전까지 유지한다.
- 에디터 개발 호환성보다 release/runtime 보호 계약을 우선한다.

## Success Criteria
- `CAssetPackReader::MaterializePayload()` 기본 경로가 사라진다.
- packed runtime load는 memory payload를 지원하는 asset loader만 허용한다.
- pack index에 `Importer`, `SourceExtension` 같은 debug/source metadata를 저장하지 않는다.
- 사용되지 않는 debug-name package flag가 제거된다.
- `ImportOptionsYaml`은 cooked payload 전환 전까지 runtime-required metadata로만 남긴다.
- SDK public mirror가 같은 asset pack record 계약을 가진다.

## Plan
- [x] pack record serialization/deserialization 계약 확인
- [x] source/debug metadata 필드 제거 및 SDK mirror 동기화
- [x] `.packcache` materialization 제거
- [x] packed runtime memory-loader 실패 처리를 명시화
- [x] audit/follow-up 문서 갱신
- [ ] diff/build 검증 및 커밋

## Verification
- [x] `rg`로 `MaterializePayload`, `.packcache`, source metadata field 잔존 확인
- [x] `git diff --check`
- [x] `Debug_Game|x64` build
- [ ] `Debug_Editor|x64` build 가능 여부 확인: skipped because `Build/Debug_Editor/Application.exe` is held by running PID 24848.

## Review
- 코드를 읽었고: `AssetPackage` index writer/reader, `CAssetManager::LoadPackedAssetManifest()`, `LoadAssetInternal()`, Sprite/File/Audio/AudioEffect loader memory path를 확인했다.
- 생각했고: default `.packcache` materialization은 resource protection 목표와 충돌하므로 release/runtime 기본 경로에서 제거해야 한다고 판단했다.
- 반례를 찾았고: Audio streaming은 memory payload만으로 현재 load할 수 없지만, 이를 파일로 풀어 해결하면 pack 보호 계약이 깨진다.
- 고쳤다: `MaterializePayload()`와 cache root를 제거하고, packed payload가 있는데 loader가 memory load를 못 하면 명시적 실패 로그를 내게 했다.
- 추가로 읽었고: runtime index의 `Importer`, `SourceExtension`, `DebugNamePresent`는 현재 loader 동작에 필요 없고 debug/source 정보에 가깝다.
- 고쳤다: 해당 필드/flag를 제거하고 index version을 2로 올렸으며, v1 pack은 제거 필드를 읽어서 버리는 호환 경로를 남겼다.
- 남겼다: `ImportOptionsYaml`은 Sprite PPU/frame과 Audio mode에 필요하므로 cooked payload 전환 전까지 runtime-required metadata로 유지했다.

---

# TODO — Engine Audit Dead-Code Cleanup

## Goal
엔진 전체 감사에서 확인된 데드/스테일 코드 중 현재 호출 계약상 제거 가능한 항목을 먼저 정리하고, 남은 작업을 실행 순서 기준으로 다시 정리한다.

## Assumptions
- 이번 1차 수정은 확정된 dead/stale 코드 제거에 한정한다.
- 런타임 호환성, pack materialization, raw-source cook 전환처럼 정책 판단이 필요한 항목은 남은 작업으로 재분류한다.
- `Build/` 산출물 변경은 이번 커밋 범위에 포함하지 않는다.

## Success Criteria
- `BuildAssetPackage()`의 stale JSON manifest writer가 제거된다.
- 사용되지 않는 `AssetPackageBuildDesc::OutputManifestPath` 계약이 제거된다.
- SDK public mirror도 같은 계약으로 동기화된다.
- 남은 audit finding은 우선순위/분류가 명확한 follow-up 문서로 정리된다.

## Plan
- [x] `BuildAssetPackage()` 호출자와 desc 계약 재확인
- [x] stale JSON manifest writer 및 죽은 manifest path 필드 제거
- [x] Engine/SDK mirror 동기화
- [x] 남은 audit 작업 재분류 문서화
- [x] diff/build 검증 및 커밋

## Verification
- [x] `rg`로 `OutputManifestPath` 잔존 확인
- [x] `git diff --check`
- [ ] `Debug_Editor|x64` build: source compile completed, but final link failed because `Build/Debug_Editor/Application.exe` is held by running PID 24848.
- [x] `Debug_Game|x64` build

## Review
- 코드를 읽었고: `BuildAssetPackage()`와 현재 유일 호출자인 `CGameBuildManager::StagePackage()`를 확인했다.
- 생각했고: JSON sidecar writer만 제거하면 `OutputManifestPath`라는 죽은 계약이 남아 다시 분기/오해를 만들 수 있다고 판단했다.
- 반례를 찾았고: 별도 manifest path를 쓰는 호출자가 있는지 `rg`로 전체 소스/SDK/스크립트 범위를 확인했지만 현재 소스 호출자는 없었다.
- 고쳤다: stale JSON writer와 `AssetPackageBuildDesc::OutputManifestPath`를 제거하고, Engine/SDK mirror와 호출부를 `OutputBlobPath` 단일 계약으로 맞췄다.
- 남은 작업을 정리했다: `tasks/EngineAuditFollowup.md`에 해결 항목과 후속 작업 우선순위를 분리했다.

---

# TODO — Engine-wide Code Audit

## Goal
엔진 전체 소스에서 데드코드, 이상한 구조, 버그 가능 코드, 최적화 후보를 찾아 근거와 함께 md 문서로 정리한다.

## Assumptions
- 검토 대상은 `Application/`, `Engine/`, `SDK/Include/`, `BuildScripts/`, `SampleProject/`, `Samples/`이다.
- `Build/`, `.git/`, `Engine/ThirdParty/`는 산출물/외부 소스라 제외한다.
- 이번 작업은 수정이 아니라 발견/분류/권장 작업 문서화가 목표다.

## Success Criteria
- 각 finding은 파일/라인 근거를 가진다.
- severity, category, impact, recommendation을 적는다.
- 불확실한 항목은 추정으로 명시하고 과장하지 않는다.
- 문서는 처음 보는 사람이 다음 작업 우선순위를 잡을 수 있게 작성한다.

## Plan
- [x] 전체 파일/모듈 구조 스캔
- [x] 정적 패턴 검색으로 위험 후보 수집
- [x] 후보별 주변 코드 읽고 반례 확인
- [x] `tasks/EngineCodeAudit.md` 작성
- [x] 문서 검토 및 커밋

## Verification
- [x] `rg` 기반 정적 검색 수행
- [x] 주요 후보 파일 직접 열람
- [x] `git diff --check`

## Review
- 코드를 읽었고: `Application/`, `Engine/`, `SDK/Include/`, `BuildScripts/`, `SampleProject/`, `Samples/`에서 build/package/runtime/render/asset/editor 후보를 `rg`로 먼저 모았다.
- 생각했고: 이번 감사의 핵심은 단순 dead code보다 빌드 산출 계약, pack 보호 계약, RHI별 병렬성, editor/runtime 분리의 drift 여부라고 판단했다.
- 반례를 찾았고: `RenderScene::Sort()` 반복 호출처럼 겉보기엔 의심스럽지만 `m_needsSort` guard가 있는 항목은 낮은 우선순위로 내렸다.
- 문서화했다: `tasks/EngineCodeAudit.md`에 severity/category/impact/recommendation과 확인한 반례를 분리해 기록했다.

---

# TODO — SceneView Translate Arrow Line Trim

## Goal
Translate Guizmo의 축 선이 화살표 삼각형 안으로 튀어나와 보이지 않게 정리한다.

## Assumptions
- 문제는 선을 화살표 tip까지 그려서 삼각형 내부에 선이 겹쳐 보이는 것이다.
- hit-test 범위는 기존 축 전체 길이를 유지한다.
- draw만 화살표 밑변까지 줄인다.

## Success Criteria
- Translate X/Y 축 선이 화살표 밑변에서 끝난다.
- 화살표 tip은 삼각형만 차지한다.
- Local/World 축 방향과 기존 hit-test 동작은 유지된다.

## Plan
- [x] Translate draw line end를 arrow base로 조정
- [x] shadow line도 같은 기준으로 조정
- [x] 빌드 검증 및 커밋

## Verification
- [x] `Debug_Editor|x64` build
- [x] `Debug_Game|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: Translate draw는 축 선을 `pivotScreen -> xEnd/yEnd`까지 그린 뒤, 같은 `xEnd/yEnd`를 화살표 tip으로 삼아 삼각형을 덮어 그리고 있었다.
- 생각했고: 이 구조에서는 선이 화살표 삼각형 내부까지 들어가므로, 이미지처럼 화살표 중앙에서 선이 삐져나온 것처럼 보일 수 있다.
- 반례를 찾았고: hit-test 선분까지 줄이면 사용자가 화살표 근처 축을 잡는 감도가 줄어든다.
- 고쳤다: hit-test는 기존 축 전체 길이를 유지하고, draw line과 shadow line만 `tip - axisDir * ARROW_SIZE` 위치에서 끝나도록 잘랐다.

---

# TODO — SceneView Guizmo Drag Follow / Scale Line Hit-Test

## Goal
Translate Guizmo를 드래그하는 동안 Guizmo가 preview된 object position을 따라가고, Scale Guizmo는 사각형 handle뿐 아니라 축/대각선 선분도 상호작용한다.

## Assumptions
- "Position Guizmo"는 현재 `Translate` mode를 의미한다.
- Translate drag 중 실제 transform preview는 기존처럼 start transform + world delta로 계산한다.
- Guizmo 표시 위치만 start pivot 고정에서 current pivot preview로 바꾼다.
- Scale line hit-test는 기존 X/Y/Uniform handle 의미를 그대로 사용한다.

## Success Criteria
- Translate X/Y/XY drag 중 Guizmo pivot과 축이 오브젝트 이동을 따라간다.
- Translate undo/redo transaction 구조는 기존과 동일하다.
- Scale mode에서 X/Y/Uniform 사각형뿐 아니라 연결 선을 눌러도 해당 scale drag가 시작된다.
- Scale 사각형 handle 우선순위는 기존처럼 유지된다.

## Plan
- [x] Translate drag current pivot 저장 추가
- [x] Translate drag draw 기준을 current pivot으로 변경
- [x] Scale line segment hit-test 추가
- [x] 빌드 검증 및 커밋

## Verification
- [x] `Debug_Editor|x64` build
- [x] `Debug_Game|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: Translate drag 중 preview transform은 매 프레임 갱신되지만, Guizmo draw는 계속 `m_dragStartWorld`를 기준으로 했다.
- 생각했고: TransformSystem이 돌기 전에는 `GetWorldTransform()`이 stale일 수 있으므로, preview된 object를 다시 스캔해 pivot을 계산하는 것보다 drag start pivot + constrained delta가 더 직접적이고 안정적이다.
- 반례를 찾았고: Local X/Y 축 drag는 constrained delta만큼만 이동하므로 raw mouse delta를 쓰면 Guizmo 표시 위치가 실제 object preview와 어긋난다.
- 고쳤다: `m_dragCurrentWorld`를 추가하고, Translate drag에서 constrained delta 적용 후 `m_dragStartWorld + constrainedDelta`로 갱신해 그 위치에 Guizmo를 그리게 했다.
- 추가로 읽었고: Scale hit-test는 사각형 handle `SquareRect()`만 검사하고, 실제로 그려지는 X/Y/Uniform 선분은 검사하지 않았다.
- 생각했고: 시각적으로 선이 handle의 일부로 보이므로 선분도 같은 ScaleX/Y/XY drag를 시작해야 한다.
- 반례를 찾았고: 선분 검사를 먼저 하면 사각형 handle 근처에서 의도한 handle 우선순위가 흐려질 수 있다.
- 고쳤다: 기존 사각형 hit-test를 먼저 유지하고, miss일 때만 `DistanceToSegmentSq()`로 X/Y/Uniform 선분 hit-test를 수행했다.

---

# TODO — SceneView 2D Local Translate Space

## Goal
SceneView Guizmo에서 World/Local 공간을 전환할 수 있고, Translate X/Y handle이 Local 공간에서는 active object의 world 방향축을 따른다.

## Assumptions
- 우선 Local space는 Translate에만 적용한다.
- Local 축 기준은 active object의 world transform basis를 사용한다.
- active object가 없거나 basis 길이가 0에 가까우면 World 축으로 fallback한다.
- drag 중 object transform이 변해도 시작 시점의 축 기준을 유지한다.

## Success Criteria
- SceneView overlay에서 World/Local space를 전환할 수 있다.
- World space Translate는 기존처럼 화면상 world X/Y 축을 따른다.
- Local space Translate는 active object 회전 방향의 X/Y 축으로 handle을 그리고, drag delta도 해당 축으로 projection한다.
- toolbar hover/click은 기존 selection/collider input을 막는다.
- release 후 undo/redo 1회로 시작/끝 transform을 복원한다.

## Plan
- [x] 2D translate axis basis 계산 추가
- [x] Translate hit-test/draw/drag constraint에 basis 적용
- [x] SceneView toolbar에 World/Local space toggle 추가
- [x] 문서 review 갱신
- [x] 빌드 검증 및 커밋

## Verification
- [x] `Debug_Editor|x64` build
- [x] `Debug_Game|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: `EGuizmoSpace`는 controller에 있었지만 SceneView에서 바꿀 UI가 없고, `CGuizmo2D::UpdateAndDraw()`도 space 인자를 무시하고 있었다.
- 생각했고: Local/World state만 노출하고 실제 handle 동작이 바뀌지 않으면 사용자가 신뢰할 수 없는 토글이 되므로, 우선 Translate X/Y handle의 draw/hit-test/drag constraint를 같은 basis로 묶어야 했다.
- 반례를 찾았고: idle draw/hit-test에서 drag 시작 좌표를 재사용하면 이전 drag 상태나 원점에 의존해 handle 방향이 틀어질 수 있다.
- 고쳤다: `pivotWorld`를 draw/hit-test에 명시적으로 넘겨 현재 프레임 pivot 기준으로 screen-space axis 방향을 계산했다.
- 추가로 읽었고: `Matrix3x2`의 x/y basis는 `(M11, M12)`와 `(M21, M22)`이고, scale이 섞이면 길이가 달라진다.
- 생각했고: handle 길이는 screen-space constant여야 하므로 basis는 방향만 정규화해서 쓰는 것이 맞다.
- 반례를 찾았고: active object가 없거나 scale 0에 가까운 basis면 Local 축이 정의되지 않는다.
- 고쳤다: Local space는 active object world basis를 정규화해 사용하고, basis가 유효하지 않으면 World 축으로 fallback했다.
- 추가 반례: drag 중 transform이 바뀌면 local axis도 계속 바뀌어 mouse projection이 흔들릴 수 있다.
- 고쳤다: Translate drag 시작 시 `m_dragAxisX/Y`를 저장하고, drag 중 preview와 draw는 저장된 축을 사용하도록 했다.

---

# TODO — SceneView 2D Rotate Snap

## Goal
SceneView Rotate Guizmo에서 Shift 드래그 시 15도 단위로 회전 delta를 스냅한다.

## Assumptions
- 별도 설정 UI 없이 우선 Shift modifier를 snap trigger로 사용한다.
- 스냅은 absolute rotation이 아니라 drag 시작점 대비 delta에만 적용한다.
- undo/redo transaction 구조는 기존 Rotate drag와 동일하게 유지한다.

## Success Criteria
- Shift 없이 Rotate drag는 기존처럼 연속 회전한다.
- Shift를 누른 상태의 Rotate drag는 15도 단위 delta만 적용한다.
- Shift를 drag 중에 누르거나 떼도 preview는 같은 drag transaction 안에서 안정적으로 갱신된다.
- release 후 undo/redo 1회로 시작/끝 transform을 복원한다.

## Plan
- [x] Rotate delta snap helper 추가
- [x] UpdateRotateDrag에 Shift snap 적용
- [x] 문서 review 갱신
- [x] 빌드 검증 및 커밋

## Verification
- [x] `Debug_Editor|x64` build
- [x] `Debug_Game|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: Rotate drag는 mouse angle과 drag start angle의 차이를 그대로 `RotateObjectAroundPivot()`에 넘기고 있었다.
- 생각했고: snap은 absolute rotation에 적용하면 드래그 시작 시 object가 튈 수 있으므로, drag 시작점 대비 delta에만 적용해야 한다.
- 반례를 찾았고: snap을 별도 command나 설정 저장으로 만들면 Guizmo 기본 편집 단계에 비해 범위가 커지고, editor/runtime 경계와 저장 정책까지 불필요하게 건드린다.
- 고쳤다: `ImGui::GetIO().KeyShift`가 true인 frame에서만 rotate delta를 15도 단위로 반올림하고, 기존 preview/undo transaction 경로는 그대로 유지했다.

---

# TODO — SceneView 2D Scale Guizmo

## Goal
SceneView Guizmo에서 Scale mode를 선택할 수 있고, 선택 오브젝트를 pivot 기준으로 X/Y/Uniform 스케일 편집한다.

## Assumptions
- Scale은 2D Transform2D의 local Scale 값을 편집한다.
- X/Y handle은 화면/월드축 기준으로 mouse delta를 읽지만, 회전된 오브젝트에 대한 완전한 world-axis non-uniform scale은 Transform2D만으로 shear 없이 표현할 수 없으므로 local Scale 증감으로 제한한다.
- 부모가 있는 object는 pivot 기준으로 계산한 target world position을 parent local position으로 환산한다.
- 음수 scale sign flip은 허용하지 않고, 기존 sign을 보존하며 최소 절대 scale 값으로 clamp한다.

## Success Criteria
- SceneView overlay에서 Scale mode를 선택할 수 있다.
- Scale mode는 X/Y/Uniform handle을 그리고 screen-space hit-test로 drag를 시작한다.
- Scale drag는 선택 top-level object들의 position과 scale을 pivot 기준으로 preview 갱신한다.
- release 후 undo/redo 1회로 시작/끝 transform을 복원한다.
- scale 값은 0 또는 sign flip으로 붕괴하지 않는다.

## Plan
- [x] SceneView Guizmo mode toolbar에 Scale 추가
- [x] Scale handle draw/hit-test 추가
- [x] Scale drag preview/commit 구현
- [x] 음수 scale sign 보존 및 minimum clamp 적용
- [x] 빌드 검증 및 커밋

## Verification
- [x] `Debug_Editor|x64` build
- [x] `Debug_Game|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: Translate/Rotate는 SceneViewTool이 mode toolbar와 overlay block만 담당하고, 실제 hit-test/drag/undo는 `CGuizmo2D`가 담당하는 구조였다.
- 생각했고: Scale도 같은 host/controller 계약을 따라야 SceneView 선택, collider editing, undo 흐름과 충돌하지 않는다.
- 반례를 찾았고: Scale toolbar 클릭이 기존 SceneView picking이나 collider input으로 흘러가면 mode 변경과 선택/편집이 동시에 발생할 수 있다.
- 고쳤다: SceneView mode toolbar에 `S` 버튼만 추가하고 기존 `modeToolbarHovered -> overlayBlocked` 차단 경로를 그대로 타게 했다.
- 추가로 읽었고: Transform2D는 position/rotation/scale만 표현하므로 회전된 object에 대한 완전한 world-axis non-uniform scale은 shear 없이 표현할 수 없다.
- 생각했고: 여기서 억지로 world-axis scale을 가장하면 부모/회전 조합에서 결과가 불명확해지므로, handle은 월드/화면 기준 입력을 쓰되 실제 값은 local Scale 증감으로 제한하는 것이 현재 Transform2D 계약에 맞다.
- 반례를 찾았고: pivot을 지나 드래그하면 scale factor가 음수가 되어 sprite flip이나 0 scale 붕괴가 발생할 수 있다.
- 고쳤다: sign flip은 허용하지 않고 기존 scale sign을 보존하며 `0.001` minimum absolute scale로 clamp했다.
- 추가 반례: 부모가 있는 object의 position을 world target 그대로 local에 넣으면 부모 transform 아래에서 위치가 틀어진다.
- 고쳤다: Scale preview에서도 pivot 기준 target world position을 계산한 뒤 `WorldPositionToLocalPosition()`으로 parent local position에 환산했다.

---

# TODO — SceneView 2D Rotate Guizmo

## Goal
SceneView Guizmo에서 Translate와 Rotate mode를 선택할 수 있고, Rotate mode에서 선택 오브젝트를 pivot 기준으로 회전 편집한다.

## Assumptions
- Scale은 다음 단계로 미루고 이번에는 Translate/Rotate만 노출한다.
- Rotate는 기본 `SelectionCenter` pivot 기준으로 동작한다.
- 부모가 있는 object는 회전 후 world position을 parent local position으로 환산한다.
- 드래그 중 preview 후 release 때 old/new transform 스냅샷 커맨드 1개만 기록한다.

## Success Criteria
- SceneView overlay에서 Translate/Rotate mode를 전환할 수 있다.
- Rotate mode는 circular handle을 그리고 screen-space ring hit-test로 drag를 시작한다.
- 회전 drag는 선택 top-level object들의 position과 rotation을 pivot 기준으로 preview 갱신한다.
- release 후 undo/redo 1회로 시작/끝 transform을 복원한다.
- mode toolbar hover/click은 SceneView picking, box selection, collider editing을 막는다.

## Plan
- [x] SceneView Guizmo mode toolbar 추가
- [x] toolbar 영역 입력 차단을 SceneView/Collider/Guizmo context에 반영
- [x] Rotate handle draw/hit-test 추가
- [x] Rotate drag preview/commit 구현
- [x] 빌드 검증 및 커밋

## Verification
- [x] `Debug_Editor|x64` build
- [x] `Debug_Game|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: Translate Guizmo는 구현되어 있었지만 controller mode를 바꿀 UI가 없어 Rotate를 추가해도 접근할 수 없었다.
- 생각했고: 기능을 숨겨 둔 채 구현만 하는 것은 사용 가능한 단계가 아니므로, SceneView overlay에 Translate/Rotate mode toolbar가 먼저 필요했다.
- 반례를 찾았고: toolbar 위 클릭이 기존 SceneView picking이나 collider vertex editing으로도 흘러가면 mode 변경과 선택/편집이 동시에 발생한다.
- 고쳤다: mode toolbar hover를 `overlayBlocked`에 포함하고, collider editing, Guizmo hit-test, SceneView click state machine이 같은 차단 값을 쓰도록 했다.
- 추가로 읽었고: Transform world cache는 `TransformSystem`이 갱신하는 값이라 drag preview 중 object world가 즉시 최신이라는 보장이 없다.
- 생각했고: Rotate drag는 preview 중 `object.GetWorld()`에 의존하지 말고, 드래그 시작 local transform과 parent world만으로 target local position을 계산해야 한다.
- 반례를 찾았고: 부모가 있는 object를 selection pivot 기준으로 회전할 때, world target position을 그대로 local position에 넣으면 부모 회전/스케일 아래에서 위치가 틀어진다.
- 고쳤다: `RotateObjectAroundPivot()`에서 initial local position을 parent world로 world position화하고, 회전 후 target world position을 parent inverse로 local position에 환산했다.
- 추가 반례: drag 중 controller mode가 바뀌어도 active drag는 시작한 handle 계약을 계속 따라야 한다.
- 고쳤다: `m_activeHandle`이 `Rotate`면 현재 mode와 무관하게 rotate drag update/draw를 계속 타도록 했다.

---

# TODO — SceneView 2D Translate Guizmo

## Goal
SceneView에 에디터 전용 2D Translate Guizmo를 추가한다.

## Assumptions
- Guizmo는 런타임 기능이 아니므로 `Application/Editor/Main/Guizmo/` 아래에 둔다.
- 1차 구현은 Translate만 실제 동작시키고, Rotate/Scale/3D는 구조만 열어 둔다.
- 드래그 중에는 preview로 transform을 직접 갱신하고, release 때 old/new transform 스냅샷 커맨드 1개만 기록한다.
- 부모+자식 동시 선택은 기존 `Editor::GetSelectedTopLevel()` 정책을 사용한다.

## Success Criteria
- 선택된 2D object에 X/Y/XY translate handle이 SceneView overlay로 표시된다.
- handle hover/click/drag가 기존 object picking/box selection을 소비한다.
- 드래그는 화면 공간 handle hit-test와 world delta 변환을 통해 transform을 preview 갱신한다.
- release 후 undo 1회로 드래그 시작 전 transform으로 돌아가고, redo 1회로 끝 transform이 복원된다.
- Game build에는 Guizmo 파일이 포함되지 않는다.

## Plan
- [x] Guizmo 타입/controller/2D/3D stub 파일 추가
- [x] old/new transform 스냅샷 editor command 추가
- [x] SceneViewTool에 Guizmo host 연결
- [x] Application project 파일 동기화
- [x] 빌드 검증 및 커밋

## Verification
- [x] `Debug_Editor|x64` build
- [x] `Debug_Game|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: SceneViewTool은 이미 하나의 `InvisibleButton`으로 SceneView 입력을 소유하고, 선택/박스선택/우클릭 팬 상태 머신을 그 아이템 상태에 묶어 처리하고 있었다.
- 생각했고: Guizmo가 별도 Tick이나 별도 런타임 renderer path를 만들면 기존 SceneView 입력 계약과 editor/runtime 경계를 깨므로, SceneViewTool이 frame context만 넘기고 Guizmo가 소비 결과를 반환하는 구조가 맞다.
- 반례를 찾았고: handle 클릭이 기존 좌클릭 상태 머신에도 전달되면 object picking 또는 box selection이 동시에 실행될 수 있다.
- 고쳤다: `CEditorGuizmoController`/`CGuizmo2D`를 `Application/Editor/Main/Guizmo`에 추가하고, `GuizmoFrameResult::ConsumedMouse`가 true인 프레임은 SceneView 선택 인텐트를 초기화하도록 했다.
- 추가로 읽었고: Inspector transform command는 모든 대상에 같은 local delta를 적용하는 `CSetObjectTransformCommand`를 사용한다.
- 생각했고: Guizmo translate는 world-space delta가 기본이고, 서로 다른 부모를 가진 다중 선택은 대상별 local delta가 달라질 수 있다.
- 반례를 찾았고: 부모가 회전/스케일된 object에 같은 local delta를 적용하면 화면상 같은 world 이동이 되지 않는다.
- 고쳤다: Guizmo release 시 old/new local transform 스냅샷을 한 번만 커밋하는 `CSetObjectTransformsCommand`를 추가했다.
- 추가 반례: 문서 기본 pivot은 selection center인데 active object를 우선하면 다중 선택 pivot이 사용자의 기대와 달라진다.
- 고쳤다: controller 기본 pivot을 `SelectionCenter`로 두고, active object pivot은 명시 모드일 때만 사용하도록 했다.
- 검증 중 첫 `Debug_Editor` 링크는 실행 중인 `Build/Debug_Editor/Application.exe` 잠금으로 실패했다.
- 고쳤다: 해당 프로세스만 종료한 뒤 재빌드했고, `Debug_Editor|x64`와 `Debug_Game|x64`가 통과했다.

---

# TODO — 기본 Sprite instanced batching 구현

## Goal
기본 sprite pipeline을 쓰는 연속 RenderItem을 instance buffer로 묶어 draw call과 constant buffer update 횟수를 줄인다.

## Assumptions
- 커스텀 material/pipeline은 batch 호환성을 알 수 없으므로 기존 per-sprite draw 경로로 유지한다.
- 투명도 순서 보존을 위해 RenderItem을 재정렬하지 않고, 현재 정렬 결과에서 연속된 같은 mesh/texture/sampler 구간만 batch한다.
- Batch shader는 D3D11/HLSL, WebGPU/WGSL, Vulkan/SPIR-V 모두 제공한다.

## Success Criteria
- 기본 sprite pipeline을 사용하는 연속 item은 `DrawIndexedInstanced`를 탄다.
- batch는 view constant buffer 1개와 instance vertex buffer 1개를 사용한다.
- custom pipeline, invalid resource, filtered/excluded item 경계에서는 기존 단일 draw 경로 또는 batch flush가 동작한다.
- D3D11/WebGPU/Vulkan 공통 renderer path를 유지한다.

## Plan
- [x] batch shader/pipeline 추가
- [x] view constant buffer와 sprite instance buffer pool 추가
- [x] RenderImpl/RenderFiltered batching 적용
- [x] SDK mirror 동기화
- [x] 빌드/Web 검증 및 커밋

## Verification
- [x] `Debug_Game|x64` build
- [x] `Debug_Editor|x64` build
- [x] Web Release build with `SampleProject/Project.Jproject`
- [x] `git diff --check`

## Review
- 코드를 읽었고: Forward2DRenderer의 기본 sprite draw는 item마다 constant buffer를 갱신하고 `DrawIndexed`를 호출하는 구조였다.
- 생각했고: 이전 단계에서 RHI instanced draw 계약을 만들었으므로, 기본 quad mesh와 같은 texture/sampler를 쓰는 연속 item은 instance buffer로 묶을 수 있다.
- 반례를 찾았고: 투명도 정렬을 바꾸면 blending 결과가 달라질 수 있으므로 재정렬 batch는 하지 않고 현재 정렬 결과의 연속 구간만 묶었다.
- 고쳤다: batch 전용 view constant buffer와 sprite instance buffer pool을 추가하고, D3D11/HLSL, WebGPU/WGSL, Vulkan/SPIR-V batch vertex shader를 추가했다.
- 추가로 읽었고: batch pipeline 생성은 최적화 경로인데 초기화 필수 조건에 넣으면 shader/backend 문제 하나로 기존 단일 sprite 렌더까지 실패할 수 있었다.
- 생각했고: batch는 실패해도 correctness를 깨면 안 되는 선택 경로여야 한다.
- 반례를 찾았고: `DrawSpriteBatch()` 실패 후 batchCount만큼 인덱스를 넘기면 해당 sprite들이 화면에서 사라진다.
- 고쳤다: batch pipeline은 best-effort로 생성하고, batch draw 실패 시 현재 item을 기존 `DrawSpriteItem()` 경로로 그리도록 fallback을 보장했다.
- 추가 반례: SDK public mirror와 engine header가 갈라지면 script/sample 쪽에서 다른 renderer layout을 보게 된다.
- 고쳤다: `SDK/Include/Core/Renderer/Forward2DRenderer.h`와 `SpriteVulkanShaders.h`를 engine source와 동기화했다.

---

# TODO — RenderItem draw resource pre-resolve

## Goal
Sprite render loop에서 material virtual getter 반복 조회를 줄이고, 추후 멀티스레드 렌더링에 맞게 RenderItem이 렌더에 필요한 RHI 리소스를 제출 시점에 들고 있게 한다.

## Assumptions
- 현재 `CRenderMaterial`은 생성 후 pipeline/texture/sampler setter가 없는 immutable resource이다.
- 외부 submitter가 새 필드를 채우지 않아도 기존 `Material` fallback으로 동작해야 한다.
- SDK mirror도 같은 `RenderItem` layout을 가져야 한다.

## Success Criteria
- `SpriteRenderSystem`은 `RenderItem` 제출 시 Pipeline/Texture/Sampler를 함께 채운다.
- `Forward2DRenderer`는 먼저 RenderItem의 pre-resolved resource를 쓰고, 없으면 기존 Material getter로 fallback한다.
- batch run 탐색은 같은 item의 texture/sampler/pipeline을 반복 조회하지 않는다.
- 기존 단일 draw fallback과 custom material fallback은 유지된다.

## Plan
- [x] `RenderItem`에 pre-resolved Pipeline/Texture/Sampler 추가
- [x] `SpriteRenderSystem` 제출 시 draw resource 채우기
- [x] `Forward2DRenderer` resource resolve helper 적용
- [x] SDK mirror 동기화
- [x] 빌드/Web 검증 및 커밋

## Verification
- [x] `Debug_Game|x64` build
- [x] `Debug_Editor|x64` build
- [x] Web Release build with `SampleProject/Project.Jproject`
- [x] `git diff --check`

## Review
- 코드를 읽었고: `SpriteRenderSystem`은 `RenderItem` 제출 시 material queue만 해석하고, renderer loop에서 pipeline/texture/sampler를 다시 material virtual getter로 반복 조회했다.
- 생각했고: 렌더 제출 시점에 draw resource를 같이 넣으면 Forward2DRenderer의 batch 탐색과 fallback draw가 같은 item resource를 재조회하지 않아도 된다.
- 반례를 찾았고: 외부 submitter가 새 필드를 채우지 않으면 기존 렌더가 깨질 수 있다.
- 고쳤다: `RenderItem`에 Pipeline/Texture/Sampler를 추가하되, `Forward2DRenderer::ResolveSpriteDrawResources()`가 pre-resolved 값이 비어 있으면 기존 `Material` getter로 fallback하도록 했다.
- 추가로 읽었고: `CRenderMaterial`은 생성자로 받은 pipeline/texture/sampler를 setter 없이 반환하는 immutable resource 형태였다.
- 생각했고: 같은 프레임의 render item에 resource snapshot을 저장하는 것은 현재 material 구조와 맞고, 멀티스레드 render command 준비에도 유리하다.
- 반례를 찾았고: custom material pipeline은 batch 대상이 아니어야 하며, default sprite pipeline만 instancing 대상이어야 한다.
- 고쳤다: batch 가능 판정은 resolved pipeline이 기본 sprite pipeline과 같은 경우로 제한하고, custom pipeline은 기존 단일 draw 경로로 유지했다.
- 검증 중 `Debug_Editor` 첫 빌드는 stale MSBuild node/PDB/PCH 중간 산출물 문제로 실패했다.
- 고쳤다: MSBuild node reuse 프로세스를 종료하고 `Build/Intermediate/Engine/Debug_Editor/x64` 중간 산출물만 정리한 뒤 `/nr:false` 단일 빌드로 통과시켰다.

---

# TODO — Sprite instancing RHI 계약 추가

## Goal
Sprite batching/instancing을 구현하기 위한 공통 RHI 입력 레이아웃과 instanced draw 명령을 추가한다.

## Assumptions
- 실제 sprite batch renderer는 다음 단계에서 구현한다.
- 이번 단계는 D3D11/WebGPU/Vulkan 모두 같은 RHI 계약을 지원하도록 만드는 기반 작업이다.
- 기존 vertex element는 기본값 `InputSlot=0`, `InputRate=PerVertex`로 유지해 기존 파이프라인 동작을 바꾸지 않는다.

## Success Criteria
- `RHIVertexElementDesc`가 input slot과 per-vertex/per-instance rate를 표현한다.
- D3D11/WebGPU/Vulkan pipeline 생성이 slot별 vertex buffer layout을 만든다.
- D3D11/WebGPU/Vulkan command context가 `DrawIndexedInstanced`를 지원한다.
- 기존 sprite pipeline은 기존과 동일하게 컴파일된다.

## Plan
- [x] RHI vertex input contract 확장
- [x] D3D11/WebGPU/Vulkan pipeline input layout 적용
- [x] D3D11/WebGPU/Vulkan instanced indexed draw 추가
- [x] SDK mirror 확인
- [x] 빌드 검증 및 커밋

## Verification
- [x] `Debug_Game|x64` build
- [x] `Debug_Editor|x64` build
- [x] Web Release build with `SampleProject/Project.Jproject`
- [x] `git diff --check`

## Review
- 코드를 읽었고: 기존 RHI vertex input은 slot 0/per-vertex로 고정되어 있고, indexed instanced draw API가 없었다.
- 생각했고: sprite batching/instancing을 renderer에 직접 넣기 전에 D3D11/WebGPU/Vulkan 공통 계약이 먼저 필요하다.
- 반례를 찾았고: 한 backend만 임시 구현하면 멀티 플랫폼 렌더러 병렬 개발 규칙을 깨고, Vulkan/WebGPU에서 batch renderer가 막힌다.
- 고쳤다: `RHIVertexElementDesc`에 `InputSlot`/`InputRate`를 추가하고, `IRHICommandContext::DrawIndexedInstanced`를 공통 API로 추가했다.
- 추가로 읽었고: WebGPU는 vertex buffer stride가 `sizeof(float) * 4`로 고정되어 있었다.
- 생각했고: instancing뿐 아니라 다른 vertex layout에서도 잘못된 stride가 될 수 있으므로 slot별 stride 계산이 필요하다.
- 반례를 찾았고: WebGPU/Vulkan은 vertex buffer slot별 layout이 필요하며 slot 0/1을 안정적으로 써야 한다.
- 고쳤다: D3D11/WebGPU/Vulkan pipeline 생성이 element의 input slot/rate를 반영하고 slot별 stride를 계산하도록 변경했다.
- SDK에는 RHI header mirror가 없어 별도 동기화 대상은 없었다.

---

# TODO — RenderScene sort / SpriteRenderSystem 루프 비용 정리

## Goal
멀티 플랫폼 RHI 경계를 흔들지 않는 범위에서 렌더 제출 전 CPU 비용을 먼저 줄인다.

## Assumptions
- Sprite batching/instancing은 RHI vertex input, shader, Vulkan SPIR-V까지 같이 바뀌어야 하므로 별도 큰 단위로 진행한다.
- 현재 RenderScene은 매 프레임 Clear 후 Submit 순서가 이미 정렬되어 있을 수 있다.
- SpriteRenderSystem의 renderer 타입은 한 프레임 루프 안에서 변하지 않는다.

## Success Criteria
- RenderScene은 제출 순서가 이미 Queue/SortOrder 기준 정렬이면 `std::sort`를 호출하지 않는다.
- RenderFiltered/RenderImpl이 같은 scene을 여러 번 렌더해도 이미 정렬된 scene은 재정렬하지 않는다.
- SpriteRenderSystem은 sprite마다 `dynamic_cast<CForward2DRenderer*>`를 반복하지 않는다.
- 렌더 결과 순서 계약은 기존 Queue/SortOrder 기준을 유지한다.

## Plan
- [x] RenderScene incremental dirty sort 추가
- [x] SpriteRenderSystem renderer cast 루프 밖 이동
- [x] 빌드 검증
- [x] 커밋

## Verification
- [x] `Debug_Game|x64` build
- [x] `Debug_Editor|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: `CRenderScene::Sort()`는 렌더 호출마다 무조건 `std::sort`를 수행했고, 같은 scene을 `RenderImpl`/`RenderFiltered`에서 다시 렌더해도 같은 sort를 반복할 수 있었다.
- 생각했고: Clear 후 Submit 되는 순서가 이미 Queue/SortOrder 기준이면 정렬 결과는 동일하므로 `std::sort`를 피할 수 있다.
- 반례를 찾았고: 제출 중간에 낮은 Queue/SortOrder item이 뒤늦게 들어오면 정렬을 유지해야 한다.
- 고쳤다: Submit 시 직전 item과 sort key를 비교해 순서가 깨진 경우만 `m_needsSort`를 세우고, Sort 후에는 dirty를 내린다.
- 추가로 읽었고: `SpriteRenderSystem::OnUpdate()`는 sprite마다 `dynamic_cast<CForward2DRenderer*>`를 반복했다.
- 생각했고: renderer dependency는 한 update 루프 안에서 바뀌지 않으므로 루프 밖에서 한 번만 cast해도 동작이 같다.
- 반례를 찾았고: forward renderer가 없어도 기존 mesh/material이 살아있는 sprite는 submit 가능해야 한다.
- 고쳤다: `forwardRenderer`를 루프 밖에서 캡처하되, 기존 생성 분기 조건만 그대로 사용해 submit 흐름은 유지했다.
- SDK public mirror의 `RenderScene.h`도 같은 private layout으로 동기화했다.

---

# TODO — WebGPU bind group cache lookup 정리

## Goal
WebGPU bind group cache가 많은 sprite에서 선형 검색을 반복하지 않도록 안정적인 draw 순서 기반 cursor를 추가한다.

## Assumptions
- Forward2DRenderer의 draw item 순서는 대체로 프레임 간 안정적이다.
- cache key 비교는 계속 `SafePtr` 비교를 사용한다.
- 순서가 바뀌는 경우에도 correctness가 유지되어야 한다.

## Success Criteria
- 같은 draw 순서에서는 cache hit가 cursor 위치에서 O(1)로 처리된다.
- cache miss 또는 순서 변경 시에도 기존 entry를 SafePtr 비교로 찾거나 새 entry를 cursor 위치에 삽입한다.
- WebGPU cache가 첫 프레임부터 매 draw마다 전체 cache를 선형 검색하지 않는다.

## Plan
- [x] WebGPU bind group cache cursor 추가
- [x] cursor hit/move/insert 흐름 구현
- [x] 빌드 및 Web Release 검증
- [x] 커밋

## Verification
- [x] `Debug_Game|x64` build
- [x] `Debug_Editor|x64` build
- [x] Web Release build with `SampleProject/Project.Jproject`
- [x] `git diff --check`

## Review
- 코드를 읽었고: 직전 WebGPU cache는 `std::vector`를 앞에서부터 순회해 같은 binding 조합을 찾는 구조였다.
- 생각했고: bind group 생성/해제는 줄었지만 stable sprite draw order에서도 N개 sprite가 매 프레임 N단계 선형 탐색을 반복할 수 있다.
- 반례를 찾았고: 첫 프레임 cache 생성 중에도 이미 추가된 entry를 계속 훑으면 O(N²) 초기 비용이 생기고, 이후 프레임도 순서가 같아도 앞에서부터 다시 찾는다.
- 고쳤다: 프레임별 cursor를 추가해 같은 draw 순서에서는 cursor 위치에서 즉시 hit하고, 순서가 바뀐 경우에만 SafePtr 비교로 기존 entry를 찾아 cursor 위치로 이동한다.
- 추가 반례: cursor 뒤쪽만 찾으면 같은 프레임에서 이전 resource 조합을 다시 쓸 때 중복 entry가 생길 수 있어, cursor mismatch일 때만 전체 cache를 검색하도록 보정했다.

---

# TODO — WebGPU bind group / Vulkan descriptor pool 병목 정리

## Goal
WebGPU의 draw별 bind group 생성/해제 병목을 줄이고, Vulkan에서 draw 수가 descriptor pool 고정 크기를 넘으면 이후 draw가 조용히 누락되는 위험을 제거한다.

## Assumptions
- Forward2DRenderer의 per-sprite constant buffer pool은 프레임을 넘어 재사용되므로 WebGPU bind group은 같은 pipeline/buffer/texture/sampler 조합에서 재사용할 수 있다.
- WebGPU bind group cache는 RHI 리소스의 `SafePtr` 유효성을 기준으로 정리해야 한다.
- Vulkan descriptor set은 현재 프레임 안에서만 필요하므로 descriptor pool은 프레임마다 reset하되, 필요한 경우 pool을 추가 생성해 draw 누락을 막는다.

## Success Criteria
- WebGPU Draw/DrawIndexed가 매번 `wgpuDeviceCreateBindGroup`/`wgpuBindGroupRelease`를 반복하지 않는다.
- WebGPU cache 비교는 resource raw pointer가 아니라 `SafePtr` 비교를 사용한다.
- Vulkan descriptor allocation이 첫 pool 한계에 닿아도 추가 pool로 재시도한다.
- 기존 D3D11 경로는 영향받지 않는다.

## Plan
- [x] WebGPU bind group cache 추가 및 수명 정리
- [x] Vulkan descriptor pool 추가 생성/프레임 reset 구조 추가
- [x] Debug_Game/Debug_Editor/Web Release 빌드 검증
- [x] diff 검토 및 커밋

## Verification
- [x] `Debug_Game|x64` build
- [x] `Debug_Editor|x64` build
- [x] Web Release build with `SampleProject/Project.Jproject`
- [x] `git diff --check`

## Review
- 코드를 읽었고: WebGPU `Draw`/`DrawIndexed`가 매 draw마다 `CreateCurrentBindGroup()`로 bind group을 만들고 draw 직후 release하고 있었다.
- 생각했고: 이전 작업으로 constant buffer가 풀 기반 재사용으로 바뀌었으므로 같은 pipeline/buffer/texture/sampler 조합은 bind group도 재사용할 수 있다.
- 반례를 찾았고: 캐시가 resource raw pointer만 보면 `SafePtr` 정책과 어긋나고, resource가 파괴되면 stale bind group을 잡고 있을 수 있다.
- 고쳤다: WebGPU command context에 `SafePtr` 기반 bind group cache를 추가하고, 프레임 시작/소멸/device 변경 시 invalid entry와 API 객체를 정리한다.
- 추가로 읽었고: Vulkan은 draw마다 descriptor set을 새로 allocate/update하며 pool이 1024 set 고정이었다.
- 생각했고: 지금 단계에서 descriptor set 캐시는 constant buffer가 draw마다 달라 효과가 제한적이고, 먼저 1024 draw 이후 조용히 draw가 빠지는 버그 후보가 더 명확하다.
- 반례를 찾았고: `vkAllocateDescriptorSets` 실패 시 `BindPendingDescriptors()`가 그냥 return하므로 sprite가 많은 장면에서 일부 draw가 descriptor 없이 진행될 수 있다.
- 고쳤다: Vulkan descriptor pool을 프레임 reset 가능한 pool 목록으로 바꾸고, 현재 pool 할당 실패 시 추가 pool을 생성해 재시도한다.
- 검증 중 사용자 프로젝트 경로 `C:\Users\박주형\Desktop\Project\Project.Jproject`는 현재 존재하지 않아 Web 검증은 `SampleProject/Project.Jproject`로 대체했다.

---

# TODO — 렌더 반복 계산/상태 바인딩 병목 정리

## Goal
Forward2DRenderer에서 draw마다 반복되는 카메라 계산과 중복 RHI state bind를 줄인다.

## Assumptions
- RHI command context는 마지막으로 바인딩된 pipeline/mesh/texture/sampler 상태를 유지한다.
- Constant buffer는 draw마다 달라지므로 계속 바인딩해야 한다.
- SafePtr 비교 연산을 추가하고, state cache는 raw pointer가 아니라 SafePtr 비교를 사용한다.

## Success Criteria
- RenderImpl의 view parameter 계산이 item 루프 밖으로 이동한다.
- 같은 pipeline, vertex buffer, index buffer, texture, sampler는 같은 render 호출 내에서 중복 Set 호출을 피한다.
- RenderImpl, RenderFiltered, FillViewportColor가 같은 draw helper를 사용한다.
- D3D11/WebGPU/Vulkan 공통 renderer path를 유지한다.

## Plan
- [x] View parameter helper 추가
- [x] Render state cache/helper 추가
- [x] RenderImpl/RenderFiltered/FillViewportColor 적용
- [x] 정적 검색 및 빌드 검증

## Verification
- [x] `Debug_Game|x64` build
- [x] `Debug_Editor|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: `RenderImpl`은 item마다 view half extent/camera row 재계산을 했고, pipeline/mesh/texture/sampler state도 매 draw마다 무조건 다시 바인딩했다.
- 생각했고: constant buffer는 draw마다 바뀌므로 유지해야 하지만, view parameter 계산과 동일 resource state bind는 render 호출 범위에서 줄일 수 있다.
- 반례를 찾았고: state cache를 raw pointer로 두면 `SafePtr` 의미와 어긋나므로 `SafePtr` 비교 연산을 추가하고 cache도 `SafePtr`로 보관해야 한다.
- 고쳤다: `SafePtr`에 `==`/`!=` 비교를 추가했고, `Forward2DRenderer`는 `ViewParameters`와 `RenderStateCache`를 통해 반복 계산/중복 state bind를 줄인다.
- 추가 반례: mesh는 유효하지만 내부 vertex/index buffer가 죽은 경우 SetBuffer가 무시된 뒤 DrawIndexed가 호출될 수 있어 draw helper에서 선검증한다.
- SDK public mirror의 `SafePtr.h`와 `Forward2DRenderer.h`도 같이 동기화했다.
- 남은 큰 병목 후보는 WebGPU per-draw bind group 생성, Vulkan per-draw descriptor set allocate/update, sprite batching/instancing 부재다.

---

# TODO — 하이라키 오브젝트 순서 안정화

## Goal
오브젝트 추가 시 하이라키 "맨 아래"에 쌓이고, 시뮬레이션 정지 후에도 순서가 유지되도록 안정적인 생성순서 키를 도입한다.

## 근본 원인
하이라키 표시 순서 = `CScene::ForEachObject`(= 풀 슬롯 순회) 순서. 풀 슬롯 순서는 생성순서와 무관하다.
- 신규: `AddChunk` 가 free list 를 slot9→slot0 로 쌓아 할당은 slot9 부터, 순회는 slot0 부터 → 역순. 새 오브젝트가 위로 쌓임.
- 시뮬정지: 스폰/파괴로 슬롯이 free list 에 LIFO 재배치 → 재사용 순서가 원래 위치와 어긋나 순회 순서가 섞임.

## Assumptions
- 자식 순서도 풀 순회 기반(`childrenByParent`)이라 동일 증상 → 같은 키로 해결.
- CreationOrder 는 런타임/직렬화 양쪽에서 순서 기준이 되며, 저장 목록도 같은 키로 정렬해야 reload 후에도 일관.

## Success Criteria
- 새 오브젝트가 하이라키 맨 아래에 추가된다(형제 그룹 내 마지막).
- 시뮬레이션 정지 후 순서 불변.
- 저장→로드 후 순서 보존.

## Plan
- [ ] CGameObject: `std::uint64_t CreationOrder = 0;` 추가(비직렬화 — 로드 시 파일 순서로 재할당)
- [ ] CScene: `m_nextCreationOrder` 카운터, `CreateGameObject` 에서 할당
- [ ] HierarchyTool: root/형제 그룹을 CreationOrder 로 정렬
- [ ] SceneSerializer: 저장 목록을 CreationOrder 로 정렬(reload 일관성)
- [ ] 빌드 Debug_Editor + Debug_Game

## Verification
- [ ] 빌드 양쪽 EXIT 0
- [ ] 동작: 오브젝트 추가 → 맨 아래 / 시뮬 정지 → 순서 유지 (사용자 확인)

## Review

### 하이라키 순서 (CreationOrder)
- 원인: 표시 순서가 풀 슬롯 순회 순서였고, 슬롯 순서는 생성순서와 무관(할당 역순·재사용 섞임).
- 수정: CGameObject.CreationOrder(단조), CreateGameObject 부여, Hierarchy root/형제 정렬, SceneSerializer 저장 정렬.
- 검증: Game 컴파일+링크 클린(exe 생성), Editor 컴파일 클린(링크는 exe 실행중 잠금=코드무관).

### 게임 빌드 실패 2건 (이 세션 추가 발견)
1. Web 링크: `web_game_sources.txt` 에 삭제된 SceneSnapshot.cpp 잔존 + 신규 AudioEffectAsset.cpp 누락
   → SceneSnapshot 제거, AudioEffectAsset 추가. (SceneDebugDrawSystem 은 에디터 전용이라 제외 유지)
2. 에셋팩 실패(Windows+Web 공통): `.jfx`(AudioEffect) 메타가 Type=Unknown 으로 저장돼 패키징 첫 분기 거부.
   근본: AssetMetaFile ToString/ParseType 에 AudioEffect 분기 누락(과거 Audio 와 동일 버그 재발).
   → 두 곳에 AudioEffect 추가. 기존 자가복구(Importer→ParseType)가 디스크의 Type:Unknown 메타도 로드시 회복.
- 검증: Engine.lib Debug_Editor EXIT 0.

### Not Verified
- 실제 게임 빌드(Windows/Web) 재실행 후 통과 여부 — 사용자 확인 필요.
- 하이라키 동작(추가→맨아래 / 시뮬정지→순서유지) 런타임 확인 — 사용자 확인 필요.

# Android 화면 방향(orientation) 처리 — 작업 정리 / 핸드오프

> 콘텍스트 압축 대비 자급식 문서. 콜드 재개 시 이 파일 + `tasks/AndroidNativeBuildPlan.md` 먼저 읽기.
> 브랜치: `feat/input-touch-mobile-web`. 기기: Galaxy S23 Ultra(SM-S918N, arm64, Adreno).

## 큰 그림 (지금 상태)
Android 네이티브 런타임은 **동작**한다(Vulkan 렌더 + 씬 + 스크립트 + AAudio, 실기기 검증 완료, 커밋 `363acf7`).
남은 단 하나의 문제 = **화면 방향(회전) 보정**. 현재 화면이 윈도우 빌드와 다르게(회전/줌 어긋남) 나온다.

## 문제 본질 (실측으로 규명)
- 패널이 **세로 네이티브**인 기기에서 앱은 **가로 고정**(manifest `screenOrientation="sensorLandscape"`).
- Vulkan surface = **세로 버퍼 1440x3088**, `currentTransform=IDENTITY`(=1) 로 보고.
  → **Samsung 은 transform 을 IDENTITY 로 거짓 보고하면서 시스템이 버퍼를 90° 돌려 가로로 표시.**
  Vulkan 표준 신호(surface transform)를 못 믿는 기기.
- 앱 윈도우 실제 = **가로 3088x1440** (`dumpsys window` 의 `cur=3088x1440`).
- 카메라가 버퍼(세로) 종횡비를 쓰면: 전단(평행사변형)/회전/줌 어긋남.

## 좌표/기하 메모 (재구현 시 필수)
- 버퍼(렌더 타깃) = 1440x3088 세로. 뷰포트는 **이 네이티브 크기** 써야 함(안 그럼 잘림).
- 표시(디스플레이) = 3088x1440 가로. **카메라 종횡비는 이 가로 기준**이어야 정상.
- 시스템이 버퍼를 90° 회전해 표시 → 콘텐츠를 클립공간서 **-90°(보정)** 회전해 렌더해야 최종 똑바로.
- 즉: ① 카메라 aspect = 표시(가로) ② 콘텐츠 클립 90° 회전 ③ 뷰포트 = 네이티브(세로).
  세 개 맞으면 균일·정상(검증된 수식: 가로/세로 px-per-world ≈ 동일).

## 이미 한 것 — 커밋됨 (`6c41198` fix(render): handle Android surface pre-rotation)
- **렌더러 클립 회전**: `CForward2DRenderer::ApplySurfacePreRotation(row0,row1)` — 뷰행렬(2x3)에 surface 회전 S=[c -s; s c] 후처리. `SetSurfacePreRotation(cos,sin)` 로 주입. `BuildSpriteConstants`/`BuildSpriteViewConstants` 양쪽 적용. (ViewRow 는 float[4] 패딩 주의.)
- **IRenderer::SetSurfacePreRotation(cos,sin)** 인터페이스 추가.
- **스왑체인**: `GetSize()` = 네이티브 extent, `AcquireNextImage` 가 매 프레임 surface caps 확인 → transform/extent 변하면 **자가 recreate**(런타임 회전 트리거), `OUT_OF_DATE` 처리. `IRHISwapchain::GetPreRotationCos/SinR` (현재 미사용, 무해).
- **엔진**: `CEngine::GetRenderTargetSize()`(표시방향 크기), 렌더 경로서 `SetSurfacePreRotation` + 카메라 종횡비 배선.
- **결과**: 전단(평행사변형)은 **사라짐**(카메라가 버퍼와 일치). 하지만 방향/줌은 여전히 어긋남(아래 이유).

## 이미 한 것 — 미커밋 WIP (JNI 회전, 신호 불충분 판명)
파일: `IPlatform.h`(GetDisplayRotationDegrees), `MobilePlatform.{h,cpp}`(저장/Set/override), `AndroidMain.cpp`(`QueryDisplayRotation` JNI = `activity.getWindowManager().getDefaultDisplay().getRotation()`, `UpdateDisplayRotation` 호출: init+CONFIG_CHANGED+RESIZE), `Engine.cpp`(GetRenderTargetSize/SetSurfacePreRotation 를 `m_platform->GetDisplayRotationDegrees()` 기준으로), `VulkanSwapchain.cpp`(GetSize 네이티브 반환).

### ⚠ 핵심 발견 (왜 JNI 단독으론 안 되나)
`Display.getRotation()` = **기기 물리 회전**(폰 책상에 세로로 두면 0). **앱 창 방향이 아님.**
방향 **고정**(sensorLandscape) 앱은 기기가 세로(getRotation=0)여도 창은 가로 강제 →
getRotation(0) 으론 "90° 회전 필요"를 못 잡음. 실측: `Display rotation: 0 degrees` 인데 화면은 가로.
→ **순수 JNI getRotation 은 방향고정 게임엔 부적합 신호.** 단독 구현으론 현재 케이스 못 고침.

## ✅ 구현 완료 (정공법 = desired orientation 권위) — 2026-06-15
빌드설정 "화면 방향"(Landscape/Portrait/Auto) 을 회전 권위로 추가. 전 계층 배선 완료, 컴파일+툴 라운드트립 검증.
- **타입**: `PlatformTypes.h` `EScreenOrientation{Auto,Landscape,Portrait}` + `PlatformDesc.DesiredOrientation`.
- **플랫폼**: `IPlatform::Get/SetDesiredOrientation`(기본 Auto), `CMobilePlatform` 저장(Initialize 에서 desc 복사).
- **엔진 회전 계산**(`Engine.cpp`): `GetEffectiveDisplayRotation()` — Auto 면 JNI getRotation, 그 외엔 desired-vs-버퍼방향 비교(mismatch→90). `GetNativeRenderBufferSize()` 추출. 렌더경로 preRot + `GetRenderTargetSize` 둘 다 이 함수 사용.
- **런타임 주입**(`Application.cpp` OnPreInitialize): manifest.Orientation 문자열 → `platformDesc.DesiredOrientation`.
- **manifest**: `BuildManifest.Orientation` 필드, binary write/read(ProductName 뒤 append, 전방호환) + yaml `orientation` 키, `BuildManifestTool --orientation`.
- **빌드설정**: `ProjectInfo.Build.AndroidOrientation`(.Jproject 직렬화) + `BuildSettingsWindow` 콤보 + loc(ko/en).
- **BuildGame.ps1**: Read-JBroProject 기본값 Landscape, Write-JBroBuildManifest `-Orientation`, AndroidManifest `screenOrientation`(Landscape→sensorLandscape / Portrait→sensorPortrait / Auto→fullSensor).
- **버그 수정**(부수): `BuildManifestTool.vcxproj` 의 `AdditionalLibraryDirectories` 가 stale `Engine\x64`(2026-06-07 고정 leftover) 를 링크 → 실제 OutDir `$(RepoRoot)x64\$(Configuration)` 로 수정. 이전엔 구조체 변경이 없어 잠복, Orientation 필드 추가가 ABI 불일치(0xC0000409/AV)를 노출.

### 남은 검증 (기기 필요)
- 실기기 스크린샷으로 방향 정상 확인.
- 회전 부호(90 vs 270): `Engine.cpp` GetEffectiveDisplayRotation 은 mismatch 시 90 고정. 틀리면 270 으로(또는 preRotSin 부호) 뒤집기.
- sensorLandscape(두 가로 모두 허용) 자동회전 시 90/270 분기는 아직 없음 — 필요하면 Auto 모드처럼 getRotation 결합.
- 런타임 방향 지정 API(스크립트)는 미구현(후순위, 사용자 요청).

## (구) 다음 작업 (정공법 = 사용자 채택 방향 #1)
**게임 desired orientation(빌드설정) 을 회전의 권위 신호로.**
- 회전 = `(desired orientation) vs (버퍼 방향)` 비교.
  - desired=Landscape && 버퍼 세로(h>w) → 콘텐츠 90°(또는 270) + 종횡비 swap.
  - desired=Landscape && 버퍼 가로 → 0°.
  - desired=Portrait 대칭.
  - desired=Auto → device rotation(getRotation, 이미 만든 JNI) 추가 사용해 4방향.
- **빌드설정에 "초기 화면 방향" 추가**(사용자 요청): `ProjectInfo.Build` 에 orientation enum(Landscape/Portrait/Auto). `.Jproject` 직렬화 + 에디터 ProjectSettings UI + manifest `screenOrientation` 생성에 반영(BuildGame.ps1).
- **런타임 방향 지정 API**(사용자 요청, 후순위): 스크립트에서 방향 바꾸기 → manifest 고정과 별개로 런타임 회전 허용/잠금.
- 회전 부호(90 vs 270, sin 방향)는 **기기서 스크린샷 보고 튜닝**(현재 Engine.cpp 의 90→(0,1) 매핑이 맞는지 미검증). 틀리면 sin 부호만 뒤집기.

### 구현 위치 요약
- desired orientation 저장: `Engine.Platform` 또는 RuntimeConfig 에. 엔진 렌더 경로가 읽어 회전/aspect 계산.
- 현재 `Engine.cpp` 가 `m_platform->GetDisplayRotationDegrees()` 쓰는 부분을 desired-vs-buffer 로 교체(또는 결합).
- 버퍼 방향 = `GetRenderTargetSize` 내부의 nativeSize(swapchain GetSize) 가로/세로.

## 기기 테스트 루프 (무선 adb — USB 안 됨)
- 폰 무선 디버깅 ON, **포트가 자주 바뀜** → 매번 폰 "무선 디버깅" 화면의 `IP:포트` 받아서 `adb connect`. 페어링 만료 시 "페어링 코드로 기기 페어링" → `adb pair <ip:pairport> <code>`.
- 폰이 자주 잠김 → `adb shell input keyevent KEYCODE_WAKEUP`. 잠금화면 캡처되면 무효(폰 깨워 앱 포커스 확인: `dumpsys window | grep mCurrentFocus`).
- 빌드: `BuildScripts\BuildGame.ps1 -Project .\SampleProject\Project.Jproject -Platform Android -Configuration Debug -AndroidSdkRoot C:\Android` (env: JAVA_HOME jdk-21, ANDROID_HOME C:\Android). 멀티 ABI(arm64+x86_64) 자동.
- 설치/실행/캡처: `adb -s <dev> install -r "C:\Users\박주형\Downloads\Build\My First Build-Android-Debug\My First Build.apk"` → `am start -n com.jbro.project/android.app.NativeActivity` → `exec-out screencap -p > shot.png`.
- 엔진 로그: `adb -s <dev> logcat -s JBroEngine` (logcat 출력 켜져있음, tag JBroEngine).

## 참고 핵심 파일
- `Engine/Core/Renderer/Forward2DRenderer.cpp` (ApplySurfacePreRotation, BuildSprite*Constants, 뷰행렬 수식)
- `Engine/Core/Engine.cpp` (GetRenderTargetSize, 렌더 경로 SetSurfacePreRotation)
- `Engine/Core/RHI/Vulkan/VulkanSwapchain.cpp` (extent/transform/recreate)
- `Engine/GameFramework/Rendering/GameCamera.cpp` (CollectGameRenderCameras: aspect, OrthoSize/OrthoSizeX, viewport)
- `Application/Entry/AndroidMain.cpp` (JNI QueryDisplayRotation, lifecycle)
- `Application/Application.cpp` (ConfigureRuntimeViewCamera: 카메라 종횡비 = GetRenderTargetSize)
- `Engine/Editor/Project/ProjectTypes.h` / `ProjectManager.cpp` / `BuildScripts/BuildGame.ps1` (빌드설정 orientation 추가 지점)

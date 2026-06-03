# Mobile Platform Plan

## 목적

JBroEngine 에 Android/iOS 모바일 플랫폼을 추가하기 위한 설계 계획입니다.

목표는 Windows/Web 에 임시 분기를 덧붙이는 것이 아니라, 기존 빌드 파이프라인의 공통 계약을 유지하면서 모바일 특화 차이를 명확한 계층에 격리하는 것입니다.

핵심 기준:

- 에디터 GameView, Windows build, Web build, Mobile build 는 가능한 한 같은 `CGameApplication` runtime bootstrap 을 사용합니다.
- 플랫폼 차이는 entry point, render surface, filesystem, asset/package mounting, input, audio, script link 방식, app lifecycle 계층에 둡니다.
- 에디터 전용 산출물과 localization 은 모바일 runtime package 에 포함하지 않습니다.
- asset 은 loose source file 이 아니라 build manifest 와 asset pack 계약을 유지합니다.

## 현재 기준점

이미 존재하는 플랫폼 축:

- Windows
  - exe + `GameScript.dll`
  - native filesystem package
  - D3D11 renderer

- Web
  - `index.html/js/wasm/data`
  - Emscripten preload filesystem
  - static script module
  - WebGPU renderer

모바일은 Web 과 유사하게 dynamic DLL 을 기본으로 기대하기 어렵고, package/resource 접근도 OS sandbox 를 거치므로 Windows staging 을 재사용하면 안 됩니다.

## 플랫폼 분류

### 공통 플랫폼 설정

모든 플랫폼 공통:

- ProductName
- BuildConfiguration
- StartupScene
- BuildScenes
- OutputDirectory
- IconAssetGuid
- SplashAssetGuid
- Orientation 정책
- PackageVersion
- CompanyName / BundleIdentifier seed

### Android 전용 설정

- ApplicationId
- MinSdkVersion
- TargetSdkVersion
- ABI 목록
  - arm64-v8a 기본
  - armeabi-v7a 는 후순위 또는 제외 검토
  - x86_64 는 emulator/debug 용
- Keystore 설정
  - Debug keystore 자동
  - Release keystore path/password alias 는 secret 처리 필요
- Gradle project template 선택
- Android manifest 권한
- App icon density asset 생성 정책
- Splash screen theme 설정
- Native library packaging 방식

### iOS 전용 설정

- BundleIdentifier
- TeamId
- ProvisioningProfile
- SigningCertificate
- MinimumOSVersion
- DeviceFamily
  - iPhone
  - iPad
  - Universal
- Orientation
- Asset catalog icon set
- Launch screen storyboard/template
- Xcode project/workspace 생성 정책

## 빌드 파이프라인 구조

### 공통 단계

모든 플랫폼에서 유지할 단계:

1. Build settings normalize
2. Project/scene save
3. Startup scene validation
4. Build scene validation
5. Asset dependency collection
6. Asset import/cook
7. Asset pack 생성
8. Build manifest 생성
9. Runtime executable/module build
10. Platform package staging
11. Editor-only artifact 검증
12. Package smoke validation

주의:

- asset pack 과 manifest 는 플랫폼별 package 안의 위치만 달라지고 구조는 공유합니다.
- 모바일에서 source asset path, original filename, editor localization 은 release package 에 들어가면 안 됩니다.
- Web 처럼 static script 로 갈지, 모바일 native library 로 갈지는 플랫폼별 script contract 로 분리합니다.

### Android 단계

1. Native engine static/shared library build
   - `libJBroGame.so` 또는 `libJBroEngine.so + libGameScript.so`
   - 초기 구현은 static script linked single `.so` 를 추천합니다.

2. Gradle project 생성/갱신
   - `PlatformBuild/Android` template 사용
   - `app/src/main/assets/Content` 에 manifest/pack 배치
   - `app/src/main/jniLibs/<abi>` 또는 CMake externalNativeBuild 사용

3. Java/Kotlin Activity
   - NativeActivity 또는 custom Activity 선택
   - SurfaceView/NativeWindow 연결
   - lifecycle callback 전달

4. Packaging
   - Debug: unsigned/debug signed APK
   - Release: AAB 우선, APK 옵션 제공

5. Verification
   - APK/AAB 생성
   - `aapt`/Gradle task 성공
   - package 내부에 `Content/game_assets.jbpack`, manifest 존재
   - loose `Assets`, `SDK`, `Editor`, `Localization` 없음

### iOS 단계

1. Native engine static library build
   - iOS 는 dynamic library 제약이 크므로 static link 우선
   - `libJBroGame.a` 또는 framework 검토

2. Xcode project 생성/갱신
   - `PlatformBuild/iOS` template 사용
   - bundle resource 에 `Content` package 포함
   - app delegate/lifecycle bridge 추가

3. Vulkan 또는 WebGPU 대체 전략 결정
   - iOS browser WebGPU 와 native WebGPU 는 별개로 봐야 합니다.
   - native iOS 에서는 Vulkan을 직접 제공하지 않으므로 Vulkan 방향을 유지하려면 MoltenVK 같은 Vulkan-to-Metal 계층이 필요합니다.

4. Signing
   - Debug simulator/device signing
   - Release provisioning/certificate

5. Verification
   - simulator build
   - device build
   - archive/export
   - package resource 검증

## Runtime 계층 요구사항

### Application entry

플랫폼별 entry 는 얇게 유지합니다.

- Windows: WinMain/main
- Web: Emscripten main loop
- Android: JNI/NativeActivity entry
- iOS: UIApplication/AppDelegate bridge

공통 runtime:

- `CGameApplication::InitializeRuntimeGame`
- `LoadRuntimeBuildManifest`
- `LoadRuntimeScriptModule`
- `LoadRuntimeStartupScene`
- `ConfigureRuntimeViewCamera`
- 공통 tick/render loop

금지:

- Android/iOS 전용 scene load 구현을 따로 만들지 않습니다.
- 플랫폼 entry 에서 scene/script/asset 로딩 규칙을 복사하지 않습니다.

### Lifecycle

모바일에는 suspend/resume 이 필수입니다.

필요 callback:

- OnAppStart
- OnAppPause
- OnAppResume
- OnAppStop
- OnLowMemory
- OnSurfaceCreated
- OnSurfaceResized
- OnSurfaceDestroyed

엔진 영향:

- RHI device/swapchain 재생성
- audio pause/resume
- input state reset
- timer delta spike 방어
- asset streaming 중단/재개

### Filesystem

모바일은 package resource 와 writable data path 가 분리됩니다.

필요 경로 API:

- GetBundleResourcePath
- GetPersistentDataPath
- GetCachePath
- OpenPackFromBundle
- OpenUserFile

주의:

- Android asset manager 는 일반 파일 경로가 아닐 수 있습니다.
- iOS bundle resource 는 read-only 입니다.
- save data 는 package 내부가 아니라 persistent path 로 가야 합니다.

### Asset loading

모바일 release package:

- `Content/build_manifest.jbmanifest`
- `Content/game_assets.jbpack`
- optional debug metadata 는 release 제외

추가 요구:

- pack reader 가 memory mapped file 에만 의존하면 Android asset manager 에서 막힐 수 있습니다.
- stream/file abstraction 은 `IStream` 또는 `IFileReader` 계층으로 가는 것이 안전합니다.
- 압축/암호화가 들어가면 mobile CPU/battery 비용 검증이 필요합니다.

### Script

권장 1차 방향:

- Android/iOS 는 static script link 를 기본으로 합니다.
- Windows 의 `GameScript.dll` 모델을 모바일에 그대로 가져가지 않습니다.

이유:

- iOS 는 JIT/dynamic loading 제약이 큽니다.
- Android 도 ABI별 `.so` 관리가 필요하고 hot reload 와 release package 계약이 다릅니다.
- Web static script contract 와 개념적으로 맞출 수 있습니다.

필요 작업:

- `ScriptMode::Static` 을 Web 전용이 아니라 non-Windows package 공통 옵션으로 확장
- generated registry 를 platform compiler 로 빌드
- script public SDK header 가 mobile compiler 에서 self-contained 인지 검증
- yaml-cpp 같은 third-party link 의 transitive dependency 누락 방지

### Rendering

모바일 공통 1차 방향은 Vulkan 입니다.

단, iOS는 OS 기본 그래픽 API가 Metal이고 Vulkan을 직접 제공하지 않습니다.

따라서 iOS에서 Vulkan 계약을 유지하려면 MoltenVK 같은 Vulkan-to-Metal 계층을 패키징/링크/검증 대상에 포함해야 합니다.

Android:

- 우선순위 후보
  - Vulkan
  - OpenGL ES 3.x
  - WebGPU native

추천:

- 단기: Vulkan 기반을 우선 구축
- 장기: RHI 통합 관점에서 Vulkan/WebGPU 계열로 수렴

iOS:

- Vulkan 계약을 유지하려면 MoltenVK 기반 구현이 필요합니다.
- MoltenVK를 쓰지 않는다면 iOS 전용 Metal backend를 별도로 만들어야 하므로, 현재 결정과 충돌합니다.
- OpenGL ES 는 deprecated 이므로 새 구현 기본값으로 부적합합니다.

공통 RHI 요구:

- surface 생성/파괴 분리
- swapchain resize/recreate
- texture format 명시 mapping
- sRGB/linear 색공간 정책
- DPI/content scale 반영
- mobile GPU validation path

색공간 주의:

- 모바일은 sRGB framebuffer, asset gamma, premultiplied alpha 정책이 플랫폼마다 다르게 드러날 수 있습니다.
- Web 에서 발생한 RGBA/BGRA 문제처럼, backend 별 texture format mapping 은 반드시 RHI format 계약을 기준으로 검증해야 합니다.

### Input

필요 입력 모델:

- Touch
- Multi-touch
- Virtual mouse fallback
- Soft keyboard
- Gamepad
- Accelerometer/Gyro 선택

엔진 API 방향:

- 기존 mouse/keyboard 와 touch 를 억지로 같은 타입으로 합치지 않습니다.
- UI/게임 코드가 필요하면 pointer abstraction 을 별도로 제공합니다.
- raw touch list 와 high-level pointer event 를 모두 보존합니다.

### Audio

Android:

- AAudio/OpenSL ES/miniaudio backend 확인

iOS:

- AVAudioSession category/pause/resume 처리 필요

공통:

- app pause 시 audio suspend
- device route change 대응
- compressed audio decode 비용 검증

## Build Settings UI 계획

빌드 설정 UI는 공통/플랫폼별 분리를 강화합니다.

왼쪽 카테고리 제안:

- 일반
- 씬
- 출력
- 아이콘/스플래시
- Windows
- Web
- Android
- iOS
- 고급

공통 탭:

- ProductName
- TargetPlatform
- BuildConfiguration
- StartupScene
- BuildScenes
- OutputDirectory
- IconAssetGuid
- SplashAssetGuid

Android 탭:

- ApplicationId
- Min/Target SDK
- ABI
- Keystore
- Permissions

iOS 탭:

- BundleIdentifier
- TeamId
- Provisioning
- MinimumOSVersion
- DeviceFamily

주의:

- 사용자가 직접 찾을 수 있는 값은 직접 입력시키지 않습니다.
- project/script 경로는 기존 `ProjectManager` 경로 계산 규칙을 사용합니다.
- icon/splash 는 Assets 폴더 내부 asset GUID 로 저장합니다.

## 패키징 계약

### Android package layout

예상:

```text
Project-Android-Release/
  GradleProject/
  Outputs/
    Project-release.aab
    Project-release.apk
  Logs/
```

APK/AAB 내부:

```text
assets/Content/build_manifest.jbmanifest
assets/Content/game_assets.jbpack
lib/arm64-v8a/libJBroGame.so
```

금지:

- `SDK/`
- `Editor/`
- `Localization/`
- loose `Content/Assets/`
- source script `.cpp/.h`

### iOS package layout

예상:

```text
Project-iOS-Release/
  XcodeProject/
  Outputs/
    Project.ipa
    Project.xcarchive
  Logs/
```

App bundle 내부:

```text
Project.app/Content/build_manifest.jbmanifest
Project.app/Content/game_assets.jbpack
Project.app/Project
```

## 작업 순서

## 2026-06-02 Foundation 구현 상태

이번 작업에서 완료한 초석:

- `JBRO_PLATFORM_ANDROID`, `JBRO_PLATFORM_IOS`, `JBRO_PLATFORM_MOBILE` 매크로를 추가했습니다.
- `EPlatformType` 에 `Android`, `IOS` 를 추가했습니다.
- `ERenderSurfaceType::MobileNativeSurface` 를 추가했습니다.
- `CMobilePlatform`, `CMobileRenderSurface` 기본 클래스를 추가했습니다.
  - 실제 Android `ANativeWindow` / iOS `UIView/CAMetalLayer` 는 아직 연결하지 않았습니다.
  - 모바일 entry layer 가 native surface handle 과 size/focus 를 주입할 수 있는 최소 계약만 둔 상태입니다.
- `CEngine::InitializePlatform()` 이 mobile compile 에서 `CMobilePlatform` 을 만들도록 연결했습니다.
- `CEngine::InitializeRHI()` 이 mobile compile 에서 `CVulkanRHIDevice` 를 만들도록 연결했습니다.
- SDK public mirror 에도 platform define/type/mobile header 를 반영했습니다.
- `.Jproject` Build 설정에 Android/iOS 기본 빌드 설정을 추가했습니다.
  - Android: `AndroidApplicationId`, `AndroidMinSdkVersion`, `AndroidTargetSdkVersion`, `AndroidAbi`
  - iOS: `IOSBundleIdentifier`, `IOSTeamId`, `IOSMinimumOSVersion`
- Build Settings UI 에 `Android`, `iOS` 카테고리를 추가했습니다.
- `BuildGame.ps1` 은 `Android`/`IOS` 플랫폼 값을 인식하되, 현재는 명확한 unsupported 메시지로 패키징을 막습니다.
- Vulkan RHI 객체 계층을 추가했습니다.
  - `CVulkanRHIDevice`
  - `CVulkanSwapchain`
  - `CVulkanCommandContext`
  - `CVulkanBuffer`
  - `CVulkanTexture`
  - `CVulkanSampler`
  - `CVulkanProgram`
  - `CVulkanGraphicsPipeline`
- `ERHIProgramLanguage::SPIRV` 와 `RHIProgramDesc::SourceSize` 를 추가해 Vulkan shader module 입력 계약을 명확히 했습니다.
- 모바일 platform entry 가 surface/focus/exit/pause/resume 이벤트를 엔진에 주입할 수 있는 API 를 추가했습니다.

이번 작업에서 의도적으로 완료 처리하지 않은 것:

- Android Gradle template
- Android native library build
- Android Activity/JNI/lifecycle bridge
- iOS Xcode template
- iOS signing/project generation
- Vulkan 실제 모바일 RHI backend
- 모바일 파일 시스템 bundle/persistent path abstraction
- 모바일 touch/soft keyboard/audio lifecycle
- Vulkan descriptor set / texture upload / shader cook 완성
- Android NDK Vulkan compile 검증
- iOS MoltenVK compile/link 검증

현재 상태의 의미:

- 모바일은 이제 UI/프로젝트 파일/빌드 스크립트/엔진 플랫폼 타입에서 정식 타깃으로 인식됩니다.
- 모바일 compile 경로는 Vulkan RHI 를 선택합니다.
- 하지만 Android/iOS 실행 산출물을 만들 수 있는 단계는 아닙니다.
- Vulkan backend 는 객체 계층과 주요 생성 경로를 갖췄지만, descriptor set/texture upload/shader cook 이 남아 실제 sprite rendering 완료 단계는 아닙니다.
- 다음 단계는 Android Debug APK 를 먼저 목표로 `PlatformBuild/Android` template 과 native entry/lifecycle 을 붙이는 것입니다.

### Phase 1: 플랫폼 추상화 점검

- platform enum 에 Android/iOS 추가
- build settings serialization 추가
- output folder 계산 추가
- package verifier 분리
- platform artifact deny-list 공통화

완료 조건:

- UI 에 Android/iOS target 이 표시되지만 아직 build 는 명확한 unsupported 메시지를 냅니다.
- `.Jproject` 에 모바일 설정이 저장/로드됩니다.

### Phase 2: Android 최소 빌드

- Android template 추가
- Gradle build script 추가
- native library build path 결정
- asset pack 을 `assets/Content` 로 staging
- static script link 검증

완료 조건:

- Debug Android APK 생성
- emulator 또는 device 에서 앱 launch
- startup scene 로드 로그 확인

### Phase 3: Android 렌더링/입력

- Android surface 와 RHI backend 연결
- touch input 연결
- audio lifecycle 연결
- orientation/resize 처리

완료 조건:

- device 에서 sprite scene 이 보임
- pause/resume 후 surface 복구

### Phase 4: iOS 빌드 기반

- Xcode template 추가
- bundle resource staging
- static script link
- signing 설정 저장/로드

완료 조건:

- simulator build 성공
- app launch 와 startup scene load 로그 확인

### Phase 5: iOS 렌더링/입력

- Vulkan RHI backend 또는 MoltenVK 기반 iOS bridge 구현
- touch/input/lifecycle/audio 연결

완료 조건:

- simulator/device 에서 sprite scene 표시
- pause/resume 안정성 확인

### Phase 6: Release 품질

- Android AAB signing
- iOS archive/export
- package contents 검사 자동화
- asset pack integrity 검증
- startup smoke test 자동화

완료 조건:

- store 제출 가능한 기본 산출물 생성
- release package 에 editor/source 산출물 없음

## 주요 리스크

### RHI backend 비용

모바일 추가에서 가장 큰 비용은 빌드 스크립트가 아니라 render backend 입니다.

Android 는 Vulkan을 직접 사용할 수 있지만, iOS 는 MoltenVK 없이는 Vulkan을 직접 사용할 수 없습니다.

### 색공간/텍스처 포맷

Web 에서 이미 RGBA/BGRA mismatch 가 발생했습니다.

모바일에서는 추가로 sRGB, premultiplied alpha, texture compression 이 얽힙니다.

### 동적 스크립트 로딩

Windows DLL 모델은 모바일에 그대로 맞지 않습니다.

static script link 를 기본으로 두고, hot reload 는 에디터/Windows 개발용 기능으로 분리하는 편이 안전합니다.

### 파일 접근

Android asset manager 와 iOS bundle resource 는 일반 PC 파일 경로와 다릅니다.

PackReader 는 path 기반뿐 아니라 stream 기반 로딩으로 확장되어야 합니다.

### 서명/비밀값

Android keystore password, iOS signing 정보는 `.Jproject` 에 평문 저장하면 안 됩니다.

에디터 UI 에서는 reference/path 만 저장하고 secret 은 OS credential store 또는 사용자 입력으로 처리해야 합니다.

## 검증 매트릭스

| 항목 | Windows | Web | Android | iOS |
|---|---:|---:|---:|---:|
| Startup scene load | 필요 | 필요 | 필요 | 필요 |
| Asset pack only | 필요 | 필요 | 필요 | 필요 |
| Static script | 선택 | 필요 | 권장 | 필요 |
| Dynamic script | 필요 | 불가 | 후순위 | 비권장 |
| Runtime localization 제외 | 필요 | 필요 | 필요 | 필요 |
| Icon package | 필요 | 필요 | 필요 | 필요 |
| Splash screen | 선택 | 선택 | 필요 | 필요 |
| App lifecycle | 낮음 | 중간 | 높음 | 높음 |
| Store signing | 없음 | 없음 | 필요 | 필요 |

## 우선 추천

1. Android 를 먼저 추가합니다.
2. Android 1차 목표는 Release store package 가 아니라 Debug APK + startup scene render 입니다.
3. script 는 Web 처럼 static link 로 시작합니다.
4. rendering 은 현재 팀이 빠르게 검증 가능한 backend 를 선택하되, RHI format/lifecycle 계약을 먼저 정리합니다.
5. iOS 는 signing/MoltenVK 비용이 크므로 Android 의 mobile lifecycle/filesystem/pack contract 가 안정된 뒤 들어갑니다.

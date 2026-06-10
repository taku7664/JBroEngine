# Android Native Build (libJBroGame.so) — 작업 계획 / 핸드오프

> 콘텍스트 압축 대비 자급식 문서. 콜드 재개 시 이 파일 먼저 읽고 시작.
> 상위 맥락: `tasks/MobilePlatformPlan.md`(전체 모바일 설계), `tasks/InputSystemRoadmap.md`(입력).
> 사용자 지시: 입력 작업 완료됨 → 모바일 글루 착수 중.

## ⚠ 빌드 방식 전환 (2026-06-08): CMake → 직접 clang
**CMake 포기.** 이 머신서 cmake.exe 가 *모든* 컴파일러 try_compile 에서 fastfail(`0xC0000409` STATUS_STACK_BUFFER_OVERRUN) 로 죽는다.
- cmake 3.22.1(NDK 번들) / 3.31.6(VS) **둘 다**, 그리고 **MSVC / clang / Android NDK toolchain 전부** 동일 크래시.
- toolchain 파일 경유 → CXX 컴파일러 *식별* 단계서, 내장 Android 지원/native MSVC → ABI 탐지 단계서 크래시. 위치만 다르고 동일 코드.
- NDK clang++ 자체는 **완벽 정상**(C++20 + STL + vulkan.h 컴파일+링크 직접 호출 전부 통과). 비ASCII %TEMP%/소스경로도 clang 은 문제없음.
- 결론: cmake 환경 고장(주입 DLL 의심). cmake 우회가 정답.

**채택: 웹빌드와 동일 모델 — 소스리스트 + 컴파일러 1회 직접 호출.** (web 은 emcc, android 는 NDK clang++.)
- `BuildScripts/Android/android_engine_sources.txt` — 엔진+yaml 소스 단일출처(web_game_sources.txt 패턴).
- `BuildScripts/Android/BuildAndroidNative.ps1` — clang++ `@response.rsp` 1회 호출로 .so 직접 생성.
  - rsp 는 **forward-slash 필수**(clang response file 은 `\` 를 escape 로 먹음).
  - `-static-libstdc++`(=ANDROID_STL c++_static), `--target=aarch64-linux-android26`, `-shared -fPIC`.
- `PlatformBuild/Android/CMakeLists.txt` 는 **참고용 보존**(cmake 고쳐지면 사용 가능). 현재 빌드는 PS1 직접경로.

## 빌드 방식 (구) — CMake 스캐폴딩 메모 (참고용, 위 전환으로 비활성)

## 목표
모바일 글루의 블로커 = `libJBroGame.so`(엔진+게임 arm64 네이티브)가 아직 안 빌드됨.
이걸 **CMake** 로 빌드 → NativeActivity 글루 → Gradle Debug APK → 에뮬/기기 스모크.
입력(InjectTouch)은 .so + NativeActivity 진입 생긴 뒤 붙는 작은 조각(이미 엔진쪽 계약 `CMobilePlatform::InjectTouch` 준비됨).

## 툴체인 (설치 완료 2026-06-08)
- `C:\Android` (cmdline-tools 경로 설치, Android Studio 미사용)
  - platform-tools, platforms;android-35, build-tools;35.0.0, cmake;3.22.1, ndk;27.3.13750724(clang 18)
- JDK = Oracle JDK 21 `C:\Program Files\Java\jdk-21` → **Gradle 은 JDK21 호환 버전 핀 필요**(Gradle 8.7+/AGP 8.5+)
- Gradle = `C:\Android\gradle\gradle-8.10.2` (2026-06-08 설치). AGP 8.7.3(BuildGame.ps1 핀)이 Gradle 8.9+ 요구. PATH 에 `...\bin` 추가해 사용(gradlew 래퍼 미생성).
- User 환경변수 등록됨: `ANDROID_HOME=C:\Android`, `ANDROID_SDK_ROOT=C:\Android`,
  `ANDROID_NDK_HOME=C:\Android\ndk\27.3.13750724`, `JAVA_HOME=C:\Program Files\Java\jdk-21`
  (※ 이미 떠 있던 셸엔 반영 안 됨 — 새 셸 또는 빌드 시 주입)
- NDK toolchain 파일: `C:\Android\ndk\27.3.13750724\build\cmake\android.toolchain.cmake`
- 빌드 타깃: min SDK 26 / target 35 / **abi arm64-v8a**(우선), x86_64(에뮬 옵션). `.Jproject` 기준.
- NDK clang arm64 sanity 컴파일 통과 확인함.

## 기존 스캐폴딩 (이미 있음 — 재사용)
`BuildScripts/BuildGame.ps1 -Platform Android` 가 이미:
- libJBroGame.so 를 다음 경로에서 탐색(`~line 1116`):
  - `Build\Android\<abi>\<Config>\libJBroGame.so`
  - `Build\Android\<Config>\<abi>\libJBroGame.so`
  - (Engine root 동일 2종) / `PlatformBuild\Android\libs\<abi>\libJBroGame.so`
- 찾으면 Gradle `app/src/main/jniLibs/<abi>/libJBroGame.so` 로 staging(`~1179,1298`)
- Gradle 프로젝트 생성: `android.app.NativeActivity` + Vulkan feature, assets/Content(manifest+pack)
- .so 없으면 명확히 throw(`~1296`)
→ **즉 CMake 가 .so 를 위 경로에 produce 하면 BuildGame.ps1 이 나머지(APK) 처리.**
  통합 옵션 (a) CMake 독립 빌드 → 경로에 출력(권장, 기존 staging 재사용) /
            (b) Gradle externalNativeBuild 가 CMake 호출(더 통합적, Gradle 템플릿 수정). **먼저 (a).**

## 소스 서브셋 전략
기준선 = `BuildScripts/Web/web_game_sources.txt`(런타임 소스 = Application + Engine runtime, 에디터 0).
Android = web 목록에서:
- **빼기**(web 전용): `Application/Entry/WebMain.cpp`, `Core/Platform/Web/*`(WebCanvasSurface/WebPlatform),
  `Core/RHI/WebGPU/*`, `Core/Network/Web/WebSocketTransport.cpp`(→ Android 용 transport 또는 stub).
- **넣기**(android 전용): `Core/Platform/Mobile/MobilePlatform.cpp`, `Core/Platform/Mobile/MobileRenderSurface.cpp`,
  `Core/RHI/Vulkan/*`(CVulkanRHIDevice/Swapchain/CommandContext/Buffer/Texture/Sampler/Program/GraphicsPipeline),
  Android 엔트리(`Application/Entry/AndroidMain.cpp` 신규) + `native_app_glue`(NDK 제공: `$NDK/sources/android/native_app_glue/android_native_app_glue.c`).
- **공통 유지**: Utillity, Core/Asset/Build/Debug/Engine/EngineCore/ScriptCore/FileSystem/Game/Input/Logging/
  Math/Module/Network(코어)/Random/Renderer/Resource/Task/Time, GameFramework/*, yaml-cpp/src/*.
- 정적 스크립트: 게임은 static link(web 처럼). SampleProject 의 `Contents/GeneratedScriptRegistry.cpp` + 스크립트 .cpp 를
  .so 에 함께 컴파일(또는 별도 staticlib). web 빌드가 하는 방식 참고(`BuildGame.ps1` web 경로의 "static script sources").

## 서드파티 (arm64 크로스컴파일)
- **yaml-cpp**: 소스 직접 컴파일(web 처럼 `Engine/ThirdParty/yaml-cpp/src/*.cpp`). `-DYAML_CPP_STATIC_DEFINE`.
- **stb / miniaudio**: 헤더 온리(impl 매크로 정의하는 .cpp 존재 위치 확인). 오디오는 Android=miniaudio(AAudio/OpenSL) — 동작 검증 후순위, 초기엔 EmptyAudioDevice 로 빌드 가능하면 우선.
- **magic_enum**: **호스트 전용**(ReflectionEnumRegister.h). 런타임 게임은 generated registry 사용 → libJBroGame.so 는 magic_enum **불필요**해야 함. (만약 링크서 magic_enum 의존 나오면 그 사용처를 호스트로 격리.)
- **Vulkan**: NDK 제공 `libvulkan.so` 링크(`-lvulkan`). 별도 SDK 불필요(Windows 와 다름).
- **imgui**: 런타임 게임 빌드엔 미포함(에디터 전용).

## 빌드 제외 주의 (Windows-ism)
- `JBRO_PLATFORM_WINDOWS`/`JBRO_EDITOR` 가드 밖에서 Windows API 쓰는 코드 없어야. 컴파일 에러로 드러남 → 가드 보강.
- D3D11 RHI, Win32 surface, LiveCompile, Editor/* 전부 제외.
- 정의 매크로: `JBRO_PLATFORM_ANDROID`(NDK 가 `__ANDROID__` 정의 → PlatformDefines.h 가 자동 설정), `JBRO_GAME`,
  `JBRO_RHI_VULKAN`(Vulkan 경로 활성 — Windows 검증용 가드와 동일), `YAML_CPP_STATIC_DEFINE`.
- STL: `-DANDROID_STL=c++_static`.

## 런타임 부트스트랩 (공통 경로 재사용 — 새로 만들지 말 것)
- 모바일 엔트리는 Windows/Web 과 **같은** `CGameApplication` 부트스트랩 호출:
  `InitializeRuntimeGame` / `LoadRuntimeBuildManifest` / `LoadRuntimeScriptModule`(static) /
  `LoadRuntimeStartupScene` / `ConfigureRuntimeViewCamera` / 공통 tick·render 루프.
  (정확한 시그니처는 `Application/Application.cpp` + `Application/Entry/WebMain.cpp` 참고해 AndroidMain 작성.)

## 마일스톤 (체크리스트)
- [x] **M1 빌드 뚫기** (CMake 시도→ 환경 크래시 → 직접 clang 전환, 위 "빌드 방식 전환" 참조).
- [x] **M2 전체 소스 → libJBroGame.so 링크** ✅ 2026-06-08.
      `BuildScripts/Android/BuildAndroidNative.ps1 -Configuration Debug` → EXIT=0, 110 소스.
      산출: `Build\Android\arm64-v8a\Debug\libJBroGame.so` (AArch64 ELF64 DYN, 34MB, `CreateGameModule`+스크립트 등록 export 확인).
      수정한 컴파일 반례:
      - `std::aligned_alloc` 는 Android API 28+ 한정(타깃 26) → GameModuleLoader.cpp / ReflectionRegistry.cpp 에 `#elif defined(__ANDROID__)` posix_memalign 분기 추가(웹/리눅스 경로 불변).
      - native_app_glue.c(C) 는 clang++ 가 C++ 로 컴파일해 깨짐 → AndroidMain(M3) 생기기 전까지 빌드서 제외(그때 C 로 별도 컴파일).
      - Vulkan RHI 8종 + MobilePlatform/MobileRenderSurface 전부 무수정 컴파일/링크 통과.
- [x] **M3 AndroidMain + native_app_glue** ✅ 2026-06-08. `Application/Entry/AndroidMain.cpp`.
      android_main 루프(ALooper_pollOnce; pollAll 은 NDK27서 사용불가), userData=AndroidAppState.
      lifecycle: INIT_WINDOW→`SetPendingNativeWindow`+(init 후면)`SetNativeSurfaceHandle`/resize,
      TERM_WINDOW→핸들 null, WINDOW_RESIZED/CONFIG_CHANGED→`ResizeSurface`, GAINED/LOST_FOCUS→`SetFocus`,
      PAUSE/RESUME→`NotifyPause/Resume`. 윈도우 준비 후 1회 `InitializeApplication`(Vulkan surface 가 init 시점에
      ANativeWindow 필요 → pending-window 시드로 해결), 이후 매 프레임 `TickApplication`.
      .so export 확인: `ANativeActivity_onCreate`(glue) + `android_main` 둘 다 T.
      부팅순서 수정: `CMobilePlatform::SetPendingNativeWindow/GetPendingNativeWindow`(static, Android 전용) 추가 →
      `CMobilePlatform::Initialize` 가 서피스 생성 직후 시드. (PlatformDesc/공유코드 무변경, mobile 레이어 격리.)
- [ ] **M4 Vulkan 스왑체인 on ANativeWindow**: 코드 경로는 이미 존재(`CVulkanRHIDevice::CreateSurface` android 분기 +
      `vkCreateAndroidSurfaceKHR`, CVulkanSwapchain). **빌드상 완료, 런타임 미검증**(APK+기기 필요=M6).
      clear + sprite draw 가 기기/에뮬서 보이는지 실측 필요.
- [x] **M5 입력** ✅ 2026-06-08 (M3 와 동일 파일서 배선). native_app_glue input cb `OnInputEvent`:
      AMotionEvent DOWN/POINTER_DOWN→Began, UP/POINTER_UP→Ended, MOVE→(전 포인터)Moved, CANCEL→Cancelled.
      pointerId/getX/getY → `CMobilePlatform::InjectTouch`. **빌드 완료, 런타임 미검증**(M6).
- [x] **M6 Gradle APK (빌드)** ✅ 2026-06-08. `BuildGame.ps1 -Platform Android -Configuration Debug -AndroidSdkRoot C:\Android -OutputRoot C:\JBroPkg`.
      BUILD SUCCESSFUL(1m15s). APK 47.5MB, 엔트리 검증: `lib/arm64-v8a/libJBroGame.so` + `assets/Content/{build_manifest.jbmanifest,game_assets.jbpack}` + AndroidManifest.xml.
      Gradle 미설치였음 → **Gradle 8.10.2 설치**(`C:\Android\gradle\gradle-8.10.2`, AGP 8.7.3 핀이 Gradle 8.9+ 요구). JDK21 호환 OK. gradlew 래퍼는 미생성(시스템 gradle PATH 사용).
      **함정(중요·해결됨)**: AGP 가 비ASCII 프로젝트 경로 거부("non-ASCII characters" 실패, 한글 사용자명). → BuildGame.ps1 이 생성하는 Gradle 프로젝트에 `gradle.properties`(`android.overridePathCheck=true`) 자동 작성으로 해결(비ASCII 경로서 BUILD SUCCESSFUL 검증). ASCII OutputRoot 지정 불필요해짐.
      stripDebugDebugSymbols 경고("Unable to strip libJBroGame.so")는 Debug 무해.
- [x] **M6c 에디터 인앱 빌드 배선** ✅ 2026-06-08. 에디터 빌드 버튼이 Android 를 "not implemented" 스텁으로 막고 있었음 → 해제.
      `CGameBuildManager`: Android 태스크(Validate/BuildAndroid/Verify) + `BuildAndroidPackage`(=`BuildGame.ps1 -Platform Android` 위임, 웹과 동일 패턴) + `VerifyAndroidPackage`(APK 존재/`GameScript.dll` 부재).
      `BuildGame.ps1`: ① `-Platform Android` 가 .so(BuildAndroidNative.ps1)+매니페스트+팩+APK 를 **한 번에** 처리 ② gradle.properties override 자동작성 ③ `Find-GradleCommand` 가 PATH 없을 때 `C:\Android\gradle\gradle-*`/`GRADLE_HOME` 탐색(에디터는 PATH 주입 안 함).
      검증: gradle PATH 없이 + 비ASCII OutputRoot 로 `BuildGame.ps1 -Platform Android` EXIT=0; 에디터 Debug_Editor 컴파일/링크 OK.
- [ ] **M6b 런타임 스모크 (미완)**: `adb install` → logcat 으로 startup scene 로드 확인. **기기/에뮬 미설치로 불가.**
      에뮬: `system-images;android-35;google_apis;x86_64` + `emulator` + AVD 생성 필요(x86_64 ABI .so 도 별도 빌드: `BuildAndroidNative.ps1 -Abi x86_64`). 또는 실기기 USB+adb.
      이 단계서 M4(Vulkan ANativeWindow surface) / M5(터치) 실측 검증된다.

## 런타임 브링업 + 검토 후속 (2026-06-10, MuMu x86_64 에뮬서 진단)
on-device logcat(태그 `JBroEngine`)로 검은화면 원인 4개 잡고 수정:
- **android_main 무한블로킹**: init 을 폴 루프 뒤에 둬서 `ALooper_pollOnce(-1)` 에 갇힘 → init 을 `APP_CMD_INIT_WINDOW` 핸들러(`EnsureInitialized`)로 이동 + 캐노니컬 루프(폴 타임아웃 매 반복 재평가).
- **에셋**: APK 내부를 std::filesystem 으로 못 읽음 → `ExtractApkContentAssets`(AAssetManager→내부저장소 추출 + chdir). 이후 **재귀화**: 패키지가 `_assetindex.txt`(상대경로 목록) 생성, 런타임이 읽어 중첩까지 추출(AAssetManager 는 디렉토리 열거 불가). 크기-스킵으로 재실행 비용 절감.
- **스크립트**: 정적 모듈 로드가 `#if JBRO_PLATFORM_WEB` 전용 → `WEB||ANDROID` 로 확장(extern 약심볼 + 로드).
- **로깅**: `OutputDebugString`/stderr → Android 는 logcat(`__android_log`, tag JBroEngine). CLogger + fallback 양쪽.

검토(하드코딩/확장성/누락/최적화) 후속 수정:
- **Vulkan 분리 큐패밀리**: `SelectPhysicalDevice` 가 graphics+present 동일 패밀리만 찾던 것 → 분리 허용 + `CreateLogicalDevice` 패밀리별 큐. **스왑체인도** graphics 패밀리 받아 분리 시 `VK_SHARING_MODE_CONCURRENT`(반쪽구현 완성). Windows(MSVC) 컴파일도 검증 — PC Vulkan 경로 무해/개선.
- **멀티 ABI**: `AndroidAbi` 쉼표 리스트 허용(arm64-v8a,x86_64) → ABI별 .so 빌드 + jniLibs staging + abiFilters. 한 APK 가 실기기+에뮬 모두 커버.
- **오디오**: 모바일 EmptyAudio → miniaudio(AAudio/OpenSL, dlopen). `JBRO_HAS_MINIAUDIO=1` + MiniAudioDevice/miniaudio_impl 소스 추가. arm64/x86_64 링크 clean.
- 죽은 상태(`HasWindow`) 제거.

**최종 미해결 = MuMu Vulkan 디바이스 0개**(`vkEnumeratePhysicalDevices=0`, ABI 무관). MuMu 가 게스트 앱에 Vulkan ICD(`vulkan.ranchu.so`) 만 두고 실제 디바이스 미노출. **코드 문제 아님.** → 실기기(arm64) 또는 Vulkan 되는 에뮬서 M4(Vulkan surface/clear)·M5(터치)·오디오 실측 필요(내일).

## 검증
- M1/M2: CMake 빌드 green(`Build/Android/.../libJBroGame.so` 생성).
- M3~M5: 빌드 green + (가능하면) 에뮬레이터(`avdmanager`/`emulator` 별도 설치 필요 — 현재 미설치)에서 실행.
  ※ 에뮬레이터/시스템이미지(`system-images;android-35;google_apis;x86_64`)는 아직 안 깔림 — M6 직전 필요.
- M6: APK 생성 + adb install + startup 로그.

## 알려진 함정
- JDK 21 ↔ Gradle: Gradle 8.7+ / AGP 8.5+ 아니면 JDK21 거부. BuildGame.ps1 Gradle 템플릿 버전 확인/핀.
- 한글 사용자명 경로(`C:\Users\박주형`) — NDK/CMake 가 싫어할 수 있음. 빌드 출력은 가능하면 ASCII 경로(`C:\Android` 등) 또는 repo 내. 문제 시 임시 빌드디렉토리 ASCII.
- `web_game_sources.txt` 가 최근 InputSystem.cpp/ScriptCore.cpp 누락으로 깨졌던 전례 → Android 소스리스트도 동일 누락 주의(특히 *Core.cpp, InputSystem.cpp).
- 줄끝(CRLF) 일관성.
- 에뮬레이터 미설치 → M6 전에 `system-images` + `emulator` + AVD 생성 필요(또는 실기기 adb).

## 핵심 참조 파일 (재개 시 읽기)
- `BuildScripts/Web/web_game_sources.txt` — 소스 서브셋 기준선
- `BuildScripts/BuildGame.ps1` (Android 섹션 ~line 1100-1300) — .so 탐색/staging/Gradle 생성
- `Engine/Engine.vcxproj` — 전체 소스 + 에디터/플랫폼 제외 패턴
- `Engine/Core/Platform/Mobile/MobilePlatform.{h,cpp}`, `MobileRenderSurface.{h,cpp}` — inject 계약
- `Engine/Core/Platform/PlatformDefines.h` — 플랫폼 매크로
- `Engine/Core/RHI/Vulkan/*` — Vulkan backend
- `Application/Application.cpp`, `Application/Entry/WebMain.cpp` — 런타임 부트스트랩 패턴
- `Engine/Core/Input/InputSystem.h` (AccumulateTouch), `MobilePlatform.h` (InjectTouch) — 입력 배선 지점

## 현재 입력 시스템 상태 (참고 — 완료됨)
브랜치 `feat/input-touch-mobile-web` 에 커밋됨: 뷰포트 포커스, 텍스트입력, 터치(웹+모바일inject+WM_POINTER),
웹 진동, 레이어 프로젝트세팅, 키테이블 확장, InputMap(액션매핑 런타임+직렬화+UI), 웹빌드 선결버그 수정.
모바일 터치 수신 계약(`CMobilePlatform::InjectTouch`→`Engine.InputSystem->AccumulateTouch`)은 **생산자(native_app_glue)만 붙이면 동작**.

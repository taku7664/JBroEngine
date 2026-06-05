# Build Pipeline Design

이 문서는 현재 JBroEngine 빌드 파이프라인을 처음 보는 사람이 Windows 게임 빌드 산출물이
어떻게 만들어지고 런타임에서 어떻게 소비되는지 추적할 수 있도록 정리한 인수인계 문서입니다.

## 범위

- 현재 완성 대상은 Windows 에디터에서 Windows 게임 패키지를 만드는 흐름입니다.
- Web 은 Windows 와 같은 manifest/asset pack 계약을 사용하되, Emscripten package 산출물과 preload/mount 방식만 Web 전용으로 처리합니다.
- mobile 은 같은 설정 구조와 공통 manifest/asset pack 계약을 따라야 하지만, 현재 패키저가 임시로 처리하지 않습니다.
- 에디터 전용 로컬라이징, SDK, Editor 산출물은 게임 패키지에 들어가면 안 됩니다.

## 주요 파일

- `Application/Editor/Main/BuildSettingsWindow.*`
  - 빌드 설정 UI.
  - 좌측 카테고리 / 우측 내용 레이아웃.
  - `Apply` 시에만 `.Jproject` 의 `Build:` 섹션 저장.

- `Application/Editor/RootDockWindow.*`
  - `파일 > 빌드` 메뉴 진입점.
  - 빌드 진행 팝업 표시.
  - 빌드 성공 시 출력 폴더 열기.

- `Application/Editor/Build/GameBuildManager.*`
  - 에디터 내부 게임 빌드 실행 계층.
  - 빌드 task 상태, 로그 경로, 출력 패키지 경로를 관리.
  - Windows 는 내부 staging 함수를 사용하고, Web 은 `BuildWeb.ps1` 로 위임합니다.

- `BuildScripts/BuildGame.ps1`
  - 에디터 밖에서 실행 가능한 공통 게임 패키징 스크립트.
  - `.Jproject` 를 읽고 Windows/Web 플랫폼별 산출물을 만들되, 에셋 pack 과 manifest 계약은 공유합니다.

- `BuildScripts/BuildWeb.ps1`
  - Web 전용 명령줄 진입점.
  - 내부적으로 `BuildGame.ps1 -Platform Web` 을 호출해 공통 패키징 계약에서 벗어나지 않게 합니다.

- `BuildScripts/Web/*`
  - Visual Studio Makefile project 와 개발 편의용 bat.
  - 직접 `emcc` 계약을 갖지 않고 `BuildWeb.ps1` 로 위임해야 합니다.

- `Engine/Core/Build/BuildManifest.*`
  - 런타임 게임이 읽는 빌드 manifest 로더/라이터.
  - 기본 산출물은 `Content/build_manifest.jbmanifest` 바이너리 파일.

- `Application/Application.cpp`
  - 게임 빌드 런타임 bootstrap.
  - manifest 로 window size, PPU, asset pack, script module, startup scene 을 연결.

- `Engine/Core/Asset/*AssetPack*`, `IAssetManager`
  - asset pack 작성/로드 계약.
  - 런타임은 loose `Content/Assets` 가 아니라 pack 을 mount 해야 합니다.

## 설정 저장

프로젝트 파일 `.Jproject` 는 top-level 프로젝트 설정과 `Build:` 설정을 함께 저장합니다.

빌드 세팅의 공통/플랫폼별 분류 기준은 `tasks/BuildSettingsPlatformSplit.md` 를 따릅니다.
핵심 원칙은 Common Build Settings, Platform Build Settings, Derived Build Settings 를 분리하는 것입니다.

빌드 설정 UI 정책:

- UI는 `BuildSettingsWindow` 에서 독립 관리합니다.
- `ProjectSettingsWindow` 에 빌드 카테고리를 섞지 않습니다.
- 사용자가 직접 지정할 수 있는 값은 제품명, Windows 아이콘, 타깃 플랫폼, Debug/Release, 시작 씬, 빌드 씬 목록, 출력 폴더입니다.
- 스크립트 vcxproj 경로, script DLL 이름, script build configuration 은 라이브컴파일/프로젝트 생성 규칙에서 파생합니다.
- `Apply` 를 누르기 전에는 `.Jproject` 를 바꾸지 않습니다.
- `파일 > 빌드` 는 저장되지 않은 빌드 설정이 있으면 막고, 적용 후 빌드하도록 안내합니다.

Windows 아이콘 설정:

- `.Jproject` 에는 `Build.WindowsIconGuid` 만 저장합니다.
- 원본 파일 경로나 exe resource 이름은 빌드 설정에 저장하지 않습니다.
- 선택한 `.ico` 가 `Contents/Assets` 내부이면 해당 자산을 import/refresh 하고 GUID 를 저장합니다.
- 외부 `.ico` 를 선택하면 `Contents/Assets/Package/Windows/AppIcon.ico` 로 복사한 뒤 해당 자산 GUID 를 저장합니다.
- `.ico` 는 현재 `EAssetType::Custom` + `Importer: WindowsIcon` 으로 관리합니다.
- Windows package 단계에서만 GUID 를 실제 `.ico` 경로로 resolve 하고, 복사된 exe 의 icon resource 를 수정합니다.

현재 `.Jproject` 의 런타임 관련 top-level 값:

- `ResolutionWidth`
- `ResolutionHeight`
- `PixelsPerUnit`

`PixelsPerUnit` 은 에디터에서는 `ProjectManager::LoadProject()` / `SetPixelsPerUnit()` 에서
`Runtime.PixelsPerUnit` 으로 들어갑니다. 게임 빌드에서는 manifest 에 기록한 뒤 런타임 bootstrap 이
같은 `Runtime.PixelsPerUnit` 에 주입합니다.
(`Runtime` = `RuntimeConfig` 호스트 전역. EngineCore 는 서비스 포인터 전용 번들이라 스칼라 설정값은 분리.)

## 에디터 빌드 흐름

`RootDockWindow::StartGameBuild()` 가 빌드 진입점입니다.

1. 프로젝트 로드 여부와 빌드 설정 dirty 상태를 확인합니다.
2. `CGameBuildManager::StartBuild()` 를 호출합니다.
3. `CGameBuildManager` 는 현재 씬/프로젝트 상태를 저장합니다.
4. `ProjectBuildSettings` 를 정규화합니다.
5. Windows/Web 이 아닌 플랫폼은 현재 에디터 패키저에서는 실패 처리합니다.
6. 시작 씬과 빌드 씬 목록을 검증합니다.
7. `ProjectManager::RegenerateScriptProject()` 로 script project/codegen 을 최신화합니다.
8. 별도 worker thread 에서 플랫폼별 순차 task 를 실행합니다.

Windows task 목록:

1. `Validate`
   - 시작 씬 경로 확인.
   - 모든 빌드 씬 파일 존재 확인.
   - 시작 씬 GUID 가 asset registry 에 있는지 확인.

2. `BuildEngine`
   - `JBroEngine.sln` 을 `Debug_Game|x64` 또는 `Release_Game|x64` 로 빌드합니다.
   - 결과 exe 는 `Build/<Configuration>_Game/Application.exe` 입니다.

3. `BuildScripts`
   - `ProjectManager::FindScriptVcxprojPath()` 결과를 빌드합니다.
   - Windows 는 `GameScript.dll` 동적 라이브러리 방식입니다.

4. `Package`
   - 출력 폴더 아래 `<Product>-Windows-<Debug|Release>` 패키지 폴더를 생성합니다.
   - game exe, `GameScript.dll`, asset pack, build manifest 를 stage 합니다.
   - `Build.WindowsIconGuid` 가 있으면 복사된 exe 의 `RT_GROUP_ICON` / `RT_ICON` resource 를 업데이트합니다.

5. `Verify`
   - exe, script DLL, `Content/build_manifest.jbmanifest`, `Content/game_assets.jbpack` 존재를 확인합니다.
   - loose `Content/Assets` 가 없는지 확인합니다.
   - `SDK`, `Editor`, `Localization` 같은 에디터 전용 산출물이 없는지 확인합니다.

Web task 목록:

1. `Validate`
   - 시작 씬 경로 확인.
   - 모든 빌드 씬 파일 존재 확인.
   - 시작 씬 GUID 가 asset registry 에 있는지 확인.

2. `BuildWeb`
   - `BuildScripts/BuildWeb.ps1` 을 실행합니다.
   - `-Project`, `-Configuration`, `-OutputRoot`, `-Clean` 을 넘깁니다.
   - 현재 프로세스에 `EMSDK` 환경변수가 있으면 `-EmsdkRoot` 로 넘깁니다.
   - 실제 package 생성, asset pack, manifest, `emcc` 호출은 `BuildGame.ps1 -Platform Web` 계약을 그대로 사용합니다.

3. `Verify`
   - `index.html`, `index.js`, `index.wasm`, `Content/build_manifest.jbmanifest`, `Content/game_assets.jbpack` 존재를 확인합니다.
   - `GameScript.dll`, loose `Content/Assets`, `SDK`, `Editor`, `Localization` 이 없는지 확인합니다.

Web editor build 는 Windows staging 코드를 재사용하지 않습니다.
이유는 exe, DLL, icon resource 처리가 Web 에는 없는 계약이고, 같은 함수에서 bool 분기로 처리하면
Windows 전용 산출물이 Web package 에 섞이는 반례가 생기기 때문입니다.

## 출력 경로

출력 폴더가 비어 있으면 기본값은 프로젝트 루트의 `Dist/Games` 입니다.

출력 폴더가 상대 경로이면 프로젝트 루트 기준입니다.
출력 폴더가 절대 경로이면 그대로 사용합니다.

패키지 폴더명:

```txt
<OutputRoot>/<ProductName>-<TargetPlatform>-<BuildConfiguration>
```

예:

```txt
C:/Users/박주형/Desktop/Project/Dist/Games/MyGame-Windows-Release
```

## Asset Pack 계약

목표 구조는 B+안입니다.

```txt
AssetGuid -> AssetRecord -> EntryLocator -> Payload
```

상위 계층 규칙:

- 씬, 프리팹, 컴포넌트는 `AssetGuid` 만 저장합니다.
- `EntryLocator` 는 pack 내부 payload 위치를 찾기 위한 저장소 접근 ID 입니다.
- `AssetManager` 위 계층은 pack 내부 entry 이름 규칙을 몰라야 합니다.
- Release package 에 원본 파일명/원본 폴더 구조를 저장하지 않는 방향을 유지합니다.
- 런타임은 source raw 파일이 아니라 cooked payload 또는 pack payload 를 로드해야 합니다.

현재 Windows 패키지 산출물:

- `Content/game_assets.jbpack`
- `Content/build_manifest.jbmanifest`

게임 패키지에 loose `Content/Assets` 가 있으면 검증 실패로 처리합니다.

## Build Manifest 계약

기본 manifest 파일:

```txt
Content/build_manifest.jbmanifest
```

바이너리 manifest 는 다음 역할만 맡습니다.

- window resolution
- startup scene GUID
- project pixels-per-unit
- runtime default asset mount/script module 규칙의 기준점

현재 바이너리 payload 순서:

```txt
int32 ResolutionWidth
int32 ResolutionHeight
string StartupSceneGuid
float PixelsPerUnit
string TargetPlatform
string ScriptMode
string ScriptModule
```

`PixelsPerUnit` 은 뒤에 추가된 필드입니다.
기존 manifest 에 tail 이 없으면 로더는 100으로 fallback 합니다.
1.0 미만 값도 100으로 정규화합니다.

`TargetPlatform`, `ScriptMode`, `ScriptModule` 은 그 뒤에 추가된 tail 필드입니다.
기존 manifest 에 tail 이 없으면 script mode 는 legacy Windows 호환을 위해 `DynamicLibrary`,
script module 은 `GameScript.dll` 로 fallback 합니다.
Web package 는 `ScriptMode = Static`, `ScriptModule = ""` 로 기록해 공통 runtime bootstrap 이
Windows DLL 로딩을 시도하지 않게 합니다.

불필요한 디버그성 정보는 기본 manifest 에 넣지 않습니다.
원본 경로, 원본 파일명, 사람이 읽는 debug metadata 는 별도 debug metadata 파일로 분리하는 방향입니다.

## 런타임 부트 흐름

게임 빌드에서는 `Application/Application.cpp` 의 `#if !JBRO_EDITOR` 경로만 탑니다.

1. `OnPreInitialize()`
   - 기본 manifest 를 찾습니다.
   - manifest 의 resolution 을 읽어 platform window size 에 반영합니다.

2. `OnPostInitialize()`
   - `InitializeRuntimeGame()` 을 호출합니다.

3. `InitializeRuntimeGame()`
   - manifest 를 다시 로드합니다.
   - `m_runtimeRenderWidth/Height` 를 설정합니다.
   - `Runtime.PixelsPerUnit` 에 manifest PPU 를 주입합니다.
   - asset pack 을 mount 합니다.
   - `GameScript.dll` 을 로드합니다.
   - startup scene GUID 로 씬 asset 을 읽고 deserialize 합니다.
   - runtime scene 에 sprite/audio system dependency 를 연결합니다.
   - scene referenced asset preload 후 active scene 으로 설정하고 simulation 을 시작합니다.

4. `OnPreTick()`
   - script module tick.
   - runtime camera 목록 갱신.

에디터 GameView 와 실제 게임 빌드는 같은 scene/system/runtime asset loader 계약을 타야 합니다.
빌드 전용 우회 로직을 추가할 때는 에디터 GameView 와 동작이 달라지는지 먼저 반례를 확인해야 합니다.

## Script Build 계약

Windows:

- `RegenerateScriptProject()` 가 빌드 직전 실행되어 script vcxproj/codegen 을 최신화합니다.
- `Debug` 게임 빌드는 script `Debug`.
- `Release` 게임 빌드는 script `Release`.
- package root 에 `GameScript.dll` 을 복사합니다.

Web/mobile:

- 정적 스크립트 방향을 유지합니다.
- Windows 패키저에서 임의로 dynamic DLL 처리하지 않습니다.
- 추후 플랫폼 패키저가 생겨도 `.Jproject Build:` 구조와 manifest/asset pack 계약을 공유해야 합니다.

## BuildGame.ps1 역할

`BuildScripts/BuildGame.ps1` 은 에디터 UI 없이 같은 게임 패키지 계약을 만들기 위한 스크립트입니다.

주요 단계:

1. `.Jproject` 를 읽습니다.
2. 프로젝트 루트, `Contents`, `Contents/Assets` 를 계산합니다.
3. 시작 씬과 빌드 씬 파일을 검증합니다.
4. 플랫폼별 package 폴더를 생성합니다.
5. `.Jmeta` 를 기준으로 asset pack 을 작성합니다.
6. startup scene GUID 와 resolution, PPU 를 binary manifest 에 씁니다.
7. 플랫폼별 executable/web shell 산출물을 stage 합니다.
8. 금지 산출물과 필수 산출물을 검증합니다.

이 스크립트는 에디터 빌드 파이프라인과 같은 산출물 계약을 유지해야 합니다.
에디터에서만 되는 빌드, 스크립트에서만 되는 빌드가 생기면 즉시 같은 계약으로 맞춰야 합니다.

### Windows script path

Windows 는 `BuildGame.ps1 -Platform Windows` 경로를 사용합니다.

1. 필요하면 `JBroEngine.sln` 을 `Debug_Game|x64` 또는 `Release_Game|x64` 로 빌드합니다.
2. 필요하면 script vcxproj 를 `Debug|x64` 또는 `Release|x64` 로 빌드합니다.
3. package root 에 `<Product>.exe` 와 `GameScript.dll` 을 복사합니다.
4. `Build.WindowsIconGuid` 가 있으면 `.Jmeta` 에서 `.ico` 자산을 찾아 복사된 exe resource 에 적용합니다.
5. `Content/build_manifest.jbmanifest` 와 `Content/game_assets.jbpack` 을 검증합니다.

### Web script path

Web 은 `BuildWeb.ps1` 또는 `BuildGame.ps1 -Platform Web` 경로를 사용합니다.

1. `.Jproject` 와 `Build.OutputDirectory` 를 기준으로 `<Product>-Web-<Debug|Release>` package folder 를 계산합니다.
2. Windows DLL, exe icon, x64 script build 단계는 타지 않습니다.
3. `Content/build_manifest.jbmanifest` 와 `Content/game_assets.jbpack` 을 먼저 stage 합니다.
4. `emcc @BuildScripts/Web/web_game_sources.txt` 로 engine/application source 를 컴파일합니다.
5. 프로젝트 `Contents/pch.cpp`, `Contents/GameModule.cpp`, `Contents/GeneratedScriptRegistry.cpp`, `Contents/Scripts/**/*.cpp` 를 추가 입력으로 넘겨 static script module 을 링크합니다.
6. `index.html`, `.js`, `.wasm` 산출물을 package root 에 생성합니다.
7. Emscripten preload 에 package `Content` 를 `/Content` 로 싣습니다.
8. 검증 시 `index.html`, `.js`, `.wasm`, manifest, pack 존재를 확인하고 `GameScript.dll`, loose `Content/Assets`, `SDK`, `Editor`, `Localization` 은 금지합니다.

현재 Web 첫 단계의 한계:

- Web runtime 은 공통 `CGameApplication` bootstrap 을 타지만, fetch/virtual FS 준비 방식은 Emscripten preload 를 우선 사용합니다.
- Web template 은 기본 `PlatformBuild/Web/shell.html` 을 사용하며, template asset GUID 는 다음 UI/설정 확장 단계에서 다룹니다.

Web static script contract:

- Web 은 `GameScript.dll` 을 만들거나 패키지에 복사하지 않습니다.
- 기존 script generator 가 만든 `GameModule.cpp` 의 `CreateGameModule` / `DestroyGameModule` symbol 을 wasm 실행 파일에 정적으로 링크합니다.
- `CGameApplication::LoadRuntimeScriptModule()` 은 `ScriptMode = Static` 일 때 Web weak symbol 로 정적 module 을 찾고, 있으면 `CGameModuleLoader::LoadStaticModule()` 로 초기화합니다.
- 정적 module symbol 이 없으면 스크립트 없는 프로젝트로 보고 경고 후 계속 진행합니다.
- `CGameModuleLoader` 는 dynamic/static module 모두 같은 `Tick()` / `Finalize()` / destroy lifecycle 을 사용합니다.

Web YAML dependency:

- 현재 asset pack payload 는 scene/import option 을 cooked binary 로 바꾸지 않고 raw source 를 담습니다.
- 따라서 Web 런타임에서도 `SceneSerializer`, sprite/audio import option 처리를 위해 yaml-cpp 구현이 필요합니다.
- Windows 는 `Engine/ThirdParty/lib/yaml-cpp*.lib` 를 링크하지만 Web/wasm 은 Windows `.lib` 를 사용할 수 없습니다.
- Web build 는 `Engine/ThirdParty/yaml-cpp/src/*.cpp` 를 `web_game_sources.txt` 에 포함해 wasm 으로 직접 컴파일합니다.
- 장기적으로 cooked scene/import payload 가 완성되면 Web runtime 에서 YAML parser 의존성을 제거할 수 있습니다.

개발 편의 wrapper:

- `BuildScripts/Web/build_web_debug.bat <Project.Jproject>`
- `BuildScripts/Web/build_web_release.bat <Project.Jproject>`
- Visual Studio `WebBuild.vcxproj` 에서 실행하려면 `JBRO_PROJECT_FILE` 환경변수에 `.Jproject` 경로를 지정합니다.
- `BuildScripts/Web/serve_web_debug.bat <WebPackageDir>` 는 생성된 Web package 폴더를 로컬 서버로 엽니다.

Emscripten 환경:

- Web build 는 `emcc` 가 필요합니다.
- 에디터 Web build 는 현재 프로세스의 `PATH` 또는 `EMSDK` 환경변수를 사용합니다.
- `EMSDK` 가 설정되어 있으면 `BuildWeb.ps1 -EmsdkRoot <EMSDK>` 로 넘겨 `emsdk_env.bat` 을 현재 PowerShell 프로세스에 import 합니다.
- `EMSDK` 가 없어도 `C:/emsdk/emsdk_env.bat` 이 있으면 그 설치를 자동으로 사용합니다.
- `emcc` 를 찾지 못하면 package 생성을 성공으로 위장하지 않고 빌드 실패로 처리합니다.
- Emscripten file packager 는 한글 Windows 사용자 경로에서 출력 디코딩 실패를 낼 수 있으므로, Web application compile/package 단계는 `C:/JBroWebBuildTemp/<guid>` ASCII 임시 폴더에서 수행한 뒤 `index.*` 산출물을 최종 package root 로 복사합니다.
- 최종 package 위치와 `Content/build_manifest.jbmanifest`, `Content/game_assets.jbpack` 위치는 사용자가 지정한 출력 폴더를 그대로 유지합니다.

Web build compatibility notes:

- Web source 는 MSVC 확장 문법에 의존하면 안 됩니다. 예: `class Foo abstract` 는 Clang/Emscripten 에서 깨지므로 표준 C++ 문법을 사용합니다.
- Windows API 전용 유틸은 `JBRO_PLATFORM_WINDOWS` 로 선언/구현을 가드해야 합니다. Web 에서는 HRESULT/DWORD/GetLastError 같은 타입이 없습니다.
- Web build source list 는 링크 심볼 기준으로 런타임 필수 `.cpp` 를 빠뜨리면 안 됩니다. `GameCamera.cpp`, `RenderResourceCache.cpp`, `Ref.cpp` 는 현재 runtime render/script reference 경로에 필요합니다.
- yaml-cpp 는 header/source/lib 버전이 일치해야 합니다. Windows `.lib` 만 맞고 Web source/header 가 다르면 wasm compile 에서 다시 깨집니다.
- emdawnwebgpu 의 현재 C API 이름을 기준으로 유지합니다. 예: `WGPUEmscriptenSurfaceSourceCanvasHTMLSelector`, `WGPUTexelCopyTextureInfo`, `WGPUTexelCopyBufferLayout`.
- WebGPU 구조체는 zero initialize 만 믿지 않습니다. 최신 WebGPU 검증에서는 `TextureViewDescriptor.mipLevelCount/arrayLayerCount`, `RenderPassColorAttachment.depthSlice`, `SamplerDescriptor.maxAnisotropy` 같은 필드를 명시해야 합니다.
- WebGPU bind group layout 의 uniform `minBindingSize` 는 RHI desc 에 실제 constant buffer 크기가 없으면 0 으로 둡니다. 작은 값으로 하드코딩하면 shader 가 요구하는 uniform 크기와 어긋나 pipeline/bind group validation 실패가 납니다.
- WebGPU graphics pipeline 은 D3D11 처럼 `ERHIBlendMode` 를 반영해야 합니다. 스프라이트 clear quad 와 alpha sprite 가 같은 renderer 경로를 쓰므로 Web 에서 blend 구현이 빠지면 GameView 와 Web build 렌더 결과가 갈라집니다.
- WebGPU swapchain surface format 과 asset texture format 을 섞으면 안 됩니다. 현재 swapchain 은 `BGRA8Unorm` 이지만 RHI texture payload 는 `ERHITextureFormat::RGBA8` 이므로 asset texture 는 `RGBA8Unorm` 으로 생성해야 합니다. 이를 어기면 Web 에서 red/blue 채널이 뒤집혀 sprite 색상이 파랗게 보입니다.
- Web runtime log 는 browser console 로도 나가야 합니다. `Core::Logger` 가 존재하는 정상 런타임에서는 fallback stderr 를 타지 않으므로, Web 은 `CLogger::Write()` 에서 stderr 로도 출력합니다.
- `PlatformBuild/Web/shell.html` 은 WebGPU/critical 오류가 있을 때만 우상단 오류 오버레이를 표시합니다. 일반 INFO 로그는 화면을 가리지 않고 콘솔에만 남깁니다.

## 진행 팝업

빌드 진행 UI는 프로젝트 로드 팝업과 같은 `ImPopupDesc` modal 흐름을 사용합니다.

- 진행률은 완료 task 수 / 전체 task 수입니다.
- 완료 task 는 check mark, 진행 중 task 는 spinner 로 표시합니다.
- 실패 시 팝업을 닫지 않고 실패 메시지와 로그 열기 버튼을 보여줍니다.
- 성공 시 package directory 를 자동으로 엽니다.

## 검증 체크리스트

코드 변경 후 최소 확인:

```txt
MSBuild JBroEngine.sln /p:Configuration=Debug_Game /p:Platform=x64
MSBuild JBroEngine.sln /p:Configuration=Release_Game /p:Platform=x64
MSBuild JBroEngine.sln /p:Configuration=Debug_Editor /p:Platform=x64
```

실제 프로젝트 패키지 확인:

```txt
BuildScripts/BuildGame.ps1 -Project C:/Users/박주형/Desktop/Project/Project.Jproject -Configuration Release -Clean
```

확인할 산출물:

- `<Product>.exe`
- `GameScript.dll`
- `Content/build_manifest.jbmanifest`
- `Content/game_assets.jbpack`
- `Build.WindowsIconGuid` 를 설정한 경우 exe 아이콘 resource 반영

없어야 하는 산출물:

- `Content/Assets`
- `SDK`
- `Editor`
- `Localization`

런타임 동작 확인:

- package exe 실행.
- 시작 씬이 로드되는지 확인.
- 에디터 GameView 와 렌더링 위치/스케일이 같은지 확인.
- PPU 가 100이 아닌 프로젝트에서는 스프라이트 스케일이 에디터와 같은지 확인.

## 자주 깨지는 지점

- `.Jproject` 에 저장된 build setting 을 적용하지 않고 빌드하면 이전 설정으로 패키징됩니다.
- startup scene 은 경로 문자열보다 GUID 가 런타임 기준입니다. asset registry 에 GUID 가 없으면 빌드를 실패시켜야 합니다.
- manifest 에 디버그 편의 정보를 넣기 시작하면 release package 정보 노출이 늘어납니다.
- 에디터 런타임 GameView 와 게임 빌드 bootstrap 이 다른 초기화 값을 쓰면 화면 스케일/카메라/씬 로드 결과가 달라집니다.
- 패키징 스크립트와 에디터 빌드 매니저가 서로 다른 manifest payload 를 쓰면 어느 한쪽 빌드만 동작합니다.
- exe 아이콘은 `.ico` 파일을 패키지 옆에 복사해도 바뀌지 않습니다. 반드시 복사된 exe resource 를 수정해야 합니다.

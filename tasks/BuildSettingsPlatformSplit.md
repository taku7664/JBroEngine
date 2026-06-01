# Build Settings Platform Split

이 문서는 빌드 세팅을 플랫폼에 상관없이 공통인 부분과 플랫폼별로 달라야 하는 부분으로
분류하기 위한 설계 메모입니다. 목적은 Windows/Web/mobile 을 같은 빌드 UI와 `.Jproject`
구조에서 다루되, 특정 플랫폼 규칙이 다른 플랫폼으로 새지 않게 하는 것입니다.

## 핵심 판단

빌드 세팅은 다음 세 층으로 분리해야 합니다.

1. Common Build Settings
   - 모든 플랫폼 빌드가 공유하는 사용자의 의도입니다.
   - 씬, 제품명, 출력 루트, 빌드 구성처럼 플랫폼이 달라도 의미가 유지되는 값입니다.

2. Platform Build Settings
   - 특정 플랫폼 패키저만 해석하는 값입니다.
   - Windows icon, Web canvas/html, Android package id, iOS bundle id 같은 값입니다.

3. Derived Build Settings
   - 사용자가 직접 입력하면 안 되는 파생값입니다.
   - script vcxproj 경로, script DLL 이름, 내부 staging 경로, 최종 exe 이름처럼 엔진 규칙에서 계산합니다.

이 세 층을 섞으면 다음 문제가 생깁니다.

- Web/mobile 빌드에서 Windows 전용 `GameScript.dll` / `.ico` 설정이 노출됩니다.
- 플랫폼별 패키저가 늘어날수록 `ProjectBuildSettings` 가 의미가 애매한 flat struct 가 됩니다.
- UI에서 사용자에게 직접 입력시키지 않아야 할 내부 경로가 다시 노출됩니다.
- `.Jproject` 저장값만 봐서는 어떤 값이 공통 계약이고 어떤 값이 플랫폼 전용인지 알기 어려워집니다.

## 현재 필드 분류

현재 `ProjectBuildSettings` 기준 분류입니다.

| 필드 | 현재 위치 | 분류 | 이유 | 권장 위치 |
| --- | --- | --- | --- | --- |
| `ProductName` | `Build` | Common | 산출물 이름, 표시 이름, 패키지 이름의 기본값으로 모든 플랫폼에 필요 | `Build.Common.ProductName` |
| `TargetPlatform` | `Build` | Common selector | 어떤 플랫폼 세팅을 사용할지 고르는 선택자 | `Build.ActiveTarget` 또는 `Build.Common.TargetPlatform` |
| `BuildConfiguration` | `Build` | Common | Debug/Release 의미는 모든 플랫폼에 존재 | `Build.Common.Configuration` |
| `OutputDirectory` | `Build` | Common | 최종 패키지 출력 루트는 모든 플랫폼에 필요 | `Build.Common.OutputDirectory` |
| `StartupScene` | `Build` | Common | 런타임 시작 씬은 모든 플랫폼에 필요 | `Build.Common.StartupScene` |
| `BuildScenes` | `Build` | Common | 포함할 씬 목록은 모든 플랫폼 asset collection 의 시작점 | `Build.Common.BuildScenes` |
| `ScriptMode` | `Build` | Derived / platform policy | Windows 는 DLL, Web/mobile 은 static 계열로 플랫폼 규칙에서 결정 | 저장은 호환성 유지, UI 숨김 |
| `ScriptProjectPath` | `Build` | Derived | `ProjectManager::FindScriptVcxprojPath()` / regeneration 규칙에서 계산 | 저장은 호환성 유지, UI 숨김 |
| `ScriptBuildConfiguration` | `Build` | Derived | Common `BuildConfiguration` 에서 결정 | 저장은 호환성 유지, UI 숨김 |
| `ScriptOutputLibraryPath` | `Build` | Derived / Windows policy | Windows DLL 산출물 이름이며 다른 플랫폼에는 의미 없음 | `Build.Platforms.Windows.ScriptOutputName` 또는 파생 |
| `WindowsIconGuid` | `Build` | Windows-only | `.ico` 와 exe resource patch 는 Windows 전용 | `Build.Platforms.Windows.IconGuid` |

현재 `ProjectInfo` top-level 이지만 빌드와 런타임에 직접 영향을 주는 값도 별도 분류해야 합니다.

| 필드 | 현재 위치 | 분류 | 이유 | 권장 처리 |
| --- | --- | --- | --- | --- |
| `ResolutionWidth` / `ResolutionHeight` | top-level project | Runtime common default | GameView 와 빌드 런타임이 공유하는 기본 렌더 해상도 | 당장은 top-level 유지, Build UI 에서 읽기 전용 표시 가능 |
| `PixelsPerUnit` | top-level project | Runtime common default | 플랫폼과 무관하게 씬 스케일에 필요 | 당장은 top-level 유지, manifest common payload 로 유지 |
| `EditorLocaleCode` | top-level project | Editor-only | 에디터 로컬라이징용이며 게임 빌드에 들어가면 안 됨 | Build UI/manifest/package 제외 |
| `ScriptSourceDirectory` 등 live compile 설정 | top-level project | Editor-only / derived source | 에디터 live compile 용이며 빌드 세팅 UI에서 직접 입력 금지 | Build 에서는 기존 ProjectManager 규칙으로만 참조 |

## 권장 `.Jproject` 구조

즉시 마이그레이션은 별도 작업으로 두되, 목표 구조는 아래처럼 구분합니다.

```yaml
Build:
  Common:
    ProductName: Project
    TargetPlatform: Windows
    Configuration: Release
    OutputDirectory: Dist/Games
    StartupScene: test.JScene
    BuildScenes:
      - test.JScene

  Platforms:
    Windows:
      IconGuid: ""

    Web:
      CanvasWidth: 1280
      CanvasHeight: 720
      HtmlTemplateGuid: ""
      WebShellMode: Default
      Compression: Brotli

    Android:
      PackageName: com.company.project
      VersionCode: 1
      VersionName: 1.0.0
      Orientation: Landscape
      MinSdkVersion: 26
      TargetSdkVersion: 35
      KeystoreGuid: ""

    IOS:
      BundleIdentifier: com.company.project
      Version: 1.0.0
      BuildNumber: 1
      Orientation: Landscape
      TeamId: ""
      ProvisioningProfileGuid: ""
```

호환성 원칙:

- 기존 flat `Build.ProductName`, `Build.OutputDirectory` 등은 읽기 호환을 유지합니다.
- 저장은 한 번에 새 구조로 바꾸기보다, 코드/스크립트 양쪽이 새 구조를 읽을 수 있게 된 뒤 전환합니다.
- `ScriptMode`, `ScriptProjectPath`, `ScriptBuildConfiguration`, `ScriptOutputLibraryPath` 는 legacy 저장 필드로 남겨도 UI에서는 계속 숨깁니다.

## UI 분류

빌드 세팅 창의 좌측 카테고리는 공통과 플랫폼별을 명확히 분리해야 합니다.

### Common / General

모든 플랫폼에 공통으로 저장합니다.

- Product Name
- Active Target Platform
- Build Configuration
- Output Directory
- Package Path Preview

주의:

- Target Platform 은 Common 이지만, 선택한 플랫폼에 따라 우측 Platform category 내용이 바뀌어야 합니다.
- Output Directory 는 플랫폼별 폴더가 아니라 공통 출력 루트입니다.
- 최종 패키지 폴더명은 `<Output>/<Product>-<Platform>-<Configuration>` 규칙으로 파생합니다.

### Common / Scenes

모든 플랫폼에 공통입니다.

- Startup Scene
- Build Scene List
- Use Current Scene
- Add Scene

주의:

- Runtime 은 scene path 보다 StartupSceneGuid 를 우선해야 합니다.
- StartupSceneGuid 는 빌드 시 asset registry 에서 파생하며 `.Jproject` 에 디버그용으로 저장하지 않습니다.

### Common / Runtime

현재는 Project Settings 에 가까운 값이지만, 빌드 결과에도 직접 영향을 줍니다.

- Resolution Width / Height
- Pixels Per Unit

권장:

- 지금 당장 BuildSettings 로 옮기지는 않습니다.
- 빌드 세팅 창에서는 읽기 전용 요약 또는 Project Settings 로 이동 버튼만 제공하는 편이 안전합니다.
- 실제 런타임 적용은 manifest common payload 로 유지합니다.

### Platform / Windows

Windows 패키저만 사용합니다.

- Application Icon GUID
- Console Window 여부
- Include PDB / Symbols
- Visual C++ runtime 배포 정책
- Script Module Mode 표시값: Dynamic DLL, read-only

현재 구현된 값:

- `WindowsIconGuid`

주의:

- `.ico` 파일 경로나 resource id 를 저장하지 않습니다.
- 선택 UI는 AssetGuid 를 저장하고, 패키징 단계에서 exe resource 를 수정합니다.
- Windows DLL 이름은 현재 `GameScript.dll` 로 고정된 파생 규칙이며 사용자가 직접 입력하지 않습니다.

### Platform / Web

Web 패키저만 사용합니다.

- Canvas Size 또는 Browser Fit Mode
- HTML shell/template asset GUID
- Wasm memory initial/max
- Compression format: None/Gzip/Brotli
- Asset preload policy
- Web worker/threading 사용 여부
- WebGL/WebGPU backend 선택
- IndexedDB cache policy

주의:

- Web 은 에디터 로컬라이징이 필요 없습니다.
- Windows DLL/script path 를 Web 설정에 노출하면 안 됩니다.
- Web build 는 loose path 가 아니라 pack/manifest 를 그대로 사용하되, fetch/mount 방식만 Web 전용으로 바뀌어야 합니다.

### Platform / Android

Android 패키저만 사용합니다.

- Package Name
- App Label
- Version Code / Version Name
- Min SDK / Target SDK
- Orientation
- Icon adaptive foreground/background asset GUID
- Keystore asset GUID 또는 external secure reference
- Signing mode: Debug/Release
- ABI: arm64-v8a 우선, 필요 시 x86_64
- Permissions

주의:

- keystore 원본 비밀번호를 `.Jproject` 에 평문 저장하면 안 됩니다.
- Android icon 은 Windows `.ico` 와 다르게 mipmap/adaptive icon asset pipeline 이 필요합니다.
- 모바일은 파일 시스템 접근 방식이 desktop 과 다르므로 package mount root 를 플랫폼별로 분리해야 합니다.

### Platform / iOS

iOS 패키저만 사용합니다.

- Bundle Identifier
- Display Name
- Version / Build Number
- Team ID
- Provisioning Profile reference
- Signing certificate reference
- Orientation
- App icon set asset GUID
- Required capabilities

주의:

- Windows 환경에서 iOS 최종 signing/build 는 원격 Mac 또는 별도 pipeline 이 필요할 수 있습니다.
- 인증서/프로비저닝 관련 secret 은 `.Jproject` 에 직접 저장하지 않습니다.

### Advanced / Diagnostics

공통이지만 기본 UI에서는 접어두는 것이 좋습니다.

- Clean build
- Verbose log
- Keep staging directory
- Include debug metadata
- Open package folder after build
- Smoke launch after build

주의:

- Debug metadata 는 release package 기본값에 포함하지 않습니다.
- `Keep staging directory` 는 개발자 진단 옵션이며 shipping 기본값이 되면 안 됩니다.

## 데이터 모델 권장안

장기적으로는 flat struct 보다 아래처럼 분리하는 편이 안전합니다.

```cpp
struct CommonBuildSettings
{
    std::string ProductName;
    EBuildTargetPlatform TargetPlatform;
    EBuildConfiguration Configuration;
    std::string OutputDirectory;
    std::string StartupScene;
    std::vector<std::string> BuildScenes;
};

struct WindowsBuildSettings
{
    AssetGuid IconGuid;
    bool IncludeSymbols = false;
};

struct WebBuildSettings
{
    std::string CanvasMode;
    AssetGuid HtmlTemplateGuid;
    std::string Compression;
};

struct AndroidBuildSettings
{
    std::string PackageName;
    std::string VersionName;
    int VersionCode = 1;
    AssetGuid IconGuid;
};

struct IOSBuildSettings
{
    std::string BundleIdentifier;
    std::string Version;
    std::string BuildNumber;
    AssetGuid IconSetGuid;
};

struct ProjectBuildSettings
{
    CommonBuildSettings Common;
    WindowsBuildSettings Windows;
    WebBuildSettings Web;
    AndroidBuildSettings Android;
    IOSBuildSettings IOS;

    // Legacy/derived fields are read for compatibility, but not exposed in UI.
};
```

## 구현 순서 제안

1. 현재 flat `ProjectBuildSettings` 를 유지한 채 문서 기준으로 UI 카테고리부터 분리합니다.
2. Windows icon 을 `Windows` 카테고리로 옮기고, `General` 에서는 공통 항목만 남깁니다.
3. Web/Android/iOS 카테고리는 초기에는 placeholder 가 아니라 실제로 저장할 최소 필드만 추가합니다.
4. `.Jproject` loader 는 flat legacy 와 nested new format 을 모두 읽게 만듭니다.
5. saver 는 새 nested format 으로 저장하도록 전환합니다.
6. `BuildGame.ps1` 과 editor `CGameBuildManager` 가 같은 구조를 읽도록 맞춥니다.
7. 각 플랫폼 packager 는 자기 platform settings 만 해석합니다.

## 금지 사항

- Windows 전용 값을 Common category 에 추가하지 않습니다.
- Web/mobile 설정에 `GameScript.dll`, `.vcxproj`, `.ico` 를 노출하지 않습니다.
- 사용자가 직접 입력할 필요 없는 내부 경로를 UI에 만들지 않습니다.
- 플랫폼별 secret 을 `.Jproject` 에 평문 저장하지 않습니다.
- Runtime manifest 에 debug/에디터 편의 정보를 기본 포함하지 않습니다.
- 플랫폼별 packager 가 서로 다른 asset pack/manifest 계약을 만들지 않습니다.

## 현재 코드에서 바로 고쳐야 할 방향

- `BuildSettingsWindow::DrawGeneralCategory()` 에 있는 Windows icon selector 는 `DrawWindowsCategory()` 로 이동해야 합니다.
- 좌측 카테고리는 최소 `일반`, `씬`, `런타임`, `Windows`, `Web`, `Android`, `iOS`, `고급` 으로 나누는 것이 맞습니다.
- 현재 `ProjectBuildSettings::WindowsIconGuid` 는 의미상 `WindowsBuildSettings::IconGuid` 이므로 다음 마이그레이션 대상입니다.
- `ScriptMode` 관련 필드는 계속 저장 호환은 하되 UI 항목으로 되살리면 안 됩니다.
- `BuildGame.ps1` 은 nested format 을 읽기 전까지 flat `WindowsIconGuid` 와 추후 `Build.Platforms.Windows.IconGuid` 를 모두 읽게 해야 합니다.

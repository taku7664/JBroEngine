; ============================================================================
; JBro Engine Editor - Inno Setup Installer Script
;
; 사용법:
;   1) Inno Setup Compiler 실행 → File → Open → 본 파일 선택
;   2) Build → Compile (F9)
;   3) 산출물: Installers\Output\JBroEngineEditor-Setup-{Version}.exe
;
; 또는 명령행:
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" Installers\JBroEngineEditor.iss
;
; 빌드 전제:
;   Debug_Editor x64 구성으로 미리 빌드되어 있어야 함
;   (Build\Debug_Editor\Application.exe 존재)
; ============================================================================

#define AppName       "JBro Engine Editor"
#define AppVersion    "0.0.4"
#define AppPublisher  "PPAK_JU"
#define AppExeName    "Application.exe"
#define AppId         "{{8E3F2C71-9A4B-4D5E-A1B2-7C3D4E5F6A7B}"

#define BuildDir      "..\Build\Debug_Editor"
#define DistSdkDir    "..\Dist\JBroEngineEditor-Debug_Editor\SDK"

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\JBroEngine
DefaultGroupName=JBro Engine
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName}
OutputDir=Output
OutputBaseFilename=JBroEngineEditor-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DisableProgramGroupPage=yes
DisableWelcomePage=no

[Languages]
Name: "korean";  MessagesFile: "compiler:Languages\Korean.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; ── 실행파일 ────────────────────────────────────────────────────────────────
Source: "{#BuildDir}\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion

; ── 런타임 리소스 ──────────────────────────────────────────────────────────
; Resources/ : ResourceRegistry 가 읽는 영구 아이콘 + resources.yaml
Source: "{#BuildDir}\Resources\*"; DestDir: "{app}\Resources"; Flags: ignoreversion recursesubdirs createallsubdirs

; Localization/ : ko-KR/en-US yaml
Source: "{#BuildDir}\Localization\*"; DestDir: "{app}\Localization"; Flags: ignoreversion recursesubdirs createallsubdirs

; ── SDK (스크립트 작성용 헤더 + lib + 프로젝트 템플릿) ─────────────────────
Source: "{#DistSdkDir}\*"; DestDir: "{app}\SDK"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}";        Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}";  Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

; ── 한국어 사용자 정의 메시지 ─────────────────────────────────────────────
[CustomMessages]
korean.LaunchAfterInstall=설치 후 에디터 실행
english.LaunchAfterInstall=Launch Editor after installation

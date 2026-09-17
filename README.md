# JBroEngine

Windows / D3D12 위에서 도는 2D 우선 게임 엔진이다. 기존 엔진(`JBroEngine_old`)을 처음부터 다시 세운 것으로,
오브젝트-컴포넌트 모델, 스크립트 DLL 핫 리로드, 리플렉션 기반 인스펙터와 직렬화, ImGui 에디터까지가 지금 서 있다.
게임 로직은 C++ 스크립트로 쓰고, 자작 언어 JBroScript(`.jscript`)는 문법과 컴파일러 규칙만 정리된 상태다.

자세한 내용은 [위키](https://github.com/taku7664/JBroEngine/wiki)에 있다. 이 문서는 어디에 무엇이 있는지만 말한다.

## 지금 되는 것

- `Canvas` 하나가 오브젝트 풀·컴포넌트 풀·레이어·시스템 스케줄러를 소유하고 프레임을 돌린다
- 게임 스크립트는 별도 DLL 로 빌드하고, 실행 중에 갈아 끼운다(핫 리로드)
- 2D 빌트인 컴포넌트 다섯(`Transform2D`, `SpriteRenderer2D`, `Camera2D`, `Rigidbody2D`, `Collider2D`)과 3D 빌트인 다섯
- 컴포넌트 필드는 `JBRO_FIELD` 한 줄로 선언과 등록을 같이 한다. 인스펙터와 `.jcanvas` 저장이 그 표를 읽는다
- 에디터(`JBroEditorHost`)가 프로젝트(`.jproject`)를 열고, 씬(`.jcanvas`)을 읽고 그리고 저장한다.
  편집은 전부 커맨드라 되돌리기가 된다
- 렌더러는 프레임 패킷을 모았다가 프레임 끝에 정렬·기록한다. RHI 뒤에 D3D12 가 있다

## 아직 없는 것

- JBroScript 언어와 컴파일러 `jbroc` (렉서와 파서만 있다. 타입체커·이미터·명령줄 실행 파일은 아직 없다)
- 에셋 로드(`AssetSystem::Load` 는 스텁이다). 스프라이트는 아직 텍스처를 받지 못한다
- 3D 렌더 시스템, 물리 시뮬레이션 본체, 오디오
- Web 빌드(스텁만 있다)

## 저장소 구성

```
source/JBroEngine/
  Modules/           모듈마다 vcxproj 하나. 공개 헤더는 Include/JBro/<이름>/ 아래
  Tests/             단일 테스트 실행 파일(JBroTests). 경계 위반이 컴파일 실패하는지 보는 음성 테스트 포함
  ThirdParty/        외부 라이브러리를 소스째로 둔다(지금은 ImGui)
  Localization/      에디터 문구(ko-KR, en-US). jbroc/ 아래는 컴파일러 진단 메시지
source/JBroLauncher/       런처(C# / WinUI 3). 프로젝트 목록을 관리하고 에디터를 띄운다
source/JBroLauncher.Tests/ 런처의 순수 로직을 재는 실행 파일
docs/
  ProjectRule.md     지켜야 하는 규칙. 코드와 문서가 다르면 이쪽이 맞다
  JBroEngine.drawio.xml  구조 다이어그램 6장. draw.io 에서 연다
tasks/
  todo.md            진행 현황과 결정 기록(Decisions). "왜 이렇게 했나"는 여기 있다
  jbroscript-*.md    JBroScript 언어 계획·문법·컴파일러 규칙
tools/mutate.py      뮤테이션 테스트 도구
```

## 빌드

Visual Studio 2026(MSVC 툴셋 `v145`, Windows SDK `10.0.22621.0`. 둘 다 `JBro.Common.props` 에 고정돼 있다), C++20, x64.
솔루션 파일은 없다. 모듈이 각자 vcxproj 를 갖고 프로젝트 참조로 의존을 끌어오므로, 원하는 실행 파일의 vcxproj 를 바로 빌드한다.

```bash
msbuild source/JBroEngine/Tests/JBroTests.vcxproj -p:Configuration=Debug -p:Platform=x64
```

```bash
msbuild source/JBroEngine/Modules/JBroEditorHost/JBroEditorHost.vcxproj -p:Configuration=Debug -p:Platform=x64
```

런처는 .NET 10 SDK 로 따로 빌드한다. WinUI 3(Windows App SDK)을 쓰고 비패키지로 배포한다.

```bash
dotnet build source/JBroLauncher/JBroLauncher.csproj -c Debug
```

```bash
dotnet run --project source/JBroLauncher.Tests/JBroLauncherTests.csproj
```

테스트는 빌드된 `JBroTests.exe` 를 실행하면 된다. 그래픽 테스트가 포함돼 있어 D3D12 디버그 레이어를 켠 채 돈다.
셰이더는 컴파일된 DXIL 이 헤더로 커밋돼 있어서 별도 셰이더 컴파일러가 없어도 빌드된다.

## 에디터 실행

인자 없이 실행하면 확인용 씬이 뜬다. 프로젝트를 주면 그 `.jproject` 를 연다.
프로젝트 파일에는 `EngineVersion` 과 `Framework`(`2D` 또는 `3D`)가 있어야 하고, 없으면 열기를 거절한다.
2D 인지 3D 인지는 이 파일이 정하므로 실행할 때 따로 고르지 않는다.
`--content-root` 는 로컬라이징 표와 아이콘 글꼴이 있는 폴더이고, 주지 않으면 현재 작업 폴더가 기준이다.
전체 규약은 `--help` 와 [tasks/todo.md](tasks/todo.md) 의 D-97 에 있다.

```bash
source/JBroEngine/Build/x64/Debug/JBroEditorHost.exe --project 내게임.jproject --content-root source/JBroEngine
```

## 읽는 순서

1. [위키](https://github.com/taku7664/JBroEngine/wiki)에서 전체 그림을 본다.
   구조를 한눈에 보려면 [구조 다이어그램](docs/JBroEngine.drawio.xml)을 draw.io 에서 연다
2. 코드를 만지기 전에 [ProjectRule.md](docs/ProjectRule.md)를 읽는다
3. 어떤 결정이 왜 났는지 궁금하면 [tasks/todo.md](tasks/todo.md)의 Decisions 절을 찾는다

## 라이선스

[LICENSE](LICENSE) 참고.

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

- JBroScript 언어와 컴파일러 `jbroc` (문법·규칙 문서만 있다)
- 에셋 로드(`AssetSystem::Load` 는 스텁이다). 스프라이트는 아직 텍스처를 받지 못한다
- 3D 렌더 시스템, 물리 시뮬레이션 본체, 오디오
- Web 빌드(스텁만 있다)

## 저장소 구성

```
source/JBroEngine/
  Modules/           모듈마다 vcxproj 하나. 공개 헤더는 Include/JBro/<이름>/ 아래
  Tests/             단일 테스트 실행 파일(JBroTests). 경계 위반이 컴파일 실패하는지 보는 음성 테스트 포함
  ThirdParty/        외부 라이브러리를 소스째로 둔다(지금은 ImGui)
  Localization/      에디터 문구(ko-KR, en-US)
docs/
  ProjectRule.md     지켜야 하는 규칙. 코드와 문서가 다르면 이쪽이 맞다
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

테스트는 빌드된 `JBroTests.exe` 를 실행하면 된다. 그래픽 테스트가 포함돼 있어 D3D12 디버그 레이어를 켠 채 돈다.
셰이더는 컴파일된 DXIL 이 헤더로 커밋돼 있어서 별도 셰이더 컴파일러가 없어도 빌드된다.

## 읽는 순서

1. [위키](https://github.com/taku7664/JBroEngine/wiki)에서 전체 그림을 본다
2. 코드를 만지기 전에 [ProjectRule.md](docs/ProjectRule.md)를 읽는다
3. 어떤 결정이 왜 났는지 궁금하면 [tasks/todo.md](tasks/todo.md)의 Decisions 절을 찾는다

## 라이선스

[LICENSE](LICENSE) 참고.

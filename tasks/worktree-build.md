# W-build · 빌드 구성과 2D/3D 배타

**브랜치**: `work/build` (경로: `../JBro-build`)
**병합 순서**: 1번 (독립. 언제 넣어도 무해)

## 목표

게임 실행 파일에 **선택한 프레임워크(Framework2D 또는 Framework3D)만 링크**되도록 빌드 구성을
분리하고, 잘못된 include 를 컴파일러가 막게 한다.

에디터 · 엔진 빌드는 둘 다 포함해서 어느 프로젝트든 열 수 있게 유지한다.

## 소유 파일 (이 워크트리만 수정)

- `source/JBroEngine/*.slnx` — 솔루션 구성
- `source/JBroEngine/JBro.Common.props` — 공통 프로퍼티 시트
- `source/JBroEngine/Modules/*/JBroXxx.vcxproj` — 모든 모듈 프로젝트
- `source/JBroEngine/Templates/**` — 프로젝트 템플릿 (신규 생성. 아직 없음)

**MUST**: 다른 워크트리는 어떤 `.vcxproj` 도 수정하지 않는다. 새 소스 파일은 glob
(`Source\**\*.cpp` / `Include\**\*.h`) 이 자동으로 잡으므로 vcxproj 를 건드릴 필요가 없다.

## 배경 — 이 워크트리에 처음 온 사람을 위한 설명

- 각 `JBroXxx` 모듈은 **StaticLibrary** 로 빌드된다 (Stage A 결과).
- 링크는 `JBro.Common.props` 의 `AdditionalIncludeDirectories` 로 강제된다 — 프로젝트 참조
  안 한 모듈의 헤더를 include 하면 `error C1083` (헤더를 못 찾음) 이 난다.
- 게임 스크립트 DLL 만 유일한 DLL 경계이다. 다른 모듈은 전부 정적 링크.
- 신규 리포는 아직 게임 실행 파일 프로젝트가 없다. 지금은 `JBroTests.exe` 만 있다.

## 작업 항목 (순서대로)

### E1. 스크립트 타깃 include 경로를 2D/3D 별로 정의한다

**Why**: 게임 스크립트가 `#include <JBro/Framework2D/...>` 만 되어야 2D 프로젝트, 반대는 3D.
같이 열려 있으면 사용자가 실수로 반대 프레임워크 헤더를 포함할 수 있다.

**How**:

1. `JBro.Common.props` 에 다음 프로퍼티 셋을 추가한다:
   ```xml
   <PropertyGroup Label="JBroScriptTarget">
     <JBroScriptDimension Condition="'$(JBroScriptDimension)' == ''">2D</JBroScriptDimension>
     <JBroScriptFrameworkIncludes Condition="'$(JBroScriptDimension)' == '2D'">
       $(JBroModulesDir)JBroFramework2D\Include
     </JBroScriptFrameworkIncludes>
     <JBroScriptFrameworkIncludes Condition="'$(JBroScriptDimension)' == '3D'">
       $(JBroModulesDir)JBroFramework3D\Include
     </JBroScriptFrameworkIncludes>
   </PropertyGroup>
   ```
2. 스크립트 타깃 전용 프로퍼티 시트 `JBro.Script.props` 를 새로 만든다.
   `JBroScriptFrameworkIncludes` + `JBroModulesDir\JBroCore\Include`
   + `JBroModulesDir\JBroRuntime\Include` + `JBroModulesDir\JBroAsset\Include` 만 include 경로에
   포함한다. Platform · RHI · Graphics 는 포함하지 않는다 (스크립트가 GPU/OS 를 직접 만지지 않음).

### E2. 2D 스크립트가 Framework3D 헤더를 include 하면 컴파일 실패하는 음성 테스트

**Why**: E1 의 include 경로 강제가 실제로 동작하는지 확인이 없으면 사용자 프로젝트에서만 늦게
드러난다. Stage A 에서 `JBroCore → JBro/Runtime/...` 음성 테스트를 같은 방식으로 넣었다.

**How**:

1. `source/JBroEngine/Tests/NegativeIncludeProbe/` 폴더에 두 개의 임시 `.cpp` 를 만든다:
   - `Probe2DFromScript.cpp` — `#include <JBro/Framework3D/Framework3D.h>` (실패해야 정상)
   - `Probe3DFromScript.cpp` — `#include <JBro/Framework2D/Framework2D.h>` (실패해야 정상)
2. 이들을 `JBro.Script.props` 를 상속한 임시 vcxproj 로 빌드해 본다. C1083 이 나면 통과.
3. 확인 후 프로브 파일을 지운다 (실제 빌드에 남기지 않는다).

### E3. 게임 익스포트: 선택된 프레임워크만 링크

**Why**: 배포 시 2D 게임 실행 파일에 3D 코드가 딸려 오면 안 된다. 링커가 자동으로 dead code 를
잘라 주지 않는 이유는 vtable · 스태틱 이니셜라이저 등이 유지되기 때문이다.

**How**:

1. 게임 실행 파일 vcxproj 를 새로 만든다: `source/JBroEngine/Modules/JBroGameHost/JBroGameHost.vcxproj`
   (신규 모듈).
2. `JBroGameHost` 는 `JBroScriptDimension` 조건으로 프로젝트 참조를 나눈다:
   ```xml
   <ItemGroup Condition="'$(JBroScriptDimension)' == '2D'">
     <ProjectReference Include="$(JBroModulesDir)JBroFramework2D\JBroFramework2D.vcxproj" />
   </ItemGroup>
   <ItemGroup Condition="'$(JBroScriptDimension)' == '3D'">
     <ProjectReference Include="$(JBroModulesDir)JBroFramework3D\JBroFramework3D.vcxproj" />
   </ItemGroup>
   ```
3. 솔루션 구성 `Debug_Game2D` / `Release_Game2D` / `Debug_Game3D` / `Release_Game3D` 를 추가한다.
   각 구성에서 `JBroScriptDimension` 을 프로퍼티로 세팅.

### E4. 엔진 · 에디터 빌드는 둘 다 포함함을 확인

**Why**: 에디터는 어느 프로젝트든 열 수 있어야 하므로 두 프레임워크를 모두 알아야 한다.
게임 빌드만 배타적이다.

**How**:

1. `Debug` / `Release` 기본 구성은 `JBroScriptDimension` 을 설정하지 않는다 (엔진 빌드).
2. 엔진 빌드에서 Editor 프로젝트가 Framework2D 와 Framework3D 를 모두 참조하는지 확인.
3. Debug/Release 클린 빌드 통과 확인.

### E5. 프로젝트 템플릿 (아직 사용자 프로젝트 생성 흐름 없음 — 스켈레톤만)

**Why**: 사용자가 "새 프로젝트 → 2D 선택" 흐름을 쓰려면 템플릿이 필요하다. 지금은 그 흐름 자체가
없으므로 **템플릿 파일만 위치 잡아 두고 내용은 최소**로 한다. 실제 사용은 W-host 의 EditorApplication
이 구현한다.

**How**:

1. `source/JBroEngine/Templates/Game2D/` — 스켈레톤 `.jproject` (형식은 W-host 가 확정) + 예시
   `PlayerScript.h` 하나. `#include <JBro/ScriptAPI.h>` + `JBRO_SCRIPT` 사용.
2. `source/JBroEngine/Templates/Game3D/` — 대칭.

## 검증

- [ ] Debug / Release x64 (엔진 빌드) 클린 빌드 통과
- [ ] `Debug_Game2D` / `Debug_Game3D` 빌드 통과, 각각 반대 프레임워크 lib 가 링크되지 않음
      (`dumpbin /dependents` 로 확인 — Framework3D.lib 이 2D 게임 exe 에 없음)
- [ ] E2 음성 테스트 프로브 두 개 C1083 확인 후 제거
- [ ] `JBroTests.exe` Debug / Release 통과

## 다른 워크트리와의 인터페이스

- **W-platform** 이 새 모듈 (JBroGraphics 안의 Renderer 등) 을 만들면, 그 프로젝트의 vcxproj
  틀은 이미 있다 (Stage A). 여기서는 안 건드림.
- **W-host** 가 게임 실행 파일의 main() 을 짜면 `JBroGameHost` 프로젝트에 들어간다.
- 새 프로퍼티 시트 (`JBro.Script.props`) 는 이후 스크립트 DLL 프로젝트가 상속한다.

## 병합

- 자체 검증 통과 후 main 에 병합.
- 병합 직후 다른 워크트리들은 `git rebase main` 필수 (공통 props 가 바뀌므로).

#pragma once

#include <JBro/Host/ProjectFile.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    class IPlatform;
    struct Uuid;

    // 사용자 게임 스크립트 프로젝트를 만든다(cpp-script-plan §3.3, D-266). 에셋 브라우저의 `스크립트 추가` 가 부른다.
    //
    // 자리는 기존 엔진과 같다: `<프로젝트>/<ScriptSourceDirectory>/` 에 `GameScript.sln`·`GameScript.vcxproj`, 스크립트는 그 아래 `Scripts/`.
    // **프로젝트 파일은 한 번만 쓴다**(기존 엔진의 C2). vcxproj 는 `Scripts\**\*.cpp` 를 와일드카드로 잡고 엔진 설정은 엔진의
    // `JBro.GameScript.props`·`.targets` 에서 가져오므로, 스크립트를 더하거나 엔진이 바뀌어도 다시 쓸 일이 없다.
    // **사용자 코드를 읽지 않는다**(기존 엔진의 C3) - 필드는 사용자가 적은 `JBRO_FIELD` 를 컴파일러가 읽는다.
    namespace ScriptProject
    {
        // 스크립트 추가 창에서 고르는 필드 타입이다. 리플렉션 설명자가 있는 것만 둔다 - 컴포넌트 참조(`Ref<T>`)는 설명자가 없어 아직 없다.
        enum class FieldType : std::uint8_t
        {
            Bool,
            Int,
            Float,
            String,
            Vector2,
            Vector3,
            Color,
            GameObject,
            Asset,
            Count
        };

        // 헤더에 적히는 C++ 타입 이름이다(프렐류드가 `using namespace JBro;` 를 하므로 짧은 이름이다).
        const char* FieldCppType(FieldType type);

        struct FieldSpec
        {
            String    name;
            FieldType type = FieldType::Float;
        };

        // 이름이 쓸 수 없는 까닭이다. 창이 이 값으로 안내 문구를 고른다.
        enum class NameProblem : std::uint8_t
        {
            None,
            Empty,
            // C++ 식별자가 아니다(글자·숫자·밑줄, 숫자로 시작하지 않는다).
            NotIdentifier,
            // C++ 예약어이거나 스크립트 기반 타입이 이미 쓰는 이름이다(`OnUpdate`·`GetOwner` 등).
            Reserved,
            // 빌트인 컴포넌트나 로드된 스크립트가 이미 쓰는 타입 이름이다. 이 이름이 캔버스 파일의 `Type:` 이 된다.
            TakenByType,
            // 그 폴더에 같은 이름의 `.h` 나 `.cpp` 가 있다. **번호를 붙여 피하지 않는다** - 타입 이름이 조용히 바뀐다.
            TakenByFile,
            // 필드 목록 안에서 겹친다.
            Duplicate
        };

        NameProblem CheckScriptName(IPlatform& platform, const char* folder, const char* name);
        NameProblem CheckFieldName(const Array<FieldSpec>& fields, std::size_t index);

        // 만들어지는 글자들이다. 줄 끝은 CRLF 다 - Visual Studio 가 여는 파일이다.
        String MakeScriptHeader(const char* className, const Array<FieldSpec>& fields, FrameworkKind framework);
        String MakeScriptSource(const char* className, FrameworkKind framework);
        String MakeModuleSource(FrameworkKind framework);
        String MakeProjectFile(FrameworkKind framework, const Uuid& projectGuid);
        String MakeSolutionFile(const Uuid& projectGuid);
        String MakeEngineProps(const char* engineRoot);
        String MakeGitIgnore();

        // 엔진 폴더(`JBro.GameScript.props` 가 있는 곳)를 찾는다. `startFolder` 에서 위로 몇 칸 올라가며 본다 - 개발 빌드의
        // 에디터는 `source/JBroEngine/Build/x64/Debug/` 에 있다. 엔진 설치본에는 SDK 가 없어 빈 글자다(cpp-script-plan §4).
        String FindEngineRoot(IPlatform& platform, const char* startFolder);

        // 스크립트 프로젝트가 있는가(`GameScript.vcxproj`).
        bool HasProject(IPlatform& platform, const char* contentsFolder);

        // 없는 것만 만든다(`GameScript.sln`·`.vcxproj`·`Scripts/ScriptModule.cpp`·`.gitignore`). 이미 있는 파일은 건드리지 않는다.
        bool EnsureProject(IPlatform& platform, const char* contentsFolder, FrameworkKind framework, String& error);

        // `JBroEngine.props` 를 이 엔진의 위치로 맞춘다. 내용이 같으면 쓰지 않는다 - 에디터가 프로젝트를 열 때마다 부른다.
        // 이 PC 의 경로라 `.gitignore` 에 들어 있다.
        bool RefreshEngineProps(IPlatform& platform, const char* contentsFolder, const char* engineRoot);

        // 스크립트 `.h`/`.cpp` 한 쌍을 `folder` 에 쓴다. 이름이 쓸 수 없거나 파일이 이미 있으면 아무것도 쓰지 않고 거짓이다.
        bool CreateScript(IPlatform& platform, const char* folder, const char* className,
            const Array<FieldSpec>& fields, FrameworkKind framework, String& error);
    }
}

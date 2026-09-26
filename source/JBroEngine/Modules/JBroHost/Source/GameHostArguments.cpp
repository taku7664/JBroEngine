#include <JBro/Host/GameHostArguments.h>

#include <JBro/Host/ProjectFile.h>
#include <JBro/Platform/Platform.h>

#include <cstring>
#include <string_view>

namespace JBro
{
    GameHostArguments ParseGameHostArguments(int argumentCount, const char* const* arguments)
    {
        GameHostArguments result;
        if (arguments == nullptr)
        {
            return result;
        }
        // 0 번은 실행 파일이다.
        for (int index = 1; index < argumentCount; ++index)
        {
            const char* argument = arguments[index];
            if (argument == nullptr)
            {
                continue;
            }
            const bool isProject = std::strcmp(argument, "--project") == 0;
            const bool isCanvas = std::strcmp(argument, "--canvas") == 0;
            if (false == isProject && false == isCanvas)
            {
                result.error = "unknown argument: ";
                result.error.append(argument);
                return result;
            }
            if (index + 1 >= argumentCount || arguments[index + 1] == nullptr || arguments[index + 1][0] == '\0')
            {
                result.error = "missing value after ";
                result.error.append(argument);
                return result;
            }
            (isProject ? result.projectFile : result.canvasFile) = arguments[index + 1];
            ++index;
        }
        return result;
    }

    String FindProjectBesideExecutable(IPlatform& platform)
    {
        const String folder = platform.GetExecutableFolder();
        if (folder.empty())
        {
            return String();
        }
        struct Search
        {
            String best;
        } search;
        platform.EnumerateDirectory(folder.c_str(), [](const char* relative, bool isDirectory, void* user) {
            auto& found = *static_cast<Search*>(user);
            const std::string_view name(relative);
            constexpr std::string_view extension(".jproject");
            // 옆의 파일만 본다 - 폴더에 거짓을 돌려주어 아래로 내려가지 않는다. 차례는 바이트 차례다(파일 시스템의 열거 차례를 믿지 않는다).
            if (false == isDirectory && name.size() > extension.size()
                && name.compare(name.size() - extension.size(), extension.size(), extension) == 0
                && (found.best.empty() || name < std::string_view(found.best)))
            {
                found.best.assign(name.data(), name.size());
            }
            return false;
        }, &search);
        if (search.best.empty())
        {
            return String();
        }
        String path = folder;
        if (path.back() != '/' && path.back() != '\\')
        {
            path.push_back('/');
        }
        path.append(search.best);
        return path;
    }

    String ResolvePackagedStartupCanvas(const GameHostArguments& arguments, const ProjectFile& project)
    {
        return false == arguments.canvasFile.empty() ? arguments.canvasFile : project.build.startupCanvas;
    }

    String ResolveStartupCanvasPath(const GameHostArguments& arguments, const ProjectFile& project, const char* projectFilePath)
    {
        if (false == arguments.canvasFile.empty())
        {
            return ResolveProjectRelativePath(arguments.canvasFile.c_str(), projectFilePath);
        }
        if (project.build.startupCanvas.empty())
        {
            return String();
        }
        return ResolveProjectRelativePath(project.build.startupCanvas.c_str(), projectFilePath);
    }
}

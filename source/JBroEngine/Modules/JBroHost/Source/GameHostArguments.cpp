#include <JBro/Host/GameHostArguments.h>

#include <JBro/Host/ProjectFile.h>

#include <cstring>

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

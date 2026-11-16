#include <JBro/Editor/EditorHostArguments.h>

#include <JBro/Platform/Platform.h>

#include <cstdlib>
#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>

namespace JBro
{
    namespace
    {
        Bool ParseCount(const char* text, Int64& result)
        {
            char* end = nullptr;
            const long long value = std::strtoll(text, &end, 10);
            if (end == text || end == nullptr || *end != '\0' || value < 0)
            {
                return false;
            }
            result = value;
            return true;
        }

        // 값을 받는 옵션이 값 없이 마지막에 오면 다음 인자를 읽다가 배열 밖으로 나간다. 여기서 한 번에 막고, 무엇이 빠졌는지 옵션 이름으로 알린다.
        Bool TakeValue(Int32 argumentCount, const char* const* arguments, Int32& index, const char*& value, String& error)
        {
            if (index + 1 >= argumentCount || arguments[index + 1] == nullptr)
            {
                error = "this option needs a value: ";
                error.append(arguments[index]);
                return false;
            }
            ++index;
            value = arguments[index];
            return true;
        }

        Bool Is(const char* argument, const char* name)
        {
            return std::strcmp(argument, name) == 0;
        }
    }

    EditorHostArguments ParseEditorHostArguments(Int32 argumentCount, const char* const* arguments)
    {
        EditorHostArguments result;
        if (arguments == nullptr)
        {
            return result;
        }
        // 0 번은 실행 파일이다.
        for (Int32 index = 1; index < argumentCount; ++index)
        {
            const char* argument = arguments[index];
            if (argument == nullptr)
            {
                continue;
            }
            if (Is(argument, "--help") || Is(argument, "-h"))
            {
                result.showHelp = true;
                return result;
            }
            if (Is(argument, "--register-file-type"))
            {
                result.registerFileType = true;
                continue;
            }
            if (Is(argument, "--unregister-file-type"))
            {
                result.unregisterFileType = true;
                continue;
            }
            const char* value = nullptr;
            if (Is(argument, "--project") || Is(argument, "--content-root"))
            {
                if (false == TakeValue(argumentCount, arguments, index, value, result.error))
                {
                    return result;
                }
                if (Is(argument, "--content-root"))
                {
                    result.contentRoot = value;
                    continue;
                }
                if (false == result.projectFile.empty())
                {
                    result.error = "the project was given twice: ";
                    result.error.append(value);
                    return result;
                }
                result.projectFile = value;
                continue;
            }
            if (Is(argument, "--frames"))
            {
                if (false == TakeValue(argumentCount, arguments, index, value, result.error))
                {
                    return result;
                }
                if (false == ParseCount(value, result.frameLimit))
                {
                    result.error = "the frame count must be zero or more, not: ";
                    result.error.append(value);
                }
                if (false == result.IsValid())
                {
                    return result;
                }
                continue;
            }
            if (Is(argument, "--control-port"))
            {
                if (false == TakeValue(argumentCount, arguments, index, value, result.error))
                {
                    return result;
                }
                if (false == ParseCount(value, result.controlPort) || result.controlPort > 65535)
                {
                    result.error = "the control port must be 0 to 65535, not: ";
                    result.error.append(value);
                    return result;
                }
                continue;
            }
            if (argument[0] == '-' && argument[1] != '\0')
            {
                result.error = "this option is not known: ";
                result.error.append(argument);
                return result;
            }
            // 이름 없는 인자는 프로젝트 경로다. 사람이 손으로 열 때와 탐색기에서 두 번 눌렀을 때(D-336) 온다.
            if (false == result.projectFile.empty())
            {
                result.error = "the project was given twice: ";
                result.error.append(argument);
                return result;
            }
            result.projectFile = argument;
        }
        if (result.registerFileType && result.unregisterFileType)
        {
            result.error = "--register-file-type and --unregister-file-type cannot be given together";
        }
        return result;
    }

    Bool MakeProjectFileType(const IPlatform& platform, FileTypeAssociation& desc)
    {
        const String executable = platform.GetExecutablePath();
        if (executable.empty())
        {
            return false;
        }
        desc.extension = ".jproject";
        desc.progId = "JBroEngine.Project";
        desc.description = "JBro Project";
        desc.command = "\"";
        desc.command.append(executable);
        desc.command.append("\" \"%1\"");
        desc.icon = "\"";
        desc.icon.append(executable);
        desc.icon.append("\",0");
        return true;
    }
}

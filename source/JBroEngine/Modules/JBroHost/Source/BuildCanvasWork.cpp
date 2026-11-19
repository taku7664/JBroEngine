#include <JBro/Host/BuildCanvasWork.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/CanvasFile.h>
#include <JBro/Core/Log.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/Platform/Platform.h>

#include <cstddef>

namespace JBro
{
    UInt32 SumBuildCanvasWork(IPlatform& platform, const ProjectFile& project, const char* projectFilePath, UInt32 (*count)(Canvas& canvas))
    {
        if (count == nullptr)
        {
            return 0;
        }
        Array<String> canvases = project.build.buildCanvases;
        if (canvases.IsEmpty() && false == project.build.startupCanvas.empty())
        {
            canvases.Add(project.build.startupCanvas);
        }
        UInt32 work = 0;
        Array<std::byte> text;
        for (const String& relative : canvases)
        {
            const String path = ResolveProjectRelativePath(relative.c_str(), projectFilePath);
            if (false == platform.ReadWholeFile(path.c_str(), text))
            {
                Log::Write(LogLevel::Info, "physics", "a build canvas could not be read for the physics threads: %s", path.c_str());
                continue;
            }
            // 따로 세운 캔버스에 읽어 센다. 열린 캔버스와 섞이지 않고, 읽은 뒤 바로 버린다.
            Canvas scratch(CreateDefaultAllocator());
            CanvasFileError error;
            if (false == ReadCanvasText(scratch, reinterpret_cast<const char*>(text.Data()), text.Size(), error))
            {
                Log::Write(LogLevel::Info, "physics", "a build canvas could not be counted for the physics threads: %s (%s)",
                    path.c_str(), error.message.c_str());
                continue;
            }
            work += count(scratch);
        }
        return work;
    }

    void ResolvePhysicsIgnoredLayers(const ProjectFile& project, UInt32 (&rows)[32])
    {
        for (UInt32& row : rows)
        {
            row = 0u;
        }
        for (const ProjectLayerPair& pair : project.physicsIgnoredLayerPairs)
        {
            if (pair.first >= 32 || pair.second >= 32)
            {
                continue;
            }
            rows[pair.first] |= 1u << pair.second;
            rows[pair.second] |= 1u << pair.first;
        }
    }
}

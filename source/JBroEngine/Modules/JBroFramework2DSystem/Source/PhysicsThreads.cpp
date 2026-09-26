#include <JBro/Framework2DSystem/PhysicsThreads.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/CanvasFile.h>
#include <JBro/Core/Log.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/Physics2D/World.h>
#include <JBro/Platform/Platform.h>

#include <algorithm>
#include <thread>

namespace JBro
{
    std::uint32_t CountPhysicsWork(Canvas& canvas)
    {
        std::uint32_t work = 0;
        canvas.ForEach<Component::Collider2D>([&work](Component::Collider2D& collider)
        {
            if (false == collider.IsActiveComponent())
            {
                return;
            }
            const std::size_t points = collider.points.Size();
            const bool pieced = collider.shape == Component::ColliderShape2D::Polygon && points > 4;
            work += pieced ? static_cast<std::uint32_t>(points - 2) : 1u;
        });
        return work;
    }

    std::uint32_t CountProjectPhysicsWork(IPlatform& platform, const ProjectFile& project, const char* projectFilePath)
    {
        Array<String> canvases = project.build.buildCanvases;
        if (canvases.IsEmpty() && false == project.build.startupCanvas.empty())
        {
            canvases.Add(project.build.startupCanvas);
        }
        std::uint32_t work = 0;
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
            work += CountPhysicsWork(scratch);
        }
        return work;
    }

    std::uint32_t RecommendProjectPhysicsWorkers(IPlatform& platform, const ProjectFile& project, const char* projectFilePath)
    {
        const std::uint32_t work = CountProjectPhysicsWork(platform, project, projectFilePath);
        return Physics2D::RecommendWorkerCount(work, std::thread::hardware_concurrency());
    }

    std::uint32_t ResolvePhysicsWorkerCount(IPlatform& platform, const ProjectFile& project, const char* projectFilePath)
    {
        switch (project.build.physicsThreadMode)
        {
        case PhysicsThreadMode::Single:
            return 0;
        case PhysicsThreadMode::Workers:
            return std::min(project.build.physicsWorkers, Physics2D::MaxWorkerCount);
        case PhysicsThreadMode::Auto:
        default:
            return RecommendProjectPhysicsWorkers(platform, project, projectFilePath);
        }
    }
}

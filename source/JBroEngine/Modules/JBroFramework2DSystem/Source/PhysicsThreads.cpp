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
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    UInt32 CountPhysicsWork(Canvas& canvas)
    {
        UInt32 work = 0;
        canvas.ForEach<Component::Collider2D>([&work](Component::Collider2D& collider)
        {
            if (false == collider.IsActiveComponent())
            {
                return;
            }
            const std::size_t points = collider.points.Size();
            if (collider.shape == Component::ColliderShape2D::Chain)
            {
                // 선분마다 한 조각이다. 포인트가 없으면 선분 하나.
                work += points < 2 ? UInt32(1u) : static_cast<JBro::UInt32>(collider.loop ? UInt32(points) : points - 1);
                return;
            }
            const Bool pieced = collider.shape == Component::ColliderShape2D::Polygon && points > 4;
            work += pieced ? static_cast<JBro::UInt32>(points - 2) : UInt32(1u);
        });
        return work;
    }

    UInt32 CountProjectPhysicsWork(IPlatform& platform, const ProjectFile& project, const char* projectFilePath)
    {
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
            work += CountPhysicsWork(scratch);
        }
        return work;
    }

    UInt32 RecommendProjectPhysicsWorkers(IPlatform& platform, const ProjectFile& project, const char* projectFilePath)
    {
        const UInt32 work = CountProjectPhysicsWork(platform, project, projectFilePath);
        return Physics2D::RecommendWorkerCount(work, std::thread::hardware_concurrency());
    }

    UInt32 ResolvePhysicsWorkerCount(IPlatform& platform, const ProjectFile& project, const char* projectFilePath)
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

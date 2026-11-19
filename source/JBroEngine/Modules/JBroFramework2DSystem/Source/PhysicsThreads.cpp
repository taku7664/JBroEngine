#include <JBro/Framework2DSystem/PhysicsThreads.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/CanvasFile.h>
#include <JBro/Core/Log.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Host/BuildCanvasWork.h>
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
        // 빌드 캔버스를 읽는 길은 3D 와 함께 쓴다(D-419). 세는 것만 2D 다.
        return SumBuildCanvasWork(platform, project, projectFilePath, &CountPhysicsWork);
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

}

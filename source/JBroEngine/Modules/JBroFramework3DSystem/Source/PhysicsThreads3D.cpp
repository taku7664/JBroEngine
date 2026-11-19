#include <JBro/Framework3DSystem/PhysicsThreads3D.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework3D/Component/Physics3D.h>
#include <JBro/Host/BuildCanvasWork.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/Physics3D/World.h>

#include <thread>

namespace JBro
{
    UInt32 CountPhysicsWork3D(Canvas& canvas)
    {
        UInt32 work = 0;
        canvas.ForEach<Component::Collider3D>([&work](Component::Collider3D& collider) {
            if (collider.IsActiveComponent())
            {
                ++work;
            }
        });
        return work;
    }

    UInt32 RecommendProjectPhysicsWorkers3D(IPlatform& platform, const ProjectFile& project, const char* projectFilePath)
    {
        const UInt32 work = SumBuildCanvasWork(platform, project, projectFilePath, &CountPhysicsWork3D);
        return Physics3D::RecommendWorkerCount(work, std::thread::hardware_concurrency());
    }

    UInt32 ResolvePhysicsWorkerCount3D(IPlatform& platform, const ProjectFile& project, const char* projectFilePath)
    {
        switch (project.build.physicsThreadMode)
        {
        case PhysicsThreadMode::Single:
            return 0;
        case PhysicsThreadMode::Workers:
            return project.build.physicsWorkers < Physics3D::MaxWorkerCount ? project.build.physicsWorkers : Physics3D::MaxWorkerCount;
        case PhysicsThreadMode::Auto:
        default:
            return RecommendProjectPhysicsWorkers3D(platform, project, projectFilePath);
        }
    }
}

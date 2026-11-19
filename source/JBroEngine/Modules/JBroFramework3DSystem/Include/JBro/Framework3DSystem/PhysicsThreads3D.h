#pragma once

#include <JBro/Types/UInt.h>

// 물리 스레드 설정을 3D 워커 수로 푼다(D-419, 2D 의 `PhysicsThreads.h` 와 같은 자리). 게임 호스트와 에디터가 프로젝트를 연 뒤 한 번 부르고,
// 결과를 `Framework3D::SetPhysicsWorkerCount` 에 넘긴다. 프로젝트 설정 화면의 "추천 값 사용" 도 같은 계산을 쓴다.
namespace JBro
{
    class Canvas;
    class IPlatform;
    struct ProjectFile;

    // 캔버스 하나의 3D 물리 일감이다: 켜진 `Collider3D` 마다 1.
    UInt32 CountPhysicsWork3D(Canvas& canvas);
    // 설정의 추천 값이다: `Physics3D::RecommendWorkerCount(빌드 캔버스의 일감, 코어 수)`.
    UInt32 RecommendProjectPhysicsWorkers3D(IPlatform& platform, const ProjectFile& project, const char* projectFilePath);
    // 설정을 워커 수로 푼다. Single 은 0, Workers 는 그 값(상한 `Physics3D::MaxWorkerCount`), Auto 는 추천 값이다.
    UInt32 ResolvePhysicsWorkerCount3D(IPlatform& platform, const ProjectFile& project, const char* projectFilePath);
}

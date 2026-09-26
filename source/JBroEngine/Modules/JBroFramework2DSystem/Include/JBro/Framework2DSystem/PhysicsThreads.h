#pragma once

#include <cstdint>

// 물리 스레드 설정을 워커 수로 푼다(D-223). 게임 호스트와 에디터가 프로젝트를 연 뒤 한 번 부르고, 결과를
// `Framework2D::SetPhysicsWorkerCount` 에 넘긴다. 프로젝트 설정 화면의 "추천 값 사용" 도 같은 계산을 쓴다.
namespace JBro
{
    class Canvas;
    class IPlatform;
    struct ProjectFile;

    // 캔버스 하나의 물리 일감이다: 켜진 `Collider2D` 마다 1, 포인트가 넷을 넘는 폴리곤은 포인트 수 - 2(볼록 조각 수의 위쪽 어림).
    std::uint32_t CountPhysicsWork(Canvas& canvas);

    // 프로젝트의 빌드 캔버스(목록이 비었으면 시작 캔버스)를 읽어 일감을 더한다. 읽지 못한 캔버스는 건너뛰고 로그에 남긴다.
    // 캔버스를 읽으려면 컴포넌트 타입이 등록돼 있어야 하므로 프레임워크를 연 뒤에 부른다.
    std::uint32_t CountProjectPhysicsWork(IPlatform& platform, const ProjectFile& project, const char* projectFilePath);

    // 설정의 추천 값이다: `RecommendWorkerCount(CountProjectPhysicsWork(...), 코어 수)`.
    std::uint32_t RecommendProjectPhysicsWorkers(IPlatform& platform, const ProjectFile& project, const char* projectFilePath);

    // 설정을 워커 수로 푼다. Single 은 0, Workers 는 그 값(상한 `MaxWorkerCount`), Auto 는 추천 값이다.
    std::uint32_t ResolvePhysicsWorkerCount(IPlatform& platform, const ProjectFile& project, const char* projectFilePath);
}

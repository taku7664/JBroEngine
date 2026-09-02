#pragma once

namespace JBro
{
    // 3계층 컨텍스트. 각 구조체가 실제로 담을 시스템·서비스 포인터는 Stage H1 에서 채운다.
    // - EngineContext  : 호스트 프로세스 안 모든 것. 사용자에게 노출되지 않는다.
    // - SystemContext  : 게임 스크립트 DLL 로 넘어가는 시스템 부분집합. 사용자 접근 불가.
    // - ServiceContext : 게임 스크립트가 값으로 들고 다니는 서비스 부분집합.
    struct EngineContext  {};
    struct SystemContext  {};
    struct ServiceContext {};

    // 호스트가 DLL 로드 시점에 한 번 호출한다(매 프레임 아님).
    void BindSystemContext(const SystemContext& context);
    void BindServiceContext(const ServiceContext& context);
}

#include <JBro/Platform/WindowsPlatform.h>

#include <JBro/Network/Native/WinsockSocketProvider.h>

// Windows 의 소켓이다(D-122). 네트워크는 소켓을 직접 열지 않고 이 provider 를 거친다.
// 구현은 네트워크 프로젝트의 `Native::WinsockSocketProvider` 다 - 플랫폼은 그것을 내어 줄 뿐이다.
namespace JBro
{
    OwnerPtr<Network::ISocketProvider> WindowsPlatform::CreateSocketProvider()
    {
        return MakeOwnerPtr<Network::Native::WinsockSocketProvider>();
    }
}

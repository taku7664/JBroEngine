#pragma once

#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

#if defined(_MSC_VER)
#include <excpt.h>
#endif

namespace JBro
{
    // 스크립트 훅에서 난 예외 하나다(D-330). 엔진이 받은 그 자리의 값만 담는다 - 주소를 파일·줄로 바꾸는 일은 받은 뒤에 한다.
    struct ScriptFault
    {
        static constexpr UInt32 MaxFrames = 32;

        // 운영체제의 예외 코드다(`0xC0000005` 는 잘못된 메모리 접근).
        UInt32 code = 0;
        // 예외가 난 명령의 주소다.
        UInt64 address = 0;
        // 그때 엔진이 부르던 훅의 이름이다(`"OnUpdate"`). 문자열 리터럴만 넣는다.
        const char* hook = nullptr;
        // 예외 자리부터 바깥으로 거슬러 올라간 명령 주소다. 첫 칸이 예외 자리다. 붙잡는 함수가 없으면 비어 있다.
        UInt64 frames[MaxFrames] = {};
        UInt32 frameCount = 0;
    };

    // 예외 자리의 스택을 읽는 함수다. 호스트가 플랫폼의 구현을 넘긴다 - 이 모듈은 플랫폼을 보지 않는다.
    // `exceptionPointers` 는 운영체제의 예외 정보(`EXCEPTION_POINTERS*`)다. 채운 칸 수를 돌려준다.
    using ScriptFaultCapture = UInt32 (*)(void* user, const void* exceptionPointers, UInt64* frames, UInt32 capacity);

    // **스크립트가 죽어도 에디터는 산다**(D-330). 엔진이 스크립트 코드를 부르는 자리는 모두 `ScriptCallGuard::Call` 을 거친다.
    //
    // 켜져 있으면 훅을 SEH 로 감싸 예외를 받고, 첫 예외를 적어 둔 뒤 **그다음 훅은 부르지 않는다** - 예외가 난 스크립트의
    // 캔버스는 이미 믿을 수 없다. 호스트(에디터)가 다음 프레임에 적힌 것을 꺼내 실행을 멈추고 되돌린 뒤 `ClearFault` 한다.
    // 꺼져 있으면(게임) 그냥 부른다 - 예외는 지금처럼 프로세스를 끝낸다.
    //
    // 막지 못하는 것: `__fastfail`(버퍼 넘침 검사·CRT 힙 손상)·`abort`·무한 루프. 스크립트가 엔진 메모리를 덮어쓴 뒤라면 그 순간은 넘겨도 뒤에 죽을 수 있다.
    // **메인 스레드 전용이다.** 상태는 이 모듈을 링크한 바이너리마다 따로 있다 - 스크립트 DLL 에 든 사본은 켜지지 않으므로 호스트에서만 켠다.
    namespace ScriptCallGuard
    {
        void Enable(ScriptFaultCapture capture, void* user);
        void Disable();
        Bool IsEnabled();

        Bool HasFault();
        const ScriptFault& GetFault();
        void ClearFault();

        // 지금 감싼 훅 안인가. 스크립트 중단점(D-330 2 단계)이 스크립트가 도는 동안의 멈춤인지를 이것으로 가린다. 켜져 있을 때만 센다.
        Bool IsInCall();

        namespace Detail
        {
            // 예외 필터다. 예외를 적고 받겠다고 답한다.
            int Filter(const void* exceptionPointers, const char* hook);
            // 받은 뒤 처리다. 스택 넘침이면 지킴 페이지를 되살린다.
            void Recover();
            // 감싼 훅의 깊이를 센다. 예외로 나와도 `Leave` 를 부른다.
            void Enter();
            void Leave();
        }

        // 훅 하나를 부른다. 부르지 않았거나 예외가 났으면 거짓이다. `call` 은 인자 없는 호출 가능한 것이고 소멸자가 있는 값을 만들지 않는다.
        template <typename Callable>
        Bool Call(const char* hook, Callable&& call)
        {
            if (HasFault())
            {
                return false;
            }
            if (false == IsEnabled())
            {
                call();
                return true;
            }
#if defined(_MSC_VER)
            Detail::Enter();
            __try
            {
                call();
            }
            __except (Detail::Filter(_exception_info(), hook))
            {
                Detail::Leave();
                Detail::Recover();
                return false;
            }
            Detail::Leave();
            return true;
#else
            call();
            return true;
#endif
        }
    }
}

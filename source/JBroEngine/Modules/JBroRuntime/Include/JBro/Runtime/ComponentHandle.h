#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Types/Bool.h>

#include <type_traits>

namespace JBro
{
    class ComponentBase;

    namespace Handle
    {
        class GameObject;
    }

    namespace Internal
    {
        struct ComponentHandleAccess;
    }
}

namespace JBro::Internal
{
    // 빌트인 컴포넌트 핸들(`Handle::Transform2D` 등)의 공용 몸이다(D-302). 저장부는 `InstanceRef`(24B) 하나이고 가상 함수가 없다 -
    // 스크립트 DLL 경계를 값으로 넘는다. 파생 핸들은 데이터 멤버를 더하지 않고, 타입 이름(`StaticTypeName`)과 필드 접근 멤버만 둔다.
    //
    // 핸들은 컴포넌트 데이터 헤더를 모른다 - 그 헤더는 스크립트 대상 빌드에서 `#error` 다. 멤버의 몸은 프레임워크 라이브러리의 `.cpp` 에 있다.
    // 메인 스레드 전용이다(`Ref<T>` 와 같다, D-54).
    class ComponentHandle
    {
    public:
        // 가리키는 컴포넌트가 아직 붙어 있는가. 떼였거나 오브젝트가 사라졌으면 거짓이다.
        Bool IsValid() const;
        explicit operator bool() const;

        // 이 컴포넌트가 붙은 오브젝트다. 무효면 경고를 남기고 빈 핸들을 준다.
        Handle::GameObject GetGameObject() const;
        Bool IsEnabled() const;
        void SetEnabled(Bool enabled);

        // 같은 컴포넌트를 가리키는가. 해석하지 않고 번호만 견준다.
        Bool operator==(const ComponentHandle& other) const;

    protected:
        ComponentHandle() = default;

        // 해석한다. 무효면 `typeName::operation` 으로 경고를 남기고 nullptr 이다.
        ComponentBase* ResolveComponent(const char* typeName, const char* operation) const;

    private:
        friend struct ComponentHandleAccess;

        ComponentBase* ResolveSilently() const;

        InstanceRef m_reference;
    };

    // 핸들의 비공개 자리를 호스트 코드(오브젝트 핸들의 `GetComponent`·서비스·시험)가 쓰는 창구다. 스크립트 API 가 아니다.
    struct ComponentHandleAccess
    {
        template<typename THandle>
        static THandle FromReference(const InstanceRef& reference)
        {
            static_assert(std::is_base_of_v<ComponentHandle, THandle>);
            THandle handle;
            static_cast<ComponentHandle&>(handle).m_reference = reference;
            return handle;
        }

        static const InstanceRef& GetReference(const ComponentHandle& handle)
        {
            return handle.m_reference;
        }

        // 필드 글자를 읽은 자리(`ComponentHandleFromText`)가 쓴다. 타입은 부르는 쪽이 이미 확인했다.
        static void SetReference(ComponentHandle& handle, const InstanceRef& reference)
        {
            handle.m_reference = reference;
        }

        // 경고를 남기지 않는다 - 서비스는 무효한 인자를 조용히 무시한다.
        static ComponentBase* Resolve(const ComponentHandle& handle)
        {
            return handle.ResolveSilently();
        }

        // 데이터 클래스로 해석한다. 두 타입의 이름이 같아야 한다 - 다르면 컴파일이 실패한다.
        // `TData` 가 완전한 타입인 곳(프레임워크 라이브러리의 `.cpp`)에서만 부른다.
        template<typename TData, typename THandle>
        static TData* ResolveAs(const THandle& handle)
        {
            static_assert(MakeStableTypeId(TData::StaticTypeName()) == MakeStableTypeId(THandle::StaticTypeName()),
                "a component handle resolves only to the data class of the same name");
            return static_cast<TData*>(handle.ResolveSilently());
        }
    };
}

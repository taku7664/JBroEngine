#pragma once

#include <JBro/Internal/InstanceRegistry.h>
#include <JBro/Reflection/TypeDescriptor.h>
#include <JBro/Reflection/TypeDescriptorOf.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Types/NameTable.h>

#include <charconv>
#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>

namespace JBro::Internal
{
    // 핸들의 비공개 자리를 호스트 코드(캔버스 파일·에디터·물리)가 쓰는 창구다(D-233). 스크립트 API 가 아니다.
    struct GameObjectHandleAccess
    {
        // 번호만 든 핸들이다. 캐시는 비어 있고 처음 쓸 때 번호로 찾는다.
        static GameObjectHandle FromId(InstanceId objectId)
        {
            GameObjectHandle handle;
            handle.m_instanceId = objectId;
            return handle;
        }

        static GameObject* Resolve(const GameObjectHandle& handle)
        {
            return handle.Resolve();
        }
    };

    // **오브젝트 참조 필드의 글자다**(D-233). 캔버스 파일 안에서는 파일의 오브젝트 번호(`3`)이고 - 오브젝트 번호는 실행마다
    // 달라 파일에 적을 수 없다 - 그 밖(되돌리기·클립보드·스냅숏)에서는 이번 실행의 오브젝트 번호(`@123`)다. 빈 글자는 빈 참조다.
    // 어느 쪽인지는 공유 레지스트리에 선 문맥이 정한다 - 스크립트 DLL 이 제 사본의 코덱을 불러도 같은 문맥을 본다(D-44).
    inline const ValueCodec& GetGameObjectHandleCodec()
    {
        static const ValueCodec codec = [] {
            ValueCodec made;
            made.ToText = [](const void* value, char* buffer, std::size_t capacity,
                std::size_t& required) noexcept -> Bool {
                const InstanceId objectId = static_cast<const GameObjectHandle*>(value)->GetInstanceId();
                char local[32];
                std::size_t length = 0;
                if (objectId != InvalidInstanceId)
                {
                    const ObjectRefRemap* remap = InstanceRegistry::Get().GetObjectRefRemap();
                    if (remap != nullptr && remap->toIndex != nullptr)
                    {
                        // 이 캔버스에 없는 오브젝트(지워졌다)를 가리키면 빈 참조로 적는다.
                        const Int64 index = remap->toIndex(remap->user, objectId);
                        if (index >= 0)
                        {
                            length = static_cast<std::size_t>(std::to_chars(local, local + sizeof(local), index).ptr - local);
                        }
                    }
                    else
                    {
                        local[0] = '@';
                        length = static_cast<std::size_t>(std::to_chars(local + 1, local + sizeof(local), objectId).ptr - local);
                    }
                }
                required = length + 1;
                if (buffer == nullptr || capacity < required)
                {
                    return false;
                }
                std::memcpy(buffer, local, length);
                buffer[length] = '\0';
                return true;
            };
            made.FromText = [](void* value, const char* text, std::size_t length) noexcept -> Bool {
                if (text == nullptr)
                {
                    return false;
                }
                GameObjectHandle& handle = *static_cast<GameObjectHandle*>(value);
                if (length == 0)
                {
                    handle = GameObjectHandle{};
                    return true;
                }
                if (text[0] == '@')
                {
                    std::uint64_t objectId = InvalidInstanceId.Get();
                    const auto parsed = std::from_chars(text + 1, text + length, objectId);
                    if (parsed.ec != std::errc() || parsed.ptr != text + length)
                    {
                        return false;
                    }
                    handle = GameObjectHandleAccess::FromId(objectId);
                    return true;
                }
                // 파일 안 번호는 캔버스 파일을 읽는 동안에만 뜻이 있다.
                const ObjectRefRemap* remap = InstanceRegistry::Get().GetObjectRefRemap();
                if (remap == nullptr || remap->toObjectId == nullptr)
                {
                    return false;
                }
                std::int64_t index = -1;
                const auto parsed = std::from_chars(text, text + length, index);
                if (parsed.ec != std::errc() || parsed.ptr != text + length)
                {
                    return false;
                }
                handle = GameObjectHandleAccess::FromId(remap->toObjectId(remap->user, index));
                return true;
            };
            made.Equals = [](const void* left, const void* right) noexcept -> Bool {
                return static_cast<const GameObjectHandle*>(left)->GetInstanceId()
                    == static_cast<const GameObjectHandle*>(right)->GetInstanceId();
            };
            made.Assign = [](void* destination, const void* source) noexcept {
                *static_cast<GameObjectHandle*>(destination) = *static_cast<const GameObjectHandle*>(source);
            };
            return made;
        }();
        return codec;
    }
}

namespace JBro
{
    // 잎사귀다. 에디터는 이 타입 이름을 보고 캔버스의 오브젝트를 고르는 칸을 그린다.
    template <>
    struct TypeDescriptorOf<GameObjectHandle>
    {
        static const TypeDescriptor& Get()
        {
            static const TypeDescriptor descriptor = [] {
                TypeDescriptor made;
                made.typeName = NameTable::Get().Intern("JBro.GameObjectHandle");
                made.size = static_cast<std::uint32_t>(sizeof(GameObjectHandle));
                made.alignment = static_cast<std::uint32_t>(alignof(GameObjectHandle));
                made.triviallyCopyable = true;
                made.codec = &Internal::GetGameObjectHandleCodec();
                return made;
            }();
            return descriptor;
        }
    };

    // 캔버스 파일이 쓰고 읽는 동안 오브젝트 참조 글자를 파일 안 번호로 바꾼다. 둥지를 틀지 않는다(바깥 문맥을 되돌려 놓는다).
    class ObjectRefRemapScope final
    {
    public:
        explicit ObjectRefRemapScope(const Internal::ObjectRefRemap& remap)
            : m_previous(Internal::InstanceRegistry::Get().GetObjectRefRemap())
        {
            Internal::InstanceRegistry::Get().SetObjectRefRemap(&remap);
        }

        ~ObjectRefRemapScope()
        {
            Internal::InstanceRegistry::Get().SetObjectRefRemap(m_previous);
        }

        ObjectRefRemapScope(const ObjectRefRemapScope&) = delete;
        ObjectRefRemapScope& operator=(const ObjectRefRemapScope&) = delete;

    private:
        const Internal::ObjectRefRemap* m_previous = nullptr;
    };
}

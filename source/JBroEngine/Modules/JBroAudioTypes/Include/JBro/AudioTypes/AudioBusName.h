#pragma once

#include <JBro/Reflection/TypeDescriptor.h>
#include <JBro/Reflection/TypeDescriptorOf.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Types/String.h>

#include <cstring>
#include <string_view>

namespace JBro
{
    // 버스를 이름으로 가리키는 값이다(D-197). 메모리에는 인턴된 정수(`NameId`) 하나이고 파일과 인스펙터에는 이름으로
    // 적힌다 - 컴포넌트 공개 필드에 `String` 을 두지 않는다는 규칙(§10.4)과 사람이 읽는 파일을 둘 다 지킨다.
    // 빈 이름과 `Master` 는 Master 버스다. 목록에 없는 이름도 Master 로 떨어지고 경고가 한 번 남는다.
    struct AudioBusName
    {
        NameId id = InvalidNameId;

        static AudioBusName FromText(const char* text)
        {
            AudioBusName name;
            if (text != nullptr && text[0] != '\0')
            {
                name.id = NameTable::Get().Intern(text);
            }
            return name;
        }

        bool IsMaster() const
        {
            return id == InvalidNameId || id == MakeNameId("Master");
        }

        friend bool operator==(const AudioBusName& left, const AudioBusName& right)
        {
            return left.id == right.id;
        }
    };

    inline const ValueCodec& GetAudioBusNameCodec()
    {
        static const ValueCodec codec = [] {
            ValueCodec made;
            made.ToText = [](const void* value, char* buffer, std::size_t capacity,
                std::size_t& required) noexcept -> bool {
                const NameId id = static_cast<const AudioBusName*>(value)->id;
                const std::string_view text = id == InvalidNameId ? std::string_view() : NameTable::Get().Resolve(id);
                required = text.size() + 1;
                if (buffer == nullptr || capacity < required)
                {
                    return false;
                }
                std::memcpy(buffer, text.data(), text.size());
                buffer[text.size()] = '\0';
                return true;
            };
            made.FromText = [](void* value, const char* text, std::size_t length) noexcept -> bool {
                if (text == nullptr)
                {
                    return false;
                }
                try
                {
                    AudioBusName name;
                    if (length > 0)
                    {
                        const String copy(text, length);
                        name.id = NameTable::Get().Intern(copy.c_str());
                    }
                    *static_cast<AudioBusName*>(value) = name;
                    return true;
                }
                catch (...)
                {
                    return false;
                }
            };
            made.Equals = [](const void* left, const void* right) noexcept -> bool {
                return static_cast<const AudioBusName*>(left)->id == static_cast<const AudioBusName*>(right)->id;
            };
            made.Assign = [](void* destination, const void* source) noexcept {
                *static_cast<AudioBusName*>(destination) = *static_cast<const AudioBusName*>(source);
            };
            return made;
        }();
        return codec;
    }

    // 잎사귀다. 필드를 내놓지 않고 이름 글자로 오간다. 에디터는 이 타입 이름을 보고 프로젝트의 버스 목록 콤보를 그린다.
    template <>
    struct TypeDescriptorOf<AudioBusName>
    {
        static const TypeDescriptor& Get()
        {
            static const TypeDescriptor descriptor = [] {
                TypeDescriptor made;
                made.typeName = NameTable::Get().Intern("JBro.AudioBusName");
                made.size = static_cast<std::uint32_t>(sizeof(AudioBusName));
                made.alignment = static_cast<std::uint32_t>(alignof(AudioBusName));
                made.triviallyCopyable = true;
                made.codec = &GetAudioBusNameCodec();
                return made;
            }();
            return descriptor;
        }
    };
}

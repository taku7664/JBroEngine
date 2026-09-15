#pragma once

#include <JBro/Types/String.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    class YamlDocument;
    class YamlWriter;
    struct PropertyTable;
    struct TypeDescriptor;
    struct ValueCodec;

    // 리플렉션이 설명하는 값 하나를 YAML 로 쓰고 읽는다(D-86).
    //
    // **값을 걸어 내려가는 길이 여기 한 곳에만 있다.** 처음에는 캔버스 파일 안에만 있었는데,
    // 에디터의 스냅샷과 되돌리기도 같은 값을 글자로 떠야 한다. 따로 걸어 내려가면 한쪽만
    // 고쳐지는 날이 온다 - 캔버스 파일은 컨테이너를 거절하고 스냅샷은 조용히 건너뛰던 것이
    // 바로 그렇게 갈라진 결과였다.
    //
    // 필드가 있으면 타고 내려가고, 없으면 코덱으로 글자를 얻는다. 나열로 적는 타입
    // (`writeFieldsAsSequence`)은 이름 없이 줄지어 적는다.
    struct ReflectedYamlError
    {
        String message;
        // 실패한 필드의 이름이다. 없으면 빈 문자열이다.
        String fieldName;
    };

    // 코덱이 내놓는 글자를 받아 온다. 버퍼가 모자라면 필요한 만큼 잡고 한 번 더 묻는다.
    bool ReflectedValueToText(const ValueCodec& codec, const void* value, String& text);

    // 값 하나를 적는다. `key` 가 nullptr 이면 시퀀스 항목 자리다.
    bool WriteReflectedValue(
        YamlWriter& writer,
        const char* key,
        const TypeDescriptor& type,
        const void* value,
        ReflectedYamlError& error);

    // 값 하나를 읽는다. **읽히지 않는 값을 기본값으로 대신하지 않는다** - 실패다.
    bool ReadReflectedValue(
        const YamlDocument& document,
        std::uint32_t node,
        const TypeDescriptor& type,
        void* value,
        ReflectedYamlError& error);

    // 맵 하나의 키들을 프로퍼티 표에 맞춰 읽는다. `skip` 에 있는 키는 표에 없어도 넘어간다
    // (컴포넌트의 `Type`·`IsEnabled` 처럼 표가 아니라 파일 형식이 정한 키다).
    //
    // **파일에 있는데 표에 없는 키는 실패다.** 조용히 버리면 그 값이 사라지고 아무도 모른다.
    bool ReadReflectedFields(
        const YamlDocument& document,
        std::uint32_t node,
        const PropertyTable& table,
        void* value,
        const char* const* skip,
        std::size_t skipCount,
        ReflectedYamlError& error);
}

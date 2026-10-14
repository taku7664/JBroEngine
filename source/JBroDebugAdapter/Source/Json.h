#pragma once

#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/String.h>

#include <cstddef>
#include <cstdint>

namespace JBro::DebugAdapter
{
    // DAP 메시지 하나를 담는 JSON 값이다. 어댑터가 주고받는 것만큼만 한다 - 객체의 키 차례를 지키고, 수는 `double` 로 든다
    // (JSON 의 수가 그렇다. DAP 의 번호는 모두 2^53 안이다).
    class JsonValue
    {
    public:
        enum class Kind : std::uint8_t
        {
            Null,
            Bool,
            Number,
            String,
            Array,
            Object
        };

        JsonValue() = default;
        JsonValue(JsonValue&&) noexcept = default;
        JsonValue& operator=(JsonValue&&) noexcept = default;
        // 자식을 홀로 소유하므로 옮기기만 한다.
        JsonValue(const JsonValue&) = delete;
        JsonValue& operator=(const JsonValue&) = delete;
        JsonValue(Bool value);
        JsonValue(Int64 value);
        JsonValue(Int32 value);
        JsonValue(double value);
        JsonValue(const char* value);
        JsonValue(String value);

        static JsonValue MakeArray();
        static JsonValue MakeObject();

        Kind GetKind() const { return m_kind; }
        Bool IsNull() const { return m_kind == Kind::Null; }

        // 객체다. 같은 키가 있으면 바꾼다. 이어 쓰게 자신을 돌려준다.
        JsonValue& Set(const char* key, JsonValue value);
        // 없거나 객체가 아니면 nullptr 이다.
        const JsonValue* Find(const char* key) const;
        std::size_t GetMemberCount() const { return m_keys.Size(); }
        const String& GetKeyAt(std::size_t index) const { return m_keys[index]; }
        const JsonValue& GetMemberAt(std::size_t index) const { return *m_items[index]; }

        // 배열이다.
        JsonValue& Add(JsonValue value);
        std::size_t GetSize() const { return m_items.Size(); }
        const JsonValue& At(std::size_t index) const { return *m_items[index]; }

        Bool AsBool(Bool fallback = false) const;
        Int64 AsInt64(Int64 fallback = 0) const;
        double AsNumber(double fallback = 0.0) const;
        // 글자가 아니면 빈 글자다.
        const String& AsString() const;

        // 키를 따라 들어간 값의 글자다. 중간에 없으면 `fallback` 이다.
        String GetString(const char* key, const char* fallback = "") const;
        Int64 GetInt64(const char* key, Int64 fallback = 0) const;
        Bool GetBool(const char* key, Bool fallback = false) const;

        // 빈칸 없는 한 줄이다.
        String Serialize() const;
        // UTF-8 글자 전체를 읽는다. 뒤에 공백 말고 다른 것이 남거나 형식이 틀리면 거짓이다.
        static Bool Parse(const char* text, std::size_t length, JsonValue& out);

    private:
        void SerializeTo(String& out) const;

        Kind m_kind = Kind::Null;
        Bool m_bool = false;
        double m_number = 0.0;
        String m_string;
        // 배열의 원소이거나, 객체의 값이다. 객체면 `m_keys` 와 같은 차례다. `Array` 는 아직 정의되지 않은 자기 타입을 담지 못해 하나씩 소유한다.
        Array<OwnerPtr<JsonValue>> m_items;
        Array<String> m_keys;
    };
}

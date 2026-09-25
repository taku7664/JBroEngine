#include <JBro/Runtime/TextStore.h>

#include <cstring>
#include <limits>

namespace JBro
{
    namespace
    {
        TextStore* g_boundTextStore = nullptr;

        // 캔버스 파일의 YAML 은 값을 따옴표·이스케이프 없이 한 줄에 적고, 여러 줄 스칼라를 읽지 않는다(Yaml.h).
        // 그래서 코덱이 글자를 한 줄로 접는다: `\` `\n` `\r` `\t` `"` 와 첫 글자의 `[`·`{` 를 이스케이프하고, 그대로 두면
        // YAML 이 다르게 읽을 글자(앞뒤 공백, 따옴표로 시작, 이스케이프가 들어간 것)는 큰따옴표로 감싼다. 읽을 때는 감싼 따옴표가
        // 있으면 벗기고(파서가 이미 벗긴 경우도 있다) 이스케이프를 푼다. 모르는 이스케이프는 글자 그대로 둔다.
        // 인스펙터의 한 줄 칸도 이 모양을 보여 주므로 사용자는 `\n` 으로 줄을 바꿀 수 있다.
        String Escape(const char* text, std::size_t length)
        {
            String escaped;
            escaped.reserve(length + 2);
            bool changed = false;
            for (std::size_t index = 0; index < length; ++index)
            {
                const char character = text[index];
                switch (character)
                {
                case '\\': escaped += "\\\\"; changed = true; break;
                case '\n': escaped += "\\n"; changed = true; break;
                case '\r': escaped += "\\r"; changed = true; break;
                case '\t': escaped += "\\t"; changed = true; break;
                case '"': escaped += "\\\""; changed = true; break;
                default: escaped += character; break;
                }
            }
            const bool edgeSpace = length > 0 && (text[0] == ' ' || text[length - 1] == ' ');
            const bool startsQuoted = length > 0 && text[0] == '\'';
            // 파서는 따옴표를 벗긴 **뒤에** `[]`·`{}` 를 빈 컨테이너로 읽는다 - 감싸는 것으로는 막지 못한다. 첫 글자를 이스케이프한다.
            if (false == escaped.empty() && (escaped[0] == '[' || escaped[0] == '{'))
            {
                escaped.insert(escaped.begin(), '\\');
                changed = true;
            }
            if (changed || edgeSpace || startsQuoted)
            {
                escaped.insert(escaped.begin(), '"');
                escaped += '"';
            }
            return escaped;
        }

        String Unescape(const char* text, std::size_t length)
        {
            if (length >= 2 && text[0] == '"' && text[length - 1] == '"')
            {
                ++text;
                length -= 2;
            }
            String plain;
            plain.reserve(length);
            for (std::size_t index = 0; index < length; ++index)
            {
                const char character = text[index];
                if (character != '\\' || index + 1 >= length)
                {
                    plain += character;
                    continue;
                }
                const char next = text[index + 1];
                switch (next)
                {
                case '\\': plain += '\\'; ++index; break;
                case 'n': plain += '\n'; ++index; break;
                case 'r': plain += '\r'; ++index; break;
                case 't': plain += '\t'; ++index; break;
                case '"': plain += '"'; ++index; break;
                case '[': plain += '['; ++index; break;
                case '{': plain += '{'; ++index; break;
                default: plain += character; break;
                }
            }
            return plain;
        }
    }

    TextStore& TextStore::Local()
    {
        static TextStore store;
        return store;
    }

    TextStore& TextStore::Get()
    {
        return g_boundTextStore != nullptr ? *g_boundTextStore : Local();
    }

    void TextStore::Bind(TextStore* store)
    {
        g_boundTextStore = store;
    }

    const TextStore::Slot* TextStore::Find(TextId id) const
    {
        if (id.generation == 0 || id.index >= m_slots.Size())
        {
            return nullptr;
        }
        const Slot& slot = m_slots[id.index];
        return slot.occupied && slot.generation == id.generation ? &slot : nullptr;
    }

    TextStore::Slot* TextStore::Find(TextId id)
    {
        return const_cast<Slot*>(static_cast<const TextStore*>(this)->Find(id));
    }

    TextId TextStore::Create(const char* utf8, std::size_t length)
    {
        std::uint32_t index = 0;
        if (false == m_free.IsEmpty())
        {
            index = m_free[m_free.Size() - 1];
            m_free.RemoveAt(m_free.Size() - 1);
        }
        else
        {
            if (m_slots.Size() >= std::numeric_limits<std::uint32_t>::max())
            {
                return {};
            }
            index = static_cast<std::uint32_t>(m_slots.Size());
            m_slots.Emplace();
        }
        Slot& slot = m_slots[index];
        slot.occupied = true;
        slot.revision = 1;
        slot.text.assign(utf8 != nullptr ? utf8 : "", utf8 != nullptr ? length : 0);
        ++m_live;
        return { index, slot.generation };
    }

    bool TextStore::Set(TextId id, const char* utf8, std::size_t length)
    {
        Slot* slot = Find(id);
        if (slot == nullptr)
        {
            return false;
        }
        slot->text.assign(utf8 != nullptr ? utf8 : "", utf8 != nullptr ? length : 0);
        ++slot->revision;
        if (slot->revision == 0)
        {
            slot->revision = 1;
        }
        return true;
    }

    void TextStore::Assign(TextId& id, const char* utf8, std::size_t length)
    {
        if (false == Set(id, utf8, length))
        {
            id = Create(utf8, length);
        }
    }

    void TextStore::Destroy(TextId id)
    {
        Slot* slot = Find(id);
        if (slot == nullptr)
        {
            return;
        }
        slot->occupied = false;
        slot->text.clear();
        slot->text.shrink_to_fit();
        slot->revision = 0;
        // 세대를 올려 옛 번호가 새 주인의 글자를 보지 않게 한다. 0 은 "칸 없음" 이라 건너뛴다.
        ++slot->generation;
        if (slot->generation == 0)
        {
            slot->generation = 1;
        }
        m_free.Add(id.index);
        --m_live;
    }

    bool TextStore::IsAlive(TextId id) const
    {
        return Find(id) != nullptr;
    }

    ArrayView<const char> TextStore::GetText(TextId id) const
    {
        const Slot* slot = Find(id);
        if (slot == nullptr)
        {
            return {};
        }
        return ArrayView<const char>(slot->text.data(), slot->text.size());
    }

    std::uint32_t TextStore::GetRevision(TextId id) const
    {
        const Slot* slot = Find(id);
        return slot != nullptr ? slot->revision : 0;
    }

    std::uint32_t TextStore::GetLiveCount() const
    {
        return m_live;
    }

    void TextStore::Clear()
    {
        m_slots.Clear();
        m_free.Clear();
        m_live = 0;
    }

    const ValueCodec& GetTextIdCodec()
    {
        static const ValueCodec codec = [] {
            ValueCodec made;
            made.ToText = [](const void* value, char* buffer, std::size_t capacity, std::size_t& required) noexcept -> bool {
                try
                {
                    const ArrayView<const char> text = TextStore::Get().GetText(*static_cast<const TextId*>(value));
                    const String escaped = Escape(text.Data() != nullptr ? text.Data() : "", text.Size());
                    required = escaped.size() + 1;
                    if (buffer == nullptr || capacity < required)
                    {
                        return false;
                    }
                    std::memcpy(buffer, escaped.data(), escaped.size());
                    buffer[escaped.size()] = '\0';
                    return true;
                }
                catch (...)
                {
                    required = 0;
                    return false;
                }
            };
            made.FromText = [](void* value, const char* text, std::size_t length) noexcept -> bool {
                if (text == nullptr)
                {
                    return false;
                }
                try
                {
                    const String plain = Unescape(text, length);
                    TextStore::Get().Assign(*static_cast<TextId*>(value), plain.data(), plain.size());
                    return true;
                }
                catch (...)
                {
                    return false;
                }
            };
            made.Equals = [](const void* left, const void* right) noexcept -> bool {
                const TextStore& store = TextStore::Get();
                const ArrayView<const char> a = store.GetText(*static_cast<const TextId*>(left));
                const ArrayView<const char> b = store.GetText(*static_cast<const TextId*>(right));
                return a.Size() == b.Size() && (a.Size() == 0 || std::memcmp(a.Data(), b.Data(), a.Size()) == 0);
            };
            made.Assign = [](void* destination, const void* source) noexcept {
                if (destination == source)
                {
                    return;
                }
                TextStore& store = TextStore::Get();
                // 원본 글자를 먼저 떠 둔다. 같은 칸을 가리키는 두 값 사이의 대입이어도 글자가 사라지지 않는다.
                try
                {
                    const ArrayView<const char> text = store.GetText(*static_cast<const TextId*>(source));
                    const String copy(text.Data() != nullptr ? text.Data() : "", text.Size());
                    TextId& target = *static_cast<TextId*>(destination);
                    const TextId from = *static_cast<const TextId*>(source);
                    if (target.index == from.index && target.generation == from.generation && target.IsValid())
                    {
                        // 같은 칸을 나눠 가진 둘이다(C++ 복사가 만든 것). 받는 쪽에 새 칸을 준다.
                        target = store.Create(copy.data(), copy.size());
                        return;
                    }
                    store.Assign(target, copy.data(), copy.size());
                }
                catch (...)
                {
                }
            };
            return made;
        }();
        return codec;
    }
}

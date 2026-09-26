#include <JBro/Input/InputBindingText.h>

#include <JBro/Core/InputKeys.h>
#include <JBro/Core/Log.h>
#include <JBro/Types/NameTable.h>

#include <cstdio>
#include <cstring>

// 리바인딩 글자(D-218). 설정 화면에서 가끔 부르는 콜드 경로라 `String` 을 만든다.
namespace JBro::System
{
    namespace
    {
        const char* const SourceNames[] = {"Key", "MouseButton", "GamepadButton", "GamepadAxis", "GamepadStick"};
        const char* const CompositeNames[] = {"None", "Up", "Down", "Left", "Right"};
        const char* const StickNames[] = {"Left", "Right"};

        bool IsPlainName(const char* name)
        {
            if (name == nullptr || name[0] == '\0' || name[0] == ' ')
            {
                return false;
            }
            const std::size_t length = std::strlen(name);
            if (name[length - 1] == ' ')
            {
                return false;
            }
            for (std::size_t index = 0; index < length; ++index)
            {
                const char c = name[index];
                const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                    || c == '_' || c == '-' || c == '.' || c == ' ';
                if (false == allowed)
                {
                    return false;
                }
            }
            return true;
        }

        const char* CodeName(const InputBinding& binding)
        {
            switch (binding.source)
            {
            case InputBindingSource::Key:
                return GetKeyName(static_cast<Key>(binding.code));
            case InputBindingSource::MouseButton:
                return GetMouseButtonName(static_cast<MouseButton>(binding.code));
            case InputBindingSource::GamepadButton:
                return GetGamepadButtonName(static_cast<GamepadButton>(binding.code));
            case InputBindingSource::GamepadAxis:
                return GetGamepadAxisName(static_cast<GamepadAxis>(binding.code));
            case InputBindingSource::GamepadStick:
                return binding.code < 2 ? StickNames[binding.code] : nullptr;
            }
            return nullptr;
        }

        bool SameBindings(const InputActionDesc& a, const InputActionDesc& b)
        {
            if (a.bindingCount != b.bindingCount)
            {
                return false;
            }
            for (std::uint32_t index = 0; index < a.bindingCount && index < MaxInputBindingsPerAction; ++index)
            {
                const InputBinding& x = a.bindings[index];
                const InputBinding& y = b.bindings[index];
                if (x.source != y.source || x.code != y.code || x.composite != y.composite || x.gamepad != y.gamepad)
                {
                    return false;
                }
            }
            return true;
        }

        void Trim(const char*& begin, const char*& end)
        {
            while (begin < end && (*begin == ' ' || *begin == '\t'))
            {
                ++begin;
            }
            while (end > begin && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
            {
                --end;
            }
        }

        template<std::size_t Count>
        bool FindName(const char* const (&names)[Count], const String& word, std::uint8_t& out)
        {
            for (std::size_t index = 0; index < Count; ++index)
            {
                if (word == names[index])
                {
                    out = static_cast<std::uint8_t>(index);
                    return true;
                }
            }
            return false;
        }

        bool ParseCode(InputBindingSource source, const String& word, std::uint16_t& code)
        {
            switch (source)
            {
            case InputBindingSource::Key:
            {
                Key key = Key::Unknown;
                if (FindKeyByName(word.c_str(), key) && key != Key::Unknown)
                {
                    code = static_cast<std::uint16_t>(key);
                    return true;
                }
                return false;
            }
            case InputBindingSource::MouseButton:
            {
                MouseButton button = MouseButton::Count;
                if (FindMouseButtonByName(word.c_str(), button))
                {
                    code = static_cast<std::uint16_t>(button);
                    return true;
                }
                return false;
            }
            case InputBindingSource::GamepadButton:
            {
                GamepadButton button = GamepadButton::Count;
                if (FindGamepadButtonByName(word.c_str(), button))
                {
                    code = static_cast<std::uint16_t>(button);
                    return true;
                }
                return false;
            }
            case InputBindingSource::GamepadAxis:
            {
                GamepadAxis axis = GamepadAxis::Count;
                if (FindGamepadAxisByName(word.c_str(), axis))
                {
                    code = static_cast<std::uint16_t>(axis);
                    return true;
                }
                return false;
            }
            case InputBindingSource::GamepadStick:
            {
                std::uint8_t stick = 0;
                if (FindName(StickNames, word, stick))
                {
                    code = stick;
                    return true;
                }
                return false;
            }
            }
            return false;
        }

        // `원천 코드 [방향] [@패드]` 하나다.
        bool ParseBinding(const char* begin, const char* end, InputBinding& out)
        {
            out = InputBinding{};
            String words[4];
            std::size_t count = 0;
            const char* at = begin;
            while (at < end)
            {
                while (at < end && *at == ' ')
                {
                    ++at;
                }
                const char* start = at;
                while (at < end && *at != ' ')
                {
                    ++at;
                }
                if (start == at)
                {
                    break;
                }
                if (count >= 4)
                {
                    return false;
                }
                words[count].assign(start, static_cast<std::size_t>(at - start));
                ++count;
            }
            if (count < 2)
            {
                return false;
            }
            std::uint8_t source = 0;
            if (false == FindName(SourceNames, words[0], source))
            {
                return false;
            }
            out.source = static_cast<InputBindingSource>(source);
            if (false == ParseCode(out.source, words[1], out.code))
            {
                return false;
            }
            bool sawComposite = false;
            bool sawPad = false;
            for (std::size_t index = 2; index < count; ++index)
            {
                const String& word = words[index];
                if (word[0] == '@' && false == sawPad)
                {
                    if (word.size() != 2 || word[1] < '0' || word[1] > '3')
                    {
                        return false;
                    }
                    out.gamepad = static_cast<std::int8_t>(word[1] - '0');
                    sawPad = true;
                    continue;
                }
                std::uint8_t composite = 0;
                if (sawComposite || sawPad || false == FindName(CompositeNames, word, composite))
                {
                    return false;
                }
                out.composite = static_cast<InputComposite>(composite);
                sawComposite = true;
            }
            return true;
        }

        // 한 줄의 값(따옴표 안)을 바인딩 목록으로 읽는다.
        bool ParseBindings(const char* begin, const char* end, InputBinding (&out)[MaxInputBindingsPerAction], std::uint32_t& count)
        {
            count = 0;
            Trim(begin, end);
            if (end - begin >= 2 && *begin == '"' && end[-1] == '"')
            {
                ++begin;
                --end;
            }
            Trim(begin, end);
            const char* at = begin;
            while (at < end)
            {
                const char* comma = at;
                while (comma < end && *comma != ',')
                {
                    ++comma;
                }
                const char* first = at;
                const char* last = comma;
                Trim(first, last);
                if (first == last)
                {
                    return false;
                }
                if (count >= MaxInputBindingsPerAction || false == ParseBinding(first, last, out[count]))
                {
                    return false;
                }
                ++count;
                at = comma < end ? comma + 1 : end;
            }
            return true;
        }
    }

    void WriteBindingOverrides(const InputActionMap& live, const InputActionMap& project, String& out)
    {
        out.clear();
        for (std::uint32_t index = 0; index < live.count && index < MaxInputActions; ++index)
        {
            const InputActionDesc& action = live.actions[index];
            const InputActionDesc* original = project.Find(action.name);
            if (original != nullptr && SameBindings(action, *original))
            {
                continue;
            }
            const char* name = NameTable::Get().Resolve(action.name);
            if (false == IsPlainName(name))
            {
                Log::Write(LogLevel::Warning, "input", "an input action with the name \"%s\" cannot be written to the binding overrides",
                    name != nullptr ? name : "?");
                continue;
            }
            out += name;
            out += ": \"";
            for (std::uint32_t at = 0; at < action.bindingCount && at < MaxInputBindingsPerAction; ++at)
            {
                const InputBinding& binding = action.bindings[at];
                const char* code = CodeName(binding);
                const auto source = static_cast<std::size_t>(binding.source);
                if (code == nullptr || source >= sizeof(SourceNames) / sizeof(SourceNames[0]))
                {
                    continue;
                }
                if (at > 0)
                {
                    out += ", ";
                }
                out += SourceNames[source];
                out += ' ';
                out += code;
                const auto composite = static_cast<std::size_t>(binding.composite);
                if (binding.composite != InputComposite::None && composite < sizeof(CompositeNames) / sizeof(CompositeNames[0]))
                {
                    out += ' ';
                    out += CompositeNames[composite];
                }
                if (binding.gamepad >= 0)
                {
                    out += " @";
                    out += static_cast<char>('0' + binding.gamepad);
                }
            }
            out += "\"\n";
        }
    }

    bool ReadBindingOverrides(const char* text, std::size_t length, InputActionMap& live)
    {
        if (text == nullptr)
        {
            return length == 0;
        }
        bool complete = true;
        std::uint32_t lineNumber = 0;
        const char* at = text;
        const char* const end = text + length;
        while (at < end)
        {
            const char* lineEnd = at;
            while (lineEnd < end && *lineEnd != '\n')
            {
                ++lineEnd;
            }
            ++lineNumber;
            const char* begin = at;
            const char* finish = lineEnd;
            at = lineEnd < end ? lineEnd + 1 : end;
            Trim(begin, finish);
            if (begin == finish || *begin == '#')
            {
                continue;
            }
            // 이름에는 `:` 가 없다(쓸 때 거른다). 첫 `:` 가 이름과 값을 가른다.
            const char* colon = begin;
            while (colon < finish && *colon != ':')
            {
                ++colon;
            }
            if (colon == finish)
            {
                complete = false;
                Log::Write(LogLevel::Warning, "input", "line %u of the binding overrides is not \"Action: bindings\"; it is skipped", lineNumber);
                continue;
            }
            const char* nameEnd = colon;
            const char* nameBegin = begin;
            Trim(nameBegin, nameEnd);
            char name[128] = {};
            const std::size_t nameLength = static_cast<std::size_t>(nameEnd - nameBegin);
            if (nameLength == 0 || nameLength >= sizeof(name))
            {
                complete = false;
                Log::Write(LogLevel::Warning, "input", "line %u of the binding overrides has no usable action name; it is skipped", lineNumber);
                continue;
            }
            std::memcpy(name, nameBegin, nameLength);
            const InputActionId id = MakeNameId(name);
            InputActionDesc* action = nullptr;
            for (std::uint32_t index = 0; index < live.count && index < MaxInputActions; ++index)
            {
                if (live.actions[index].name == id)
                {
                    action = &live.actions[index];
                    break;
                }
            }
            if (action == nullptr)
            {
                // 게임이 지운 액션이다. 옛 세이브가 새 판에서 열려도 된다.
                continue;
            }
            InputBinding bindings[MaxInputBindingsPerAction] = {};
            std::uint32_t count = 0;
            if (false == ParseBindings(colon + 1, finish, bindings, count))
            {
                complete = false;
                Log::Write(LogLevel::Warning, "input", "line %u of the binding overrides (\"%s\") is not understood; that action keeps its bindings",
                    lineNumber, name);
                continue;
            }
            action->bindingCount = static_cast<std::uint8_t>(count);
            for (std::uint32_t index = 0; index < MaxInputBindingsPerAction; ++index)
            {
                action->bindings[index] = index < count ? bindings[index] : InputBinding{};
            }
        }
        return complete;
    }
}

#include <JBro/Editor/EditorShortcutManager.h>

#include <JBro/Core/Yaml.h>

#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        Bool IsBlank(const char* text)
        {
            return text == nullptr || text[0] == '\0';
        }

        void Append(char* buffer, std::size_t capacity, const char* text)
        {
            const std::size_t used = std::strlen(buffer);
            const std::size_t room = capacity - used - 1;
            const std::size_t length = std::strlen(text);
            const std::size_t copied = length < room ? length : room;
            std::memcpy(buffer + used, text, copied);
            buffer[used + copied] = '\0';
        }

        Bool IsModifierKey(ImGuiKey key)
        {
            return (key >= ImGuiKey_LeftCtrl && key <= ImGuiKey_RightSuper)
                || (key >= ImGuiKey_ReservedForModCtrl && key <= ImGuiKey_ReservedForModSuper);
        }

        // 단축키가 될 수 있는 키다. 키보드 키와 **마우스 엄지 버튼 둘**(`MouseX1`·`MouseX2`)이다(D-258).
        // 왼쪽·오른쪽·가운데 버튼은 고르기·메뉴·팬이 쓰고, 게임패드는 게임의 것이라 빠진다.
        // 잡기(`CaptureBinding`)와 읽기(`Parse`)가 이 한 판정을 쓴다 - 둘이 갈리면 잡은 키가 다음 실행에 사라진다.
        Bool IsBindableKey(ImGuiKey key)
        {
            if (key == ImGuiKey_MouseX1 || key == ImGuiKey_MouseX2)
            {
                return true;
            }
            return key >= ImGuiKey_NamedKey_BEGIN && key < ImGuiKey_GamepadStart && false == IsModifierKey(key);
        }

        Bool Pressed(const EditorShortcutBinding& binding)
        {
            if (false == binding.IsSet())
            {
                return false;
            }
            const ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl != binding.control || io.KeyShift != binding.shift || io.KeyAlt != binding.alt || io.KeySuper)
            {
                return false;
            }
            return ImGui::IsKeyPressed(binding.key, false);
        }

        Bool SameScope(const String& a, const String& b)
        {
            return a == b;
        }

        const char* ScopeOrNull(const String& scope)
        {
            return scope.IsEmpty() ? nullptr : scope.c_str();
        }

        constexpr const char* SlotKeys[2] = {"Primary", "Secondary"};
    }

    ShortcutHandle EditorShortcutManager::Register(EditorShortcutDesc desc)
    {
        if (IsBlank(desc.id) || desc.handler.Get() == nullptr || FindEntry(desc.id) != nullptr)
        {
            return InvalidShortcutHandle;
        }
        OwnerPtr<Entry> entry = MakeOwnerPtr<Entry>();
        entry->handle = m_nextHandle++;
        entry->id = desc.id;
        entry->labelKey = IsBlank(desc.labelKey) ? desc.id : desc.labelKey;
        entry->categoryKey = IsBlank(desc.categoryKey) ? "" : desc.categoryKey;
        entry->scope = IsBlank(desc.scope) ? "" : desc.scope;
        entry->defaults[0] = desc.primary;
        entry->defaults[1] = desc.secondary;
        entry->blocksGlobal = desc.blocksGlobal;
        entry->whileTyping = desc.whileTyping;
        entry->duringGame = desc.duringGame;
        entry->handler = std::move(desc.handler);
        Apply(*entry);
        const ShortcutHandle handle = entry->handle;
        m_entries.Add(std::move(entry));
        return handle;
    }

    void EditorShortcutManager::Unregister(ShortcutHandle handle)
    {
        for (std::size_t index = 0; index < m_entries.Size(); ++index)
        {
            if (m_entries[index]->handle == handle)
            {
                m_entries.RemoveAt(index);
                return;
            }
        }
    }

    UInt32 EditorShortcutManager::GetCount() const
    {
        return static_cast<std::uint32_t>(m_entries.Size());
    }

    EditorShortcutView EditorShortcutManager::GetAt(UInt32 index) const
    {
        if (index >= m_entries.Size())
        {
            return {};
        }
        return ToView(*m_entries[index]);
    }

    EditorShortcutView EditorShortcutManager::Find(const char* id) const
    {
        const Entry* entry = FindEntry(id);
        return entry != nullptr ? ToView(*entry) : EditorShortcutView{};
    }

    Bool EditorShortcutManager::CanExecute(const char* id, const EditorApplication& editor) const
    {
        const Entry* entry = FindEntry(id);
        return entry != nullptr && entry->handler->CanExecute(editor);
    }

    const char* EditorShortcutManager::WhyBlocked(const char* id, const EditorApplication& editor) const
    {
        const Entry* entry = FindEntry(id);
        if (entry == nullptr || entry->handler->CanExecute(editor))
        {
            return nullptr;
        }
        return entry->handler->WhyBlocked(editor);
    }

    Bool EditorShortcutManager::Execute(const char* id, EditorApplication& editor)
    {
        Entry* entry = FindEntry(id);
        if (entry == nullptr || false == entry->handler->CanExecute(editor))
        {
            return false;
        }
        return entry->handler->Execute(editor);
    }

    Bool EditorShortcutManager::SetBinding(const char* id, UInt32 slot, const EditorShortcutBinding& binding)
    {
        Entry* entry = FindEntry(id);
        if (entry == nullptr || slot > 1)
        {
            return false;
        }
        EditorShortcutBinding next[2] = {entry->bindings[0], entry->bindings[1]};
        next[slot] = binding;
        if (next[0] == entry->bindings[0] && next[1] == entry->bindings[1])
        {
            return true;
        }
        // 기본값으로 돌아왔으면 사용자의 것이 아니다 - 적어 두면 기본값을 고칠 때 이 사람만 옛 값에 묶인다.
        const Bool isDefault = next[0] == entry->defaults[0] && next[1] == entry->defaults[1];
        Override* existing = FindOverride(id);
        if (isDefault)
        {
            if (existing != nullptr)
            {
                m_overrides.RemoveAt(static_cast<std::size_t>(existing - m_overrides.Data()));
            }
        }
        else if (existing != nullptr)
        {
            existing->bindings[0] = next[0];
            existing->bindings[1] = next[1];
        }
        else
        {
            Override added;
            added.id = id;
            added.bindings[0] = next[0];
            added.bindings[1] = next[1];
            m_overrides.Add(std::move(added));
        }
        Apply(*entry);
        ++m_revision;
        return true;
    }

    Bool EditorShortcutManager::ResetBinding(const char* id)
    {
        Entry* entry = FindEntry(id);
        if (entry == nullptr)
        {
            return false;
        }
        return SetBinding(id, 0, entry->defaults[0]) && SetBinding(id, 1, entry->defaults[1]);
    }

    void EditorShortcutManager::ResetAll()
    {
        if (m_overrides.IsEmpty())
        {
            return;
        }
        m_overrides.Clear();
        for (OwnerPtr<Entry>& entry : m_entries)
        {
            Apply(*entry);
        }
        ++m_revision;
    }

    UInt64 EditorShortcutManager::GetRevision() const
    {
        return m_revision;
    }

    void EditorShortcutManager::FindConflicts(Array<ShortcutConflict>& out) const
    {
        out.Clear();
        for (UInt32 first = 0; first < m_entries.Size(); ++first)
        {
            const Entry& a = *m_entries[first];
            for (UInt32 second = first + 1; second < m_entries.Size(); ++second)
            {
                const Entry& b = *m_entries[second];
                const Bool same = SameScope(a.scope, b.scope);
                // 서로 다른 두 패널은 동시에 포커스를 가질 수 없어 겹쳐도 부딪히지 않는다.
                const Bool aShadowsB = false == a.scope.IsEmpty() && b.scope.IsEmpty() && a.blocksGlobal;
                const Bool bShadowsA = false == b.scope.IsEmpty() && a.scope.IsEmpty() && b.blocksGlobal;
                if (false == same && false == aShadowsB && false == bShadowsA)
                {
                    continue;
                }
                Bool found = false;
                for (UInt32 slotA = 0; slotA < 2 && false == found; ++slotA)
                {
                    for (UInt32 slotB = 0; slotB < 2 && false == found; ++slotB)
                    {
                        if (false == a.bindings[slotA].IsSet() || a.bindings[slotA] != b.bindings[slotB])
                        {
                            continue;
                        }
                        ShortcutConflict conflict;
                        conflict.binding = a.bindings[slotA];
                        if (same)
                        {
                            conflict.kind = ShortcutConflictKind::Clash;
                            conflict.first = first;
                            conflict.second = second;
                        }
                        else
                        {
                            conflict.kind = ShortcutConflictKind::Shadows;
                            conflict.first = aShadowsB ? first : second;
                            conflict.second = aShadowsB ? second : first;
                        }
                        out.Add(conflict);
                        found = true;
                    }
                }
            }
        }
    }

    void EditorShortcutManager::Write(YamlWriter& writer) const
    {
        writer.BeginMap("Shortcuts");
        for (const Override& entry : m_overrides)
        {
            writer.BeginMap(entry.id.c_str());
            for (UInt32 slot = 0; slot < 2; ++slot)
            {
                writer.WriteString(SlotKeys[slot], Describe(entry.bindings[slot]).value);
            }
            writer.EndMap();
        }
        writer.EndMap();
    }

    const char* EditorShortcutManager::RenamedId(const char* id)
    {
        // **행동 표가 서며 바뀐 이름이다**(D-284). 가이드와 메뉴가 쓰던 이름으로 하나가 됐다. 사용자 파일에 옛 이름이 남아 있으면
        // 읽을 때 새 이름으로 옮긴다 - 옮기지 않으면 그 사람이 바꿔 둔 키가 조용히 기본값으로 돌아간다.
        struct Rename
        {
            const char* from;
            const char* to;
        };
        static constexpr Rename Renames[] = {
            {"editor.save_canvas", "canvas.save"},
            {"editor.undo", "edit.undo"},
            {"editor.redo", "edit.redo"},
            {"editor.copy", "object.copy"},
            {"editor.paste", "object.paste"},
            {"editor.paste_as_child", "object.paste_as_child"},
            {"editor.delete_selection", "object.delete"},
            {"editor.toggle_play", "simulation.play"},
            {"editor.toggle_pause", "simulation.pause"},
            {"editor.step_frame", "simulation.step"},
        };
        if (IsBlank(id))
        {
            return id;
        }
        for (const Rename& rename : Renames)
        {
            if (std::strcmp(rename.from, id) == 0)
            {
                return rename.to;
            }
        }
        return id;
    }

    void EditorShortcutManager::Read(const YamlDocument& document, UInt32 root)
    {
        m_overrides.Clear();
        const UInt32 map = document.Find(root, "Shortcuts");
        // 지금 이름으로 적힌 줄을 먼저 읽는다. 옛 이름과 새 이름이 둘 다 있으면 새 이름이 이긴다.
        for (Int32 pass = 0; pass < 2 && map != YamlDocument::InvalidNode && document.GetKind(map) == YamlKind::Map; ++pass)
        {
            for (std::size_t index = 0; index < document.GetCount(map); ++index)
            {
                const char* written = document.GetKey(map, index);
                const char* id = RenamedId(written);
                const Bool renamed = id != written;
                const UInt32 value = document.GetValue(map, index);
                if (renamed != (pass == 1) || IsBlank(id) || value == YamlDocument::InvalidNode
                    || document.GetKind(value) != YamlKind::Map)
                {
                    continue;
                }
                Override read;
                read.id = id;
                Bool ok = true;
                for (UInt32 slot = 0; slot < 2 && ok; ++slot)
                {
                    String text;
                    // 없는 자리는 빈 조합이다 - 사용자가 지운 자리를 적을 때 빈 글자로 적는다.
                    if (document.FindScalar(value, SlotKeys[slot], text))
                    {
                        ok = Parse(text.c_str(), read.bindings[slot]);
                    }
                }
                // 모르는 키 이름이 섞인 줄은 통째로 버린다. 반만 읽으면 사용자가 적지 않은 조합이 선다.
                if (ok && FindOverride(id) == nullptr)
                {
                    m_overrides.Add(std::move(read));
                }
            }
        }
        for (OwnerPtr<Entry>& entry : m_entries)
        {
            Apply(*entry);
        }
    }

    void EditorShortcutManager::SetFocusedScope(const char* scope)
    {
        m_focusedScope = IsBlank(scope) ? nullptr : scope;
    }

    void EditorShortcutManager::SetSuspended(Bool suspended)
    {
        m_suspended = suspended;
    }

    Bool EditorShortcutManager::IsSuspended() const
    {
        return m_suspended;
    }

    Bool EditorShortcutManager::MatchesSearch(
        const char* query, const char* label, const char* category, const EditorShortcutView& view)
    {
        if (IsBlank(query))
        {
            return true;
        }
        const auto contains = [query](const char* text) {
            if (IsBlank(text))
            {
                return false;
            }
            const std::size_t needle = std::strlen(query);
            for (const char* at = text; *at != '\0'; ++at)
            {
                std::size_t matched = 0;
                while (matched < needle && at[matched] != '\0')
                {
                    char a = at[matched];
                    char b = query[matched];
                    // 영문만 대소문자를 접는다. 한글 같은 UTF-8 바이트는 그대로 견준다.
                    if (a >= 'A' && a <= 'Z')
                    {
                        a = static_cast<char>(a - 'A' + 'a');
                    }
                    if (b >= 'A' && b <= 'Z')
                    {
                        b = static_cast<char>(b - 'A' + 'a');
                    }
                    if (a != b)
                    {
                        break;
                    }
                    ++matched;
                }
                if (matched == needle)
                {
                    return true;
                }
            }
            return false;
        };
        return contains(label) || contains(category) || contains(view.id) || contains(Describe(view.primary).value)
            || contains(Describe(view.secondary).value);
    }

    UInt32 EditorShortcutManager::ProcessInput(EditorApplication& editor, Bool typing, Bool gameInput)
    {
        if (m_suspended)
        {
            return 0;
        }
        UInt32 executed = 0;
        // 포커스 범위의 것이 실행되며 막은 조합. 그 프레임의 전역 것은 이 조합을 건너뛴다.
        EditorShortcutBinding blocked;
        if (m_focusedScope != nullptr)
        {
            Bool done = false;
            for (std::size_t index = 0; index < m_entries.Size() && false == done; ++index)
            {
                Entry& entry = *m_entries[index];
                if (entry.scope.IsEmpty() || false == Allowed(entry, typing, gameInput))
                {
                    continue;
                }
                for (UInt32 slot = 0; slot < 2 && false == done; ++slot)
                {
                    // 글자를 견주는 것은 눌린 조합이 있을 때뿐이다 - 매 프레임 견주지 않는다.
                    if (false == Pressed(entry.bindings[slot]) || std::strcmp(entry.scope.c_str(), m_focusedScope) != 0
                        || false == entry.handler->CanExecute(editor))
                    {
                        continue;
                    }
                    entry.handler->Execute(editor);
                    ++executed;
                    done = true;
                    if (entry.blocksGlobal)
                    {
                        blocked = entry.bindings[slot];
                    }
                }
            }
        }
        for (std::size_t index = 0; index < m_entries.Size(); ++index)
        {
            Entry& entry = *m_entries[index];
            if (false == entry.scope.IsEmpty() || false == Allowed(entry, typing, gameInput))
            {
                continue;
            }
            for (UInt32 slot = 0; slot < 2; ++slot)
            {
                if (false == Pressed(entry.bindings[slot]) || (blocked.IsSet() && entry.bindings[slot] == blocked)
                    || false == entry.handler->CanExecute(editor))
                {
                    continue;
                }
                entry.handler->Execute(editor);
                // 한 프레임에 하나다. 같은 키에 둘이 걸려 있으면 앞에 등록된 것이 이긴다.
                return executed + 1;
            }
        }
        return executed;
    }

    EditorShortcutText EditorShortcutManager::Describe(const EditorShortcutBinding& binding)
    {
        EditorShortcutText text;
        if (false == binding.IsSet())
        {
            return text;
        }
        if (binding.control)
        {
            Append(text.value, sizeof(text.value), "Ctrl+");
        }
        if (binding.shift)
        {
            Append(text.value, sizeof(text.value), "Shift+");
        }
        if (binding.alt)
        {
            Append(text.value, sizeof(text.value), "Alt+");
        }
        // ImGui 가 키 이름을 안다. 우리가 표를 또 만들면 둘이 갈린다.
        Append(text.value, sizeof(text.value), ImGui::GetKeyName(binding.key));
        return text;
    }

    Bool EditorShortcutManager::Parse(const char* text, EditorShortcutBinding& out)
    {
        out = EditorShortcutBinding{};
        if (IsBlank(text))
        {
            return true;
        }
        EditorShortcutBinding parsed;
        const char* cursor = text;
        while (true)
        {
            const char* plus = std::strchr(cursor, '+');
            // 마지막 조각이 키다. 앞 조각들은 조합키다.
            if (plus == nullptr || plus[1] == '\0')
            {
                break;
            }
            const std::size_t length = static_cast<std::size_t>(plus - cursor);
            if (length == 4 && std::strncmp(cursor, "Ctrl", 4) == 0)
            {
                parsed.control = true;
            }
            else if (length == 5 && std::strncmp(cursor, "Shift", 5) == 0)
            {
                parsed.shift = true;
            }
            else if (length == 3 && std::strncmp(cursor, "Alt", 3) == 0)
            {
                parsed.alt = true;
            }
            else
            {
                return false;
            }
            cursor = plus + 1;
        }
        for (Int32 key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key)
        {
            const ImGuiKey candidate = static_cast<ImGuiKey>(key.Get());
            if (IsBindableKey(candidate) && std::strcmp(ImGui::GetKeyName(candidate), cursor) == 0)
            {
                parsed.key = candidate;
                out = parsed;
                return true;
            }
        }
        return false;
    }

    Bool EditorShortcutManager::CaptureBinding(EditorShortcutBinding& out)
    {
        const ImGuiIO& io = ImGui::GetIO();
        for (Int32 key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key)
        {
            const ImGuiKey candidate = static_cast<ImGuiKey>(key.Get());
            if (false == IsBindableKey(candidate) || false == ImGui::IsKeyPressed(candidate, false))
            {
                continue;
            }
            out.key = candidate;
            out.control = io.KeyCtrl;
            out.shift = io.KeyShift;
            out.alt = io.KeyAlt;
            return true;
        }
        return false;
    }

    EditorShortcutManager::Entry* EditorShortcutManager::FindEntry(const char* id)
    {
        if (IsBlank(id))
        {
            return nullptr;
        }
        for (OwnerPtr<Entry>& entry : m_entries)
        {
            if (entry->id == id)
            {
                return entry.Get();
            }
        }
        return nullptr;
    }

    const EditorShortcutManager::Entry* EditorShortcutManager::FindEntry(const char* id) const
    {
        return const_cast<EditorShortcutManager*>(this)->FindEntry(id);
    }

    EditorShortcutManager::Override* EditorShortcutManager::FindOverride(const char* id)
    {
        for (Override& entry : m_overrides)
        {
            if (entry.id == id)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    void EditorShortcutManager::Apply(Entry& entry) const
    {
        entry.bindings[0] = entry.defaults[0];
        entry.bindings[1] = entry.defaults[1];
        for (const Override& found : m_overrides)
        {
            if (found.id == entry.id)
            {
                entry.bindings[0] = found.bindings[0];
                entry.bindings[1] = found.bindings[1];
                return;
            }
        }
    }

    EditorShortcutView EditorShortcutManager::ToView(const Entry& entry) const
    {
        EditorShortcutView view;
        view.handle = entry.handle;
        view.id = entry.id.c_str();
        view.labelKey = entry.labelKey.c_str();
        view.categoryKey = entry.categoryKey.c_str();
        view.scope = ScopeOrNull(entry.scope);
        view.primary = entry.bindings[0];
        view.secondary = entry.bindings[1];
        view.defaultPrimary = entry.defaults[0];
        view.defaultSecondary = entry.defaults[1];
        view.blocksGlobal = entry.blocksGlobal;
        view.customized = entry.bindings[0] != entry.defaults[0] || entry.bindings[1] != entry.defaults[1];
        return view;
    }

    Bool EditorShortcutManager::Allowed(const Entry& entry, Bool typing, Bool gameInput) const
    {
        if (typing && false == entry.whileTyping)
        {
            return false;
        }
        return false == gameInput || entry.duringGame;
    }
}

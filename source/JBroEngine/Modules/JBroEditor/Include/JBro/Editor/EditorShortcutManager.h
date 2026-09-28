#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/String.h>

#include <imgui.h>

#include <cstdint>

namespace JBro
{
    class EditorApplication;
    class YamlDocument;
    class YamlWriter;

    // 키 하나와 그에 붙는 조합키다. `key` 가 `ImGuiKey_None` 이면 비어 있는 자리다.
    // 키는 키보드 키이거나 마우스 엄지 버튼(`ImGuiKey_MouseX1`·`ImGuiKey_MouseX2`)이다(D-258). 다른 마우스 버튼과 게임패드는 받지 않는다.
    // 조합키는 **정확히** 견준다 - Ctrl+Shift+V 가 Ctrl+V 를 부르지 않는다(기존 엔진과 같다).
    struct EditorShortcutBinding
    {
        ImGuiKey key = ImGuiKey_None;
        bool control = false;
        bool shift = false;
        bool alt = false;

        bool IsSet() const
        {
            return key != ImGuiKey_None;
        }
        bool operator==(const EditorShortcutBinding& other) const
        {
            return key == other.key && control == other.control && shift == other.shift && alt == other.alt;
        }
        bool operator!=(const EditorShortcutBinding& other) const
        {
            return false == (*this == other);
        }
    };

    // 한 줄에 적을 수 있는 가장 긴 글자(`Ctrl+Shift+Alt+Backspace`)보다 넉넉하다.
    struct EditorShortcutText
    {
        char value[48] = {};
    };

    // 단축키를 누르면 할 일이다. 팝업·알림과 같이 `std::function` 이 아니라 가상 함수다.
    class IEditorShortcutHandler
    {
    public:
        IEditorShortcutHandler() = default;
        virtual ~IEditorShortcutHandler() = default;
        IEditorShortcutHandler(const IEditorShortcutHandler&) = delete;
        IEditorShortcutHandler& operator=(const IEditorShortcutHandler&) = delete;

        // 지금 할 수 있는가. 거짓이면 눌러도 하지 않고, 같은 키의 전역 단축키를 막지도 않는다.
        virtual bool CanExecute(const EditorApplication& editor) const
        {
            (void)editor;
            return true;
        }
        // 왜 지금 못 하는가(D-181). 할 수 있으면 nullptr.
        virtual const char* WhyBlocked(const EditorApplication& editor) const
        {
            (void)editor;
            return nullptr;
        }
        virtual bool Execute(EditorApplication& editor) = 0;
    };

    using ShortcutHandle = std::uint64_t;
    inline constexpr ShortcutHandle InvalidShortcutHandle = 0;

    struct EditorShortcutDesc
    {
        // **저장되는 이름**이다(`editor.save_canvas`, `canvas_view.gizmo_translate`). 사용자가 바꾼 키가 이 이름으로
        // 설정 파일에 적히므로 바꾸지 않는다. 번역하지 않는다. 비울 수 없고 겹칠 수 없다.
        const char* id = nullptr;
        // 보이는 이름과 그것이 속한 무리(도움말·설정 화면의 제목). 둘 다 로컬라이징 키다(§11.2).
        const char* labelKey = nullptr;
        const char* categoryKey = nullptr;
        // **범위.** nullptr 이면 전역 - 어디서든 돈다. 패널의 `GetTitle()` 이면 그 패널(도킹된 창·그 안의 팝업 포함)에
        // 포커스가 있을 때만 돈다.
        const char* scope = nullptr;
        // 기본 조합. 사용자가 바꾸지 않았으면 이것이다.
        EditorShortcutBinding primary;
        EditorShortcutBinding secondary;
        // 패널 범위일 때만 뜻이 있다: 이것이 실행되면 **같은 조합의 전역 단축키는 그 프레임에 돌지 않는다**.
        // 거짓이면 둘 다 돈다.
        bool blocksGlobal = true;
        // 글자 칸에 타자를 치는 중에도 도는가. 기본은 거짓이다 - 이름을 고치다 Ctrl+Z 를 누르면 글자를 되돌려야 한다.
        bool whileTyping = false;
        // 재생 중 게임이 키를 받는 동안에도 도는가(D-214). 재생 제어만 참이다.
        bool duringGame = false;
        OwnerPtr<IEditorShortcutHandler> handler;
    };

    // 등록된 단축키 하나를 읽는 값이다. 글자는 관리자 안을 가리킨다 - 등록을 풀기 전까지만 쓴다.
    struct EditorShortcutView
    {
        ShortcutHandle handle = InvalidShortcutHandle;
        const char* id = nullptr;
        const char* labelKey = nullptr;
        const char* categoryKey = nullptr;
        // 전역이면 nullptr.
        const char* scope = nullptr;
        EditorShortcutBinding primary;
        EditorShortcutBinding secondary;
        EditorShortcutBinding defaultPrimary;
        EditorShortcutBinding defaultSecondary;
        bool blocksGlobal = true;
        // 사용자가 기본값에서 바꿨는가.
        bool customized = false;
    };

    enum class ShortcutConflictKind : std::uint8_t
    {
        // 같은 범위(둘 다 전역이거나 같은 패널)에 같은 조합이 있다. 앞에 등록된 것만 돈다 - 고쳐야 할 겹침이다.
        Clash,
        // 패널 범위가 같은 조합의 전역 단축키를 그 패널에 있을 때 가린다(`blocksGlobal`). 뜻한 것일 수 있다.
        Shadows,
    };

    struct ShortcutConflict
    {
        // `GetAt` 의 번호. `Shadows` 면 `first` 가 패널 범위, `second` 가 전역이다.
        std::uint32_t first = 0;
        std::uint32_t second = 0;
        ShortcutConflictKind kind = ShortcutConflictKind::Clash;
        EditorShortcutBinding binding;
    };

    // 에디터의 단축키 관리자다(todo "에디터 공용 기반" 2 번, D-228). 기존 엔진 `CEditorShortcutManager` 자리지만 그쪽은
    // 고정 열거 아홉 개였다. 여기는 **에디터(패널·도구·외부 에디터)가 제 단축키를 이름으로 등록**하고, 관리자가 이름 → 조합
    // 매핑을 든다. 사용자가 바꾼 조합은 이름으로 설정 파일에 남는다.
    //
    // **누르는 자리·보이는 자리·바꾸는 자리가 이 표 하나다.** 메뉴의 조합키 글자·도움말 목록·키매핑 설정이 모두 여기서 읽는다.
    //
    // 게임 입력(`InputService`)과는 별개다. **메인 스레드 전용이다.**
    class EditorShortcutManager
    {
    public:
        EditorShortcutManager() = default;
        EditorShortcutManager(const EditorShortcutManager&) = delete;
        EditorShortcutManager& operator=(const EditorShortcutManager&) = delete;

        // 이름이 비었거나 이미 있거나 할 일이 없으면 받지 않는다. 사용자가 이 이름의 조합을 바꿔 두었으면 그것이 선다.
        ShortcutHandle Register(EditorShortcutDesc desc);
        // 등록을 푼다. 사용자가 바꾼 조합은 **남긴다** - 외부 에디터가 빠졌다 다시 붙어도 그 사람의 키가 그대로다.
        void Unregister(ShortcutHandle handle);

        std::uint32_t GetCount() const;
        EditorShortcutView GetAt(std::uint32_t index) const;
        // 이름으로 찾는다. 없으면 `handle` 이 `InvalidShortcutHandle` 인 값이다.
        EditorShortcutView Find(const char* id) const;
        bool CanExecute(const char* id, const EditorApplication& editor) const;
        const char* WhyBlocked(const char* id, const EditorApplication& editor) const;
        bool Execute(const char* id, EditorApplication& editor);

        // ── 사용자가 바꾸는 것 ────────────────────────────────────────
        //
        // `slot` 0 은 첫째, 1 은 둘째 조합이다. 비어 있는 조합을 주면 그 자리를 비운다. 모르는 이름이면 거짓이다.
        bool SetBinding(const char* id, std::uint32_t slot, const EditorShortcutBinding& binding);
        bool ResetBinding(const char* id);
        void ResetAll();
        // 사용자의 조합이 바뀔 때마다 오른다. 에디터는 이 값이 바뀌면 설정 파일을 다시 쓴다.
        std::uint64_t GetRevision() const;
        // 겹치는 조합을 모두 적는다(설정 화면이 보이는 목록). 같은 짝은 한 번만.
        void FindConflicts(Array<ShortcutConflict>& out) const;

        // 사용자가 바꾼 조합만 쓴다(`Shortcuts:` 아래). 등록이 풀린 이름의 것도 쓴다.
        void Write(YamlWriter& writer) const;
        // `Shortcuts:` 맵을 읽어 사용자 조합을 통째로 바꾼다. 읽지 못한 줄은 건너뛴다.
        void Read(const YamlDocument& document, std::uint32_t root);

        // ── 매 프레임 ───────────────────────────────────────────────
        //
        // 이번 프레임에 포커스를 가진 패널의 `GetTitle()`. 없으면 nullptr.
        void SetFocusedScope(const char* scope);
        // **멈춤.** 키매핑 설정이 새 키를 잡는 동안 참이다 - 그 사이 누른 Ctrl+S 가 저장하면 안 되고 새 조합이 되어야 한다.
        void SetSuspended(bool suspended);
        bool IsSuspended() const;
        // 눌린 것을 찾아 한다. 포커스 범위의 것이 먼저 돌고, 그것이 실행되면(`blocksGlobal`) 같은 조합의 전역 것은 돌지 않는다.
        // 범위마다 한 프레임에 하나다. 실행한 것의 수를 돌려준다.
        std::uint32_t ProcessInput(EditorApplication& editor, bool typing, bool gameInput);

        // ── 키 ────────────────────────────────────────────────────
        //
        // `Ctrl+Shift+Z` 같은 글자. 키 이름은 ImGui 가 준다(Space 를 누르면 `Space`, 엄지 버튼은 `MouseX1`). 비어 있으면 빈 글자다.
        static EditorShortcutText Describe(const EditorShortcutBinding& binding);
        // `Describe` 의 거꾸로. 빈 글자는 빈 조합이고 참이다. 모르는 키 이름이면 거짓이다.
        static bool Parse(const char* text, EditorShortcutBinding& out);
        // **검색.** `query` 가 번역된 이름·무리 이름·저장 이름·지금 조합 글자(`Ctrl+Z`) 어디에든 들어 있으면 참이다. 영문은 대소문자를
        // 가리지 않는다. 빈 검색어는 모두 맞는다. 설정 화면이 줄마다 부른다.
        static bool MatchesSearch(const char* query, const char* label, const char* category, const EditorShortcutView& view);
        // **이번 프레임에 눌린 키를 조합으로 잡는다** - 키매핑 설정에서 "키를 누르세요" 칸이 쓴다. 조합키만 눌렸으면 거짓이다.
        // 엄지 버튼도 잡는다. 칸을 누른 왼쪽 버튼은 잡지 않는다.
        static bool CaptureBinding(EditorShortcutBinding& out);

    private:
        struct Entry
        {
            ShortcutHandle handle = InvalidShortcutHandle;
            String id;
            String labelKey;
            String categoryKey;
            String scope;
            EditorShortcutBinding defaults[2];
            EditorShortcutBinding bindings[2];
            bool blocksGlobal = true;
            bool whileTyping = false;
            bool duringGame = false;
            OwnerPtr<IEditorShortcutHandler> handler;
        };
        // 사용자가 바꾼 조합. 등록되지 않은 이름의 것도 남긴다.
        struct Override
        {
            String id;
            EditorShortcutBinding bindings[2];
        };

        Entry* FindEntry(const char* id);
        const Entry* FindEntry(const char* id) const;
        Override* FindOverride(const char* id);
        void Apply(Entry& entry) const;
        EditorShortcutView ToView(const Entry& entry) const;
        bool Allowed(const Entry& entry, bool typing, bool gameInput) const;

        Array<OwnerPtr<Entry>> m_entries;
        Array<Override> m_overrides;
        // 패널이 든 글자를 가리킨다. 매 프레임 `ProcessInput` 바로 앞에 다시 받으므로 그 사이에만 쓴다(프레임마다 글자를 복사하지 않는다).
        const char* m_focusedScope = nullptr;
        ShortcutHandle m_nextHandle = 1;
        std::uint64_t m_revision = 0;
        bool m_suspended = false;
    };
}

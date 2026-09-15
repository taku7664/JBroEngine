#include <JBro/Editor/EditorApplication.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/Command/ObjectCommands.h>
#include <JBro/Editor/EditorObjectRegistry.h>
#include <JBro/Editor/EditorPanel.h>
#include <JBro/Editor/EditorPopup.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Reflection/ContainerTypeDescriptors.h>
#include <JBro/Reflection/EnumDescriptor.h>
#include <JBro/Framework2D/Math2DReflection.h>
#include <JBro/Reflection/Field.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Types/Array.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Runtime/GameObject.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <stdexcept>
#include <utility>

// 필드의 종류가 섞인 목록 원소다. 한 줄 숫자 묶음으로 읽히지 않으므로 접기 마디 안에 필드마다
// 한 줄씩 그린다(D-89). 저장하지 않는 필드가 하나 있다 - 목록은 전체의 글자로 되돌리므로 그
// 필드는 원소 안에서 고칠 수 없어야 한다.
namespace
{
    struct Signal
    {
        float strength = 0.0f;
        bool on = false;
        float echo = 0.0f;
        // 원소 안의 배열이다. 이번에는 개수만 보여 주고 목록으로 그리지 않는다(D-89).
        JBro::Array<float> taps;
    };

    // 구조체 필드 안에 든 구조체 원소 목록이다. 인스펙터는 이 필드를 트리 마디로 타고 내려가므로
    // 마디가 열린 자리에서 표를 끊으면 표가 제 Id 대신 마디의 Id 를 뺀다 - 그 자리에서는 끊지 않는다.
    struct Relay
    {
        JBro::Array<Signal> relayed;
    };

    // enum 원소 목록용이다. 값이 연속이 아니어서 콤보의 칸 번호를 값으로 쓰면 틀린다.
    enum class Tone : std::uint8_t
    {
        Low  = 2,
        Mid  = 5,
        High = 9
    };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Tone, "Test::Tone",
        { Tone::Low,  "Low" },
        { Tone::Mid,  "Mid" },
        { Tone::High, "High" });

    template <>
    struct TypeDescriptorOf<Signal>
    {
        static const TypeDescriptor& Get()
        {
            static const FieldEntry entries[] =
            {
                MakeFieldEntry<&Signal::strength>(),
                MakeFieldEntry<&Signal::on>(),
                MakeFieldEntry<&Signal::echo>(Attribute::NoSerialize()),
                MakeFieldEntry<&Signal::taps>(),
            };
            static const StaticPropertyTable<4> fields { entries };
            static const TypeDescriptor descriptor =
                MakeStructTypeDescriptor<Signal>("Test::Signal", fields.Get());
            return descriptor;
        }
    };

    template <>
    struct TypeDescriptorOf<Relay>
    {
        static const TypeDescriptor& Get()
        {
            static const FieldEntry entries[] =
            {
                MakeFieldEntry<&Relay::relayed>(),
            };
            static const StaticPropertyTable<1> fields { entries };
            static const TypeDescriptor descriptor =
                MakeStructTypeDescriptor<Relay>("Test::Relay", fields.Get());
            return descriptor;
        }
    };
}

namespace
{
    // 에디터 창 크기다. **패널 넷이 들어갈 만큼은 되어야 한다** - 너무 좁으면
    // 가운데가 거의 남지 않아, 게임 화면이 제대로 와도 화면의 몇 퍼센트가 안 된다.
    constexpr std::uint32_t WindowWidth = 640;
    constexpr std::uint32_t WindowHeight = 480;

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << std::endl;
            throw std::runtime_error(message);
        }
    }

    // **자기 프로세스의 에디터 창만 찾는다.** `FindWindowW` 는 이름만 보므로, 같은 기계에서
    // 다른 테스트 프로세스가 같은 창을 띄우고 있으면 그쪽 창에 마우스를 보내게 된다 - 세션
    // 둘이 나란히 테스트를 돌리던 날 마우스 테스트가 무작위로 실패한 원인이었다.
    HWND FindOwnEditorWindow()
    {
        struct Search
        {
            HWND found = nullptr;
            DWORD process = GetCurrentProcessId();
        } search;
        EnumWindows(
            [](HWND hwnd, LPARAM param) -> BOOL {
                Search& search = *reinterpret_cast<Search*>(param);
                DWORD owner = 0;
                GetWindowThreadProcessId(hwnd, &owner);
                if (owner != search.process)
                {
                    return TRUE;
                }
                wchar_t className[64] = {};
                wchar_t title[64] = {};
                GetClassNameW(hwnd, className, 64);
                GetWindowTextW(hwnd, title, 64);
                if (std::wcscmp(className, L"JBroEngineWindow") == 0
                    && std::wcscmp(title, L"JBro Editor") == 0)
                {
                    search.found = hwnd;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));
        return search.found;
    }

    // 에디터 오버레이가 백버퍼를 지우는 색이다(0.09, 0.09, 0.11). 패널이 덮은
    // 자리는 이 색이 아니다. **밝기로 재면 안 된다** - ImGui 의 창 배경은
    // 오버레이가 지운 색보다 오히려 어둡다.
    constexpr int ClearRed = 23;
    constexpr int ClearGreen = 23;
    constexpr int ClearBlue = 28;

    bool DiffersFromClear(const unsigned char* pixel)
    {
        const auto apart = [](unsigned char got, int want) {
            const int gap = static_cast<int>(got) - want;
            return gap > 4 || gap < -4;
        };
        return apart(pixel[0], ClearBlue)
            || apart(pixel[1], ClearGreen)
            || apart(pixel[2], ClearRed);
    }

    // 창을 되읽어 지움색이 아닌 픽셀을 센다.
    // **화면을 파일로 남긴다**(ProjectRule §11.4).
    //
    // UI 를 고치면 눈으로 봐야 한다. 그런데 사람이 창을 띄워 보는 것은 반복되지
    // 않고, 본 것을 다음 사람에게 넘길 수도 없다 - 그래서 테스트가 찍는다.
    // 환경변수 `JBRO_EDITOR_SHOT` 에 경로를 주면 거기에 쓴다. 평소에는 아무것도
    // 쓰지 않는다: 확인은 검사가 하고, 그림은 볼 사람이 있을 때만 필요하다.
    //
    // BMP 인 이유는 **의존성이 없어서다.** 헤더 54바이트에 아래에서 위로 쌓은
    // 픽셀이 전부라, 이미지 라이브러리를 들이지 않고 쓸 수 있다.
    void SaveScreenshot(JBro::Renderer& renderer, std::uint32_t width,
        std::uint32_t height, const char* suffix)
    {
        // `getenv` 는 MSVC 가 안전하지 않다고 막는다. 우리가 놓아 주는 쪽을 쓴다.
        char* directory = nullptr;
        std::size_t directoryLength = 0;
        if (_dupenv_s(&directory, &directoryLength, "JBRO_EDITOR_SHOT") != 0
            || directory == nullptr || *directory == '\0')
        {
            std::free(directory);
            return;
        }
        struct DirectoryGuard
        {
            char* value;
            ~DirectoryGuard() { std::free(value); }
        } guard{directory};

        JBro::Array<std::byte> image;
        image.Resize(static_cast<std::size_t>(width) * height * 4);
        JBro::TextureReadback readback;
        if (false == renderer.ReadBackBuffer(image.Data(), image.Size(), readback))
        {
            return;
        }

        std::string path(directory);
        path += "/editor_";
        path += suffix;
        path += ".bmp";
        std::ofstream file(path, std::ios::binary);
        if (false == file.is_open())
        {
            return;
        }

        const std::uint32_t rowBytes = width * 3;
        const std::uint32_t padding = (4 - (rowBytes % 4)) % 4;
        const std::uint32_t pixelBytes = (rowBytes + padding) * height;
        const std::uint32_t fileBytes = 54 + pixelBytes;

        auto put32 = [&](std::uint32_t value) {
            const char bytes[4] = {
                static_cast<char>(value & 0xFF),
                static_cast<char>((value >> 8) & 0xFF),
                static_cast<char>((value >> 16) & 0xFF),
                static_cast<char>((value >> 24) & 0xFF)};
            file.write(bytes, 4);
        };
        auto put16 = [&](std::uint16_t value) {
            const char bytes[2] = {
                static_cast<char>(value & 0xFF),
                static_cast<char>((value >> 8) & 0xFF)};
            file.write(bytes, 2);
        };

        file.write("BM", 2);
        put32(fileBytes);
        put32(0);
        put32(54);
        put32(40);
        put32(width);
        put32(height);
        put16(1);
        put16(24);
        put32(0);
        put32(pixelBytes);
        put32(2835);
        put32(2835);
        put32(0);
        put32(0);

        const char zero[4] = {};
        // BMP 는 아래에서 위로 쌓는다. 읽어 온 것은 위에서 아래이므로 거꾸로 돈다.
        for (std::uint32_t y = height; y > 0; --y)
        {
            const std::byte* row = image.Data()
                + static_cast<std::size_t>(y - 1) * readback.rowPitch;
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const auto* pixel = reinterpret_cast<const unsigned char*>(row + x * 4);
                // **되읽은 것은 BGRA 다**(백버퍼가 `BGRA8Unorm` 이고, 위쪽
                // `DiffersFromClear` 도 `pixel[0]` 을 파랑으로 읽는다). BMP 도
                // BGR 이므로 순서를 그대로 쓴다 - 뒤집었다가 테마가 갈색으로
                // 나왔다.
                const char bgr[3] = {
                    static_cast<char>(pixel[0]),
                    static_cast<char>(pixel[1]),
                    static_cast<char>(pixel[2])};
                file.write(bgr, 3);
            }
            if (padding != 0)
            {
                file.write(zero, padding);
            }
        }
        std::cout << "  wrote " << path << std::endl;
    }

    std::size_t CountPaintedPixels(JBro::Renderer& renderer, std::uint32_t width,
        std::uint32_t height)
    {
        JBro::Array<std::byte> image;
        image.Resize(static_cast<std::size_t>(width) * height * 4);
        JBro::TextureReadback readback;
        Check(renderer.ReadBackBuffer(image.Data(), image.Size(), readback),
            "the editor window must read back");
        std::size_t painted = 0;
        for (std::uint32_t y = 0; y < height; ++y)
        {
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const std::size_t offset = static_cast<std::size_t>(y) * readback.rowPitch
                    + static_cast<std::size_t>(x) * 4;
                if (DiffersFromClear(
                        reinterpret_cast<const unsigned char*>(image.Data() + offset)))
                {
                    ++painted;
                }
            }
        }
        return painted;
    }

    const JBro::PropertyInfo* FindProperty(
        const JBro::PropertyTable& table, const char* name)
    {
        for (std::uint32_t index = 0; index < table.count; ++index)
        {
            const char* found =
                JBro::NameTable::Get().Resolve(table.properties[index].name);
            if (found != nullptr && std::strcmp(found, name) == 0)
            {
                return &table.properties[index];
            }
        }
        return nullptr;
    }

    // 프레임워크가 패널을 어떻게 다루는지 **세기만 하는** 패널이다. 그리지 않는다 -
    // 무엇이 그려졌는지가 아니라 어떤 훅이 언제 불렸는지를 보는 자리다.
    class CountingPanel final : public JBro::EditorPanel
    {
    public:
        explicit CountingPanel(const char* title, bool createSucceeds = true)
            : m_title(title)
            , m_createSucceeds(createSucceeds)
        {
        }

        const char* GetTitle() const override
        {
            return m_title;
        }
        bool OnCreate(JBro::EditorApplication&) override
        {
            ++createCalls;
            return m_createSucceeds;
        }
        void OnDestroy() override
        {
            ++destroyCalls;
            if (destroyOrder == 0)
            {
                firstDestroyed = m_title;
            }
            ++destroyOrder;
        }
        void OnUpdate(float deltaTime) override
        {
            ++updates;
            lastDelta = deltaTime;
        }
        void OnDraw() override
        {
            ++draws;
        }

        // **거절당한 패널은 AddPanel 안에서 죽는다**(OwnerPtr 를 값으로 받는다).
        // 그래서 부름 횟수는 인스턴스가 아니라 여기 센다 - 죽은 것을 들여다보면
        // 테스트가 저 자신의 버그를 재게 된다.
        static int createCalls;
        static int destroyCalls;
        static int destroyOrder;
        // **먼저 떠난 쪽의 제목**이다. Shutdown 은 패널을 지우므로 그 뒤에
        // 인스턴스를 들여다볼 수 없다 - 제목은 문자열 리터럴이라 살아남는다.
        static const char* firstDestroyed;

        int updates = 0;
        int draws = 0;
        float lastDelta = 0.0f;

    private:
        const char* m_title = nullptr;
        bool m_createSucceeds = true;
    };

    int CountingPanel::createCalls = 0;
    int CountingPanel::destroyCalls = 0;
    int CountingPanel::destroyOrder = 0;
    const char* CountingPanel::firstDestroyed = nullptr;

    // **레지스트리는 들일 수 없는 것을 들이지 않는다.** ImGui 는 창을 제목으로
    // 알아보므로 제목이 겹치면 둘이 한 창을 나눠 쓴다 - 둘째 패널부터 안 보인다.
    // 그리고 `OnCreate` 가 실패한 패널을 목록에 남기면, 준비되지 않은 것이 매
    // 프레임 그려진다(D-70).
    void TestThePanelRegistryRefusesWhatItCannotHold()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the panel registry not verified"
                << std::endl;
            return;
        }
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");

        const std::size_t builtin = editor.GetPanelCount();
        Check(builtin == 4, "the editor brings four panels of its own");
        Check(editor.FindPanel("Inspector") != nullptr, "and they are findable by title");
        Check(editor.FindPanel("Nothing Like This") == nullptr,
            "and a title nobody has finds nothing");

        // 제목이 겹치면 거절한다.
        Check(false == editor.AddPanel(JBro::MakeOwnerPtr<CountingPanel>("Inspector")),
            "a title another panel already uses must be refused");
        Check(editor.GetPanelCount() == builtin, "and must not be added anyway");

        // 빈 제목도, 없는 패널도 거절한다.
        Check(false == editor.AddPanel(JBro::MakeOwnerPtr<CountingPanel>("")),
            "a panel with no title has no window to live in");
        Check(false == editor.AddPanel({}), "and nothing at all is not a panel");
        Check(editor.GetPanelCount() == builtin, "neither may land in the list");

        // **`OnCreate` 가 실패하면 들이지 않는다.**
        const int asked = CountingPanel::createCalls;
        Check(false == editor.AddPanel(
                JBro::MakeOwnerPtr<CountingPanel>("Never Ready", false)),
            "a panel that cannot start must be refused");
        Check(CountingPanel::createCalls == asked + 1, "it was asked");
        Check(editor.GetPanelCount() == builtin, "and the answer was believed");
        Check(editor.FindPanel("Never Ready") == nullptr,
            "so it must not be findable either");

        // 제대로 된 것은 들어간다.
        auto good = JBro::MakeOwnerPtr<CountingPanel>("Counting");
        CountingPanel* raw = good.Get();
        Check(editor.AddPanel(std::move(good)), "a well-formed panel must be taken");
        Check(editor.GetPanelCount() == builtin + 1, "and counted");
        Check(editor.FindPanel("Counting") == raw, "and found by its title");

        Check(editor.AddPanel(JBro::MakeOwnerPtr<CountingPanel>("Counting Later")),
            "a second panel must be taken too");

        const int leaving = CountingPanel::destroyCalls;
        CountingPanel::destroyOrder = 0;
        CountingPanel::firstDestroyed = nullptr;
        editor.Shutdown();
        // 내보낼 때 `OnDestroy` 를 부른다. 부르지 않으면 패널이 잡은 것이 샌다.
        Check(CountingPanel::destroyCalls == leaving + 2,
            "shutting down must tell every panel it is going");
        // **들인 순서의 반대로 내보낸다.** 나중에 붙은 것이 앞의 것에 기대고
        // 있을 수 있어서, 기댄 쪽이 먼저 떠나야 한다.
        Check(CountingPanel::firstDestroyed != nullptr
                && std::strcmp(CountingPanel::firstDestroyed, "Counting Later") == 0,
            "the panel that arrived last must be the first to leave");
    }

    // **닫혀 있어도 갱신은 돈다(D-70).** 안 보인다고 멈출지는 프레임워크가 아니라
    // 패널이 정할 일이다 - 열어 볼 때만 세는 통계 패널은 열어 보는 행위가 측정을
    // 바꾼다.
    void TestAClosedPanelKeepsUpdatingButStopsDrawing()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; panel lifecycle not verified"
                << std::endl;
            return;
        }
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");

        auto owned = JBro::MakeOwnerPtr<CountingPanel>("Counting");
        CountingPanel* panel = owned.Get();
        Check(editor.AddPanel(std::move(owned)), "the counting panel must be taken");
        Check(panel->IsOpen(), "a new panel starts open");

        constexpr float Delta = 1.0f / 60.0f;
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Delta), "the editor must tick");
        }
        Check(panel->updates == 3, "an open panel updates once a frame");
        Check(panel->draws == 3, "and draws once a frame");
        Check(panel->lastDelta > 0.0f, "and is told how long the frame was");

        const int drawsWhenClosed = panel->draws;
        panel->SetOpen(false);
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Delta), "the editor must keep ticking");
        }
        Check(panel->draws == drawsWhenClosed, "a closed panel must not be drawn");
        Check(panel->updates == 6, "but must keep being updated");

        panel->SetOpen(true);
        Check(editor.Tick(Delta), "the editor must tick");
        Check(panel->draws == drawsWhenClosed + 1, "opening it again must draw it");

        editor.Shutdown();
    }

    // **제목줄의 X 가 패널을 닫아야 한다.** ImGui 는 닫힘을 `Begin` 에 넘긴 불리언에
    // 적어 줄 뿐이고, 그것을 패널에 도로 적어 주지 않으면 눌러도 아무 일이 없다.
    void TestClickingTheCloseButtonClosesThePanel()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the close button not verified"
                << std::endl;
            return;
        }
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");

        HWND window = FindOwnEditorWindow();
        Check(window != nullptr, "the editor window must be findable");

        constexpr float Delta = 1.0f / 60.0f;
        // 먼저 기본 배치를 잡게 둔다. 그 뒤에 붙는 패널은 도크에 들어가지 않고
        // 떠 있으므로 제 제목줄과 X 를 갖는다.
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Delta), "the editor must tick");
        }

        auto owned = JBro::MakeOwnerPtr<CountingPanel>("Closable");
        CountingPanel* panel = owned.Get();
        Check(editor.AddPanel(std::move(owned)), "the panel must be taken");
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Delta), "the editor must tick");
        }
        Check(panel->IsOpen(), "it is open before anyone touches it");

        ImGuiWindow* floating = ImGui::FindWindowByName("Closable");
        Check(floating != nullptr, "ImGui must have made a window for it");
        Check(false == floating->Collapsed, "and it must not be collapsed");

        // 닫기 단추는 제목줄 오른쪽 끝이다. `ImGui::Begin` 이 그 자리를
        // 이렇게 잡는다 - 여기서 빗나가면 ImGui 가 단추를 옮긴 것이고,
        // 그때는 조용히 지나가는 것보다 이 테스트가 우는 편이 낫다.
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImRect titleBar = floating->TitleBarRect();
        const float buttonSize = ImGui::GetFontSize();
        const int x = static_cast<int>(
            titleBar.Max.x - style.FramePadding.x - buttonSize * 0.5f);
        const int y = static_cast<int>(titleBar.GetCenter().y);

        // 가리키고, 누르고, 뗀다. ImGui 는 지난 프레임에 무엇 위에 있었는지로
        // 이번 프레임의 눌림을 정하므로 각각 한 프레임씩 준다.
        Check(PostMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(x, y)) != 0,
            "the pointer must post");
        Check(editor.Tick(Delta), "the editor must tick");
        Check(PostMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y)) != 0,
            "the press must post");
        Check(editor.Tick(Delta), "the editor must tick");
        Check(PostMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(x, y)) != 0,
            "the release must post");
        Check(editor.Tick(Delta), "the editor must tick");

        Check(false == panel->IsOpen(),
            "clicking the title bar X must close the panel");

        const int drawsWhenClosed = panel->draws;
        Check(editor.Tick(Delta), "the editor must tick");
        Check(panel->draws == drawsWhenClosed, "and it must stay closed");

        editor.Shutdown();
    }

    // ── 인스펙터 위젯 ────────────────────────────────────────────────────
    //
    // **위젯 자리를 화면에서 짐작하지 않는다.** ImGui 는 그린 항목의 사각형을
    // 남겨 두지 않지만, 항목마다 매기는 Id 는 이름과 `PushID` 로 정해져 있어
    // 밖에서도 같은 방법으로 셀 수 있다. 마우스를 패널 안에서 아래로 훑으며
    // "지금 무엇 위인가" 를 물어보면 자리가 나온다 - 글꼴이나 줄 간격이 바뀌어도
    // 견디고, 못 찾으면 조용히 통과하는 대신 운다.

    constexpr float Frame = 1.0f / 60.0f;


    ImGuiID PushedId(ImGuiID seed, int value)
    {
        // `ImGui::PushID(int)` 와 같은 계산이다.
        return ImHashData(&value, sizeof(value), seed);
    }

    ImGuiID LabelId(ImGuiID seed, const char* label)
    {
        return ImHashStr(label, 0, seed);
    }

    // 인스펙터에 그려진 컴포넌트 슬롯 `slot` 의 필드 `field` 에 붙은 Id.
    //
    // 인스펙터는 줄을 **2열 표**로 그리므로(ProjectRule §11.3) Id 사슬에 표가
    // 하나 낀다 - `BeginTable` 이 `PushOverrideID(instanceId)` 를 하고, 그 프레임의
    // 첫 인스턴스면 `instanceId == GetID(표이름)` 이다.
    //
    // 창을 `"Inspector"` 로 찾는 것은 여전히 옳다. 실제 이름은
    // `"인스펙터###Inspector"` 지만 `ImHashStr` 이 `###` 에서 해시를 다시 세므로
    // 둘이 같은 값이다 - 번역해도 창의 정체가 그대로인 이유가 이것이다.
    ImGuiID InspectorFieldId(int slot, std::uint32_t field, const char* label)
    {
        ImGuiWindow* window = ImGui::FindWindowByName("Inspector");
        Check(window != nullptr, "the inspector must have a window");
        const ImGuiID component = PushedId(window->ID, slot);
        const ImGuiID table = LabelId(component, "##component");
        return LabelId(PushedId(table, static_cast<int>(field)), label);
    }

    struct Spot
    {
        int x = 0;
        int y = 0;
        bool disabled = false;
    };

    bool FindInspectorItem(
        JBro::EditorApplication& editor, HWND hwnd, ImGuiID target, Spot& spot)
    {
        ImGuiWindow* window = ImGui::FindWindowByName("Inspector");
        Check(window != nullptr, "the inspector must have a window");
        // **값 칸**이다. 왼쪽 칸은 라벨이 차지하므로 그쪽을 훑으면 위젯을 못 만난다.
        const int x = static_cast<int>(window->Pos.x + window->Size.x * 0.65f);
        const int bottom = static_cast<int>(window->Pos.y + window->Size.y);
        for (int y = static_cast<int>(window->Pos.y); y < bottom; y += 3)
        {
            PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
            Check(editor.Tick(Frame), "the editor must tick while looking");
            if (ImGui::GetHoveredID() == target)
            {
                spot.x = x;
                spot.y = y;
                spot.disabled = ImGui::GetCurrentContext()->HoveredIdIsDisabled;
                return true;
            }
        }
        return false;
    }

    void DragFrom(
        JBro::EditorApplication& editor, HWND hwnd, const Spot& spot, int toX)
    {
        PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(spot.x, spot.y));
        Check(editor.Tick(Frame), "the editor must tick");
        PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(spot.x, spot.y));
        Check(editor.Tick(Frame), "the editor must tick");
        constexpr int Steps = 8;
        for (int step = 1; step <= Steps; ++step)
        {
            const int x = spot.x + (toX - spot.x) * step / Steps;
            PostMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x, spot.y));
            Check(editor.Tick(Frame), "the editor must tick mid-drag");
        }
        PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(toX, spot.y));
        Check(editor.Tick(Frame), "the editor must tick");
    }

    void ClickAt(JBro::EditorApplication& editor, HWND hwnd, const Spot& spot)
    {
        PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(spot.x, spot.y));
        Check(editor.Tick(Frame), "the editor must tick");
        PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(spot.x, spot.y));
        Check(editor.Tick(Frame), "the editor must tick");
        PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(spot.x, spot.y));
        Check(editor.Tick(Frame), "the editor must tick");
    }

    std::uint32_t FieldIndexOf(const JBro::PropertyTable& table, const char* name)
    {
        for (std::uint32_t index = 0; index < table.count; ++index)
        {
            const char* found =
                JBro::NameTable::Get().Resolve(table.properties[index].name);
            if (found != nullptr && std::strcmp(found, name) == 0)
            {
                return index;
            }
        }
        Check(false, "the field this test names must be in the table");
        return 0;
    }

    // ── 목록 편집 ────────────────────────────────────────────────────────

    using Weights = JBro::Array<float>;

    // 배열을 든 컴포넌트다. 빌트인에는 아직 배열 필드가 없다(D-86).
    class Weighted final : public JBro::ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Test::Weighted";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Weighted)

        JBRO_FIELD(Weights, weights);
    };

    // 인스펙터 안의 목록 몸통이다. 목록 위젯이 `BeginChild("##list_body")` 로
    // 따로 창을 열므로, 그 안의 항목은 이 창의 Id 에서 센다.
    ImGuiWindow* FindListBody()
    {
        ImGuiWindow* inspector = ImGui::FindWindowByName("Inspector");
        Check(inspector != nullptr, "the inspector must have a window");
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
        {
            if (window->ParentWindow == inspector
                && std::strstr(window->Name, "##list_body") != nullptr)
            {
                return window;
            }
        }
        return nullptr;
    }

    // 인스펙터 안의 `n` 번째 목록 몸통이다. 컴포넌트 하나가 목록을 여럿 들면 몸통도 여럿이고,
    // 화면 위에서 아래로 센다 - 창 목록의 차례는 만들어진 차례라 믿지 않는다.
    ImGuiWindow* FindListBodyAt(int n)
    {
        ImGuiWindow* inspector = ImGui::FindWindowByName("Inspector");
        Check(inspector != nullptr, "the inspector must have a window");
        ImGuiWindow* bodies[8] = {};
        int count = 0;
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
        {
            if (window->ParentWindow == inspector
                && std::strstr(window->Name, "##list_body") != nullptr && count < 8)
            {
                int at = count++;
                while (at > 0 && bodies[at - 1]->Pos.y > window->Pos.y)
                {
                    bodies[at] = bodies[at - 1];
                    --at;
                }
                bodies[at] = window;
            }
        }
        return n < count ? bodies[n] : nullptr;
    }

    // `window` 를 `x` 에서 위아래로 훑어 `target` 이 가리켜지는 자리를 찾는다.
    bool FindItemInWindow(JBro::EditorApplication& editor, HWND hwnd, ImGuiWindow* window,
        ImGuiID target, int x, Spot& spot)
    {
        Check(window != nullptr, "the window this test looks in must exist");
        const int bottom = static_cast<int>(window->Pos.y + window->Size.y);
        for (int y = static_cast<int>(window->Pos.y); y < bottom; y += 2)
        {
            PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
            Check(editor.Tick(Frame), "the editor must tick while looking");
            if (ImGui::GetHoveredID() == target)
            {
                spot.x = x;
                spot.y = y;
                return true;
            }
        }
        return false;
    }

    // `window` 를 여러 x 에서 훑는다. 한 칸짜리 위젯(켜기 칸)은 한 x 로는 빗나간다.
    bool FindItemAnywhereInWindow(JBro::EditorApplication& editor, HWND hwnd,
        ImGuiWindow* window, ImGuiID target, Spot& spot)
    {
        Check(window != nullptr, "the window this test looks in must exist");
        for (float fraction = 0.05f; fraction < 0.95f; fraction += 0.05f)
        {
            if (FindItemInWindow(editor, hwnd, window, target,
                    static_cast<int>(window->Pos.x + window->Size.x * fraction), spot))
            {
                return true;
            }
        }
        return false;
    }

    // 손잡이에서 떨어뜨릴 자리까지 끈다. 가로로만 끄는 `DragFrom` 과 달리 두 축을 다 간다.
    // 끌어 놓기는 **놓는 순간 마우스가 목표 위에 있어야** 받으므로, 마지막 움직임 뒤에 한 번
    // 더 돌려 ImGui 가 목표를 본 뒤에 놓는다.
    void DragTo(JBro::EditorApplication& editor, HWND hwnd, const Spot& from, const Spot& to)
    {
        PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(from.x, from.y));
        Check(editor.Tick(Frame), "the editor must tick");
        PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(from.x, from.y));
        Check(editor.Tick(Frame), "the editor must tick");
        constexpr int Steps = 12;
        for (int step = 1; step <= Steps; ++step)
        {
            const int x = from.x + (to.x - from.x) * step / Steps;
            const int y = from.y + (to.y - from.y) * step / Steps;
            PostMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x, y));
            Check(editor.Tick(Frame), "the editor must tick mid-drag");
        }
        Check(editor.Tick(Frame), "the editor must see the drop target before the release");
        PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(to.x, to.y));
        Check(editor.Tick(Frame), "the editor must tick");
        Check(editor.Tick(Frame), "the editor must tick once more to run the drop");
    }

    // 목록 몸통을 `x` 에서 위아래로 훑어 `target` 이 가리켜지는 자리를 찾는다.
    bool FindListItem(
        JBro::EditorApplication& editor, HWND hwnd, ImGuiID target, int x, Spot& spot)
    {
        ImGuiWindow* body = FindListBody();
        Check(body != nullptr, "the list must have its body");
        const int bottom = static_cast<int>(body->Pos.y + body->Size.y);
        for (int y = static_cast<int>(body->Pos.y); y < bottom; y += 2)
        {
            PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
            Check(editor.Tick(Frame), "the editor must tick while looking");
            if (ImGui::GetHoveredID() == target)
            {
                spot.x = x;
                spot.y = y;
                return true;
            }
        }
        return false;
    }

    // 좁은 항목(행 끝의 삭제 표시)은 한 줄로 훑으면 빗나간다. 몸통의 오른쪽 끝 띠를
    // 위쪽 몇 줄만 격자로 훑는다.
    bool FindListItemNearRightEdge(
        JBro::EditorApplication& editor, HWND hwnd, ImGuiID target, int rows, Spot& spot)
    {
        ImGuiWindow* body = FindListBody();
        Check(body != nullptr, "the list must have its body");
        const int right = static_cast<int>(body->Pos.x + body->Size.x);
        const int top = static_cast<int>(body->Pos.y);
        const int bottom = top + static_cast<int>(ImGui::GetFrameHeight()) * rows + 8;
        for (int y = top; y < bottom; y += 2)
        {
            for (int x = right - 40; x < right; x += 3)
            {
                PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                Check(editor.Tick(Frame), "the editor must tick while looking");
                if (ImGui::GetHoveredID() == target)
                {
                    spot.x = x;
                    spot.y = y;
                    return true;
                }
            }
        }
        return false;
    }

    // **목록을 만지면 고른 것 전부에 미치고, 한 손짓이 한 되돌리기다**(D-86).
    //
    // 처음에는 원소 값·추가·삭제가 전부 배열에 곧장 써서 되돌릴 수 없었고,
    // 여럿을 골라도 주된 것만 바뀌었다.
    void TestListEditsReachEveryChosenObjectAsOneUndo()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; list edits not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "ListEditProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");

        JBro::RegisterBuiltinProperties<Weighted>();
        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* alpha = canvas->CreateObject("Alpha");
        JBro::GameObject* beta = canvas->CreateObject("Beta");
        auto* a = canvas->AttachComponent<Weighted>(alpha);
        auto* b = canvas->AttachComponent<Weighted>(beta);
        Check(a != nullptr && b != nullptr, "both must hold a list");
        for (float value : {1.0f, 100.0f, 3.0f})
        {
            a->weights.Add(value);
        }
        for (float value : {10.0f, 20.0f, 30.0f, 40.0f})
        {
            b->weights.Add(value);
        }
        JBro::GameObject* chosen[] = {alpha, beta};
        editor.SelectObjects({chosen, 2});
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        ImGuiWindow* body = FindListBody();
        Check(body != nullptr, "the inspector must draw the list");
        const int middle = static_cast<int>(body->Pos.x + body->Size.x * 0.5f);
        std::size_t undo = editor.GetCommands().GetUndoCount();

        // 둘째 원소를 끈다. 둘 다 같은 만큼 움직여야 한다 - 모이면 뭉갠 것이다.
        Spot spot;
        Check(FindListItem(editor, hwnd, LabelId(PushedId(body->ID, 1), "##value"), middle, spot),
            "the second element must be on the list");
        DragFrom(editor, hwnd, spot, spot.x + 80);
        const float moved = a->weights[1] - 100.0f;
        Check(moved > 0.05f, "dragging an element must move it");
        // **끈 만큼 움직여야 한다.** 도달한 값 자체를 델타로 삼아도 둘이 같은 만큼
        // 움직이는 것은 맞으므로, 크기까지 봐야 가려진다.
        Check(moved < 5.0f, "by the distance dragged, not by the value it reached");
        Check(b->weights[1] > 20.0f + moved - 0.01f && b->weights[1] < 20.0f + moved + 0.01f,
            "and move the other chosen list's element by the same amount");
        Check(a->weights[0] == 1.0f && b->weights[0] == 10.0f, "leaving the others alone");
        Check(editor.GetCommands().GetUndoCount() == undo + 1, "one drag must be one undo");
        Check(editor.GetCommands().Undo(), "undo must run");
        Check(a->weights[1] == 100.0f && b->weights[1] == 20.0f, "and put both back");
        undo = editor.GetCommands().GetUndoCount();

        // 하나 더한다.
        const char* addLabel = JBro::Loc::TextOr(JBro::LocKeys::ListAddElement, "Add element");
        Check(FindListItem(editor, hwnd, LabelId(body->ID, addLabel), middle, spot),
            "the list must offer to add an element");
        ClickAt(editor, hwnd, spot);
        Check(a->weights.Size() == 4 && b->weights.Size() == 5,
            "adding must reach both chosen lists");
        Check(editor.GetCommands().GetUndoCount() == undo + 1, "as one undo");
        Check(editor.GetCommands().Undo(), "undo must run");
        Check(a->weights.Size() == 3 && b->weights.Size() == 4, "and take both back");
        undo = editor.GetCommands().GetUndoCount();

        // 첫 원소를 지운다. 삭제 표시는 행의 오른쪽 끝이다.
        // `TextButton` 은 이름을 `PushID` 로 쌓고 빈 이름의 단추를 그린다. 빈 이름의
        // 해시는 시드를 그대로 돌려주므로 Id 는 행 아래의 "x" 다.
        Check(FindListItemNearRightEdge(editor, hwnd, LabelId(PushedId(body->ID, 0), "x"), 1,
                spot),
            "the first row must offer to be removed");
        ClickAt(editor, hwnd, spot);
        Check(a->weights.Size() == 2 && a->weights[0] == 100.0f,
            "removing must take the first element off the list on screen");
        Check(b->weights.Size() == 3 && b->weights[0] == 20.0f,
            "and off the other chosen list");
        Check(editor.GetCommands().GetUndoCount() == undo + 1, "as one undo");
        Check(editor.GetCommands().Undo(), "undo must run");
        Check(a->weights.Size() == 3 && a->weights[0] == 1.0f
                && b->weights.Size() == 4 && b->weights[0] == 10.0f,
            "and bring both back in order");

        if (JBro::Renderer* renderer = editor.GetRenderer())
        {
            SaveScreenshot(*renderer, 1024, 768, "list");
        }
        editor.Shutdown();
    }

    using Points = JBro::Array<JBro::Vec2>;

    // 원소가 실수 묶음인 목록이다. 한 줄에 칸 둘로 그려지는 원소는 목록 편집에서
    // 따로 가는 길(실수 묶음을 모아 델타로 적는 길)이라 따로 잰다.
    class Pointed final : public JBro::ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Test::Pointed";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Pointed)

        JBRO_FIELD(Points, points);
    };

    using Signals = JBro::Array<Signal>;

    class Signalled final : public JBro::ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Test::Signalled";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Signalled)

        // 목록 앞뒤의 필드다. 목록이 표를 끊으므로 앞 조각과 뒤 조각의 라벨 칸이 맞아야 한다 -
        // 앞 라벨을 뒤 라벨보다 길게 두어, 뒤 조각이 제 라벨에 맞추면 값 칸이 어긋나게 한다.
        JBRO_FIELD(float, leadingLonger) = 0.0f;
        JBRO_FIELD(Signals, signals);
        JBRO_FIELD(float, trailing) = 0.0f;
        JBRO_FIELD(Relay, relay);
    };

    // 한 줄에서 `target` 이 가리켜지는 가장 왼쪽 x 다. 못 찾으면 -1.
    int LeftEdgeOf(JBro::EditorApplication& editor, HWND hwnd, ImGuiID target, int y)
    {
        ImGuiWindow* window = ImGui::FindWindowByName("Inspector");
        Check(window != nullptr, "the inspector must have a window");
        for (int x = static_cast<int>(window->Pos.x);
             x < static_cast<int>(window->Pos.x + window->Size.x); ++x)
        {
            PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
            Check(editor.Tick(Frame), "the editor must tick while measuring");
            if (ImGui::GetHoveredID() == target)
            {
                return x;
            }
        }
        return -1;
    }

    // 목록 몸통을 여러 x 에서 훑는다. 접기 마디의 이름표나 칸 둘로 나뉜 줄은 한 x 로는 빗나간다.
    bool FindListItemAnywhere(
        JBro::EditorApplication& editor, HWND hwnd, ImGuiID target, Spot& spot)
    {
        for (float fraction = 0.10f; fraction < 0.95f; fraction += 0.05f)
        {
            ImGuiWindow* body = FindListBody();
            Check(body != nullptr, "the list must have its body");
            if (FindListItem(editor, hwnd, target,
                    static_cast<int>(body->Pos.x + body->Size.x * fraction), spot))
            {
                return true;
            }
        }
        return false;
    }

    // **필드를 가진 구조체 원소는 접기 마디로 그리고, 펼치면 필드마다 고친다**(D-89).
    //
    // 처음에는 "(no way to show this type)" 한 줄이었다. 필드 편집은 원소 번호와 필드 길을 든
    // 목록 편집이므로 고른 목록 전부에 같은 델타가 한 되돌리기로 간다. 저장하지 않는 필드는
    // 목록 전체의 글자에 담기지 않아 되돌릴 수 없으므로 잠겨 있어야 한다.
    void TestAStructElementOpensAndEditsEveryChosenList()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; struct element lists not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "StructListProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");

        JBro::RegisterBuiltinProperties<Signalled>();
        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* alpha = canvas->CreateObject("Alpha");
        JBro::GameObject* beta = canvas->CreateObject("Beta");
        auto* a = canvas->AttachComponent<Signalled>(alpha);
        auto* b = canvas->AttachComponent<Signalled>(beta);
        Check(a != nullptr && b != nullptr, "both must hold a list of signals");
        a->signals.Add(Signal{1.0f, false, 5.0f});
        a->signals.Add(Signal{100.0f, false, 5.0f});
        b->signals.Add(Signal{10.0f, false, 5.0f});
        b->signals.Add(Signal{50.0f, false, 5.0f});
        b->signals.Add(Signal{0.0f, true, 5.0f});
        JBro::GameObject* chosen[] = {alpha, beta};
        editor.SelectObjects({chosen, 2});
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        // 둘째 원소의 마디를 연다. 이름표는 타입 이름에서 접두어를 뗀 것이다.
        ImGuiWindow* body = FindListBody();
        Check(body != nullptr, "the inspector must draw the list");
        const ImGuiID row = PushedId(body->ID, 1);
        const ImGuiID node = LabelId(row, "Signal");
        Spot spot;
        Check(FindListItemAnywhere(editor, hwnd, node, spot),
            "the second element must be drawn as a node that can be opened");
        ClickAt(editor, hwnd, spot);
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the list must grow to show the fields");
        }

        const JBro::PropertyTable& fields = *JBro::TypeDescriptorOf<Signal>::Get().fields;
        const ImGuiID table = LabelId(node, "##element");
        const auto fieldId = [&](const char* field) {
            return LabelId(PushedId(table, static_cast<int>(FieldIndexOf(fields, field))),
                "##value");
        };

        // 실수 필드를 끈다. 두 목록의 둘째 원소가 같은 만큼 움직여야 한다.
        Check(FindListItemAnywhere(editor, hwnd, fieldId("strength"), spot),
            "the opened element must show its strength field");
        // **값이 읽힐 만큼 넓어야 한다.** 목록을 값 칸 안에 두었을 때는 펼친 원소의 필드 표가 또
        // 라벨 칸을 가져, 값이 몇 픽셀만 남았다(`100` 이 `1` 로 보였다).
        int strengthRight = -1;
        {
            ImGuiWindow* list = FindListBody();
            int hovered = 0;
            for (int x = static_cast<int>(list->Pos.x);
                 x < static_cast<int>(list->Pos.x + list->Size.x); x += 2)
            {
                PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, spot.y));
                Check(editor.Tick(Frame), "the editor must tick while measuring");
                if (ImGui::GetHoveredID() == fieldId("strength"))
                {
                    hovered += 2;
                    strengthRight = x;
                }
            }
            // 목록 폭의 4분의 1 이다. 값 칸 안에 두었을 때는 1024 창에서 목록 폭의 7% 쯤이었다.
            Check(static_cast<float>(hovered) >= list->Size.x * 0.25f,
                "the value of a field inside an element must be wide enough to read");
        }
        std::size_t undo = editor.GetCommands().GetUndoCount();
        DragFrom(editor, hwnd, spot, spot.x + 60);
        const float moved = a->signals[1].strength - 100.0f;
        Check(moved > 0.05f, "dragging a field inside an element must move it");
        Check(moved < 5.0f, "by the distance dragged, not by the value it reached");
        Check(b->signals[1].strength > 50.0f + moved - 0.01f
                && b->signals[1].strength < 50.0f + moved + 0.01f,
            "and move the same element of the other chosen list by the same amount");
        Check(a->signals[0].strength == 1.0f && b->signals[2].strength == 0.0f,
            "leaving the other elements alone");
        Check(editor.GetCommands().GetUndoCount() == undo + 1, "one drag must be one undo");
        Check(editor.GetCommands().Undo(), "undo must run");
        Check(a->signals[1].strength == 100.0f && b->signals[1].strength == 50.0f,
            "and put both back");
        undo = editor.GetCommands().GetUndoCount();

        // 켜짐 칸은 델타가 없다. 누른 값이 두 목록에 그대로 간다.
        Check(FindListItemAnywhere(editor, hwnd, fieldId("on"), spot),
            "the opened element must show its flag");
        ClickAt(editor, hwnd, spot);
        Check(a->signals[1].on && b->signals[1].on, "ticking the flag must reach both lists");
        Check(false == a->signals[0].on && b->signals[2].on, "leaving the other elements' flags");
        Check(editor.GetCommands().GetUndoCount() == undo + 1, "as one undo");
        Check(editor.GetCommands().Undo(), "undo must run");
        Check(false == a->signals[1].on && false == b->signals[1].on, "and untick both");

        // 저장하지 않는 필드는 잠겨 있다.
        Check(FindListItemAnywhere(editor, hwnd, fieldId("echo"), spot),
            "a field that is not saved must still be shown");
        Check(ImGui::GetCurrentContext()->HoveredIdIsDisabled,
            "but locked, since undoing the list could not bring it back");

        // 원소 안의 배열은 목록으로 그리지 않는다. 목록 안의 목록은 목록 편집이 재귀해야 한다.
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
        {
            Check(false == (window->ParentWindow == FindListBody()
                    && std::strstr(window->Name, "##list_body") != nullptr),
                "a list inside an element must not be drawn as a list of its own");
        }

        // **표를 끊어도 앞뒤 조각의 값 칸이 맞아야 한다.** 다시 연 표는 같은 프레임의 둘째
        // 인스턴스라 Id 사슬이 다르다 - `BeginTable` 이 "##Instances" 를 쌓고 인스턴스 번호를 쌓는다.
        {
            const JBro::PropertyTable* signalledTable =
                JBro::PropertyRegistry::Lookup(Signalled::StaticTypeName());
            Check(signalledTable != nullptr, "the signalled component must have its table");
            const ImGuiID leading = InspectorFieldId(
                0, FieldIndexOf(*signalledTable, "leadingLonger"), "##value");
            ImGuiWindow* inspector = ImGui::FindWindowByName("Inspector");
            const ImGuiID firstPart = LabelId(PushedId(inspector->ID, 0), "##component");
            const ImGuiID secondPart = PushedId(LabelId(firstPart, "##Instances"), 1);
            const ImGuiID trailing = LabelId(PushedId(secondPart,
                static_cast<int>(FieldIndexOf(*signalledTable, "trailing"))), "##value");
            Spot before;
            Spot after;
            Check(FindInspectorItem(editor, hwnd, leading, before),
                "the field before the list must be in the first part of the table");
            Check(FindInspectorItem(editor, hwnd, trailing, after),
                "the field after the list must be in the part opened again");
            Check(LeftEdgeOf(editor, hwnd, leading, before.y)
                    == LeftEdgeOf(editor, hwnd, trailing, after.y),
                "and both values must start in the same column");
        }

        // **삭제 표시는 줄마다 같은 자리다.** 접힌 줄에서는 이름표에 붙고 펼친 줄에서는 필드 표에
        // 밀려 행 밖으로 반쯤 나갔다.
        Spot closedMark;
        Check(FindListItemNearRightEdge(editor, hwnd, LabelId(PushedId(body->ID, 0), "x"), 12,
                closedMark),
            "a closed element must have its remove mark at the end of the row");
        Check(FindListItemNearRightEdge(editor, hwnd, LabelId(row, "x"), 12, spot),
            "an opened element must keep its remove mark inside the list");
        Check(spot.x == closedMark.x, "and both marks must stand in the same column");
        // 필드 표는 행의 내용 폭만 쓴다. 남은 폭을 다 쓰면 값 칸이 삭제 표시 밑까지 뻗는다.
        Check(strengthRight >= 0 && strengthRight < closedMark.x,
            "a field value inside an element must stop before the remove marks");

        if (JBro::Renderer* renderer = editor.GetRenderer())
        {
            SaveScreenshot(*renderer, 1024, 768, "struct_list");
        }
        editor.Shutdown();
    }

    // **구조체 원소를 손잡이로 끌어 놓으면 고른 목록 전부에서 같은 자리로 간다**(D-89 ⑤).
    //
    // 옮기기는 지금까지 조작 함수(`ArrayOps::Move`)로만 재었다. 화면에서는 행의 손잡이를
    // 잡아 행 사이의 얇은 자리에 놓는 것이고, 그 길(끌기 시작 → 놓는 자리 → 슬롯 번호 보정 →
    // 목록 편집 → 커맨드)은 마우스로만 잴 수 있다.
    void TestDraggingAStructElementReordersEveryChosenList()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; element reordering not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "ReorderProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");

        JBro::RegisterBuiltinProperties<Signalled>();
        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* alpha = canvas->CreateObject("Alpha");
        JBro::GameObject* beta = canvas->CreateObject("Beta");
        auto* a = canvas->AttachComponent<Signalled>(alpha);
        auto* b = canvas->AttachComponent<Signalled>(beta);
        Check(a != nullptr && b != nullptr, "both must hold a list of signals");
        a->signals.Add(Signal{1.0f, false, 5.0f});
        a->signals.Add(Signal{100.0f, true, 5.0f});
        b->signals.Add(Signal{10.0f, false, 5.0f});
        b->signals.Add(Signal{50.0f, true, 5.0f});
        b->signals.Add(Signal{0.0f, false, 5.0f});
        JBro::GameObject* chosen[] = {alpha, beta};
        editor.SelectObjects({chosen, 2});
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        ImGuiWindow* body = FindListBody();
        Check(body != nullptr, "the inspector must draw the list");
        // 둘째 행의 손잡이다. 행 배경(`##row_body`)이 끌기의 출발점이고, 손잡이 글자는 그 위에
        // 얹혀 있다 - 왼쪽 끝을 훑으면 배경이 가리켜진다.
        Spot handle;
        Check(FindListItem(editor, hwnd, LabelId(PushedId(body->ID, 1), "##row_body"),
                static_cast<int>(body->Pos.x) + 6, handle),
            "the second row must have a handle to drag");
        // 첫 행 위의 떨어뜨릴 자리. 슬롯 번호는 "이 원소 앞" 이다.
        Spot slot;
        Check(FindListItem(editor, hwnd, LabelId(PushedId(body->ID, 0), "##slot"),
                static_cast<int>(body->Pos.x + body->Size.x * 0.5f), slot),
            "there must be a drop slot above the first row");
        // 둘째 원소의 마디를 펼쳐 둔다. **펼침은 원소를 따라가야 한다** - 행 번호에 붙어 있으면
        // 옮긴 뒤 옛 자리의 원소가 펼쳐져 보인다.
        Spot node;
        Check(FindListItemAnywhere(editor, hwnd, LabelId(PushedId(body->ID, 1), "Signal"), node),
            "the second element must be drawn as a node");
        ClickAt(editor, hwnd, node);
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the list must grow to show the fields");
        }
        ImGuiStorage* rowStates = body->DC.StateStorage;
        Check(rowStates->GetInt(LabelId(PushedId(body->ID, 1), "Signal"), 0) == 1
                && rowStates->GetInt(LabelId(PushedId(body->ID, 0), "Signal"), 0) == 0,
            "the second node must be open and the first closed before the drag");
        // 펼친 행은 손잡이 자리도 커졌다. 다시 찾는다.
        Check(FindListItem(editor, hwnd, LabelId(PushedId(body->ID, 1), "##row_body"),
                static_cast<int>(body->Pos.x) + 6, handle),
            "the opened second row must still have a handle to drag");

        const std::size_t undo = editor.GetCommands().GetUndoCount();
        DragTo(editor, hwnd, handle, slot);
        Check(a->signals[0].strength == 100.0f && a->signals[1].strength == 1.0f,
            "dropping the second element above the first must swap them on screen");
        Check(rowStates->GetInt(LabelId(PushedId(body->ID, 0), "Signal"), 0) == 1
                && rowStates->GetInt(LabelId(PushedId(body->ID, 1), "Signal"), 0) == 0,
            "and the opened node must travel with its element to the first row");
        for (int frame = 0; frame < 2; ++frame)
        {
            Check(editor.Tick(Frame), "the moved row must settle");
        }
        Check(FindListItemAnywhere(editor, hwnd,
                LabelId(PushedId(LabelId(LabelId(PushedId(body->ID, 0), "Signal"), "##element"),
                    static_cast<int>(FieldIndexOf(
                        *JBro::TypeDescriptorOf<Signal>::Get().fields, "strength"))),
                    "##value"),
                node),
            "so the first row now shows the fields of the element that moved there");
        // 되돌린 뒤 이어지는 검사에서 손잡이 자리가 바뀌지 않게 마디를 다시 접는다.
        Check(FindListItemAnywhere(editor, hwnd, LabelId(PushedId(body->ID, 0), "Signal"), node),
            "the first row must show its node");
        ClickAt(editor, hwnd, node);
        Check(a->signals[0].on && false == a->signals[1].on,
            "carrying every field of the element along");
        Check(b->signals[0].strength == 50.0f && b->signals[1].strength == 10.0f
                && b->signals[2].strength == 0.0f,
            "and move the same element of the other chosen list, leaving its third alone");
        Check(editor.GetCommands().GetUndoCount() == undo + 1, "one drop must be one undo");
        Check(editor.GetCommands().Undo(), "undo must run");
        Check(a->signals[0].strength == 1.0f && a->signals[1].strength == 100.0f
                && b->signals[0].strength == 10.0f && b->signals[1].strength == 50.0f,
            "and put both lists back in order");

        // 놓는 자리가 출발 행의 바로 아래면 옮길 것이 없다. 되돌리기가 하나 늘면 빈 커맨드다.
        Check(FindListItem(editor, hwnd, LabelId(PushedId(body->ID, 2), "##slot"),
                static_cast<int>(body->Pos.x + body->Size.x * 0.5f), slot),
            "there must be a drop slot below the second row");
        const std::size_t before = editor.GetCommands().GetUndoCount();
        DragTo(editor, hwnd, handle, slot);
        Check(a->signals[0].strength == 1.0f && a->signals[1].strength == 100.0f,
            "dropping an element right below itself must change nothing");
        Check(editor.GetCommands().GetUndoCount() == before, "and leave nothing to undo");

        // 첫 행을 맨 아래 자리에 놓는다. 원본을 먼저 빼므로 **뒤로 갈 때는 목표가 한 칸
        // 당겨진다** - 위로 끄는 것만 재면 그 보정이 빠져도 드러나지 않는다.
        Check(FindListItem(editor, hwnd, LabelId(PushedId(body->ID, 0), "##row_body"),
                static_cast<int>(body->Pos.x) + 6, handle),
            "the first row must have a handle to drag");
        Check(FindListItem(editor, hwnd, LabelId(PushedId(body->ID, 2), "##slot"),
                static_cast<int>(body->Pos.x + body->Size.x * 0.5f), slot),
            "there must be a drop slot at the end of the list");
        DragTo(editor, hwnd, handle, slot);
        Check(a->signals[0].strength == 100.0f && a->signals[1].strength == 1.0f,
            "dropping the first element at the end must put it last");
        Check(b->signals[0].strength == 50.0f && b->signals[1].strength == 10.0f
                && b->signals[2].strength == 0.0f,
            "and move the other list's first element to the same place, not past it");
        Check(editor.GetCommands().GetUndoCount() == before + 1, "as one undo");
        Check(editor.GetCommands().Undo(), "undo must run");
        Check(a->signals[0].strength == 1.0f && b->signals[0].strength == 10.0f,
            "and put both lists back");

        // 지울 때도 펼침이 따라온다. 둘째를 펼치고 첫째를 지우면 새 첫째가 펼쳐져 있어야 한다.
        Check(FindListItemAnywhere(editor, hwnd, LabelId(PushedId(body->ID, 1), "Signal"), node),
            "the second element must show its node");
        ClickAt(editor, hwnd, node);
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the list must grow to show the fields");
        }
        Check(rowStates->GetInt(LabelId(PushedId(body->ID, 1), "Signal"), 0) == 1,
            "the second node must be open before the removal");
        Check(FindListItemNearRightEdge(editor, hwnd, LabelId(PushedId(body->ID, 0), "x"), 1, node),
            "the first row must offer to be removed");
        ClickAt(editor, hwnd, node);
        Check(a->signals.Size() == 1 && a->signals[0].strength == 100.0f,
            "removing the first element must leave the second");
        Check(rowStates->GetInt(LabelId(PushedId(body->ID, 0), "Signal"), 0) == 1
                && rowStates->GetInt(LabelId(PushedId(body->ID, 1), "Signal"), 0) == 0,
            "and its open node must move up with it");
        Check(editor.GetCommands().Undo(), "undo must run");

        editor.Shutdown();
    }

    using Flags = JBro::Array<bool>;
    using Counts = JBro::Array<int>;
    using Tones = JBro::Array<Tone>;

    // 원소가 실수가 아닌 목록 셋이다. 켜기 칸·정수 끌기·enum 콤보는 각각 다른 위젯이고,
    // 델타 대신 고른 값이 그대로 가는 길(D-83)이라 실수 목록으로는 재어지지 않는다.
    class Toggled final : public JBro::ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Test::Toggled";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        JBRO_REFLECT_BODY(Toggled)

        JBRO_FIELD(Flags, flags);
        JBRO_FIELD(Counts, counts);
        JBRO_FIELD(Tones, tones);
    };

    // **bool·int·enum 원소도 마우스로 고치면 고른 목록 전부에 한 되돌리기로 간다**(D-89 ⑤).
    //
    // 셋은 델타가 없는 값이다 - 주된 목록에서 고른 값이 다른 목록의 같은 자리에 그대로 간다.
    // enum 은 콤보의 칸 번호가 아니라 이름이 글자로 가야 한다. 값이 연속이 아니면 번호는 틀린다.
    void TestFlagCountAndToneElementsEditByMouseOnEveryChosenList()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; flag, count and tone lists not verified"
                      << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "ToggledListProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");

        JBro::RegisterBuiltinProperties<Toggled>();
        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* alpha = canvas->CreateObject("Alpha");
        JBro::GameObject* beta = canvas->CreateObject("Beta");
        auto* a = canvas->AttachComponent<Toggled>(alpha);
        auto* b = canvas->AttachComponent<Toggled>(beta);
        Check(a != nullptr && b != nullptr, "both must hold the three lists");
        for (bool flag : {false, false})
        {
            a->flags.Add(flag);
        }
        for (bool flag : {true, false, false})
        {
            b->flags.Add(flag);
        }
        for (int count : {3, 7})
        {
            a->counts.Add(count);
        }
        for (int count : {30, 70, 0})
        {
            b->counts.Add(count);
        }
        for (Tone tone : {Tone::Low, Tone::Low})
        {
            a->tones.Add(tone);
        }
        for (Tone tone : {Tone::High, Tone::Mid, Tone::Low})
        {
            b->tones.Add(tone);
        }
        JBro::GameObject* chosen[] = {alpha, beta};
        editor.SelectObjects({chosen, 2});
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        // 목록 셋은 필드 차례대로 위에서 아래다.
        ImGuiWindow* flagBody = FindListBodyAt(0);
        ImGuiWindow* countBody = FindListBodyAt(1);
        ImGuiWindow* toneBody = FindListBodyAt(2);
        Check(flagBody != nullptr && countBody != nullptr && toneBody != nullptr,
            "the inspector must draw all three lists");

        // 켜기 칸을 누른다.
        Spot spot;
        std::size_t undo = editor.GetCommands().GetUndoCount();
        Check(FindItemAnywhereInWindow(editor, hwnd, flagBody,
                LabelId(PushedId(flagBody->ID, 1), "##value"), spot),
            "the second flag must be on the first list");
        ClickAt(editor, hwnd, spot);
        Check(a->flags[1] && b->flags[1], "ticking a flag must reach both chosen lists");
        Check(false == a->flags[0] && b->flags[0] && false == b->flags[2],
            "leaving the other flags alone");
        Check(editor.GetCommands().GetUndoCount() == undo + 1, "as one undo");
        Check(editor.GetCommands().Undo(), "undo must run");
        Check(false == a->flags[1] && false == b->flags[1], "and untick both");
        undo = editor.GetCommands().GetUndoCount();

        // 정수를 끈다. 델타가 없는 값이라 도달한 값이 두 목록에 같이 간다.
        Check(FindItemAnywhereInWindow(editor, hwnd, countBody,
                LabelId(PushedId(countBody->ID, 1), "##value"), spot),
            "the second count must be on the second list");
        DragFrom(editor, hwnd, spot, spot.x + 80);
        Check(a->counts[1] != 7, "dragging a count must change it");
        Check(b->counts[1] == a->counts[1],
            "and the other chosen list's count must take the same value");
        Check(a->counts[0] == 3 && b->counts[0] == 30 && b->counts[2] == 0,
            "leaving the other counts alone");
        Check(editor.GetCommands().GetUndoCount() == undo + 1, "one drag must be one undo");
        Check(editor.GetCommands().Undo(), "undo must run");
        Check(a->counts[1] == 7 && b->counts[1] == 70, "and put both counts back");
        undo = editor.GetCommands().GetUndoCount();

        // 콤보를 열어 셋째 이름을 고른다. 콤보의 팝업은 이름이 정해진 창이다.
        Check(FindItemAnywhereInWindow(editor, hwnd, toneBody,
                LabelId(PushedId(toneBody->ID, 1), "##value"), spot),
            "the second tone must be on the third list");
        ClickAt(editor, hwnd, spot);
        ImGuiWindow* popup = ImGui::FindWindowByName("##Combo_00");
        Check(popup != nullptr && popup->Active, "clicking the tone must open its combo");
        // 콤보는 칸 번호를 쌓고 이름의 선택 줄을 그린다. "High" 는 셋째 칸이다.
        Spot item;
        Check(FindItemAnywhereInWindow(editor, hwnd, popup,
                LabelId(PushedId(popup->ID, 2), "High"), item),
            "the combo must list the tone by its name");
        ClickAt(editor, hwnd, item);
        for (int frame = 0; frame < 2; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must tick so the combo can close");
        }
        popup = ImGui::FindWindowByName("##Combo_00");
        Check(popup == nullptr || false == popup->Active, "choosing a tone must close the combo");
        Check(a->tones[1] == Tone::High && b->tones[1] == Tone::High,
            "choosing a tone must reach both chosen lists by its name, not its slot");
        Check(a->tones[0] == Tone::Low && b->tones[0] == Tone::High && b->tones[2] == Tone::Low,
            "leaving the other tones alone");
        Check(editor.GetCommands().GetUndoCount() == undo + 1, "as one undo");
        Check(editor.GetCommands().Undo(), "undo must run");
        Check(a->tones[1] == Tone::Low && b->tones[1] == Tone::Mid, "and put both tones back");

        if (JBro::Renderer* renderer = editor.GetRenderer())
        {
            SaveScreenshot(*renderer, 1024, 768, "toggled_list");
        }
        editor.Shutdown();
    }

    // 팝업 테스트용. 훅이 몇 번 불렸는지 **밖의** 계수기에 센다 - 팝업은 큐에서 빠지는 순간
    // 파괴되므로 자기 멤버로 세면 닫힌 뒤에는 읽을 수 없다.
    struct PopupCounts
    {
        int enters = 0;
        int draws = 0;
        int exits = 0;
        bool closeOnDraw = false;
    };

    class ProbePopup final : public JBro::EditorPopup
    {
    public:
        ProbePopup(const char* id, PopupCounts& counts, bool closable = true)
            : m_id(id)
            , m_counts(&counts)
            , m_closable(closable)
        {
        }

        const char* GetTitle() const override
        {
            return "Probe";
        }

        const char* GetId() const override
        {
            return m_id;
        }

        bool HasCloseButton() const override
        {
            return m_closable;
        }

        void OnEnter(JBro::EditorApplication&) override
        {
            ++m_counts->enters;
        }

        void OnDraw(JBro::EditorApplication&) override
        {
            ++m_counts->draws;
            ImGui::TextUnformatted("probe");
            if (m_counts->closeOnDraw)
            {
                Close();
            }
        }

        void OnExit(JBro::EditorApplication&) override
        {
            ++m_counts->exits;
        }

    private:
        const char* m_id = nullptr;
        PopupCounts* m_counts = nullptr;
        bool m_closable = true;
    };

    ImGuiWindow* FindPopupWindow()
    {
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
        {
            if (std::strstr(window->Name, "###popup_") != nullptr && window->Active)
            {
                return window;
            }
        }
        return nullptr;
    }

    // **모달 팝업은 한 번에 하나만 뜨고, 핸들로 닫고, 같은 Id 는 겹쳐 뜨지 않는다**(기존 엔진
    // `ImPopupDesc`). ImGui 모달은 스택이라 둘을 한 프레임에 열면 뒤의 것이 조용히 사라진다 -
    // 그래서 큐다.
    void TestPopupsOpenOneAtATimeAndCloseByHandle()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; popups not verified" << std::endl;
            return;
        }
        PopupCounts early;
        PopupCounts first;
        PopupCounts second;
        PopupCounts third;
        Check(editor.OpenPopup(JBro::MakeOwnerPtr<ProbePopup>("early", early)) == JBro::InvalidPopupHandle,
            "a popup cannot open before the UI is on");
        JBro::ProjectDescriptor project;
        constexpr char name[] = "PopupProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        for (int frame = 0; frame < 2; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        const JBro::PopupHandle firstHandle =
            editor.OpenPopup(JBro::MakeOwnerPtr<ProbePopup>("first", first, false));
        const JBro::PopupHandle secondHandle =
            editor.OpenPopup(JBro::MakeOwnerPtr<ProbePopup>("second", second));
        Check(firstHandle != JBro::InvalidPopupHandle && secondHandle != JBro::InvalidPopupHandle
                && firstHandle != secondHandle,
            "each popup must get its own handle");
        PopupCounts copy;
        Check(editor.OpenPopup(JBro::MakeOwnerPtr<ProbePopup>("second", copy)) == secondHandle,
            "opening the same id again must hand back the waiting one instead of a copy");
        Check(editor.IsPopupOpen(firstHandle) && editor.IsPopupOpen(secondHandle)
                && editor.IsPopupOpenById("first") && false == editor.IsPopupOpenById("third"),
            "both must count as open while one waits");

        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must tick with a popup up");
        }
        Check(first.enters == 1 && first.draws >= 2, "the first popup must be entered once and drawn");
        Check(second.enters == 0 && second.draws == 0, "while the second waits its turn");
        ImGuiWindow* window = FindPopupWindow();
        Check(window != nullptr, "the popup must have a window");
        Check((window->Flags & ImGuiWindowFlags_Modal) != 0, "and it must be modal");
        Check(false == window->HasCloseButton, "a popup without a close button must not draw one");

        // 핸들로 닫는다. 다음 프레임에 나가는 훅이 오고 둘째가 뜬다.
        editor.ClosePopup(firstHandle);
        Check(editor.Tick(Frame), "the editor must tick after the close request");
        Check(first.exits == 1, "closing must call the exit hook once");
        Check(false == editor.IsPopupOpen(firstHandle), "and the handle must be dead");
        for (int frame = 0; frame < 2; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must tick so the second can show");
        }
        Check(second.enters == 1, "the second popup must show once the first is gone");
        window = FindPopupWindow();
        Check(window != nullptr && window->HasCloseButton, "and it must offer its close button");

        // 팝업이 스스로 닫는다.
        second.closeOnDraw = true;
        for (int frame = 0; frame < 2; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must tick while the popup closes itself");
        }
        Check(second.exits == 1 && false == editor.IsPopupOpen(secondHandle),
            "a popup closing itself must leave the same way");
        Check(FindPopupWindow() == nullptr, "and no popup window may remain");

        // 뜨기 전에 닫힌 것은 아무 훅도 받지 않는다.
        const JBro::PopupHandle thirdHandle =
            editor.OpenPopup(JBro::MakeOwnerPtr<ProbePopup>("third", third));
        editor.ClosePopup(thirdHandle);
        Check(editor.Tick(Frame), "the editor must tick");
        Check(third.enters == 0 && third.exits == 0 && false == editor.IsPopupOpen(thirdHandle),
            "a popup closed before it showed gets no hooks");
        Check(early.enters == 0 && copy.enters == 0, "and the refused ones never ran");

        editor.Shutdown();
    }

    // 아래 프로젝트 파일 테스트 절에 있다. 여기서 먼저 쓴다.
    JBro::String TempPath(const char* name);

    // 저장 대화상자 대신이다. 몇 번 불렸는지 세고, 정해 둔 경로를 준다(빈 경로면 취소).
    struct DialogProbe
    {
        JBro::String path;
        int calls = 0;
        bool save = false;
        JBro::String defaultFileName;

        static bool Answer(const JBro::FileDialogDesc& desc, JBro::String& outPath, void* user)
        {
            DialogProbe& probe = *static_cast<DialogProbe*>(user);
            ++probe.calls;
            probe.save = desc.save;
            probe.defaultFileName = desc.defaultFileName != nullptr ? desc.defaultFileName : "";
            if (probe.path.empty())
            {
                return false;
            }
            outPath = probe.path;
            return true;
        }
    };

    // **저장은 경로를 한 번만 묻고, 그 뒤로는 같은 파일에 쓴다.** 실패는 팝업으로 알리고
    // 취소는 아무것도 남기지 않는다. 대화상자 자체는 사람 없이 닫히지 않으므로 대신하는
    // 함수로 잰다 - 네이티브 대화상자가 뜨는지는 사람이 확인한다(§11.4).
    void TestSavingAsksForAPathOnceAndReportsFailure()
    {
        DialogProbe dialog;
        dialog.path = TempPath("JBroEditorMenuSave.jcanvas");
        std::remove(dialog.path.c_str());

        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        config.fileDialog = &DialogProbe::Answer;
        config.fileDialogUser = &dialog;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; menu save not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "MenuSaveProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* object = canvas->CreateObject("Saved");
        auto* transform = canvas->AttachComponent<JBro::Component::Transform2D>(object);
        transform->position = {1.0f, 2.0f};
        Check(editor.GetCanvasPath().empty(), "a fresh project knows no canvas path");

        // 첫 저장은 경로를 묻는다.
        editor.RequestSaveCanvas();
        Check(editor.Tick(Frame), "the editor must tick through the save");
        Check(dialog.calls == 1 && dialog.save, "the first save must ask for a path with a save dialog");
        Check(dialog.defaultFileName == "Canvas.jcanvas", "and suggest a canvas file name");
        Check(editor.GetCanvasPath() == dialog.path, "and remember the path it was given");
        Check(std::ifstream(dialog.path.c_str()).good(), "and write the file");
        Check(false == editor.GetCommands().IsDirty(), "and count the canvas as saved");

        // 둘째 저장은 묻지 않고 같은 파일에 쓴다.
        transform->position = {7.0f, 8.0f};
        editor.RequestSaveCanvas();
        Check(editor.Tick(Frame), "the editor must tick through the second save");
        Check(dialog.calls == 1, "a canvas with a known path must not ask again");
        {
            JBro::EditorApplication reader;
            JBro::EditorApplicationConfig readerConfig;
            readerConfig.windowVisible = false;
            Check(reader.Initialize(readerConfig), "the reader must initialize");
            Check(reader.OpenProject(project), "the reader must open a project");
            JBro::CanvasFileError error;
            Check(reader.LoadCanvas(dialog.path.c_str(), error), "the reader must load the saved file");
            JBro::GameObject* loaded = nullptr;
            reader.GetCanvas()->ForEachObject([&loaded](JBro::GameObject& found) { loaded = &found; });
            auto* loadedTransform =
                reader.GetCanvas()->FindComponentRaw<JBro::Component::Transform2D>(loaded);
            Check(loadedTransform != nullptr && loadedTransform->position.x == 7.0f,
                "and the second save must have written the newer values");
            Check(reader.GetCanvasPath() == dialog.path, "loading a canvas remembers its path too");
            reader.Shutdown();
        }

        // 닫으면 경로를 잊는다. 새 프로젝트는 다시 묻는다 - 취소하면 아무 일도 없다.
        editor.CloseProject();
        Check(editor.GetCanvasPath().empty(), "closing the project must forget the canvas path");
        Check(editor.OpenProject(project), "the probe project must open again");
        dialog.path.clear();
        editor.RequestSaveCanvas();
        Check(editor.Tick(Frame), "the editor must tick through the cancelled save");
        Check(dialog.calls == 2, "a new project must ask again");
        Check(editor.GetCanvasPath().empty() && false == editor.IsPopupOpenById("save_failed"),
            "and a cancelled dialog must leave no path and no complaint");

        // 쓸 수 없는 경로면 팝업으로 알린다. 다시 실패해도 같은 팝업 하나다.
        dialog.path = "Q:/no/such/folder/Canvas.jcanvas";
        editor.RequestSaveCanvas();
        Check(editor.Tick(Frame), "the editor must tick through the failing save");
        Check(editor.IsPopupOpenById("save_failed"), "a failed save must open the message popup");
        editor.RequestSaveCanvas();
        Check(editor.Tick(Frame), "the editor must tick through the second failing save");
        Check(FindPopupWindow() != nullptr, "and the popup must be on screen");
        int popups = 0;
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
        {
            if (std::strstr(window->Name, "###popup_") != nullptr && window->Active)
            {
                ++popups;
            }
        }
        Check(popups == 1, "with the same id, only one popup for the repeated failure");

        editor.Shutdown();
        std::remove(TempPath("JBroEditorMenuSave.jcanvas").c_str());
    }

    // **게임 뷰는 패널이 보이는 프레임에만 그린다**(D-63). 닫힌 패널 뒤에서 매 프레임 게임을
    // 텍스처에 그릴 이유가 없다. 다시 열면 그 프레임부터 이어진다 - 텍스처는 파기하지 않는다.
    void TestTheGameViewIsRenderedOnlyWhileItsPanelShows()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; game view opt-in not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "GameViewOptInProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* eye = canvas->CreateObject("Eye");
        Check(canvas->AttachComponent<JBro::Component::Transform2D>(eye) != nullptr,
            "the camera needs a transform");
        auto* camera = canvas->AttachComponent<JBro::Component::Camera2D>(eye);
        Check(camera != nullptr, "the probe camera must attach");
        camera->primary = true;
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }
        JBro::Renderer* renderer = editor.GetRenderer();
        Check(renderer != nullptr, "the editor must expose its renderer");
        JBro::RendererFrameStats stats = renderer->GetLastFrameStats();
        Check(stats.viewCount == 1 && stats.skippedViewCount == 0,
            "with the game view panel showing, the camera's view must be recorded");

        JBro::EditorPanel* panel = editor.FindPanel("Game");
        Check(panel != nullptr, "the game view panel must be registered");
        panel->SetOpen(false);
        for (int frame = 0; frame < 2; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must tick with the panel closed");
        }
        stats = renderer->GetLastFrameStats();
        Check(stats.skippedViewCount == 1,
            "with the panel closed the view must be submitted but not recorded");
        Check(editor.GetGameViewTexture().IsValid(),
            "and the texture must be kept so the picture can continue later");

        panel->SetOpen(true);
        for (int frame = 0; frame < 2; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must tick with the panel open again");
        }
        stats = renderer->GetLastFrameStats();
        Check(stats.viewCount == 1 && stats.skippedViewCount == 0,
            "and reopening the panel must record the view again");

        editor.Shutdown();
    }

    // **필드로 말하는 값(`Vec2`)도 커맨드로 고친다**(D-89). 한 줄 숫자 묶음은 코덱이 없어
    // 전 글자를 뜨지 못했고, 뜨지 못하면 커밋을 건너뛰었다 - 위젯이 쓴 값이 그대로 남아
    // 되돌릴 수 없었고 여럿 골라도 주된 것만 움직였다. 회전(실수)만 재서 드러나지 않았다.
    void TestAVectorFieldEditsThroughACommand()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; vector field edits not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "VectorFieldProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");

        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* alpha = canvas->CreateObject("Alpha");
        JBro::GameObject* beta = canvas->CreateObject("Beta");
        auto* a = canvas->AttachComponent<JBro::Component::Transform2D>(alpha);
        auto* b = canvas->AttachComponent<JBro::Component::Transform2D>(beta);
        Check(a != nullptr && b != nullptr, "both must have transforms");
        a->position = JBro::Vec2{0.0f, 0.0f};
        b->position = JBro::Vec2{50.0f, 7.0f};
        JBro::GameObject* chosen[] = {alpha, beta};
        editor.SelectObjects({chosen, 2});
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        const JBro::PropertyTable* table = JBro::PropertyRegistry::Lookup(
            JBro::NameTable::Get().Intern("Component::Transform2D"));
        Check(table != nullptr, "the transform must have registered its properties");
        // `DragScalarN` 은 이름을 쌓고 칸마다 번호를 쌓는다. 첫 칸이 x 다.
        const ImGuiID xField = PushedId(
            InspectorFieldId(0, FieldIndexOf(*table, "position"), "##value"), 0);
        ImGuiWindow* window = ImGui::FindWindowByName("Inspector");
        Check(window != nullptr, "the inspector must have a window");
        Spot spot;
        bool found = false;
        for (float fraction = 0.40f; fraction < 0.95f && false == found; fraction += 0.05f)
        {
            const int x = static_cast<int>(window->Pos.x + window->Size.x * fraction);
            const int bottom = static_cast<int>(window->Pos.y + window->Size.y);
            for (int y = static_cast<int>(window->Pos.y); y < bottom && false == found; y += 3)
            {
                PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                Check(editor.Tick(Frame), "the editor must tick while looking");
                if (ImGui::GetHoveredID() == xField)
                {
                    spot.x = x;
                    spot.y = y;
                    found = true;
                }
            }
        }
        Check(found, "the x field of the position must be in the inspector");

        const std::size_t undo = editor.GetCommands().GetUndoCount();
        DragFrom(editor, hwnd, spot, spot.x + 60);
        const float moved = a->position.x;
        Check(moved > 0.05f, "dragging the x field must move the position");
        Check(editor.GetCommands().GetUndoCount() == undo + 1, "and leave one thing to undo");
        Check(b->position.x > 50.0f + moved - 0.01f && b->position.x < 50.0f + moved + 0.01f,
            "moving the other chosen position by the same amount");
        Check(a->position.y == 0.0f && b->position.y == 7.0f, "leaving y alone");
        Check(editor.GetCommands().Undo(), "undo must run");
        Check(a->position.x == 0.0f && b->position.x == 50.0f, "and put both back");

        editor.Shutdown();
    }

    // 실수 묶음 원소의 한 칸을 끌면 고른 목록마다 그 칸만 같은 양으로 움직인다.
    void TestAPairElementDragsAsADeltaOnEveryChosenList()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; pair element drags not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "PairListProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");

        JBro::RegisterBuiltinProperties<Pointed>();
        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* alpha = canvas->CreateObject("Alpha");
        JBro::GameObject* beta = canvas->CreateObject("Beta");
        auto* a = canvas->AttachComponent<Pointed>(alpha);
        auto* b = canvas->AttachComponent<Pointed>(beta);
        Check(a != nullptr && b != nullptr, "both must hold a list of points");
        a->points.Add(JBro::Vec2{1.0f, 1.0f});
        a->points.Add(JBro::Vec2{100.0f, 100.0f});
        b->points.Add(JBro::Vec2{10.0f, 10.0f});
        b->points.Add(JBro::Vec2{50.0f, 50.0f});
        b->points.Add(JBro::Vec2{0.0f, 0.0f});
        JBro::GameObject* chosen[] = {alpha, beta};
        editor.SelectObjects({chosen, 2});
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        ImGuiWindow* body = FindListBody();
        Check(body != nullptr, "the inspector must draw the list");
        // `DragScalarN` 은 이름을 쌓고 칸마다 번호를 쌓는다. 첫 칸이 x 다.
        const ImGuiID firstField =
            PushedId(LabelId(PushedId(body->ID, 1), "##value"), 0);
        Spot spot;
        bool found = false;
        for (float fraction = 0.25f; fraction < 0.75f && false == found; fraction += 0.05f)
        {
            found = FindListItem(editor, hwnd, firstField,
                static_cast<int>(body->Pos.x + body->Size.x * fraction), spot);
        }
        Check(found, "the x field of the second point must be on the list");
        const std::size_t undo = editor.GetCommands().GetUndoCount();
        DragFrom(editor, hwnd, spot, spot.x + 60);

        const float moved = a->points[1].x - 100.0f;
        Check(moved > 0.05f, "dragging the field must move it");
        // **끈 만큼 움직여야 한다.** 도달한 값 자체를 델타로 삼으면 둘 다 같은 만큼
        // 움직이긴 하지만 백 넘게 튄다.
        Check(moved < 5.0f, "by the distance dragged, not by the value it reached");
        Check(b->points[1].x > 50.0f + moved - 0.01f && b->points[1].x < 50.0f + moved + 0.01f,
            "and move the other chosen list's point by the same amount");
        Check(a->points[1].y == 100.0f && b->points[1].y == 50.0f, "leaving y alone");
        Check(editor.GetCommands().GetUndoCount() == undo + 1, "one drag must be one undo");
        Check(editor.GetCommands().Undo(), "undo must run");
        Check(a->points[1].x == 100.0f && b->points[1].x == 50.0f, "and put both back");

        editor.Shutdown();
    }

    // **인스펙터를 손으로 만져 본다.** 리플렉션이 무엇을 내주는지는 위에서 봤고,
    // 여기서는 인스펙터가 그것을 가지고 무엇을 하는지를 본다 - 고친 것이
    // 커맨드로 들어가는지, 드래그 하나가 되돌리기 하나인지, 못 고치게 표시된
    // 값이 정말로 잠기는지, `Range` 가 슬라이더가 되는지(D-71).
    void TestTheInspectorEditsThroughCommands()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        // 인스펙터 칸이 좁으면 위젯이 라벨에 밀린다. 넉넉한 창을 쓴다.
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; inspector editing not verified"
                << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "InspectorEditProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");

        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");

        JBro::Canvas* canvas = editor.GetCanvas();
        Check(canvas != nullptr, "the probe project must have a canvas");
        JBro::GameObject* object = canvas->CreateObject("Subject");
        auto* transform = canvas->AttachComponent<JBro::Component::Transform2D>(object);
        Check(transform != nullptr, "the subject must have a transform");
        // **물리는 다른 오브젝트에 둔다.** Transform2D 는 필드를 여덟 개 내놓고
        // 그중 다섯이 한 단계 더 내려가서, 한 오브젝트에 둘을 붙이면 아래쪽
        // 컴포넌트가 패널 밖으로 밀려 만질 수 없다.
        JBro::GameObject* heavy = canvas->CreateObject("Heavy");
        auto* body = canvas->AttachComponent<JBro::Component::Rigidbody2D>(heavy);
        Check(body != nullptr, "the heavy object must have a body");
        editor.SetSelectedObject(object);

        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        const JBro::PropertyTable* transformTable = JBro::PropertyRegistry::Lookup(
            JBro::NameTable::Get().Intern("Component::Transform2D"));
        const JBro::PropertyTable* bodyTable = JBro::PropertyRegistry::Lookup(
            JBro::NameTable::Get().Intern("Component::Rigidbody2D"));
        Check(transformTable != nullptr && bodyTable != nullptr,
            "both components must have registered their properties");

        // ── 고치면 커맨드가 된다. 드래그 하나가 되돌리기 하나다. ──────────
        transform->rotation = 0.0f;
        const std::uint32_t rotation = FieldIndexOf(*transformTable, "rotation");
        Spot spot;
        Check(FindInspectorItem(editor, hwnd,
                InspectorFieldId(0, rotation, "##value"), spot),
            "the rotation row must be somewhere in the inspector");
        Check(false == spot.disabled, "and it must be editable");

        const std::size_t before = editor.GetCommands().GetUndoCount();
        DragFrom(editor, hwnd, spot, spot.x + 100);
        Check(transform->rotation > 0.5f,
            "dragging the rotation field must move the value");
        Check(editor.GetCommands().GetUndoCount() == before + 1,
            "and a whole drag must leave exactly one thing to undo");

        const float dragged = transform->rotation;
        Check(editor.GetCommands().Undo(), "undo must run");
        Check(transform->rotation < 0.0001f && transform->rotation > -0.0001f,
            "and put the value back where the drag started");
        Check(editor.GetCommands().Redo() && transform->rotation > 0.5f,
            "redo must do it again");
        Check(dragged > 0.5f, "the dragged value stands");

        // ── 못 고치게 표시된 값은 잠긴다. ────────────────────────────────
        //
        // `world` 는 파생값이라 `ReadOnly` 다. 잠그지 않으면 사용자가 고쳐도
        // 다음 프레임이 덮어써, 고장 난 것처럼 보인다.
        const std::uint32_t world = FieldIndexOf(*transformTable, "world");
        Spot lockedSpot;
        Check(FindInspectorItem(editor, hwnd,
                InspectorFieldId(0, world, "world"), lockedSpot),
            "the cached world matrix must be shown");
        Check(lockedSpot.disabled,
            "a value the inspector may not change must be drawn disabled");

        // ── Range 가 붙은 값은 슬라이더다. ───────────────────────────────
        //
        // 슬라이더는 칸 안의 자리가 곧 값이고 양 끝에서 멈춘다. 자유 드래그는
        // 픽셀당 0.01 씩 움직일 뿐이라, 같은 거리를 끌어도 근처에도 못 간다.
        body->mass = 1.0f;
        editor.SetSelectedObject(heavy);
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the inspector must switch over");
        }
        const std::uint32_t mass = FieldIndexOf(*bodyTable, "mass");
        Spot massSpot;
        Check(FindInspectorItem(editor, hwnd, InspectorFieldId(0, mass, "##value"), massSpot),
            "the mass row must be in the inspector");
        DragFrom(editor, hwnd, massSpot, 1020);
        Check(body->mass > 900.0f,
            "a field with a Range must be a slider that reaches its top");
        Check(body->mass <= 1000.0f, "and must stop there");

        // 인스펙터가 가장 많이 보이는 자리다. 여기서 한 장 남긴다.
        editor.SetSelectedObject(object);
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle before the shot");
        }
        if (JBro::Renderer* renderer = editor.GetRenderer())
        {
            SaveScreenshot(*renderer, 1024, 768, "inspector");
        }

        editor.Shutdown();
    }

    // **아무것도 안 바뀌었으면 되돌릴 것도 없다.** 글자 칸에서 Enter 만 치면
    // 위젯은 "바뀌었다" 고 답하지만 값은 그대로다 - 그것까지 쌓으면 Ctrl+Z 가
    // 아무 일도 안 하는 헛걸음을 만든다.
    void TestTypingTheSameValueLeavesNothingToUndo()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the text field not verified"
                << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "InspectorTextProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");

        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");

        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* object = canvas->CreateObject("Subject");
        // `spriteId` 는 AssetId 다 - float 도 bool 도 int 도 아니라서 코덱의
        // 글자 왕복으로 그려지는, 지금 유일한 글자 칸이다.
        auto* sprite = canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(object);
        Check(sprite != nullptr, "the subject must have a sprite renderer");
        editor.SetSelectedObject(object);

        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        const JBro::PropertyTable* table = JBro::PropertyRegistry::Lookup(
            JBro::NameTable::Get().Intern("Component::SpriteRenderer2D"));
        Check(table != nullptr, "the sprite renderer must have registered its properties");
        const std::uint32_t spriteId = FieldIndexOf(*table, "spriteId");

        Spot spot;
        Check(FindInspectorItem(editor, hwnd,
                InspectorFieldId(0, spriteId, "##value"), spot),
            "the spriteId row must be in the inspector");

        // 칸을 깨우고 아무것도 고치지 않은 채 Enter 를 친다.
        ClickAt(editor, hwnd, spot);
        const std::size_t before = editor.GetCommands().GetUndoCount();
        PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
        Check(editor.Tick(Frame), "the editor must tick");
        PostMessageW(hwnd, WM_KEYUP, VK_RETURN, 0);
        Check(editor.Tick(Frame), "the editor must tick");

        Check(editor.GetCommands().GetUndoCount() == before,
            "committing the value that was already there must leave nothing to undo");
        Check(false == editor.GetCommands().IsDirty()
                || editor.GetCommands().GetUndoCount() == before,
            "and must not make the document look edited");

        editor.Shutdown();
    }

    // 프레임마다 **처음 보는 글자**를 그리는 패널이다. ImGui 1.92 는 글리프를
    // 필요할 때 아틀라스에 굽고 백엔드에 "이 텍스처를 고쳐 올려라" 라고 말하므로,
    // 이 패널이 도는 동안에는 프레임마다 텍스처 업로드가 일어난다.
    class NewGlyphEveryFrame final : public JBro::EditorPanel
    {
    public:
        const char* GetTitle() const override
        {
            return "New Glyph";
        }
        void OnDraw() override
        {
            // 한글 음절은 만 개가 넘는다. 매번 다른 것을 고르면 아틀라스가
            // 계속 자란다 - ASCII 는 이미 구워져 있어서 이 길을 열지 못한다.
            const int syllable = 0xAC00 + (m_frame * 37) % 11172;
            ++m_frame;
            char utf8[4] = {};
            utf8[0] = static_cast<char>(0xE0 | (syllable >> 12));
            utf8[1] = static_cast<char>(0x80 | ((syllable >> 6) & 0x3F));
            utf8[2] = static_cast<char>(0x80 | (syllable & 0x3F));
            ImGui::TextUnformatted(utf8);
        }

    private:
        int m_frame = 0;
    };

    // **글꼴 아틀라스가 갱신되어도 디바이스가 살아 있어야 한다.**
    //
    // `WriteTexture` 는 프레임 밖에서 도는 길이라 제 명령 할당자를 되감는데,
    // 그 할당자를 아직 GPU 가 읽고 있으면 디바이스가 통째로 날아간다
    // (`DXGI_ERROR_INVALID_CALL`). **검증 레이어는 아무 말도 하지 않는다.**
    //
    // 실제로 이렇게 죽었다: 인스펙터를 훑다 처음 보는 글자가 나오는 순간.
    // 한가할 때는 멀쩡하고 프레임이 밀려 있을 때만 죽어서, 몇 프레임 돌려 보는
    // 테스트로는 잡히지 않는다 - 갱신을 **계속** 시켜야 나온다.
    void TestTheDeviceSurvivesFontAtlasUpdates()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; atlas updates not verified"
                << std::endl;
            return;
        }
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        Check(editor.AddPanel(JBro::MakeOwnerPtr<NewGlyphEveryFrame>()),
            "the glyph panel must be taken");

        for (int frame = 0; frame < 300; ++frame)
        {
            if (false == editor.Tick(1.0f / 60.0f))
            {
                std::cout << "  the editor died on frame " << frame
                    << " with status " << static_cast<int>(editor.GetLastFrameStatus())
                    << std::endl;
                Check(false, "a font atlas update must not take the device down");
            }
        }
        editor.Shutdown();
    }

    // **여럿 고르기**(기존 엔진 `Editor::SelectEntities` 계열과 같은 모양).
    // 목록과 주된 하나를 따로 든다 - 인스펙터는 주된 것을 보여 주고 편집은
    // 목록 전체에 미치므로, 목록에서 하나 뺐다고 인스펙터가 딴 것을 보여 주면
    // 손이 미끄러진 것처럼 보인다.
    void TestSelectingSeveralObjects()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; selection not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "SelectionProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");

        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* first = canvas->CreateObject("First");
        JBro::GameObject* second = canvas->CreateObject("Second");
        JBro::GameObject* third = canvas->CreateObject("Third");

        Check(editor.GetSelectionCount() == 0, "nothing is chosen yet");
        Check(editor.GetSelectedObject() == nullptr, "so there is no main one");
        Check(false == editor.IsSelected(first), "and nothing answers to being chosen");
        Check(false == editor.IsSelected(nullptr), "nothing at all is not chosen");

        // 맨 클릭은 통째로 바꾼다.
        editor.SetSelectedObject(first);
        Check(editor.GetSelectionCount() == 1, "one click chooses one");
        Check(editor.IsSelected(first), "that one");
        Check(editor.GetSelectedObject() == first, "and it is the main one");

        // Ctrl 클릭은 붙인다. 주된 것은 그대로다.
        editor.AddToSelection(second);
        editor.AddToSelection(third);
        Check(editor.GetSelectionCount() == 3, "adding puts them alongside");
        Check(editor.GetSelectedObject() == first,
            "and must not move the main one out from under the inspector");
        editor.AddToSelection(second);
        Check(editor.GetSelectionCount() == 3, "adding the same one twice changes nothing");

        // 뺄 때 목록의 순서를 지킨다.
        editor.RemoveFromSelection(second);
        Check(editor.GetSelectionCount() == 2, "removing takes one away");
        Check(false == editor.IsSelected(second), "that one");
        Check(editor.IsSelected(first) && editor.IsSelected(third), "and leaves the rest");

        // 주된 것을 빼면 남은 것의 머리가 그 자리를 받는다.
        editor.RemoveFromSelection(first);
        Check(editor.GetSelectedObject() == third,
            "removing the main one must hand the job to what is left");

        JBro::GameObject* objects[] = {second, third, first};
        editor.SelectObjects({objects, 3});
        Check(editor.GetSelectionCount() == 3, "choosing a list chooses all of it");
        Check(editor.GetSelectedObject() == second, "and the head of the list leads");

        editor.ClearSelection();
        Check(editor.GetSelectionCount() == 0, "clearing empties it");
        Check(editor.GetSelectedObject() == nullptr, "main one included");

        // **사라진 것은 목록에 있어도 없는 것이다.**
        editor.SelectObjects({objects, 3});
        Check(canvas->DestroyObject(second), "the main one must be destroyable");
        canvas->FlushPendingDestroy();
        Check(editor.GetSelectionCount() == 2, "a destroyed object stops counting");
        Check(editor.GetSelectedObject() == nullptr,
            "and if it was the main one, there is no main one");
        const JBro::Array<JBro::GameObject*> living = editor.GetSelectedObjects();
        Check(living.Size() == 2, "only the living come back");
        for (std::size_t index = 0; index < living.Size(); ++index)
        {
            Check(living[index] != nullptr, "and none of them is nothing");
        }

        editor.Shutdown();
    }

    // **조상이 함께 골라졌으면 뺀다.** 부모를 옮기면 자식은 따라 움직이므로,
    // 둘 다 대상으로 삼으면 자식에게 두 번 적용된다(기존 `GetSelectedTopLevel`).
    void TestAChosenChildUnderAChosenParentIsNotItsOwnTarget()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; top level not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "TopLevelProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");

        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* root = canvas->CreateObject("Root");
        JBro::GameObject* child = canvas->CreateObject("Child");
        JBro::GameObject* grandchild = canvas->CreateObject("Grandchild");
        JBro::GameObject* stranger = canvas->CreateObject("Stranger");
        child->SetParent(root);
        grandchild->SetParent(child);

        // 자식만 골랐으면 자식이 최상위다.
        editor.SetSelectedObject(child);
        JBro::Array<JBro::GameObject*> tops = editor.GetTopLevelSelectedObjects();
        Check(tops.Size() == 1 && tops[0] == child,
            "a child on its own is the top of its own selection");

        // 부모까지 고르면 자식은 빠진다.
        editor.AddToSelection(root);
        tops = editor.GetTopLevelSelectedObjects();
        Check(tops.Size() == 1 && tops[0] == root,
            "with the parent chosen too, only the parent is a target");

        // 손자까지 골라도 마찬가지다 - 한 단계가 아니라 조상 전체를 본다.
        editor.AddToSelection(grandchild);
        tops = editor.GetTopLevelSelectedObjects();
        Check(tops.Size() == 1 && tops[0] == root,
            "a grandchild is covered by its grandparent, not just its parent");

        // 남남은 따로 선다.
        editor.AddToSelection(stranger);
        tops = editor.GetTopLevelSelectedObjects();
        Check(tops.Size() == 2, "an unrelated object stands on its own");
        const bool hasRoot = tops[0] == root || tops[1] == root;
        const bool hasStranger = tops[0] == stranger || tops[1] == stranger;
        Check(hasRoot && hasStranger, "and both of those are the targets");

        // 부모를 선택에서 빼면 자식이 다시 최상위가 된다.
        editor.RemoveFromSelection(root);
        tops = editor.GetTopLevelSelectedObjects();
        Check(tops.Size() == 2, "dropping the parent puts the child back in charge");
        const bool hasChild = tops[0] == child || tops[1] == child;
        Check(hasChild, "the child is a target again");

        editor.Shutdown();
    }

    // **여럿을 골라 놓고 하나를 고치면 전부에 미치고, 되돌리기는 하나다.**
    //
    // 그리고 숫자는 **델타로** 간다. 위치가 저마다 다른 셋을 골라 x 를 끌었을 때
    // 셋이 한 자리로 모이면 그것은 옮긴 것이 아니라 뭉갠 것이다 - 기존 엔진이
    // 트랜스폼 편집을 델타로 다루는 이유다(D-83).
    void TestEditingWithSeveralChosenReachesThemAll()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; multi edit not verified"
                << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "MultiEditProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");

        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");

        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* alpha = canvas->CreateObject("Alpha");
        JBro::GameObject* beta = canvas->CreateObject("Beta");
        JBro::GameObject* gamma = canvas->CreateObject("Gamma");
        auto* alphaTransform =
            canvas->AttachComponent<JBro::Component::Transform2D>(alpha);
        auto* betaTransform =
            canvas->AttachComponent<JBro::Component::Transform2D>(beta);
        auto* gammaTransform =
            canvas->AttachComponent<JBro::Component::Transform2D>(gamma);
        Check(alphaTransform != nullptr && betaTransform != nullptr
            && gammaTransform != nullptr, "all three must have transforms");

        // **셋의 회전이 저마다 다르다.** 같으면 델타와 절대값을 구분할 수 없다.
        alphaTransform->rotation = 0.0f;
        betaTransform->rotation = 10.0f;
        gammaTransform->rotation = 20.0f;

        JBro::GameObject* chosen[] = {alpha, beta, gamma};
        editor.SelectObjects({chosen, 3});
        Check(editor.GetSelectionCount() == 3, "three must be chosen");
        Check(editor.GetSelectedObject() == alpha, "and the first leads");

        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        const JBro::PropertyTable* table = JBro::PropertyRegistry::Lookup(
            JBro::NameTable::Get().Intern("Component::Transform2D"));
        Check(table != nullptr, "the transform must have registered its properties");
        const std::uint32_t rotation = FieldIndexOf(*table, "rotation");

        Spot spot;
        Check(FindInspectorItem(editor, hwnd,
                InspectorFieldId(0, rotation, "##value"), spot),
            "the rotation row must be in the inspector");

        const std::size_t before = editor.GetCommands().GetUndoCount();
        DragFrom(editor, hwnd, spot, spot.x + 100);

        const float moved = alphaTransform->rotation;
        Check(moved > 0.5f, "the one the inspector shows must move");
        Check(editor.GetCommands().GetUndoCount() == before + 1,
            "and the whole drag over three objects must leave one thing to undo");

        // **같은 델타가 셋 모두에.** 각자의 시작값에서 같은 만큼 움직인다.
        Check(betaTransform->rotation > 10.0f + moved - 0.01f
                && betaTransform->rotation < 10.0f + moved + 0.01f,
            "the second must move by the same amount from where it was");
        Check(gammaTransform->rotation > 20.0f + moved - 0.01f
                && gammaTransform->rotation < 20.0f + moved + 0.01f,
            "and so must the third");
        Check(betaTransform->rotation > 10.0f,
            "they must not be flattened onto the value of the first");

        Check(editor.GetCommands().Undo(), "one undo must run");
        Check(alphaTransform->rotation < 0.01f && alphaTransform->rotation > -0.01f,
            "and put the first back");
        Check(betaTransform->rotation > 9.99f && betaTransform->rotation < 10.01f,
            "the second back to its own value");
        Check(gammaTransform->rotation > 19.99f && gammaTransform->rotation < 20.01f,
            "and the third to its own");

        Check(editor.GetCommands().Redo(), "redo must run");
        Check(betaTransform->rotation > 10.0f + moved - 0.01f,
            "and move them all again");

        editor.Shutdown();
    }

    // **조상이 함께 골라졌으면 자식은 빠진다.** 부모를 옮기면 자식은 따라
    // 움직이므로 둘 다 대상으로 삼으면 자식에게 두 번 적용된다.
    void TestAChosenChildDoesNotGetTheEditTwice()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; top level edit not verified"
                << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "TopLevelEditProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");

        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");

        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* parent = canvas->CreateObject("Parent");
        JBro::GameObject* child = canvas->CreateObject("Child");
        child->SetParent(parent);
        auto* parentTransform =
            canvas->AttachComponent<JBro::Component::Transform2D>(parent);
        auto* childTransform =
            canvas->AttachComponent<JBro::Component::Transform2D>(child);
        parentTransform->rotation = 0.0f;
        childTransform->rotation = 100.0f;

        JBro::GameObject* chosen[] = {parent, child};
        editor.SelectObjects({chosen, 2});
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        const JBro::PropertyTable* table = JBro::PropertyRegistry::Lookup(
            JBro::NameTable::Get().Intern("Component::Transform2D"));
        const std::uint32_t rotation = FieldIndexOf(*table, "rotation");
        Spot spot;
        Check(FindInspectorItem(editor, hwnd,
                InspectorFieldId(0, rotation, "##value"), spot),
            "the rotation row must be in the inspector");

        DragFrom(editor, hwnd, spot, spot.x + 100);

        Check(parentTransform->rotation > 0.5f, "the parent must move");
        // 자식의 **자기 회전**은 그대로여야 한다. 월드에서는 부모를 따라 돈다.
        Check(childTransform->rotation > 99.99f && childTransform->rotation < 100.01f,
            "the child must not be turned a second time on its own account");

        editor.Shutdown();
    }

    // **같은 타입이 둘 붙어 있으면 같은 번째끼리 고쳐야 한다.**
    //
    // 주된 오브젝트의 둘째 콜라이더를 고치는 중이라면 다른 오브젝트에서도
    // 둘째여야 한다. 번째를 무시하고 언제나 첫째를 잡으면, 화면에서 만진 것과
    // 실제로 바뀐 것이 어긋난다.
    void TestMultiEditPicksTheSameOrdinalEverywhere()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; ordinal matching not verified"
                << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "OrdinalProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");

        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");

        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* alpha = canvas->CreateObject("Alpha");
        JBro::GameObject* beta = canvas->CreateObject("Beta");

        // 둘 다 콜라이더를 두 개씩 단다. 반지름을 전부 다르게 두어야
        // 무엇이 바뀌었는지 가려낼 수 있다.
        auto* alphaFirst = canvas->AttachComponent<JBro::Component::Collider2D>(alpha);
        auto* alphaSecond = canvas->AttachComponent<JBro::Component::Collider2D>(alpha);
        auto* betaFirst = canvas->AttachComponent<JBro::Component::Collider2D>(beta);
        auto* betaSecond = canvas->AttachComponent<JBro::Component::Collider2D>(beta);
        Check(alphaFirst != nullptr && alphaSecond != nullptr
            && betaFirst != nullptr && betaSecond != nullptr,
            "two of a kind must attach to each");
        alphaFirst->radius = 1.0f;
        alphaSecond->radius = 2.0f;
        betaFirst->radius = 3.0f;
        betaSecond->radius = 4.0f;

        JBro::GameObject* chosen[] = {alpha, beta};
        editor.SelectObjects({chosen, 2});
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        const JBro::PropertyTable* table = JBro::PropertyRegistry::Lookup(
            JBro::NameTable::Get().Intern("Component::Collider2D"));
        Check(table != nullptr, "the collider must have registered its properties");
        const std::uint32_t radius = FieldIndexOf(*table, "radius");

        // **둘째** 콜라이더의 반지름 칸을 찾는다. 슬롯 1 이다.
        Spot spot;
        Check(FindInspectorItem(editor, hwnd,
                InspectorFieldId(1, radius, "##value"), spot),
            "the second collider's radius row must be in the inspector");

        DragFrom(editor, hwnd, spot, spot.x + 60);

        const float moved = alphaSecond->radius - 2.0f;
        Check(moved > 0.05f, "the second collider of the shown object must move");
        Check(betaSecond->radius > 4.0f + moved - 0.01f
                && betaSecond->radius < 4.0f + moved + 0.01f,
            "and so must the second collider of the other object");
        // **첫째는 건드리지 않는다.**
        Check(alphaFirst->radius > 0.99f && alphaFirst->radius < 1.01f,
            "the first collider here must be untouched");
        Check(betaFirst->radius > 2.99f && betaFirst->radius < 3.01f,
            "and the first collider there must be untouched too");

        editor.Shutdown();
    }

    // **인스펙터는 타입을 하나도 모른다.** 리플렉션이 내주는 것만 보고 그린다 -
    // 그래서 리플렉션이 내주는 것이 맞아야 화면도 맞는다. 파생값을 고칠 수 있게
    // 그려 놓으면 사용자가 고쳐도 다음 프레임에 덮어써져, 고장 난 것처럼 보인다.
    void TestTheInspectorIsToldWhatItMayEdit()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the inspector not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "InspectorProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");

        // 패널이 넷 다 있어야 한다. 하나라도 안 붙으면 화면에서 빈 칸이 된다.
        Check(editor.GetPanelCount() == 4, "the four default panels must be registered");
        Check(editor.FindPanel("Game") != nullptr, "the game view must be one of them");
        Check(editor.FindPanel("Hierarchy") != nullptr, "and the hierarchy");
        Check(editor.FindPanel("Inspector") != nullptr, "and the inspector");
        Check(editor.FindPanel("Stats") != nullptr, "and the stats");
        Check(editor.FindPanel("Nothing") == nullptr, "and nothing else");

        JBro::Canvas* canvas = editor.GetCanvas();
        Check(canvas != nullptr, "the probe project must have a canvas");
        JBro::GameObject* object = canvas->CreateObject("Subject");
        auto* transform = canvas->AttachComponent<JBro::Component::Transform2D>(object);
        Check(transform != nullptr, "the subject must have a transform");

        // 고른 것을 인스펙터가 받는다.
        Check(editor.GetSelectedObject() == nullptr, "nothing is selected yet");
        editor.SetSelectedObject(object);
        Check(editor.GetSelectedObject() == object, "what was chosen must be what is shown");

        // 리플렉션이 편집 가능 여부를 말해 주는지. 인스펙터는 이 값 하나로 칸을 잠근다.
        const JBro::PropertyTable* table = JBro::PropertyRegistry::Lookup(
            JBro::NameTable::Get().Intern("Component::Transform2D"));
        Check(table != nullptr, "the transform must have registered its properties");

        const JBro::PropertyInfo* position = FindProperty(*table, "position");
        Check(position != nullptr, "position must be one of them");
        Check(position->serialize, "position is saved");
        Check(position->edit == nullptr || position->edit->editable,
            "and the inspector may change it");

        const JBro::PropertyInfo* world = FindProperty(*table, "world");
        Check(world != nullptr, "the cached world matrix must be one of them");
        Check(false == world->serialize, "it is derived, so it is not saved");
        Check(world->edit != nullptr && false == world->edit->editable,
            "and the inspector must not offer to change it - the next frame overwrites it");

        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(1.0f / 60.0f), "the editor must tick with a selection");
        }

        // **고른 것이 사라지면 선택도 사라져야 한다.** 스크립트가 지운 오브젝트를
        // 인스펙터가 계속 읽으면 죽은 주소를 읽는다.
        Check(canvas->DestroyObject(object), "the subject must be destroyable");
        canvas->FlushPendingDestroy();
        Check(editor.GetSelectedObject() == nullptr,
            "a selection that was destroyed must clear itself");
        Check(editor.Tick(1.0f / 60.0f), "and the editor must keep going");

        editor.Shutdown();
    }

    JBro::GameObject* FindByName(JBro::Canvas& canvas, const char* name)
    {
        JBro::GameObject* found = nullptr;
        canvas.ForEachObject([&](JBro::GameObject& object) {
            if (found == nullptr && std::strcmp(object.GetTag(), name) == 0)
            {
                found = &object;
            }
        });
        return found;
    }

    // **지운 것을 되돌리면 값까지 돌아와야 한다.** 오브젝트만 되살리고 컴포넌트의
    // 값이 기본값으로 돌아오면, 되돌린 것이 아니라 비슷한 것을 새로 만든 것이다.
    void TestDeletingAnObjectCanBeUndoneWithItsValues()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; object commands not verified"
                << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "ObjectCommandProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");

        JBro::Canvas* canvas = editor.GetCanvas();
        Check(canvas != nullptr, "the probe project must have a canvas");

        // 부모와 자식을 만든다. 지우면 나무가 통째로 없어져야 한다.
        JBro::GameObject* parent = canvas->CreateObject("Parent");
        auto* parentTransform =
            canvas->AttachComponent<JBro::Component::Transform2D>(parent);
        parentTransform->position = {3.5f, -1.25f};
        parentTransform->rotation = 0.75f;

        JBro::GameObject* child = canvas->CreateObject("Child");
        child->SetParent(parent);
        auto* childSprite =
            canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(child);
        childSprite->tint = {0.25f, 0.5f, 0.75f, 1.0f};
        childSprite->renderOrder = 17;

        const std::size_t before = canvas->GetObjectCount();

        JBro::EditorObjectRegistry& ids = editor.GetObjectIds();
        const JBro::EditorObjectId parentId = ids.Track(parent);
        Check(parentId != JBro::InvalidEditorObjectId, "the parent must get a number");

        Check(editor.GetCommands().Execute(
                JBro::MakeOwnerPtr<JBro::DeleteObjectCommand>(*canvas, ids, parent)),
            "deleting must go through");
        Check(canvas->GetObjectCount() == before - 2,
            "the object and its child must both be gone");
        Check(ids.Resolve(parentId) == nullptr, "and the number must resolve to nothing");

        Check(editor.GetCommands().Undo(), "undo must run");
        Check(canvas->GetObjectCount() == before, "and bring the tree back");

        // **같은 번호로 돌아와야 한다.** 그러지 않으면 그 번호를 들고 있는
        // 커맨드들이 되살아난 오브젝트를 못 찾는다.
        JBro::GameObject* restored = ids.Resolve(parentId);
        Check(restored != nullptr, "the old number must find the restored object");
        Check(std::strcmp(restored->GetTag(), "Parent") == 0, "with its name");

        auto* restoredTransform = restored->GetComponent<JBro::Component::Transform2D>().Get();
        Check(restoredTransform != nullptr, "and its transform");
        Check(restoredTransform->position.x > 3.49f && restoredTransform->position.x < 3.51f,
            "with the position it had");
        Check(restoredTransform->rotation > 0.74f && restoredTransform->rotation < 0.76f,
            "and the rotation");

        JBro::GameObject* restoredChild = FindByName(*canvas, "Child");
        Check(restoredChild != nullptr, "the child must be back too");
        Check(restoredChild->GetParent() == restored, "under the same parent");
        auto* restoredSprite =
            restoredChild->GetComponent<JBro::Component::SpriteRenderer2D>().Get();
        Check(restoredSprite != nullptr, "with its sprite renderer");
        Check(restoredSprite->renderOrder == 17, "and the values it had");
        Check(restoredSprite->tint.G > 0.49f && restoredSprite->tint.G < 0.51f,
            "including the ones inside a struct");

        // 다시 지우고 다시 되살려도 같아야 한다.
        Check(editor.GetCommands().Redo(), "redo must run");
        Check(canvas->GetObjectCount() == before - 2, "and take the tree away again");
        Check(editor.GetCommands().Undo(), "and undo once more");
        Check(ids.Resolve(parentId) != nullptr, "the number must still find it");

        editor.Shutdown();
    }

    // 만들기도 되돌릴 수 있어야 하고, 다시 하면 **같은 번호**로 돌아와야 한다.
    void TestCreatingAnObjectCanBeUndone()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; create not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "CreateProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");

        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::EditorObjectRegistry& ids = editor.GetObjectIds();
        const std::size_t before = canvas->GetObjectCount();

        auto command = JBro::MakeOwnerPtr<JBro::CreateObjectCommand>(
            *canvas, ids, "Fresh", JBro::InvalidEditorObjectId);
        JBro::CreateObjectCommand* raw = command.Get();
        Check(editor.GetCommands().Execute(std::move(command)), "creating must go through");
        Check(canvas->GetObjectCount() == before + 1, "and add one object");
        const JBro::EditorObjectId id = raw->GetObjectId();
        Check(ids.Resolve(id) != nullptr, "which the number finds");

        Check(editor.GetCommands().Undo(), "undo must run");
        Check(canvas->GetObjectCount() == before, "and take it away");
        Check(ids.Resolve(id) == nullptr, "leaving the number pointing at nothing");

        Check(editor.GetCommands().Redo(), "redo must run");
        Check(canvas->GetObjectCount() == before + 1, "and put it back");
        Check(ids.Resolve(id) != nullptr, "under the same number as before");

        editor.Shutdown();
    }

    // **엔진은 창이 닫히면 그 프레임 안에서 스스로 정리한다 - 디바이스까지.**
    // 에디터 UI 가 그 디바이스로 만든 것들을 뒤늦게 해제하려 들면 그 자리에서
    // 터진다. 실제로 터졌다. 창을 닫는 것은 예외 경로가 아니라 보통 경로다.
    void TestClosingTheWindowDoesNotTakeTheUiDownWithIt()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the close path not verified"
                << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "EditorCloseProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        Check(editor.Tick(1.0f / 60.0f), "the editor must tick with its UI on");

        // 에디터가 만든 창이다. 제목은 EditorApplication 이 정한다.
        HWND window = FindOwnEditorWindow();
        Check(window != nullptr, "the editor window must be findable");
        Check(PostMessageW(window, WM_CLOSE, 0, 0) != 0, "the close must post");

        Check(false == editor.Tick(1.0f / 60.0f),
            "a closed window must stop the editor");
        Check(false == editor.IsEditorUiEnabled(),
            "and the UI must have let go of a device that is already gone");
        Check(false == editor.GetGameViewTexture().IsValid(),
            "including its game view texture");
        // 두 번 놓아도 안전해야 한다.
        editor.Shutdown();
    }

    // 플랫폼이 모은 입력이 에디터를 지나 UI 까지 가는지 본다. 모으기만 하고
    // 넘기지 않으면 창은 멀쩡히 그려지는데 아무것도 눌리지 않는다.
    void TestTheEditorForwardsInputToItsUi()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; editor input not verified"
                << std::endl;
            return;
        }
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        Check(false == editor.UiWantsMouse(), "nothing has been pointed at yet");

        HWND window = FindOwnEditorWindow();
        Check(window != nullptr, "the editor window must be findable");

        // 창을 가득 채운 패널 한가운데를 가리킨다. ImGui 는 지난 프레임에 무엇
        // 위에 있었는지로 이번 프레임의 가져감을 정하므로 몇 프레임 돌린다.
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(PostMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(WindowWidth / 2, WindowHeight / 2)) != 0,
                "the pointer must post");
            Check(editor.Tick(1.0f / 60.0f), "the editor must tick");
        }
        Check(editor.UiWantsMouse(),
            "a pointer over the editor panel must be taken by the UI");

        editor.Shutdown();
    }

    // **메뉴바가 읽은 로케일로 말하는가**(ProjectRule §11.2).
    //
    // 키와 번역이 다 있고 "키가 표에 있는가" 도 재고 있었는데, 메뉴바는 그 키를
    // 한 번도 부르지 않고 영어를 소스에 박아 두고 있었다 - 표를 재는 테스트는
    // 화면이 표를 쓰는지를 묻지 않는다.
    //
    // 글자는 되읽을 수 없지만 ImGui 는 메뉴의 Id 를 **보이는 이름으로** 센다.
    // 메뉴바 줄을 훑어 번역된 이름의 Id 가 가리켜지는지 본다.
    void TestTheMenuBarSpeaksTheLoadedLocale()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the menu bar locale not verified"
                << std::endl;
            return;
        }

        // 영어로 떨어졌으면 이 검사는 박힌 영어와 가려내지 못한다. 그때는 건너뛴다고
        // 말한다 - 조용히 통과하면 무엇도 재지 않은 것이다.
        const char* const keys[] = {
            JBro::LocKeys::MenuFile, JBro::LocKeys::MenuEdit, JBro::LocKeys::MenuWindow};
        const char* const english[] = {"File", "Edit", "Window"};
        const char* labels[3] = {};
        for (int index = 0; index < 3; ++index)
        {
            labels[index] = JBro::Loc::Text(keys[index]);
            if (std::strcmp(labels[index], english[index]) == 0
                || std::strcmp(labels[index], keys[index]) == 0)
            {
                std::cout << "  [skip] the menu names are not translated here; "
                    "the menu bar locale not verified" << std::endl;
                editor.Shutdown();
                return;
            }
        }

        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        HWND window = FindOwnEditorWindow();
        Check(window != nullptr, "the editor window must be findable");
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must tick");
        }

        ImGuiWindow* root = ImGui::FindWindowByName("##EditorRoot");
        Check(root != nullptr, "the editor must have its root window");
        // `BeginMenuBar` 가 `PushID("##MenuBar")` 를 하고, 메뉴는 그 아래에서
        // 제 이름으로 Id 를 받는다.
        const ImGuiID bar = LabelId(root->ID, "##MenuBar");
        bool found[3] = {};
        const ImRect rect = root->MenuBarRect();
        const int y = static_cast<int>(rect.GetCenter().y);
        for (int x = static_cast<int>(rect.Min.x);
            x < static_cast<int>(rect.Max.x) && x < 400; x += 4)
        {
            PostMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
            Check(editor.Tick(Frame), "the editor must tick while looking");
            for (int index = 0; index < 3; ++index)
            {
                if (ImGui::GetHoveredID() == LabelId(bar, labels[index]))
                {
                    found[index] = true;
                }
            }
        }
        Check(found[0], "the File menu must be named in the loaded locale");
        Check(found[1], "and so must the Edit menu");
        Check(found[2], "and the Window menu");

        editor.Shutdown();
    }

    // **프로젝트가 없어도 에디터 창은 살아 있어야 한다.** 호스트는 프레임워크가
    // 없으면 그릴 것이 없다고 보고 프레임을 통째로 건너뛰는데, 그러면 프로젝트를
    // 닫아 둔 에디터가 검은 창이 된다 - 메뉴도 프로젝트 브라우저도 그때 필요하다.
    void TestTheEditorDrawsWithNoProjectOpen()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the empty editor not verified"
                << std::endl;
            return;
        }
        Check(false == editor.HasOpenProject(), "this editor has no project");
        Check(editor.EnableEditorUi({64, 48}),
            "the UI must start before any project is opened");

        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(1.0f / 60.0f), "the empty editor must keep ticking");
        }

        JBro::Renderer* renderer = editor.GetRenderer();
        Check(renderer != nullptr, "the editor must expose its renderer");
        const std::size_t painted = CountPaintedPixels(*renderer, WindowWidth, WindowHeight);
        std::cout << "  the empty editor painted " << painted << " pixels" << std::endl;
        Check(painted > (WindowWidth * WindowHeight) / 2,
            "the panel must be on the window even with no project open");

        editor.Shutdown();
    }

    // **에디터 화면이 실제로 나오는가.** 게임은 텍스처로 가고 백버퍼에는 UI 만 남는다 -
    // 그 프레임은 "게임이 낼 것이 없는" 프레임이기도 해서, 배선이 하나라도 어긋나면
    // 화면이 통째로 검게 남는다. 픽셀을 되읽지 않으면 알 수 없다(D-63).
    void TestTheEditorPaintsItsOwnScreen()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the editor screen not verified"
                << std::endl;
            return;
        }

        JBro::ProjectDescriptor project;
        constexpr char name[] = "EditorScreenProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");

        Check(false == editor.IsEditorUiEnabled(), "the UI starts off");
        Check(false == editor.EnableEditorUi({0, 0}), "a game view with no size is refused");

        // 게임 뷰는 창과 다른 크기다. 비율이 다르면 패널 안에서 레터박스가 된다.
        constexpr std::uint32_t GameWidth = 64;
        constexpr std::uint32_t GameHeight = 48;
        Check(editor.EnableEditorUi({GameWidth, GameHeight}), "the editor UI must turn on");
        Check(editor.IsEditorUiEnabled(), "and say so");
        Check(editor.GetGameViewTexture().IsValid(), "with a game view to draw into");
        Check(false == editor.EnableEditorUi({GameWidth, GameHeight}),
            "turning it on twice must be refused");

        // **카메라를 하나 놓는다.** 그래야 게임이 텍스처에 실제로 무언가를 그리고,
        // 그 픽셀이 패널까지 오는지 볼 수 있다. 카메라가 없으면 텍스처는 한 번도
        // 그려지지 않은 채 패널에 붙고, 그래도 화면은 그럴듯하게 나온다.
        JBro::Canvas* canvas = editor.GetCanvas();
        Check(canvas != nullptr, "the probe project must have a canvas");
        JBro::GameObject* eye = canvas->CreateObject("Eye");
        Check(canvas->AttachComponent<JBro::Component::Transform2D>(eye) != nullptr,
            "the camera needs a transform");
        auto* camera = canvas->AttachComponent<JBro::Component::Camera2D>(eye);
        Check(camera != nullptr, "the probe camera must attach");
        camera->primary = true;
        // 창의 어느 색과도 겹치지 않는 색이다. 이 색이 화면에 있으면 게임 화면이
        // 텍스처를 거쳐 패널까지 온 것이다.
        camera->clearColor = {0.0f, 0.85f, 0.35f, 1.0f};

        // 새 창은 ImGui 가 크기를 재는 동안 감춰진다. 몇 프레임 돌린 뒤에 본다.
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(1.0f / 60.0f), "the editor must keep ticking with its UI on");
        }

        JBro::Renderer* renderer = editor.GetRenderer();
        Check(renderer != nullptr, "the editor must expose its renderer");
        JBro::Array<std::byte> image;
        image.Resize(WindowWidth * WindowHeight * 4);
        JBro::TextureReadback readback;
        Check(renderer->ReadBackBuffer(image.Data(), image.Size(), readback),
            "the editor window must read back");

        std::size_t painted = 0;
        std::size_t bright = 0;
        for (std::uint32_t y = 0; y < WindowHeight; ++y)
        {
            for (std::uint32_t x = 0; x < WindowWidth; ++x)
            {
                const std::size_t offset = static_cast<std::size_t>(y) * readback.rowPitch
                    + static_cast<std::size_t>(x) * 4;
                const auto* pixel =
                    reinterpret_cast<const unsigned char*>(image.Data() + offset);
                // **지움색과 다른지를 본다.** 밝기로 재면 안 된다 - ImGui 의 창
                // 배경은 오버레이가 지운 색보다 오히려 어둡다.
                if (DiffersFromClear(pixel))
                {
                    ++painted;
                }
                if (pixel[0] > 200 && pixel[1] > 200 && pixel[2] > 200)
                {
                    ++bright;
                }
            }
        }
        // 카메라가 지운 초록이 화면에 있어야 한다. 게임 -> 텍스처 -> 패널로
        // 이어지는 길 어디가 끊겨도 이 숫자가 0 이 된다.
        std::size_t gamePixels = 0;
        std::uint32_t gameMinX = WindowWidth;
        std::uint32_t gameMaxX = 0;
        std::uint32_t gameMinY = WindowHeight;
        std::uint32_t gameMaxY = 0;
        for (std::uint32_t y = 0; y < WindowHeight; ++y)
        {
            for (std::uint32_t x = 0; x < WindowWidth; ++x)
            {
                const std::size_t offset = static_cast<std::size_t>(y) * readback.rowPitch
                    + static_cast<std::size_t>(x) * 4;
                const auto* pixel =
                    reinterpret_cast<const unsigned char*>(image.Data() + offset);
                if (pixel[2] < 40 && pixel[1] > 180 && pixel[0] > 60 && pixel[0] < 120)
                {
                    ++gamePixels;
                    gameMinX = x < gameMinX ? x : gameMinX;
                    gameMaxX = x > gameMaxX ? x : gameMaxX;
                    gameMinY = y < gameMinY ? y : gameMinY;
                    gameMaxY = y > gameMaxY ? y : gameMaxY;
                }
            }
        }

        std::cout << "  the editor painted " << painted << " pixels (" << bright
            << " bright, " << gamePixels << " from the game) on its window" << std::endl;
        // 창을 채우는 패널이 하나 있으므로 화면 대부분이 패널 색이다.
        Check(painted > (WindowWidth * WindowHeight) / 2,
            "the editor panel must cover the window");
        // 패널 제목이 글자로 나온다. 폰트 아틀라스가 안 올라가면 여기서 걸린다.
        Check(bright > 50, "and its text must be on screen");
        // 게임 뷰는 4:3 이고 패널은 그보다 넓으므로 좌우가 남는다. 그래도 화면의
        // 상당 부분이 게임 화면이어야 한다.
        Check(gamePixels > (WindowWidth * WindowHeight) / 4,
            "the game must reach the panel through its texture");

        // **모양이 지켜져야 한다.** 패널에 늘려 붙이면 픽셀 수는 오히려 늘어나서
        // 넓이만 세는 검사는 통과한다 - 게임이 에디터 창 모양대로 찌그러진 채로.
        const float boxWidth = static_cast<float>(gameMaxX - gameMinX + 1);
        const float boxHeight = static_cast<float>(gameMaxY - gameMinY + 1);
        const float shown = boxWidth / boxHeight;
        const float wanted =
            static_cast<float>(GameWidth) / static_cast<float>(GameHeight);
        std::cout << "  the game view is " << boxWidth << "x" << boxHeight
            << " (ratio " << shown << ", wanted " << wanted << ")" << std::endl;
        Check(shown > wanted - 0.08f && shown < wanted + 0.08f,
            "and keep its own shape rather than take the panel's");

        // 꺼지면 게임이 다시 백버퍼로 간다. 남은 GPU 리소스도 함께 놓는다.
        editor.DisableEditorUi();
        Check(false == editor.IsEditorUiEnabled(), "the UI must turn off");
        Check(false == editor.GetGameViewTexture().IsValid(),
            "and give its game view texture back");
        Check(editor.Tick(1.0f / 60.0f), "the editor must keep ticking without its UI");

        editor.Shutdown();
    }

    void TestEditorProjectSessions()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 96;
        config.windowHeight = 64;
        JBro::EditorApplicationConfig unsupportedConfig = config;
        unsupportedConfig.graphicsApi = JBro::GraphicsApi::Vulkan;
        Check(false == editor.Initialize(unsupportedConfig),
            "editor must reject an unavailable graphics backend without changing state");
        Check(editor.Initialize(config), "editor process must initialize without a project");
        Check(editor.IsInitialized() && false == editor.HasOpenProject(),
            "initialized editor must begin without a project");

        JBro::ProjectDescriptor project;
        project.framework = JBro::FrameworkKind::Framework2D;
        project.graphicsApi = JBro::GraphicsApi::D3D12;
        Check(editor.OpenProject(project), "editor must open a 2D project");
        Check(false == editor.OpenProject(project), "editor must reject opening over a live project");
        Check(editor.Tick(1.0f / 60.0f), "editor must tick its project through EngineInstance");
        // 카메라 없는 빈 프로젝트는 제출할 것이 없다. 실패가 아니라 버려진 프레임이다(D-49).
        Check(editor.GetLastFrameStatus() == JBro::FrameStatus::Skipped,
            "a 2D project without a camera must report a skipped frame, not a presented one");
        editor.CloseProject();
        Check(editor.IsInitialized() && false == editor.HasOpenProject(),
            "project close must preserve the editor process");
        Check(editor.Tick(1.0f / 60.0f), "projectless editor must keep pumping its process");
        Check(editor.GetLastFrameStatus() == JBro::FrameStatus::Skipped,
            "projectless editor tick must skip GPU submission");
        Check(editor.OpenProject(project) && editor.Tick(1.0f / 60.0f),
            "editor must reopen a project on its live process resources");
        editor.CloseProject();

        // 3D 백엔드는 아직 그리지 않지만, 그것이 호스트를 끝내는 이유가 되어서는 안 된다(F-7).
        JBro::ProjectDescriptor project3D;
        project3D.framework = JBro::FrameworkKind::Framework3D;
        project3D.graphicsApi = JBro::GraphicsApi::D3D12;
        Check(editor.OpenProject(project3D), "editor must open a 3D project");
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(1.0f / 60.0f),
                "a 3D project must keep ticking even though its renderer submits nothing");
            Check(editor.GetLastFrameStatus() == JBro::FrameStatus::Skipped,
                "a non-submitting 3D frame must be skipped, not an invalid state");
        }
        Check(editor.IsInitialized() && editor.HasOpenProject(),
            "repeated empty 3D frames must leave the editor process and project alive");
        editor.Shutdown();
        Check(false == editor.IsInitialized() && false == editor.HasOpenProject(),
            "editor shutdown must release project and process resources");
        editor.Shutdown();
    }

    // 임시 파일 하나를 만들고 지운다. 여기서 만드는 `.jproject` 와 `.jcanvas` 는
    // 리포에 남기지 않는다 — 테스트가 만든 것이 소스 트리에 쌓이면 안 된다.
    JBro::String TempPath(const char* name)
    {
        char buffer[MAX_PATH] = {};
        const DWORD length = GetTempPathA(static_cast<DWORD>(sizeof(buffer)), buffer);
        JBro::String path(length > 0 ? buffer : ".");
        if (path.empty() || (path.back() != '/' && path.back() != '\\'))
        {
            path.append("\\");
        }
        path.append(name);
        return path;
    }

    bool WriteTextFile(const JBro::String& path, const char* text)
    {
        std::FILE* file = nullptr;
        if (fopen_s(&file, path.c_str(), "wb") != 0 || file == nullptr)
        {
            return false;
        }
        const std::size_t length = std::strlen(text);
        const std::size_t written = std::fwrite(text, 1, length, file);
        std::fclose(file);
        return written == length;
    }

    void TestEditorOpensAProjectFile()
    {
        // 스크립트 경로를 비워 둔다. 이 테스트가 보려는 것은 `.jproject` 를 읽는 길이지
        // DLL 을 싣는 길이 아니다(그쪽은 ScriptDLLLoaderTests 가 본다).
        const JBro::String projectPath = TempPath("JBroEditorTest.jproject");
        Check(WriteTextFile(projectPath,
            "Version: 1\n"
            "RootPath: .\n"
            "ResolutionWidth: 1280\n"
            "ResolutionHeight: 720\n"
            "PixelsPerUnit: 100\n"
            "ScriptOutputLibraryPath: \"\"\n"
            "LastOpenedCanvasPath: Scenes/Opening.jcanvas\n"
            "Build:\n"
            "  ProductName: EditorTest\n"
            "  StartupCanvas: Scenes/Opening.jcanvas\n"),
            "the test must be able to write its own project file");

        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        JBro::EditorApplication editor;
        Check(editor.Initialize(config), "the editor must initialize");

        JBro::ProjectFileError error;
        if (false == editor.OpenProjectFile(
            projectPath.c_str(), JBro::FrameworkKind::Framework2D, error))
        {
            std::cout << "  open failed at line " << error.line
                << ": " << error.message.c_str() << std::endl;
            Check(false, "the editor must open a project from its file");
        }
        Check(editor.HasOpenProject(), "the project must be open afterwards");

        // 파일의 내용이 실제로 실렸는지 본다. 열리기만 하고 값이 비면 소용이 없다.
        const JBro::ProjectFile& project = editor.GetProjectFile();
        Check(project.resolutionWidth == 1280 && project.resolutionHeight == 720,
            "what the project file said must be readable through the editor");
        Check(project.lastOpenedCanvasPath == "Scenes/Opening.jcanvas",
            "the canvas the project remembers must come through");

        // 그 안의 경로는 전부 프로젝트 폴더 기준이다.
        const JBro::String resolved =
            editor.ResolveProjectPath(project.lastOpenedCanvasPath.c_str());
        Check(resolved != project.lastOpenedCanvasPath,
            "a relative path must be joined to the project folder");
        Check(resolved.find("Scenes/Opening.jcanvas") != JBro::String::npos,
            "and must still end with what it named");
        Check(editor.ResolveProjectPath("C:/elsewhere/Other.jcanvas")
            == "C:/elsewhere/Other.jcanvas",
            "an absolute path must be left alone");

        editor.CloseProject();
        Check(false == editor.HasOpenProject(), "closing must leave the process running");
        // 프로젝트가 없으면 기준도 없어야 한다. 남아 있으면 다음 프로젝트의 경로가
        // 옛 폴더를 기준으로 풀린다.
        Check(editor.ResolveProjectPath("Scenes/Opening.jcanvas") == "Scenes/Opening.jcanvas",
            "with no project open there is nothing to resolve against");

        editor.Shutdown();
        std::remove(projectPath.c_str());
    }

    void TestEditorSavesAndOpensACanvas()
    {
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        JBro::EditorApplication editor;
        Check(editor.Initialize(config), "the editor must initialize");

        JBro::ProjectDescriptor project;
        project.name = {"EditorTest", 10};
        Check(editor.OpenProject(project), "the editor must open a 2D project");

        JBro::Canvas* canvas = editor.GetCanvas();
        Check(canvas != nullptr, "an open project must have a canvas");

        JBro::GameObject* object = canvas->CreateObject("Saved");
        auto* transform = canvas->AttachComponent<JBro::Component::Transform2D>(object);
        transform->position = { 4.5f, -1.25f };

        const JBro::String canvasPath = TempPath("JBroEditorTest.jcanvas");
        JBro::CanvasFileError error;
        if (false == editor.SaveCanvas(canvasPath.c_str(), error))
        {
            std::cout << "  save failed: " << error.message.c_str() << std::endl;
            Check(false, "the editor must save its canvas to a file");
        }

        // 닫고 다시 열면 빈 캔버스다. 거기에 읽어 넣는다.
        editor.CloseProject();
        Check(editor.OpenProject(project), "the editor must open a project again");
        JBro::Canvas* reopened = editor.GetCanvas();
        Check(reopened != nullptr, "a new project session must bring a canvas");
        // 주소가 다른지는 묻지 않는다. 앞의 것이 해제된 자리에 다시 잡힐 수 있고,
        // 그것은 틀린 것이 아니다. 중요한 것은 내용이 비어 있다는 쪽이다.
        Check(reopened->GetObjectCount() == 0, "and that canvas must start empty");

        if (false == editor.LoadCanvas(canvasPath.c_str(), error))
        {
            std::cout << "  load failed: " << error.message.c_str()
                << " (object " << error.objectName.c_str()
                << ", type " << error.typeName.c_str() << ")" << std::endl;
            Check(false, "the editor must read a canvas it wrote");
        }
        Check(reopened->GetObjectCount() == 1, "the object must come back");

        JBro::GameObject* loaded = nullptr;
        reopened->ForEachObject([&loaded](JBro::GameObject& found) { loaded = &found; });
        Check(loaded != nullptr && std::strcmp(loaded->GetTag(), "Saved") == 0,
            "and come back under its own name");
        auto* loadedTransform = reopened->FindComponentRaw<JBro::Component::Transform2D>(loaded);
        Check(loadedTransform != nullptr
            && loadedTransform->position.x == 4.5f
            && loadedTransform->position.y == -1.25f,
            "with the values it was saved with");

        // 이미 내용이 있는 캔버스에 또 읽으면 거절해야 한다.
        Check(false == editor.LoadCanvas(canvasPath.c_str(), error),
            "reading into a canvas that already holds something must be refused");

        editor.Shutdown();
        std::remove(canvasPath.c_str());
    }

    void TestCanvasWorkNeedsAnOpenProject()
    {
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        JBro::EditorApplication editor;
        Check(editor.Initialize(config), "the editor must initialize");

        Check(editor.GetCanvas() == nullptr, "there is no canvas without a project");
        JBro::CanvasFileError error;
        Check(false == editor.SaveCanvas(TempPath("never.jcanvas").c_str(), error),
            "saving with no project open must be refused");
        Check(false == error.message.empty(), "and must say why");
        Check(false == editor.LoadCanvas(TempPath("never.jcanvas").c_str(), error),
            "loading with no project open must be refused");

        editor.Shutdown();
    }

    void TestAFailedOpenSaysWhy()
    {
        // 파일은 멀쩡히 읽혔는데 여는 데 실패하는 경우다. 여기서 아무 말도 하지 않으면
        // 부르는 쪽은 빈 오류를 받고 무엇이 잘못됐는지 알 길이 없다.
        const JBro::String projectPath = TempPath("JBroEditorMissingDll.jproject");
        Check(WriteTextFile(projectPath,
            "Version: 1\n"
            "ScriptOutputLibraryPath: NoSuchScriptModule.dll\n"),
            "the test must be able to write its own project file");

        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        JBro::EditorApplication editor;
        Check(editor.Initialize(config), "the editor must initialize");

        JBro::ProjectFileError error;
        Check(false == editor.OpenProjectFile(
            projectPath.c_str(), JBro::FrameworkKind::Framework2D, error),
            "a project whose script module is missing must not open");
        Check(false == error.message.empty(),
            "and the refusal must say something rather than come back blank");
        Check(false == editor.HasOpenProject(),
            "nothing may be left half open behind a refusal");

        // 실패한 뒤에도 다시 열 수 있어야 한다. 프레임워크가 남아 있으면 막힌다.
        Check(WriteTextFile(projectPath,
            "Version: 1\n"
            "ScriptOutputLibraryPath: \"\"\n"),
            "the test must be able to rewrite its project file");
        Check(editor.OpenProjectFile(
            projectPath.c_str(), JBro::FrameworkKind::Framework2D, error),
            "the editor must still be usable after a refused open");

        editor.Shutdown();
        std::remove(projectPath.c_str());
    }
}

int RunEditorApplicationTests()
{
    TestEditorProjectSessions();
    TestTheEditorPaintsItsOwnScreen();
    TestTheEditorDrawsWithNoProjectOpen();
    TestTheMenuBarSpeaksTheLoadedLocale();
    TestTheEditorForwardsInputToItsUi();
    TestThePanelRegistryRefusesWhatItCannotHold();
    TestAClosedPanelKeepsUpdatingButStopsDrawing();
    TestClickingTheCloseButtonClosesThePanel();
    TestTheDeviceSurvivesFontAtlasUpdates();
    TestSelectingSeveralObjects();
    TestAChosenChildUnderAChosenParentIsNotItsOwnTarget();
    TestTheInspectorIsToldWhatItMayEdit();
    TestTheInspectorEditsThroughCommands();
    TestEditingWithSeveralChosenReachesThemAll();
    TestAChosenChildDoesNotGetTheEditTwice();
    TestMultiEditPicksTheSameOrdinalEverywhere();
    TestListEditsReachEveryChosenObjectAsOneUndo();
    TestAPairElementDragsAsADeltaOnEveryChosenList();
    TestAVectorFieldEditsThroughACommand();
    TestTheGameViewIsRenderedOnlyWhileItsPanelShows();
    TestPopupsOpenOneAtATimeAndCloseByHandle();
    TestSavingAsksForAPathOnceAndReportsFailure();
    TestAStructElementOpensAndEditsEveryChosenList();
    TestDraggingAStructElementReordersEveryChosenList();
    TestFlagCountAndToneElementsEditByMouseOnEveryChosenList();
    TestTypingTheSameValueLeavesNothingToUndo();
    TestCreatingAnObjectCanBeUndone();
    TestDeletingAnObjectCanBeUndoneWithItsValues();
    TestClosingTheWindowDoesNotTakeTheUiDownWithIt();
    TestEditorOpensAProjectFile();
    TestEditorSavesAndOpensACanvas();
    TestCanvasWorkNeedsAnOpenProject();
    TestAFailedOpenSaysWhy();
    std::cout << "Editor application tests passed.\n";
    return 0;
}

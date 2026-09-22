#include <JBro/Editor/EditorApplication.h>
#include <JBro/Core/Version.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Core/Profiler.h>
#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/AssetTypes/AssetTypesReflection.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/Command/ObjectCommands.h>
#include <JBro/Editor/EditorObjectRegistry.h>
#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/EditorPanel.h>
#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/EditorPopup.h>
#include <JBro/Editor/EditorActions.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
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
#include <filesystem>
#include <fstream>
#include <iterator>
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

    // 백버퍼를 그대로 받아 온다. 두 프레임을 견주려면 센 값이 아니라 픽셀이 필요하다.
    void ReadBackBufferInto(JBro::Renderer& renderer, std::uint32_t width, std::uint32_t height,
        JBro::Array<std::byte>& image, JBro::TextureReadback& readback)
    {
        image.Resize(static_cast<std::size_t>(width) * height * 4);
        Check(renderer.ReadBackBuffer(image.Data(), image.Size(), readback),
            "the editor window must read back");
    }

    // 두 프레임에서 달라진 픽셀의 수다. 무엇이 달라졌는지가 아니라 **달라지기는 했는지**를
    // 묻는 자리에 쓴다 - 겹쳐 그리는 것이 실제로 화면에 닿았음은 이것으로 드러난다.
    std::size_t CountDifferingPixels(const JBro::Array<std::byte>& first,
        const JBro::Array<std::byte>& second, const JBro::TextureReadback& readback,
        std::uint32_t width, std::uint32_t height)
    {
        std::size_t differing = 0;
        for (std::uint32_t y = 0; y < height; ++y)
        {
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const std::size_t offset = static_cast<std::size_t>(y) * readback.rowPitch
                    + static_cast<std::size_t>(x) * 4;
                const unsigned char* a =
                    reinterpret_cast<const unsigned char*>(first.Data() + offset);
                const unsigned char* b =
                    reinterpret_cast<const unsigned char*>(second.Data() + offset);
                if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2])
                {
                    ++differing;
                }
            }
        }
        return differing;
    }

    struct PixelBox
    {
        int minX = 1 << 30;
        int minY = 1 << 30;
        int maxX = -1;
        int maxY = -1;

        bool IsEmpty() const { return maxX < minX || maxY < minY; }
    };

    // 화면에서 조건에 맞는 픽셀이 차지한 사각형이다. **겹쳐 그린 것이 그림과 같은 자리에
    // 있는지**는 화면을 읽어야만 알 수 있다 - 그리는 쪽과 재는 쪽이 같은 셈을 쓰면 서로
    // 어긋나 있어도 둘 다 같은 답을 내놓기 때문이다(D-150).
    template <typename Fn>
    PixelBox MeasurePixels(const JBro::Array<std::byte>& image,
        const JBro::TextureReadback& readback, const ImRect& area, Fn&& matches)
    {
        // **재는 자리를 좁힌다.** 같은 색이 다른 창에도 있다(인스펙터의 강조 같은 것) -
        // 화면 전체를 재면 그것들까지 한 상자에 들어온다.
        PixelBox box;
        const std::uint32_t left = static_cast<std::uint32_t>((std::max)(0.0f, area.Min.x));
        const std::uint32_t top = static_cast<std::uint32_t>((std::max)(0.0f, area.Min.y));
        const std::uint32_t right = static_cast<std::uint32_t>((std::max)(0.0f, area.Max.x));
        const std::uint32_t bottom = static_cast<std::uint32_t>((std::max)(0.0f, area.Max.y));
        for (std::uint32_t y = top; y < bottom; ++y)
        {
            for (std::uint32_t x = left; x < right; ++x)
            {
                const std::size_t offset = static_cast<std::size_t>(y) * readback.rowPitch
                    + static_cast<std::size_t>(x) * 4;
                const unsigned char* pixel =
                    reinterpret_cast<const unsigned char*>(image.Data() + offset);
                if (false == matches(pixel))
                {
                    continue;
                }
                box.minX = (std::min)(box.minX, static_cast<int>(x));
                box.minY = (std::min)(box.minY, static_cast<int>(y));
                box.maxX = (std::max)(box.maxX, static_cast<int>(x));
                box.maxY = (std::max)(box.maxY, static_cast<int>(y));
            }
        }
        return box;
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
        // **이름으로 센다.** 숫자만 재면 패널을 더할 때마다 이 줄을 고치게 되고,
        // 정작 무엇이 빠졌는지는 말해 주지 않는다.
        const char* const expected[] = {
            "CanvasView", "Game", "Hierarchy", "Inspector", "Assets", "Stats", "Log",
            "ProjectSettings", "Profiler", "Shortcuts"};
        for (const char* title : expected)
        {
            Check(editor.FindPanel(title) != nullptr, title);
        }
        Check(builtin == sizeof(expected) / sizeof(expected[0]),
            "and the editor brings exactly those");
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

    // ImGui 의 자식 창은 `"<부모 이름>/<자식 이름>_<16진 Id>"` 로 이름 붙는다. 그 16진 값을
    // 손으로 맞추기보다 접두사로 찾는 쪽이 덜 깨진다 - ImGui 가 이름 짓는 법을 바꿔도 앞부분은 남는다.
    ImGuiWindow* FindChildWindow(ImGuiWindow* parent, const char* childName)
    {
        Check(parent != nullptr && childName != nullptr, "a child needs a parent and a name");
        char prefix[256] = {};
        ImFormatString(prefix, IM_ARRAYSIZE(prefix), "%s/%s_", parent->Name, childName);
        const std::size_t length = std::strlen(prefix);
        ImGuiContext& context = *ImGui::GetCurrentContext();
        for (ImGuiWindow* window : context.Windows)
        {
            if (std::strncmp(window->Name, prefix, length) == 0)
            {
                return window;
            }
        }
        return nullptr;
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

    // 이름의 일부로 활성 창을 찾는다. 컨텍스트 메뉴의 창 이름에는 부모와 Id 가 섞여 붙어
    // 있어 이름을 통째로 짚을 수 없다.
    ImGuiWindow* FindActiveWindowContaining(const char* fragment)
    {
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
        {
            if (window->Active && std::strstr(window->Name, fragment) != nullptr)
            {
                return window;
            }
        }
        return nullptr;
    }

    // 조합키를 누른 채 클릭한다.
    //
    // 키를 창에 부쳐 보낼 수는 없다 - 플랫폼이 조합키를 `GetKeyState` 로 읽는데, 부친
    // 메시지는 그 상태를 바꾸지 않는다. 그래서 ImGui 에 바로 알린다. 마우스 메시지는
    // 조합키를 건드리지 않으므로, 프레임마다 한 번씩 다시 알려 누른 채로 둔다.
    void ClickAtWith(JBro::EditorApplication& editor, HWND hwnd, const Spot& spot, ImGuiKey modifier)
    {
        const auto hold = [&]() {
            ImGui::GetIO().AddKeyEvent(modifier, true);
            Check(editor.Tick(Frame), "the editor must tick with the modifier held");
        };
        PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(spot.x, spot.y));
        hold();
        PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(spot.x, spot.y));
        hold();
        PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(spot.x, spot.y));
        hold();
        ImGui::GetIO().AddKeyEvent(modifier, false);
        Check(editor.Tick(Frame), "the editor must tick after letting the modifier go");
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
        // 해시는 시드를 그대로 돌려주므로 Id 는 행 아래의 삭제 글리프(D-96)다.
        Check(FindListItemNearRightEdge(editor, hwnd, LabelId(PushedId(body->ID, 0), JBro::Icons::Xmark), 1,
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

        // **손잡이와 삭제 표시는 아이콘 글꼴의 글리프다**(D-96). 글꼴이 합쳐졌고 그 글리프가
        // 글꼴 안에 있어야 한다 - 없으면 네모가 그려지는데, 화면을 보지 않으면 모른다.
        Check(JBro::EditorTheme::HasIconFont(), "the icon font must be merged into the UI font");
        Check(ImGui::GetFont()->IsGlyphInFont(0xF7A4) && ImGui::GetFont()->IsGlyphInFont(0xF00D),
            "and hold the grip and the x mark the list draws");

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
        Check(FindListItemNearRightEdge(editor, hwnd, LabelId(PushedId(body->ID, 0), JBro::Icons::Xmark), 12,
                closedMark),
            "a closed element must have its remove mark at the end of the row");
        Check(FindListItemNearRightEdge(editor, hwnd, LabelId(row, JBro::Icons::Xmark), 12, spot),
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
        Check(FindListItemNearRightEdge(editor, hwnd, LabelId(PushedId(body->ID, 0), JBro::Icons::Xmark), 1, node),
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
        bool pickFolder = false;
        JBro::String defaultFileName;

        static bool Answer(const JBro::FileDialogDesc& desc, JBro::String& outPath, void* user)
        {
            DialogProbe& probe = *static_cast<DialogProbe*>(user);
            ++probe.calls;
            probe.save = desc.save;
            probe.pickFolder = desc.pickFolder;
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
        // 커맨드 하나를 쌓아 "저장되지 않음" 상태를 만든다. 저장이 그것을 지워야 한다.
        Check(editor.GetCommands().Execute(JBro::MakeOwnerPtr<JBro::CreateObjectCommand>(
                  *canvas, editor.GetObjectIds(), "Extra", JBro::InvalidEditorObjectId)),
            "the probe edit must run");
        Check(editor.GetCommands().IsDirty(), "and leave the canvas unsaved");

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
            reader.GetCanvas()->ForEachObject([&loaded](JBro::GameObject& found) {
                if (std::strcmp(found.GetTag(), "Saved") == 0)
                {
                    loaded = &found;
                }
            });
            Check(loaded != nullptr, "the saved object must be in the file");
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
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must keep ticking after the cancel");
        }
        Check(dialog.calls == 2, "one request asks once - later frames must not ask again");

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

    void RightClickAt(JBro::EditorApplication& editor, HWND hwnd, const Spot& spot)
    {
        PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(spot.x, spot.y));
        Check(editor.Tick(Frame), "the editor must tick");
        PostMessageW(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(spot.x, spot.y));
        Check(editor.Tick(Frame), "the editor must tick");
        PostMessageW(hwnd, WM_RBUTTONUP, 0, MAKELPARAM(spot.x, spot.y));
        Check(editor.Tick(Frame), "the editor must tick");
    }

    // 지금 떠 있는 우클릭 메뉴(모달이 아닌 팝업) 창이다.
    ImGuiWindow* FindContextMenuWindow()
    {
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
        {
            if (std::strncmp(window->Name, "##Popup_", 8) == 0 && window->Active
                && (window->Flags & ImGuiWindowFlags_Modal) == 0)
            {
                return window;
            }
        }
        return nullptr;
    }

    // **컴포넌트 머리의 우클릭 메뉴로 자리를 옮기고, 그것은 되돌릴 수 있다.** 슬롯 순서가
    // 스크립트 실행 순서다(D-45). 양 끝에서는 그쪽 항목이 잠긴다.
    void TestMovingAComponentFromItsHeaderMenuCanBeUndone()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; component moving not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "MoveComponentProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");

        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* alpha = canvas->CreateObject("Alpha");
        auto* transform = canvas->AttachComponent<JBro::Component::Transform2D>(alpha);
        auto* sprite = canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(alpha);
        Check(transform != nullptr && sprite != nullptr, "both components must attach");
        JBro::GameObject* chosen[] = {alpha};
        editor.SelectObjects({chosen, 1});
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        ImGuiWindow* inspector = ImGui::FindWindowByName("Inspector");
        Check(inspector != nullptr, "the inspector must have a window");
        // 첫 슬롯의 머리. 슬롯 번호를 쌓고 타입 이름의 접기 머리를 그린다.
        Spot header;
        Check(FindInspectorItem(editor, hwnd, LabelId(PushedId(inspector->ID, 0), "Transform2D"), header),
            "the first component header must be in the inspector");
        RightClickAt(editor, hwnd, header);
        ImGuiWindow* menu = FindContextMenuWindow();
        Check(menu != nullptr, "right-clicking the header must open its menu");

        // 맨 위 슬롯에서 "위로 이동" 은 잠겨 있다.
        const char* upLabel = JBro::Loc::TextOr(JBro::LocKeys::InspectorMoveComponentUp, "Move Up");
        const char* downLabel = JBro::Loc::TextOr(JBro::LocKeys::InspectorMoveComponentDown, "Move Down");
        Spot item;
        Check(FindItemAnywhereInWindow(editor, hwnd, menu, LabelId(menu->ID, upLabel), item),
            "the menu must offer to move the component up");
        Check(ImGui::GetCurrentContext()->HoveredIdIsDisabled,
            "but the first slot cannot move up, so that item is locked");
        Check(FindItemAnywhereInWindow(editor, hwnd, menu, LabelId(menu->ID, downLabel), item),
            "and offer to move it down");
        const std::size_t undo = editor.GetCommands().GetUndoCount();
        ClickAt(editor, hwnd, item);
        for (int frame = 0; frame < 2; ++frame)
        {
            Check(editor.Tick(Frame), "the inspector must redraw after the move");
        }
        std::size_t slot = 99;
        Check(alpha->FindComponentIndex(sprite, slot) && slot == 0,
            "moving the transform down must put the sprite first");
        Check(alpha->FindComponentIndex(transform, slot) && slot == 1, "and the transform second");
        Check(editor.GetCommands().GetUndoCount() == undo + 1, "as one undo");
        Check(editor.GetCommands().Undo(), "undo must run");
        Check(alpha->FindComponentIndex(transform, slot) && slot == 0,
            "and put the transform back first");
        Check(FindContextMenuWindow() == nullptr, "and the menu must be gone");

        editor.Shutdown();
    }

    // 프로퍼티를 등록하지 않은 컴포넌트다. 스냅샷으로 뜰 수 없다.
    class Opaque final : public JBro::ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Test::Opaque";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }
    };

    // **복사는 고른 것 중 맨 위 것들을 뜨고, 붙여넣기는 주된 선택의 형제로 붙여 그것을 고른다.**
    void TestCopyAndPasteMakeASiblingAndSelectIt()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; copy and paste not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "PasteProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* holder = canvas->CreateObject("Holder");
        JBro::GameObject* alpha = canvas->CreateObject("Alpha");
        alpha->SetParent(holder);
        JBro::GameObject* leaf = canvas->CreateObject("Leaf");
        leaf->SetParent(alpha);
        auto* transform = canvas->AttachComponent<JBro::Component::Transform2D>(alpha);
        transform->rotation = 0.5f;

        Check(false == editor.HasClipboard(), "the clipboard starts empty");
        Check(false == editor.CopySelection(), "copying with nothing chosen does nothing");
        Check(false == editor.PasteClipboard(), "and pasting an empty clipboard does nothing");

        // 부모와 자식을 함께 골라도 맨 위 것 하나만 뜬다 - 자식은 그 안에 있다.
        JBro::GameObject* chosen[] = {alpha, leaf};
        editor.SelectObjects({chosen, 2});
        Check(editor.CopySelection(), "copying the chosen tree must go through");
        Check(editor.HasClipboard(), "and fill the clipboard");

        const std::size_t before = canvas->GetObjectCount();
        const std::size_t undo = editor.GetCommands().GetUndoCount();
        Check(editor.PasteClipboard(), "pasting must go through");
        Check(canvas->GetObjectCount() == before + 2, "and add the tree once, not the child twice");
        Check(editor.GetCommands().GetUndoCount() == undo + 1, "as one undo");
        JBro::GameObject* pasted = editor.GetSelectedObject();
        Check(pasted != nullptr && pasted != alpha && std::strcmp(pasted->GetTag(), "Alpha") == 0,
            "and choose the pasted root instead of the source");
        Check(editor.GetSelectionCount() == 1, "and nothing else");
        Check(pasted->GetParent() == holder, "placed beside the source, under the same parent");
        Check(pasted->GetChildren().Size() == 1, "with its child");
        auto* pastedTransform = canvas->FindComponentRaw<JBro::Component::Transform2D>(pasted);
        Check(pastedTransform != nullptr && pastedTransform->rotation == 0.5f,
            "and its component values");
        Check(editor.GetCommands().Undo(), "undo must run");
        Check(canvas->GetObjectCount() == before, "and take the pasted tree away");

        // 뜨지 못하는 것이 하나라도 섞여 있으면 클립보드를 건드리지 않는다.
        JBro::GameObject* sealed = canvas->CreateObject("Sealed");
        Check(canvas->AttachComponent<Opaque>(sealed) != nullptr, "the opaque component must attach");
        JBro::GameObject* mixed[] = {alpha, sealed};
        editor.SelectObjects({mixed, 2});
        Check(false == editor.CopySelection(), "copying a tree that cannot be captured is refused");
        editor.ClearSelection();
        Check(editor.PasteClipboard(), "and the earlier clipboard must still paste");
        pasted = editor.GetSelectedObject();
        Check(pasted != nullptr && std::strcmp(pasted->GetTag(), "Alpha") == 0
                && canvas->GetObjectCount() == before + 3,
            "the earlier tree, untouched by the refused copy");
        Check(editor.GetCommands().Undo(), "undo must run");

        // 고른 것이 없으면 뿌리에 붙는다.
        editor.ClearSelection();
        Check(editor.PasteClipboard(), "pasting with nothing chosen must go through");
        pasted = editor.GetSelectedObject();
        Check(pasted != nullptr && pasted->GetParent() == nullptr, "and land at the canvas root");
        Check(editor.GetCommands().Undo(), "undo must run");

        // 프로젝트를 닫으면 클립보드도 비운다.
        editor.CloseProject();
        Check(false == editor.HasClipboard(), "closing the project must empty the clipboard");

        editor.Shutdown();
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
        // 이 테스트가 재는 것은 **게임 뷰**의 opt-in 이다. 편집 화면은 자기 텍스처에
        // 따로 그려 뷰를 하나 더 내므로(D-130), 세는 것이 섞이지 않게 닫아 둔다.
        if (JBro::EditorPanel* canvasView = editor.FindPanel("CanvasView"))
        {
            canvasView->SetOpen(false);
        }
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

        // 프로파일러는 닫혀 있는 창이다(재는 것 자체가 프레임에 얹힌다). 한 번 열어
        // **그리는 길이 도는지** 보고 그림도 한 장 남긴다 - 닫힌 창은 아무도 보지 않는다.
        if (JBro::EditorPanel* profiler = editor.FindPanel("Profiler"))
        {
            profiler->SetOpen(true);
            for (int frame = 0; frame < 6; ++frame)
            {
                Check(editor.Tick(Frame), "the editor must tick with the profiler open");
            }
            Check(JBro::Profiler::IsEnabled(),
                "an open profiler window must turn measuring on");
            Check(JBro::Profiler::GetCount() != 0, "and there must be sections to show");
            if (JBro::Renderer* renderer = editor.GetRenderer())
            {
                SaveScreenshot(*renderer, 1024, 768, "profiler");
            }
            profiler->SetOpen(false);
            Check(editor.Tick(Frame), "the editor must tick once more");
            Check(false == JBro::Profiler::IsEnabled(),
                "closing it must turn measuring back off");
        }

        editor.Shutdown();
    }

    // **아무것도 안 바뀌었으면 되돌릴 것도 없다.** 에셋 칸을 열고 Enter 만 치면 비우기
    // 항목이 다시 골라지는데 값은 이미 비어 있다 - 그것까지 쌓으면 Ctrl+Z 가 아무 일도
    // 안 하는 헛걸음을 만든다. (D-116 전에는 글자 칸이었고 같은 계약이었다.)
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
        // `spriteId` 는 AssetId 라 에셋 드롭다운으로 그려진다(D-116). 프로젝트에 파일이 없으니
        // 목록은 비우기 항목뿐이다.
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

        // 드롭다운을 열고 아무것도 고치지 않은 채 Enter 를 친다.
        ClickAt(editor, hwnd, spot);
        Check(editor.Tick(Frame), "the popup must appear");
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


    // 아래(프로젝트 파일 테스트 옆)에 있다.
    bool WriteTextFile(const JBro::String& path, const char* text);

    // 2x2 RGBA PNG. AssetSystemTests 와 같은 바이트다 - 레지스트리가 스프라이트로 등록하고
    // 에셋 시스템이 실제로 디코드해야 핸들이 선다.
    constexpr unsigned char TinyPng[] =
    {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d,
        0x24, 0x00, 0x00, 0x00, 0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
        0x1f, 0x0c, 0x81, 0x34, 0x08, 0x34, 0x00, 0x00, 0x49, 0x49, 0x09, 0x78, 0x9c, 0x51, 0x17, 0x92,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
    };

    // **에셋 칸은 레지스트리의 이름을 보이고, 고르면 커맨드 하나와 해석된 핸들이다**(D-116).
    // 프로젝트 폴더에 그림 하나를 두고 열어, 인스펙터의 `spriteId` 드롭다운에서 이름을 쳐 고른다.
    // 되돌리면 아이디와 핸들이 함께 비어야 한다 - 해석은 커맨드 판번호를 따라 다시 돈다(D-115).
    void TestTheAssetFieldPicksARegisteredSprite()
    {
        namespace fs = std::filesystem;
        const fs::path root(TempPath("JBroAssetFieldProbe").c_str());
        std::error_code ignored;
        fs::remove_all(root, ignored);
        fs::create_directories(root / "Assets", ignored);
        {
            std::ofstream png(root / "Assets" / "hero.png", std::ios::binary);
            png.write(reinterpret_cast<const char*>(TinyPng), sizeof(TinyPng));
        }
        const JBro::String projectPath = TempPath("JBroAssetFieldProbe\\AssetField.jproject");
        Check(WriteTextFile(projectPath,
            "Version: 1\n"
            "EngineVersion: 0.1.0\n"
            "Framework: 2D\n"
            "RootPath: .\n"
            "ResolutionWidth: 640\n"
            "ResolutionHeight: 480\n"
            "AssetDirectory: Assets\n"
            "ScriptOutputLibraryPath: \"\"\n"
            "Build:\n"
            "  ProductName: AssetFieldProbe\n"
            "  StartupCanvas: Scenes/Opening.jcanvas\n"),
            "the test must be able to write its own project file");

        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the asset field not verified" << std::endl;
            return;
        }
        JBro::ProjectFileError error;
        Check(editor.OpenProjectFile(projectPath.c_str(), error), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");

        // 스캔이 hero.png 를 Texture 와 Sprite 둘로 등록했어야 한다. 칸에는 Sprite 만 보인다.
        const JBro::AssetRegistry& registry = editor.GetAssetRegistry();
        JBro::AssetId spriteAsset;
        for (std::size_t index = 0; index < registry.GetCount(); ++index)
        {
            const JBro::AssetRecord& record = registry.GetRecord(index);
            if (record.type == JBro::AssetType::Sprite && record.relativePath == "hero.png")
            {
                spriteAsset = record.id;
            }
        }
        Check(false == spriteAsset.IsNull(), "the scan must have registered hero.png as a sprite");

        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* object = canvas->CreateObject("Hero");
        auto* sprite = canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(object);
        Check(sprite != nullptr, "the hero must have a sprite renderer");
        editor.SetSelectedObject(object);
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        const JBro::PropertyTable* table = JBro::PropertyRegistry::Lookup(
            JBro::NameTable::Get().Intern("Component::SpriteRenderer2D"));
        Check(table != nullptr, "the sprite renderer must have registered its properties");
        Spot spot;
        Check(FindInspectorItem(editor, hwnd,
                InspectorFieldId(0, FieldIndexOf(*table, "spriteId"), "##value"), spot),
            "the spriteId row must be in the inspector");

        const std::size_t undoBefore = editor.GetCommands().GetUndoCount();
        ClickAt(editor, hwnd, spot);
        Check(editor.Tick(Frame), "the popup must appear");
        Check(editor.Tick(Frame), "and its search box must take focus");
        for (const char* at = "hero"; *at != '\0'; ++at)
        {
            PostMessageW(hwnd, WM_CHAR, static_cast<WPARAM>(*at), 0);
            Check(editor.Tick(Frame), "the editor must tick while typing");
        }
        PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
        Check(editor.Tick(Frame), "the editor must tick");
        PostMessageW(hwnd, WM_KEYUP, VK_RETURN, 0);
        Check(editor.Tick(Frame), "the editor must tick");
        Check(editor.Tick(Frame), "and once more so the rebind after the command has run");

        Check(sprite->spriteId == spriteAsset, "typing the name and Enter must write the sprite's id");
        Check(editor.GetCommands().GetUndoCount() == undoBefore + 1, "through exactly one command");
        Check(sprite->sprite.index != 0 || sprite->sprite.generation != 0,
            "and the handle must be resolved again after the command");

        Check(editor.GetCommands().Undo(), "undo must run");
        Check(editor.Tick(Frame), "the editor must tick after undo");
        Check(sprite->spriteId.IsNull(), "undo must empty the id");
        Check(sprite->sprite.index == 0 && sprite->sprite.generation == 0,
            "and the handle must be released with it");

        editor.Shutdown();
        fs::remove_all(root, ignored);
    }


    // **에셋 브라우저에서 고르면 인스펙터가 임포트 옵션을 보이고, 고치면 메타가 커맨드로 다시 쓰인다**(D-120).
    // 폴더 안의 그림 줄을 눌러 고르고, 인스펙터의 `pixelsPerUnit` 을 끌어 메타 파일에 옵션 블록이 생기는지, 스프라이트
    // 아이디가 보존되는지, 되돌리면 파일이 원래대로 오는지 잰다. 오브젝트를 고르면 에셋 선택은 빈다.
    void TestTheAssetBrowserSelectsAnAssetAndTheInspectorRewritesItsMeta()
    {
        namespace fs = std::filesystem;
        const fs::path root(TempPath("JBroAssetBrowserProbe").c_str());
        std::error_code ignored;
        fs::remove_all(root, ignored);
        fs::create_directories(root / "Assets" / "art", ignored);
        {
            std::ofstream png(root / "Assets" / "art" / "hero.png", std::ios::binary);
            png.write(reinterpret_cast<const char*>(TinyPng), sizeof(TinyPng));
        }
        const JBro::String projectPath = TempPath("JBroAssetBrowserProbe\\Browser.jproject");
        Check(WriteTextFile(projectPath,
            "Version: 1\n"
            "EngineVersion: 0.1.0\n"
            "Framework: 2D\n"
            "RootPath: .\n"
            "ResolutionWidth: 640\n"
            "ResolutionHeight: 480\n"
            "AssetDirectory: Assets\n"
            "ScriptOutputLibraryPath: \"\"\n"
            "Build:\n"
            "  ProductName: BrowserProbe\n"
            "  StartupCanvas: Scenes/Opening.jcanvas\n"),
            "the test must be able to write its own project file");

        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the asset browser not verified" << std::endl;
            return;
        }
        JBro::ProjectFileError error;
        Check(editor.OpenProjectFile(projectPath.c_str(), error), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        const JBro::AssetRecord* hero = editor.GetAssetRegistry().FindByPath("art/hero.png");
        Check(hero != nullptr && hero->type == JBro::AssetType::Texture, "the scan must have registered art/hero.png");
        // 레코드 포인터는 다시 스캔되면 죽는다. 뒤에서 쓸 것은 아이디로 든다.
        const JBro::AssetId heroTextureId = hero->id;
        // 짝 스프라이트를 로드해 둔다. 메타를 고치면 로드된 것이 그 자리에서 다시 읽혀야 한다(asset-plan §2.7).
        JBro::AssetId heroSprite;
        for (std::size_t index = 0; index < editor.GetAssetRegistry().GetCount(); ++index)
        {
            const JBro::AssetRecord& record = editor.GetAssetRegistry().GetRecord(index);
            if (record.type == JBro::AssetType::Sprite && record.owner == hero->id)
            {
                heroSprite = record.id;
            }
        }
        JBro::AssetSystem* assetSystem = editor.GetAssetSystem();
        Check(assetSystem != nullptr && false == heroSprite.IsNull(), "the image has a sprite record and an asset system");
        const JBro::AssetHandle loadedSprite = assetSystem->Load(heroSprite);
        Check(loadedSprite.generation != 0 && assetSystem->GetSprite(loadedSprite)->options.pixelsPerUnit == 100.0f,
            "the sprite loads with the default pixels per unit");
        Check(editor.FindPanel("Assets") != nullptr, "the asset browser is a default panel");

        // 에셋 패널은 통계 패널과 같은 아래쪽 독의 탭이다. 앞에 있지 않으면 탭을 눌러 꺼낸다.
        ImGuiWindow* assets = ImGui::FindWindowByName("Assets");
        Check(assets != nullptr, "the asset browser must have a window");
        if (false == assets->DockTabIsVisible && assets->DockNode != nullptr && assets->DockNode->TabBar != nullptr)
        {
            ImGuiTabBar* tabBar = assets->DockNode->TabBar;
            ImGuiTabItem* tab = ImGui::TabBarFindTabByID(tabBar, assets->TabId);
            Check(tab != nullptr, "the asset browser must have a tab in its dock");
            Spot tabSpot;
            tabSpot.x = static_cast<int>(tabBar->BarRect.Min.x + tab->Offset + tab->Width * 0.5f);
            tabSpot.y = static_cast<int>((tabBar->BarRect.Min.y + tabBar->BarRect.Max.y) * 0.5f);
            ClickAt(editor, hwnd, tabSpot);
            for (int frame = 0; frame < 3; ++frame)
            {
                Check(editor.Tick(Frame), "the editor must settle on the tab");
            }
            assets = ImGui::FindWindowByName("Assets");
        }
        Check(assets != nullptr && assets->DockTabIsVisible, "the asset browser tab must be in front");
        // **에셋 브라우저는 두 칸이다**(D-139). 왼쪽 나무에서 `art` 를 눌러 열고,
        // 오른쪽 칸에서 그 안의 파일을 찾는다.
        Spot spot;
        {
            ImGuiWindow* tree = FindChildWindow(assets, "##tree");
            Check(tree != nullptr, "the folder tree pane must exist");
            const ImGuiID artRow = LabelId(LabelId(tree->ID, "art"), "##folder");
            bool foundFolder = false;
            const int x = static_cast<int>(tree->Pos.x + 40.0f);
            const int bottom = static_cast<int>(tree->Pos.y + tree->Size.y);
            for (int y = static_cast<int>(tree->Pos.y); y < bottom && false == foundFolder; y += 3)
            {
                PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                Check(editor.Tick(Frame), "the editor must tick while looking for the folder");
                if (ImGui::GetHoveredID() == artRow)
                {
                    spot.x = x;
                    spot.y = y;
                    foundFolder = true;
                }
            }
            Check(foundFolder, "the art folder must be a row in the tree");
            ClickAt(editor, hwnd, spot);
            for (int frame = 0; frame < 3; ++frame)
            {
                Check(editor.Tick(Frame), "the editor must settle on the folder");
            }
        }

        bool found = false;
        {
            assets = ImGui::FindWindowByName("Assets");
            Check(assets != nullptr, "the asset browser must still have a window");
            ImGuiWindow* contents = FindChildWindow(assets, "##contents");
            Check(contents != nullptr, "the contents pane must exist");
            // 줄의 Id: 자식 창 → PushID("art/hero.png") → "##file".
            const ImGuiID heroRow = LabelId(LabelId(contents->ID, "art/hero.png"), "##file");
            const int x = static_cast<int>(contents->Pos.x + 40.0f);
            const int bottom = static_cast<int>(contents->Pos.y + contents->Size.y);
            for (int y = static_cast<int>(contents->Pos.y); y < bottom && false == found; y += 3)
            {
                PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                Check(editor.Tick(Frame), "the editor must tick while looking");
                if (ImGui::GetHoveredID() == heroRow)
                {
                    spot.x = x;
                    spot.y = y;
                    found = true;
                }
            }
        }
        Check(found, "hero.png must be a row in the art folder");
        // 두 칸이 어떻게 보이는지 한 장 남긴다(§11.4).
        if (JBro::Renderer* shotRenderer = editor.GetRenderer())
        {
            SaveScreenshot(*shotRenderer, 1024, 768, "assets");
        }

        // **그림이 있는 보기**(D-147). 아이콘 단추를 누르면 칸마다 작은 그림이 선다.
        {
            const char* iconLabel = JBro::Loc::TextOr(JBro::LocKeys::AssetsIconView, "Icons");
            Spot iconButton;
            Check(FindItemAnywhereInWindow(editor, hwnd, assets,
                    LabelId(assets->ID, iconLabel), iconButton),
                "the view button must be on the asset browser tool bar");
            JBro::Renderer* shotRenderer = editor.GetRenderer();
            Check(shotRenderer != nullptr, "the editor must expose its renderer");
            JBro::Array<std::byte> asList;
            JBro::Array<std::byte> asIcons;
            JBro::TextureReadback readback;
            ReadBackBufferInto(*shotRenderer, 1024, 768, asList, readback);
            ClickAt(editor, hwnd, iconButton);
            for (int frame = 0; frame < 4; ++frame)
            {
                // 그림은 프레임마다 몇 개씩만 올라간다. 몇 프레임 돌려 채운다.
                Check(editor.Tick(Frame), "the editor must settle on the icon view");
            }
            // 그림이 실제로 만들어졌는가. 화면을 읽기 전에 이것부터 묻는다 -
            // 화면만 보면 단추가 눌린 것과 그림이 선 것을 가르지 못한다.
            Check(editor.GetAssetThumbnail(heroTextureId).IsValid(),
                "the texture must have a thumbnail the editor can draw");
            ReadBackBufferInto(*shotRenderer, 1024, 768, asIcons, readback);
            const std::size_t changed =
                CountDifferingPixels(asList, asIcons, readback, 1024, 768);
            std::cout << "  the icon view changed " << changed << " pixels" << std::endl;
            Check(changed > 500, "and the icon view must look different from the list");
            SaveScreenshot(*shotRenderer, 1024, 768, "assets_icons");
            // 목록으로 되돌린다. 아래의 검사들은 줄을 짚는다.
            ClickAt(editor, hwnd, iconButton);
            for (int frame = 0; frame < 3; ++frame)
            {
                Check(editor.Tick(Frame), "the editor must settle back on the list");
            }
            assets = ImGui::FindWindowByName("Assets");
            ImGuiWindow* contents = FindChildWindow(assets, "##contents");
            Check(contents != nullptr, "the contents pane must still exist");
            const ImGuiID heroRow = LabelId(LabelId(contents->ID, "art/hero.png"), "##file");
            bool againFound = false;
            const int x = static_cast<int>(contents->Pos.x + 40.0f);
            const int bottom = static_cast<int>(contents->Pos.y + contents->Size.y);
            for (int y = static_cast<int>(contents->Pos.y); y < bottom && false == againFound; y += 3)
            {
                PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                Check(editor.Tick(Frame), "the editor must tick while looking again");
                if (ImGui::GetHoveredID() == heroRow)
                {
                    spot.x = x;
                    spot.y = y;
                    againFound = true;
                }
            }
            Check(againFound, "hero.png must be a row again");
        }
        ClickAt(editor, hwnd, spot);
        Check(editor.GetSelectedAsset() == hero->id, "clicking the row selects the texture record");
        Check(editor.GetSelectedObject() == nullptr, "and no object");
        Check(editor.GetSelectedAssetMeta() != nullptr && false == editor.GetSelectedAssetMeta()->hasSpriteOptions,
            "the meta is read and has no sprite options yet");
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle on the asset");
        }
        // 고른 에셋의 인스펙터에는 **그림이 먼저 선다**(D-147).
        Check(editor.GetAssetThumbnail(hero->id).IsValid(),
            "the inspector has a picture of what it is editing");
        if (JBro::Renderer* shotRenderer = editor.GetRenderer())
        {
            SaveScreenshot(*shotRenderer, 1024, 768, "asset_inspector");
        }

        // 인스펙터의 둘째 블록(Sprite)의 `pixelsPerUnit`. 컴포넌트와 같은 Id 사슬이되 표 이름이 "##import" 다.
        const JBro::TypeDescriptor& spriteOptions = JBro::TypeDescriptorOf<JBro::SpriteImportOptions>::Get();
        Check(spriteOptions.fields != nullptr, "sprite import options have a property table");
        const std::uint32_t ppu = FieldIndexOf(*spriteOptions.fields, "pixelsPerUnit");
        ImGuiWindow* inspector = ImGui::FindWindowByName("Inspector");
        Check(inspector != nullptr, "the inspector must have a window");
        const ImGuiID ppuField = LabelId(
            PushedId(LabelId(PushedId(inspector->ID, 1), "##import"), static_cast<int>(ppu)), "##value");
        Check(FindInspectorItem(editor, hwnd, ppuField, spot), "the pixels-per-unit row must be in the inspector");

        const fs::path metaPath = root / "Assets" / "art" / "hero.png.jmeta";
        const auto readMeta = [&]() {
            std::ifstream in(metaPath, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        };
        const std::string metaBefore = readMeta();
        Check(metaBefore.find("ImportOptions") == std::string::npos, "the fresh meta has no options block");

        const std::size_t undo = editor.GetCommands().GetUndoCount();
        DragFrom(editor, hwnd, spot, spot.x + 60);
        Check(editor.Tick(Frame), "the editor must tick after the drag");
        Check(editor.GetCommands().GetUndoCount() == undo + 1, "one drag is one command");
        const std::string metaAfter = readMeta();
        Check(metaAfter.find("ImportOptions") != std::string::npos && metaAfter.find("pixelsPerUnit:") != std::string::npos,
            "the meta on disk now carries the sprite options block");
        Check(metaAfter.find("pixelsPerUnit: 100\n") == std::string::npos, "with a value the drag moved");
        JBro::AssetMetaFile rewritten;
        JBro::AssetMetaError metaError;
        Check(JBro::ParseAssetMetaFile(metaAfter.c_str(), metaAfter.size(), rewritten, metaError)
                && rewritten.id == hero->id && false == rewritten.spriteId.IsNull() && rewritten.hasSpriteOptions,
            "the rewritten meta keeps its ids and reads back");
        Check(editor.GetSelectedAssetMeta() != nullptr && editor.GetSelectedAssetMeta()->hasSpriteOptions
                && editor.GetSelectedAssetMeta()->spriteOptions.pixelsPerUnit == rewritten.spriteOptions.pixelsPerUnit,
            "the inspector's copy follows the disk");
        Check(assetSystem->GetSprite(loadedSprite) != nullptr
                && assetSystem->GetSprite(loadedSprite)->options.pixelsPerUnit == rewritten.spriteOptions.pixelsPerUnit,
            "and the loaded sprite was reloaded in place with the new options");

        Check(editor.GetCommands().Undo(), "undo must run");
        Check(editor.Tick(Frame), "the editor must tick after undo");
        Check(readMeta() == metaBefore, "undo puts the original meta text back");
        Check(editor.GetSelectedAssetMeta() != nullptr && false == editor.GetSelectedAssetMeta()->hasSpriteOptions,
            "and the inspector's copy follows");
        Check(assetSystem->GetSprite(loadedSprite)->options.pixelsPerUnit == 100.0f,
            "and so does the loaded sprite");
        assetSystem->Release(loadedSprite);

        // 오브젝트를 고르면 에셋 선택은 빈다.
        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* object = canvas->CreateObject("Subject");
        editor.SetSelectedObject(object);
        Check(editor.GetSelectedAsset().IsNull() && editor.GetSelectedAssetMeta() == nullptr,
            "selecting an object clears the asset selection");

        // 프로젝트가 열린 채로 폴더에 그림이 하나 더 생기면 에디터가 감시로 알아 레지스트리에 넣는다(D-121).
        {
            std::ofstream png(root / "Assets" / "art" / "villain.png", std::ios::binary);
            png.write(reinterpret_cast<const char*>(TinyPng), sizeof(TinyPng));
        }
        bool registered = false;
        for (int attempt = 0; attempt < 300 && false == registered; ++attempt)
        {
            Check(editor.Tick(Frame), "the editor must tick while the watcher catches up");
            registered = editor.GetAssetRegistry().FindByPath("art/villain.png") != nullptr;
            if (false == registered)
            {
                Sleep(10);
            }
        }
        Check(registered, "a file added while the project is open is registered without reopening");
        Check(editor.GetAssetRegistry().Find(heroTextureId) != nullptr, "and the rescan keeps hero's id");

        editor.Shutdown();
        fs::remove_all(root, ignored);
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

        // 기본 패널이 다 있어야 한다. 하나라도 안 붙으면 화면에서 빈 칸이 된다.
        // 어느 것이 있어야 하는지는 `TestThePanelRegistryRefusesWhatItCannotHold` 가 이름으로 잰다.
        Check(editor.GetPanelCount() == 10, "the default panels must be registered");
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

        // **메뉴는 두 겹이다**(D-134). 프로젝트에 대한 `파일` 은 도크 뿌리에, 지금 연
        // 캔버스에 대한 `편집`·`창` 은 메인 도크에 있다 - 기존 엔진과 같은 나눔이다.
        // 그래서 두 막대를 다 훑는다.
        ImGuiWindow* root = ImGui::FindWindowByName("##EditorRoot");
        Check(root != nullptr, "the editor must have its root window");
        ImGuiWindow* main = ImGui::FindWindowByName("###MainDock");
        Check(main != nullptr, "and its main dock");

        bool found[3] = {};
        ImGuiWindow* const bars[2] = {root, main};
        for (ImGuiWindow* barWindow : bars)
        {
            // `BeginMenuBar` 가 `PushID("##MenuBar")` 를 하고, 메뉴는 그 아래에서
            // 제 이름으로 Id 를 받는다.
            const ImGuiID bar = LabelId(barWindow->ID, "##MenuBar");
            const ImRect rect = barWindow->MenuBarRect();
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
    // 세 백엔드에서 돈다(D-107·D-108). 에디터 UI 의 폰트 아틀라스·게임 뷰 텍스처·시저가 백엔드마다
    // 다른 길을 타므로, 화면이 나오는지는 백엔드마다 봐야 한다.
    void TestTheEditorPaintsItsOwnScreen(JBro::GraphicsApi api)
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.graphicsApi = api;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no device for this API; the editor screen not verified"
                << std::endl;
            return;
        }

        JBro::ProjectDescriptor project;
        constexpr char name[] = "EditorScreenProbe";
        project.name = {name, sizeof(name) - 1};
        project.graphicsApi = api;
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

        // **가운데 칸은 캔버스 뷰와 게임 뷰가 탭으로 나눠 쓴다**(D-130). 처음 보이는 것은
        // 편집 화면이므로, 게임 화면이 텍스처를 거쳐 패널까지 오는지 보려면 이쪽을 닫아
        // 게임 뷰를 앞으로 내놓는다.
        if (JBro::EditorPanel* canvasView = editor.FindPanel("CanvasView"))
        {
            canvasView->SetOpen(false);
        }

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
        // Vulkan 은 이제 백엔드다(D-108). 없는 것은 WebGPU 다.
        unsupportedConfig.graphicsApi = JBro::GraphicsApi::WebGPU;
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
            "EngineVersion: 0.1.0\n"
            "Framework: 2D\n"
            "RootPath: .\n"
            "ResolutionWidth: 1280\n"
            "ResolutionHeight: 720\n"
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
            projectPath.c_str(), error))
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

    void TestTheProjectFileDecidesTheFramework()
    {
        // 차원은 파일이 정한다(D-99). 부르는 쪽이 고르지 않으므로, 3D 라고 적힌 파일은
        // 3D 프레임워크로 열려야 한다. **어긋나면 조용히 틀리는 자리다** -
        // `GetCanvas` 가 만든 쪽을 믿고 static_cast 로 내려가기 때문이다.
        const JBro::String projectPath = TempPath("JBroEditor3D.jproject");
        Check(WriteTextFile(projectPath,
            "Version: 1\n"
            "EngineVersion: 0.1.0\n"
            "Framework: 3D\n"
            "ScriptOutputLibraryPath: \"\"\n"),
            "the test must be able to write its own project file");

        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        JBro::EditorApplication editor;
        Check(editor.Initialize(config), "the editor must initialize");

        JBro::ProjectFileError error;
        Check(editor.OpenProjectFile(projectPath.c_str(), error),
            "a 3D project must open without anyone naming the framework");
        Check(editor.GetProjectFile().framework == JBro::FrameworkKind::Framework3D,
            "and must come back as the 3D project it said it was");
        Check(editor.GetCanvas() != nullptr,
            "the canvas must come from the framework the file asked for");

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

    void TestAProjectOpensWithoutItsScriptModule()
    {
        // 스크립트 DLL 이 없는 프로젝트다(D-98). 아직 한 번도 빌드하지 않은 프로젝트가
        // 이 모양이고, 여기서 막으면 그것을 빌드할 에디터를 열 길이 없어진다.
        // 다만 **못 실었다는 사실은 남아야 한다** - 조용히 열면 스크립트가 도는 줄 안다.
        const JBro::String projectPath = TempPath("JBroEditorMissingDll.jproject");
        Check(WriteTextFile(projectPath,
            "Version: 1\n"
            "EngineVersion: 0.1.0\n"
            "Framework: 2D\n"
            "ScriptOutputLibraryPath: NoSuchScriptModule.dll\n"),
            "the test must be able to write its own project file");

        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        JBro::EditorApplication editor;
        Check(editor.Initialize(config), "the editor must initialize");

        JBro::ProjectFileError error;
        Check(editor.OpenProjectFile(
            projectPath.c_str(), error),
            "a project whose script module is missing must still open");
        Check(editor.HasOpenProject(), "and must really be open");
        Check(false == editor.IsScriptModuleLoaded(),
            "but it must not claim the script module is loaded");
        Check(false == editor.GetScriptModuleError().empty(),
            "and must say what it could not load rather than stay silent");
        Check(editor.GetScriptModuleError().find("NoSuchScriptModule.dll") != JBro::String::npos,
            "naming the module it failed on");

        editor.CloseProject();

        // 스크립트를 가리키지 않는 프로젝트는 싣지 못한 것이 없으므로 사유도 없다.
        Check(WriteTextFile(projectPath,
            "Version: 1\n"
            "EngineVersion: 0.1.0\n"
            "Framework: 2D\n"
            "ScriptOutputLibraryPath: \"\"\n"),
            "the test must be able to rewrite its project file");
        Check(editor.OpenProjectFile(
            projectPath.c_str(), error),
            "a project with no script module at all must open");
        Check(false == editor.IsScriptModuleLoaded(), "with nothing loaded");
        Check(editor.GetScriptModuleError().empty(),
            "and with no complaint, because it never asked for one");

        editor.Shutdown();
        std::remove(projectPath.c_str());
    }
    // **기즈모를 끌면 고른 오브젝트가 움직이고, 되돌리기 한 번이 그것을 되돌린다**(D-109). 손잡이는
    // ImGui 의 hovered Id 로 보이므로 화면을 훑어 찾는다 - 어디에 그려졌는지 미리 알 필요가 없다.
    void TestDraggingTheGizmoMovesTheSelectionUnderOneUndo()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = WindowWidth;
        config.windowHeight = WindowHeight;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the gizmo not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "GizmoProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({320, 240}), "the editor UI must turn on");
        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* eye = canvas->CreateObject("Eye");
        Check(canvas->AttachComponent<JBro::Component::Transform2D>(eye) != nullptr, "the camera needs a transform");
        auto* camera = canvas->AttachComponent<JBro::Component::Camera2D>(eye);
        Check(camera != nullptr, "the probe camera must attach");
        camera->primary = true;
        JBro::GameObject* box = canvas->CreateObject("Box");
        auto* transform = canvas->AttachComponent<JBro::Component::Transform2D>(box);
        Check(transform != nullptr, "the box needs a transform for the gizmo to hold");
        editor.SetSelectedObject(box);
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must exist");
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle before the gizmo appears");
        }
        // **기즈모는 캔버스 뷰에 있다**(D-130·D-131). 게임 뷰는 시뮬레이션 화면이라
        // 손잡이도 피킹도 없다 - 기존 엔진의 `CGameViewTool` 과 같다.
        ImGuiWindow* game = ImGui::FindWindowByName("CanvasView");
        Check(game != nullptr, "the canvas view window must exist");

        Spot spot;
        Check(FindItemAnywhereInWindow(editor, hwnd, game, LabelId(game->ID, "##gizmo_x"), spot),
            "the x handle of the translate gizmo must be on screen");
        const std::size_t undo = editor.GetCommands().GetUndoCount();
        DragFrom(editor, hwnd, spot, spot.x + 40);
        Check(transform->position.x > 0.05f && std::fabs(transform->position.y) < 1.0e-4f,
            "dragging the x handle to the right moves the box along +x and nowhere else");
        Check(editor.GetCommands().GetUndoCount() == undo + 1, "one drag must be one undo step");
        Check(editor.GetCommands().Undo() && std::fabs(transform->position.x) < 1.0e-4f,
            "undoing the drag must put the box back");

        // E 는 회전이다. 고리가 나오고, 그것을 끌면 돈다. 마우스는 편집 화면 밖으로 빼 둔다 - 손잡이를 잡았을 때
        // 창이 포커스를 받았어야 핫키가 먹는다.
        PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(2, 2));
        Check(editor.Tick(Frame), "the editor must tick with the mouse away");
        PostMessageW(hwnd, WM_KEYDOWN, 'E', 0);
        Check(editor.Tick(Frame), "the editor must tick with E down");
        PostMessageW(hwnd, WM_KEYUP, 'E', 0);
        Check(editor.Tick(Frame), "the editor must tick with E up");
        Check(FindItemAnywhereInWindow(editor, hwnd, game, LabelId(game->ID, "##gizmo_z"), spot),
            "E must switch to rotation and put the z ring on screen");
        Spot to = spot;
        to.x += 40;
        to.y += 40;
        DragTo(editor, hwnd, spot, to);
        Check(std::fabs(transform->rotation) > 1.0f, "dragging the ring must turn the box");
        Check(std::fabs(transform->position.x) < 1.0e-4f, "and must not move it");
        Check(editor.GetCommands().GetUndoCount() == undo + 1, "the rotation is one undo step too");

        // R 은 크기다. x 상자를 바깥으로 끌면 x 만 커진다.
        PostMessageW(hwnd, WM_KEYDOWN, 'R', 0);
        Check(editor.Tick(Frame), "the editor must tick with R down");
        PostMessageW(hwnd, WM_KEYUP, 'R', 0);
        Check(editor.Tick(Frame), "the editor must tick with R up");
        Check(editor.GetCommands().Undo(), "undo the rotation so the x handle points right again");
        Check(FindItemAnywhereInWindow(editor, hwnd, game, LabelId(game->ID, "##gizmo_x"), spot),
            "R must switch to scale and keep an x handle on screen");
        DragFrom(editor, hwnd, spot, spot.x + 30);
        Check(transform->scale.x > 1.05f && std::fabs(transform->scale.y - 1.0f) < 1.0e-4f,
            "dragging the x box outwards scales x only");

        // **부모가 돌아 있으면 델타는 부모 좌표계로 돌아온다.** 90도 돈 부모 아래의 자식은 자기 x 축이 화면 위를
        // 가리킨다. 그 손잡이를 위로 끌면 월드로는 +y 지만 로컬 position 은 +x 만 늘어야 한다.
        PostMessageW(hwnd, WM_KEYDOWN, 'W', 0);
        Check(editor.Tick(Frame), "the editor must tick with W down");
        PostMessageW(hwnd, WM_KEYUP, 'W', 0);
        Check(editor.Tick(Frame), "the editor must tick with W up");
        JBro::GameObject* parent = canvas->CreateObject("Turned");
        auto* parentTransform = canvas->AttachComponent<JBro::Component::Transform2D>(parent);
        Check(parentTransform != nullptr, "the parent needs a transform");
        parentTransform->rotation = 90.0f;
        JBro::GameObject* child = canvas->CreateObject("Child");
        auto* childTransform = canvas->AttachComponent<JBro::Component::Transform2D>(child);
        Check(childTransform != nullptr, "the child needs a transform");
        child->SetParent(parent);
        editor.SetSelectedObject(child);
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle on the child");
        }
        Check(FindItemAnywhereInWindow(editor, hwnd, game, LabelId(game->ID, "##gizmo_x"), spot),
            "the child's x handle must be on screen");
        // 잡기만 하고 놓으면 바뀐 것이 없다. 빈 커맨드를 되돌리기 더미에 넣으면 안 된다.
        const std::size_t untouched = editor.GetCommands().GetUndoCount();
        ClickAt(editor, hwnd, spot);
        Check(editor.GetCommands().GetUndoCount() == untouched, "a click that moves nothing is not an undo step");
        to = spot;
        to.y -= 40;
        DragTo(editor, hwnd, spot, to);
        Check(childTransform->position.x > 0.05f && std::fabs(childTransform->position.y) < 1.0e-3f,
            "a world +y drag on a child of a 90-degree parent must land in the child's local +x");
        editor.Shutdown();
    }

    // **재생을 누르기 전의 캔버스로 돌아온다**(D-131). 게임이 만든 것과 고친 값이
    // 편집 중인 캔버스에 남으면, 저장했을 때 게임이 만든 상태가 파일이 된다.
    void TestPlayingAndStoppingRestoresTheCanvas()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 640;
        config.windowHeight = 480;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; simulation not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "SimulationProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        Check(false == editor.IsSimulationPlaying(), "the editor opens stopped");

        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* kept = canvas->CreateObject("Kept");
        auto* transform = canvas->AttachComponent<JBro::Component::Transform2D>(kept);
        Check(transform != nullptr, "the object needs a transform");
        transform->position = JBro::Vec2{3.0f, 4.0f};
        Check(editor.Tick(Frame), "the editor must tick before play");

        Check(editor.StartSimulation(), "play must start");
        Check(editor.IsSimulationPlaying(), "and say so");

        // 게임이 하는 일을 흉내 낸다: 오브젝트를 하나 만들고 값을 고친다.
        JBro::GameObject* spawned = canvas->CreateObject("Spawned");
        Check(spawned != nullptr, "the running game may spawn");
        transform->position = JBro::Vec2{-9.0f, -9.0f};
        Check(editor.Tick(Frame), "the editor must tick while playing");

        // **재생 중에는 캔버스를 파일로 쓰지 않는다**(D-153). 게임이 만든 오브젝트가 파일이 되면
        // 정지로 되돌린 뒤에도 파일에 남는다.
        {
            const JBro::String blockedPath = TempPath("JBroPlaySaveProbe.jcanvas");
            std::error_code ignoredError;
            std::filesystem::remove(std::filesystem::path(blockedPath.c_str()), ignoredError);
            JBro::CanvasFileError saveError;
            Check(false == editor.SaveCanvas(blockedPath.c_str(), saveError),
                "saving while the simulation runs must be refused");
            Check(false == std::filesystem::exists(std::filesystem::path(blockedPath.c_str()),
                    ignoredError),
                "and nothing may be written");
        }

        editor.StopSimulation();
        Check(false == editor.IsSimulationPlaying(), "stop must stop");
        Check(canvas->GetObjectCount() == 1, "what the game made must be gone");

        JBro::Array<JBro::GameObject*> roots;
        canvas->GetRootObjects(roots);
        Check(roots.Size() == 1, "and one root must be back");
        Check(std::strcmp(roots[0]->GetTag(), "Kept") == 0, "the one that was there before play");
        auto* restored = canvas->FindComponentRaw<JBro::Component::Transform2D>(roots[0]);
        Check(restored != nullptr, "with its component");
        Check(std::fabs(restored->position.x - 3.0f) < 1.0e-4f
                && std::fabs(restored->position.y - 4.0f) < 1.0e-4f,
            "and the value it had before play, not the one the game wrote");
        Check(editor.GetSelectedObject() == nullptr,
            "the selection is cleared, because the objects it pointed at are gone");

        editor.Shutdown();
    }

    // **빈 곳을 끌면 상자가 되고, 그 안에 닿은 것이 모두 골라진다**(기존 엔진의 드래그 박스
    // 선택). 클릭 한 번과 갈라야 한다 - 끌지 않고 누른 것은 빈 상자가 아니라 클릭이다.
    void TestBoxSelectInTheCanvasViewPicksWhatItTouches()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; box select not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "BoxSelectProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");

        JBro::Canvas* canvas = editor.GetCanvas();
        // 가까이 둘, 멀리 하나. 상자가 앞의 둘만 잡아야 한다.
        JBro::GameObject* left = canvas->CreateObject("Left");
        JBro::GameObject* right = canvas->CreateObject("Right");
        JBro::GameObject* far_ = canvas->CreateObject("Far");
        auto* leftTransform = canvas->AttachComponent<JBro::Component::Transform2D>(left);
        auto* rightTransform = canvas->AttachComponent<JBro::Component::Transform2D>(right);
        auto* farTransform = canvas->AttachComponent<JBro::Component::Transform2D>(far_);
        Check(leftTransform != nullptr && rightTransform != nullptr && farTransform != nullptr,
            "all three need transforms");
        leftTransform->position = JBro::Vec2{-1.0f, 0.0f};
        rightTransform->position = JBro::Vec2{1.0f, 0.0f};
        farTransform->position = JBro::Vec2{0.0f, 4.0f};
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        ImGuiWindow* view = ImGui::FindWindowByName("CanvasView");
        Check(view != nullptr, "the canvas view must have a window");
        // 화면 한가운데가 월드 원점이다. 기본 배율에서 x -1..1 은 가운데 근처이고
        // y 4 는 위쪽이라 상자 밖이다.
        const float centerX = view->Pos.x + view->Size.x * 0.5f;
        const float centerY = view->Pos.y + view->Size.y * 0.5f;

        Spot from;
        from.x = static_cast<int>(centerX - 120.0f);
        from.y = static_cast<int>(centerY - 40.0f);
        Spot to;
        to.x = static_cast<int>(centerX + 120.0f);
        to.y = static_cast<int>(centerY + 40.0f);
        DragTo(editor, hwnd, from, to);

        Check(editor.GetSelectionCount() == 2, "the box must pick the two it touched");
        Check(editor.IsSelected(left) && editor.IsSelected(right), "those two");
        Check(false == editor.IsSelected(far_), "and not the one outside it");

        // **끌지 않고 누른 것은 상자가 아니다.** 빈 곳을 한 번 누르면 선택이 풀린다.
        Spot empty;
        empty.x = static_cast<int>(centerX);
        empty.y = static_cast<int>(centerY - 150.0f);
        ClickAt(editor, hwnd, empty);
        Check(editor.GetSelectionCount() == 0,
            "a plain click on empty space clears the selection instead of boxing nothing");

        editor.Shutdown();
    }

    // **3D 프로젝트에서도 편집 화면이 그려진다**(D-136). 게임 카메라가 하나도 없어도
    // 그려야 한다 - 캔버스 뷰는 만드는 사람이 보는 화면이고, 카메라를 놓기 전에도 필요하다.
    void TestTheCanvasViewDrawsInA3DProject()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 640;
        config.windowHeight = 480;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the 3D canvas view not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "CanvasView3DProbe";
        project.name = {name, sizeof(name) - 1};
        project.framework = JBro::FrameworkKind::Framework3D;
        Check(editor.OpenProject(project), "the 3D probe project must open");
        Check(editor.GetFrameworkKind() == JBro::FrameworkKind::Framework3D,
            "and the editor must know which dimension it is in");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");

        // 게임 뷰는 닫아 둔다. 그래야 남는 뷰가 편집 화면의 것 하나뿐이다.
        if (JBro::EditorPanel* gameView = editor.FindPanel("Game"))
        {
            gameView->SetOpen(false);
        }
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        Check(editor.GetCanvasViewTexture().IsValid(),
            "the canvas view must have a texture to draw into");
        JBro::Renderer* renderer = editor.GetRenderer();
        Check(renderer != nullptr, "the editor must expose its renderer");
        const JBro::RendererFrameStats stats = renderer->GetLastFrameStats();
        Check(stats.viewCount >= 1,
            "and a view must be recorded for it even with no camera in the canvas");

        // **그린 편집 카메라를 렌더러가 내준다**(D-140). 3D 의 기즈모가 화면과 월드를
        // 이으려면 이것이 있어야 하고, 여기서 같은 행렬을 다시 세우면 둘로 갈린다.
        JBro::CameraParams editorCamera;
        Check(renderer->GetLastEditorViewCamera(editorCamera),
            "the renderer must hand back the editor camera it drew with");

        // 그 카메라로 손잡이가 선다. 오브젝트를 하나 놓고 골라 본다.
        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* box = canvas->CreateObject("Box");
        Check(canvas->AttachComponent<JBro::Component::Transform3D>(box) != nullptr,
            "the box needs a 3D transform for the gizmo to hold");
        editor.SetSelectedObject(box);
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle on the selection");
        }
        ImGuiWindow* view = ImGui::FindWindowByName("CanvasView");
        Check(view != nullptr, "the canvas view must have a window");
        Spot spot;
        Check(FindItemAnywhereInWindow(editor, hwnd, view, LabelId(view->ID, "##gizmo_x"), spot),
            "the x handle must be on screen in a 3D project too");
        SaveScreenshot(*renderer, 640, 480, "canvas3d");

        // **바닥 격자가 실제로 화면에 닿는다**(D-140). 켠 프레임과 끈 프레임의 픽셀이
        // 달라야 한다 - 그리는 함수를 불렀는지가 아니라 그림이 바뀌었는지를 묻는다.
        const char* gridLabel = JBro::Loc::TextOr(JBro::LocKeys::CanvasViewGrid, "Grid");
        Spot gridButton;
        Check(FindItemAnywhereInWindow(editor, hwnd, view, LabelId(view->ID, gridLabel), gridButton),
            "the grid button must be on the canvas view tool bar");
        JBro::Array<std::byte> withoutGrid;
        JBro::Array<std::byte> withGrid;
        JBro::TextureReadback readback;
        ClickAt(editor, hwnd, gridButton);
        Check(editor.Tick(Frame), "the editor must settle with the grid off");
        ReadBackBufferInto(*renderer, 640, 480, withoutGrid, readback);
        // 마우스는 단추 위에 그대로 둔 채 다시 누른다. 자리를 옮기면 단추의 강조가
        // 달라져 그 픽셀까지 차이에 섞인다.
        ClickAt(editor, hwnd, gridButton);
        Check(editor.Tick(Frame), "the editor must settle with the grid on");
        ReadBackBufferInto(*renderer, 640, 480, withGrid, readback);
        const std::size_t gridPixels =
            CountDifferingPixels(withoutGrid, withGrid, readback, 640, 480);
        std::cout << "  the 3D floor grid painted " << gridPixels << " pixels" << std::endl;
        Check(gridPixels > 500, "the floor grid must paint something the empty view does not");

        editor.Shutdown();
    }

    // **설정 창이 고친 값은 파일에 남고, 모르는 키는 그대로 남는다**(D-137).
    void TestProjectSettingsAreWrittenBackToTheFile()
    {
        const JBro::String projectPath = TempPath("JBroSettingsTest.jproject");
        Check(WriteTextFile(projectPath,
            "# 이 주석은 살아남아야 한다\n"
            "Version: 1\n"
            "EngineVersion: 0.1.0\n"
            "Framework: 2D\n"
            "ResolutionWidth: 1280\n"
            "ResolutionHeight: 720\n"
            "SomeFutureKey: keep me\n"
            "ScriptOutputLibraryPath: \"\"\n"
            "Build:\n"
            "  ProductName: SettingsTest\n"),
            "the test must be able to write its own project file");

        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        JBro::EditorApplication editor;
        Check(editor.Initialize(config), "the editor must initialize");

        JBro::ProjectFileError error;
        Check(editor.OpenProjectFile(projectPath.c_str(), error),
            "the editor must open the project");
        Check(editor.GetProjectFilePath() == projectPath,
            "and must know which file it came from");

        JBro::ProjectFile edited = editor.GetProjectFile();
        edited.resolutionWidth = 640;
        edited.resolutionHeight = 480;
        edited.build.productName = "Renamed";
        edited.assetDirectory = "Art";
        Check(editor.SaveProjectSettings(edited, error), "saving the settings must go through");

        // 에디터가 든 값도 파일의 것으로 맞춰져야 한다.
        Check(editor.GetProjectFile().resolutionWidth == 640,
            "the editor must hold what the file now says");
        Check(editor.GetProjectFile().assetDirectory == "Art", "all of it");

        // 파일을 직접 읽어 본다. 모르는 키와 주석이 살아 있어야 한다.
        JBro::String text;
        {
            std::FILE* file = nullptr;
            Check(fopen_s(&file, projectPath.c_str(), "rb") == 0 && file != nullptr,
                "the project file must be readable again");
            char buffer[4096] = {};
            const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
            std::fclose(file);
            text.assign(buffer, read);
        }
        Check(text.find("SomeFutureKey: keep me") != JBro::String::npos,
            "a key the engine does not know must survive the write");
        Check(text.find("# 이 주석은 살아남아야 한다") != JBro::String::npos,
            "and so must a comment");
        Check(text.find("ResolutionWidth: 640") != JBro::String::npos,
            "with the value that changed");
        Check(text.find("ProductName: Renamed") != JBro::String::npos, "inside the block too");
        Check(text.find("AssetDirectory: Art") != JBro::String::npos,
            "and a key that was not in the file is added");

        editor.Shutdown();
        std::remove(projectPath.c_str());
    }

    // **에셋 파일을 다루면 `.jmeta` 가 함께 움직인다**(D-139). 짝을 잃으면 그 에셋의
    // 아이디가 사라지고, 그것을 가리키던 컴포넌트의 참조가 전부 풀린다.
    // **에셋 브라우저에서 파일 여러 개를 고른다**(D-141). Shift 는 닻에서 누른 줄까지를
    // 고르고, 고른 것들은 한 번에 지워진다. `.jmeta` 는 그때도 함께 간다.
    void TestTheAssetBrowserSelectsManyFilesAtOnce()
    {
        namespace fs = std::filesystem;
        const fs::path root(TempPath("JBroAssetMultiProbe").c_str());
        std::error_code ignored;
        fs::remove_all(root, ignored);
        fs::create_directories(root / "Assets" / "art", ignored);
        const char* names[] = {"a.png", "b.png", "c.png", "d.png"};
        for (const char* name : names)
        {
            std::ofstream png(root / "Assets" / "art" / name, std::ios::binary);
            png.write(reinterpret_cast<const char*>(TinyPng), sizeof(TinyPng));
        }
        const JBro::String projectPath = TempPath("JBroAssetMultiProbe\\Multi.jproject");
        Check(WriteTextFile(projectPath,
            "Version: 1\n"
            "EngineVersion: 0.1.0\n"
            "Framework: 2D\n"
            "RootPath: .\n"
            "ResolutionWidth: 640\n"
            "ResolutionHeight: 480\n"
            "AssetDirectory: Assets\n"
            "ScriptOutputLibraryPath: \"\"\n"),
            "the test must be able to write its own project file");

        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; multi-select not verified" << std::endl;
            return;
        }
        JBro::ProjectFileError error;
        Check(editor.OpenProjectFile(projectPath.c_str(), error), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        ImGuiWindow* assets = ImGui::FindWindowByName("Assets");
        Check(assets != nullptr, "the asset browser must have a window");
        if (false == assets->DockTabIsVisible && assets->DockNode != nullptr
            && assets->DockNode->TabBar != nullptr)
        {
            ImGuiTabBar* tabBar = assets->DockNode->TabBar;
            ImGuiTabItem* tab = ImGui::TabBarFindTabByID(tabBar, assets->TabId);
            Check(tab != nullptr, "the asset browser must have a tab in its dock");
            Spot tabSpot;
            tabSpot.x = static_cast<int>(tabBar->BarRect.Min.x + tab->Offset + tab->Width * 0.5f);
            tabSpot.y = static_cast<int>((tabBar->BarRect.Min.y + tabBar->BarRect.Max.y) * 0.5f);
            ClickAt(editor, hwnd, tabSpot);
            for (int frame = 0; frame < 3; ++frame)
            {
                Check(editor.Tick(Frame), "the editor must settle on the tab");
            }
            assets = ImGui::FindWindowByName("Assets");
        }
        Check(assets != nullptr && assets->DockTabIsVisible, "the asset browser tab must be in front");

        // 왼쪽 나무에서 `art` 를 연다.
        const auto findRow = [&](ImGuiWindow* pane, ImGuiID rowId, Spot& out) {
            const int x = static_cast<int>(pane->Pos.x + 40.0f);
            const int bottom = static_cast<int>(pane->Pos.y + pane->Size.y);
            for (int y = static_cast<int>(pane->Pos.y); y < bottom; y += 3)
            {
                PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                Check(editor.Tick(Frame), "the editor must tick while looking for a row");
                if (ImGui::GetHoveredID() == rowId)
                {
                    out.x = x;
                    out.y = y;
                    return true;
                }
            }
            return false;
        };

        {
            ImGuiWindow* tree = FindChildWindow(assets, "##tree");
            Check(tree != nullptr, "the folder tree pane must exist");
            Spot folder;
            Check(findRow(tree, LabelId(LabelId(tree->ID, "art"), "##folder"), folder),
                "the art folder must be a row in the tree");
            ClickAt(editor, hwnd, folder);
            for (int frame = 0; frame < 3; ++frame)
            {
                Check(editor.Tick(Frame), "the editor must settle on the folder");
            }
        }

        assets = ImGui::FindWindowByName("Assets");
        ImGuiWindow* contents = FindChildWindow(assets, "##contents");
        Check(contents != nullptr, "the contents pane must exist");
        Spot first;
        Spot third;
        Check(findRow(contents, LabelId(LabelId(contents->ID, "art/a.png"), "##file"), first),
            "a.png must be a row in the art folder");
        Check(findRow(contents, LabelId(LabelId(contents->ID, "art/c.png"), "##file"), third),
            "c.png must be a row in the art folder");

        // **닻에서 누른 줄까지.** a 를 누르고 c 를 Shift 로 누르면 a·b·c 가 고른 것이다.
        ClickAt(editor, hwnd, first);
        ClickAtWith(editor, hwnd, third, ImGuiMod_Shift);
        for (int frame = 0; frame < 2; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle on the range");
        }

        // 지우기는 메뉴를 거친다. 줄에 대고 오른쪽을 누르면 고른 것이 그대로 남는다.
        PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(third.x, third.y));
        Check(editor.Tick(Frame), "the editor must tick before the menu");
        PostMessageW(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(third.x, third.y));
        Check(editor.Tick(Frame), "the editor must tick on the right press");
        PostMessageW(hwnd, WM_RBUTTONUP, 0, MAKELPARAM(third.x, third.y));
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle with the menu open");
        }
        // 컨텍스트 메뉴의 창 이름은 ImGui 가 짓는다("##Popup_xxxxxxxx"). 우리가 준 이름은
        // Id 로만 남으므로 이름 조각으로는 그쪽을 짚는다.
        ImGuiWindow* menu = FindActiveWindowContaining("##Popup_");
        Check(menu != nullptr, "the entry menu must be open");
        Spot deleteItem;
        const char* deleteLabel = JBro::Loc::TextOr(JBro::LocKeys::AssetsDelete, "Delete");
        Check(FindItemAnywhereInWindow(editor, hwnd, menu, LabelId(menu->ID, deleteLabel), deleteItem),
            "the menu must have a delete item");
        ClickAt(editor, hwnd, deleteItem);
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle with the question open");
        }

        ImGuiWindow* ask = ImGui::FindWindowByName("##DeleteAsset");
        Check(ask != nullptr, "the delete question must be on screen");
        Spot confirm;
        Check(FindItemAnywhereInWindow(editor, hwnd, ask, LabelId(ask->ID, deleteLabel), confirm),
            "the question must have a delete button");
        ClickAt(editor, hwnd, confirm);
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle after the delete");
        }

        // **셋이 갔고 넷째는 남았다.** `.jmeta` 도 함께 갔다 - 남으면 다음 스캔이 주인 없는
        // 아이디를 다시 들여온다.
        Check(false == fs::exists(root / "Assets" / "art" / "a.png", ignored), "a.png is gone");
        Check(false == fs::exists(root / "Assets" / "art" / "b.png", ignored), "b.png is gone");
        Check(false == fs::exists(root / "Assets" / "art" / "c.png", ignored), "c.png is gone");
        Check(false == fs::exists(root / "Assets" / "art" / "a.png.jmeta", ignored),
            "and its meta went with it");
        Check(fs::exists(root / "Assets" / "art" / "d.png", ignored),
            "the one that was not selected stays");
        Check(editor.GetAssetRegistry().FindByPath("art/a.png") == nullptr,
            "the registry no longer knows the deleted file");
        Check(editor.GetAssetRegistry().FindByPath("art/d.png") != nullptr,
            "and still knows the one that stayed");

        editor.Shutdown();
        fs::remove_all(root, ignored);
    }

    // **이름과 활성은 커맨드를 거친다**(D-142). 예전에는 인스펙터가 값을 그대로 썼고
    // 이름은 아예 고칠 수 없었다 - 만든 오브젝트의 이름이 `GameObject` 인 채로 굳었다.
    // **콜라이더의 모양이 캔버스 뷰에 보인다**(D-143). 물리는 눈에 보이지 않아서,
    // 그려 주지 않으면 충돌 칸이 그림과 어긋난 것을 부딪혀 봐야만 안다.
    // **통계 창이 캔버스의 쓰임새를 보인다**(D-145). 기존 엔진의 CPU 프로파일러가 내던
    // 숫자들(오브젝트·풀·선택·되돌리기)이 우리에게는 없었다.
    // **다시 열면 보던 자리에서 이어 본다**(D-146). 보던 캔버스와 편집 카메라, 에디터 언어가
    // 프로젝트 파일에 남는다. 기존 엔진도 이 셋을 프로젝트에 적었다.
    // **집는 칸이 에셋이 정한 크기를 따른다**(D-148). `sizeMode` 가 `FromSprite` 면 실제 크기는
    // 칸 픽셀을 그 에셋의 PPU 로 나눈 값이다(D-117). 선언된 `size` 를 대신 쓰면 집는 칸이
    // 그림과 어긋나, 그림 밖 빈 곳을 눌러도 잡히고 그림 가장자리를 눌러도 놓친다.
    // **패널은 공용 위젯 계층을 거친다**(§11.1, D-152). 글자·단추·메뉴·팝업·콤보를 패널이
    // `ImGui::` 로 곧장 부르면 같은 자리가 패널마다 다른 모양이 된다. 소스를 읽어 막는다 -
    // 눈으로 훑는 검사는 새 패널이 생길 때마다 다시 해야 하고, 다시 하지 않게 된다.
    // **에셋 브라우저에서 끌어 에셋 칸에 놓으면 고른다**(D-154, 기존 `ImAssetField::AllowDrop`).
    // 목록의 줄은 그림의 Texture 레코드인데 스프라이트 칸은 Sprite 를 받는다 - 꾸러미가 짝을
    // 함께 싣고 오지 않으면 이 드롭은 조용히 무시된다.
    void TestDraggingAnAssetOntoTheFieldPicksIt()
    {
        namespace fs = std::filesystem;
        const fs::path root(TempPath("JBroAssetDropProbe").c_str());
        std::error_code ignored;
        fs::remove_all(root, ignored);
        fs::create_directories(root / "Assets" / "sub", ignored);
        {
            std::ofstream png(root / "Assets" / "hero.png", std::ios::binary);
            png.write(reinterpret_cast<const char*>(TinyPng), sizeof(TinyPng));
            std::ofstream inner(root / "Assets" / "sub" / "inner.png", std::ios::binary);
            inner.write(reinterpret_cast<const char*>(TinyPng), sizeof(TinyPng));
        }
        const JBro::String projectPath = TempPath("JBroAssetDropProbe\\Drop.jproject");
        Check(WriteTextFile(projectPath,
            "Version: 1\n"
            "EngineVersion: 0.1.0\n"
            "Framework: 2D\n"
            "RootPath: .\n"
            "AssetDirectory: Assets\n"),
            "the test must be able to write its own project file");

        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; asset drop not verified" << std::endl;
            return;
        }
        JBro::ProjectFileError error;
        Check(editor.OpenProjectFile(projectPath.c_str(), error), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");

        const JBro::AssetRegistry& registry = editor.GetAssetRegistry();
        JBro::AssetId spriteAsset;
        for (std::size_t index = 0; index < registry.GetCount(); ++index)
        {
            const JBro::AssetRecord& record = registry.GetRecord(index);
            if (record.type == JBro::AssetType::Sprite && record.relativePath == "hero.png")
            {
                spriteAsset = record.id;
            }
        }
        Check(false == spriteAsset.IsNull(), "the scan must have registered hero.png as a sprite");

        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* object = canvas->CreateObject("Hero");
        auto* sprite = canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(object);
        Check(sprite != nullptr, "the hero must have a sprite renderer");
        editor.SetSelectedObject(object);
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        // 에셋 창을 앞으로 꺼내고, 뿌리 폴더(처음 열린 자리)에서 hero.png 줄을 찾는다.
        ImGui::SetWindowFocus("Assets");
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle on the asset tab");
        }
        ImGuiWindow* assets = ImGui::FindWindowByName("Assets");
        ImGuiWindow* contents = FindChildWindow(assets, "##contents");
        Check(contents != nullptr, "the contents pane must exist");
        const ImGuiID heroRow = LabelId(LabelId(contents->ID, "hero.png"), "##file");
        Spot from;
        bool found = false;
        const int x = static_cast<int>(contents->Pos.x + 40.0f);
        const int bottom = static_cast<int>(contents->Pos.y + contents->Size.y);
        for (int y = static_cast<int>(contents->Pos.y); y < bottom && false == found; y += 3)
        {
            PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
            Check(editor.Tick(Frame), "the editor must tick while looking for the row");
            if (ImGui::GetHoveredID() == heroRow)
            {
                from.x = x;
                from.y = y;
                found = true;
            }
        }
        Check(found, "hero.png must be a row in the asset browser");

        const JBro::PropertyTable* table = JBro::PropertyRegistry::Lookup(
            JBro::NameTable::Get().Intern("Component::SpriteRenderer2D"));
        Check(table != nullptr, "the sprite renderer must have registered its properties");
        Spot to;
        Check(FindInspectorItem(editor, hwnd,
                InspectorFieldId(0, FieldIndexOf(*table, "spriteId"), "##value"), to),
            "the spriteId row must be in the inspector");

        const std::size_t undoBefore = editor.GetCommands().GetUndoCount();
        DragTo(editor, hwnd, from, to);
        Check(sprite->spriteId == spriteAsset,
            "dropping the image on the sprite field picks its sprite record");
        Check(editor.GetCommands().GetUndoCount() == undoBefore + 1, "through one command");
        Check(editor.GetCommands().Undo(), "and it undoes");
        Check(sprite->spriteId.IsNull(), "back to no sprite");

        // **누른 줄에서 뗐을 때만 고른다**(D-158). 오른쪽 칸에서 폴더를 누르면 그 자리에 폴더 속
        // 파일이 나타나는데, 뗀 자리만 보고 고르면 그 파일이 골라졌다(실제 에디터에서 그랬다).
        {
            Check(editor.GetSelectedAsset().IsNull(), "no asset is selected before opening the folder");
            assets = ImGui::FindWindowByName("Assets");
            contents = FindChildWindow(assets, "##contents");
            const ImGuiID subRow = LabelId(LabelId(contents->ID, "sub"), "##sub");
            Spot folder;
            bool foundFolder = false;
            for (int y = static_cast<int>(contents->Pos.y); y < bottom && false == foundFolder; y += 3)
            {
                PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                Check(editor.Tick(Frame), "the editor must tick while looking for the folder");
                if (ImGui::GetHoveredID() == subRow)
                {
                    folder.x = x;
                    folder.y = y;
                    foundFolder = true;
                }
            }
            Check(foundFolder, "the sub folder must be a row in the contents pane");
            // **사람의 누름 길이로 누른다.** 누름과 뗌 사이에 한 프레임만 두면, 사라진 폴더 줄이 아직
            // 눌린 항목으로 남아 새 줄의 호버가 막혀 결함이 드러나지 않는다(처음 쓴 판이 그랬다 -
            // 누른 줄 검사를 빼도 통과했다). 사람은 누른 채로 몇 프레임을 보낸다.
            PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(folder.x, folder.y));
            Check(editor.Tick(Frame), "the editor must tick");
            PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(folder.x, folder.y));
            for (int frame = 0; frame < 5; ++frame)
            {
                Check(editor.Tick(Frame), "the editor must tick while the button is held");
            }
            PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(folder.x, folder.y));
            Check(editor.Tick(Frame), "the editor must tick on the release");
            Check(editor.Tick(Frame), "the editor must settle in the folder");
            Check(editor.GetSelectedAsset().IsNull(),
                "opening a folder must not select the file that appears under the cursor");
        }

        // **그림 줄을 두 번 누르면 스프라이트 뷰어가 열린다**(D-155·D-159). 실제 에디터에서 열리지 않았다.
        // 사람처럼 누른다: 누름마다 몇 프레임을 들고, 두 번째 누름은 창이 받는 모양대로 `WM_LBUTTONDBLCLK` 다.
        {
            for (int frame = 0; frame < 30; ++frame)
            {
                Check(editor.Tick(Frame), "the editor must wait out the double-click time");
            }
            Check(editor.GetSpriteViewerTabCount() == 0, "no viewer tab before the double-click");
            assets = ImGui::FindWindowByName("Assets");
            contents = FindChildWindow(assets, "##contents");
            const ImGuiID innerRow = LabelId(LabelId(contents->ID, "sub/inner.png"), "##file");
            // **줄의 가운데를 누른다.** 호버가 잡힌 구간의 한가운데를 쓴다.
            int firstY = -1;
            int lastY = -1;
            for (int y = static_cast<int>(contents->Pos.y); y < bottom; y += 2)
            {
                PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                Check(editor.Tick(Frame), "the editor must tick while looking for the picture row");
                // `GetHoveredID` 는 이 프레임에 없으면 **앞 프레임의 호버**를 준다. 폴더를 누른 자리에 지금
                // inner.png 가 있어, 첫 줄(창 맨 위)이 그 줄로 잘못 읽혔다. 이 프레임의 것만 본다.
                if (ImGui::GetCurrentContext()->HoveredId == innerRow)
                {
                    firstY = firstY < 0 ? y : firstY;
                    lastY = y;
                }
                else if (firstY >= 0)
                {
                    break;
                }
            }
            Check(firstY >= 0, "inner.png must be a row inside the sub folder");
            Spot inner;
            inner.x = x;
            inner.y = (firstY + lastY) / 2;
            PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(inner.x, inner.y));
            Check(editor.Tick(Frame), "the editor must tick");
            Check(ImGui::GetCurrentContext()->HoveredId == innerRow, "the middle of the row is on the row");
            for (const UINT press : {static_cast<UINT>(WM_LBUTTONDOWN), static_cast<UINT>(WM_LBUTTONDBLCLK)})
            {
                PostMessageW(hwnd, press, MK_LBUTTON, MAKELPARAM(inner.x, inner.y));
                for (int frame = 0; frame < 3; ++frame)
                {
                    Check(editor.Tick(Frame), "the editor must tick while the button is held");
                }
                PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(inner.x, inner.y));
                for (int frame = 0; frame < 2; ++frame)
                {
                    Check(editor.Tick(Frame), "the editor must tick on the release");
                }
            }
            Check(editor.GetSpriteViewerTabCount() == 1, "double-clicking a picture opens it in the sprite viewer");
        }

        editor.Shutdown();
        fs::remove_all(root, ignored);
    }

    // **스프라이트 뷰어는 메인 도크와 나란히 뿌리에 붙는다**(D-155, 기존 `CSpriteViewerDockWindow`).
    // 도구 창(패널)은 메인 도크 안에, 파일을 여는 창은 뿌리에 - 두 겹 도크의 나눔이다(D-134).
    void TestTheSpriteViewerDocksBesideTheMainDock()
    {
        // **그림은 비율을 지켜 칸에 넣는다**(D-159). 인스펙터 미리보기가 128×32 시트를 정사각형으로 늘여 그렸다.
        {
            const ImVec2 wide = JBro::Widget::FitInside(128, 32, ImVec2(100.0f, 100.0f));
            Check(wide.x == 100.0f && wide.y == 25.0f, "a wide sheet fills the width and keeps its ratio");
            const ImVec2 tall = JBro::Widget::FitInside(16, 64, ImVec2(100.0f, 50.0f));
            Check(tall.x == 12.5f && tall.y == 50.0f, "a tall picture fills the height and keeps its ratio");
            const ImVec2 unknown = JBro::Widget::FitInside(0, 0, ImVec2(80.0f, 80.0f));
            Check(unknown.x == 80.0f && unknown.y == 80.0f, "an unknown size keeps the box");
        }
        namespace fs = std::filesystem;
        const fs::path root(TempPath("JBroSpriteViewerProbe").c_str());
        std::error_code ignored;
        fs::remove_all(root, ignored);
        fs::create_directories(root / "Assets", ignored);
        {
            std::ofstream png(root / "Assets" / "hero.png", std::ios::binary);
            png.write(reinterpret_cast<const char*>(TinyPng), sizeof(TinyPng));
        }
        const JBro::String projectPath = TempPath("JBroSpriteViewerProbe\\Viewer.jproject");
        Check(WriteTextFile(projectPath,
            "Version: 1\n"
            "EngineVersion: 0.1.0\n"
            "Framework: 2D\n"
            "RootPath: .\n"
            "AssetDirectory: Assets\n"),
            "the test must be able to write its own project file");

        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the sprite viewer not verified" << std::endl;
            return;
        }
        JBro::ProjectFileError error;
        Check(editor.OpenProjectFile(projectPath.c_str(), error), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        const JBro::AssetRecord* hero = editor.GetAssetRegistry().FindByPath("hero.png");
        Check(hero != nullptr, "the scan must have registered hero.png");
        const JBro::AssetId heroTexture = hero->id;
        Check(false == editor.OpenSpriteViewer(JBro::AssetId{}), "nothing to open is refused");
        Check(editor.OpenSpriteViewer(heroTexture), "an image opens in the sprite viewer");
        Check(editor.OpenSpriteViewer(heroTexture), "opening it again only brings its tab forward");
        Check(editor.GetSpriteViewerTabCount() == 1, "so there is still one tab");
        for (int frame = 0; frame < 6; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle on the viewer");
        }

        ImGuiWindow* viewer = ImGui::FindWindowByName("###SpriteViewer");
        if (viewer == nullptr)
        {
            // 보이는 이름이 앞에 붙는다. 식별자만으로 찾는다.
            for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
            {
                if (std::strstr(window->Name, "###SpriteViewer") != nullptr)
                {
                    viewer = window;
                }
            }
        }
        ImGuiWindow* mainDock = ImGui::FindWindowByName("###MainDock");
        Check(viewer != nullptr && mainDock != nullptr, "the viewer and the main dock must both exist");
        Check(viewer->DockNode != nullptr && mainDock->DockNode != nullptr
                && viewer->DockNode == mainDock->DockNode,
            "the viewer docks into the same root node as the main dock, as a tab beside it");
        // **뷰어가 앞에 있어도 메인 도크의 패널들은 제자리에 붙어 있다.** 가려진 메인 도크가
        // 안쪽 도크 공간을 살려 두지 않으면 패널들이 떠 있는 창으로 흩어진다(실제로 그랬다).
        for (const char* panel : {"Hierarchy", "Inspector", "CanvasView", "Log"})
        {
            ImGuiWindow* window = ImGui::FindWindowByName(panel);
            Check(window != nullptr && window->DockId != 0,
                "every main-dock panel stays docked while the viewer is in front");
        }
        // 옵션 칸은 인스펙터와 같은 것을 고친다 - 연 그림이 고른 에셋이다.
        Check(editor.GetSelectedAsset() == heroTexture, "opening the viewer selects the picture");
        std::uint32_t frameIndex = 99;
        Check(editor.GetSpriteViewerFrame(frameIndex) && frameIndex == 0, "the first frame is shown");
        if (JBro::Renderer* renderer = editor.GetRenderer())
        {
            SaveScreenshot(*renderer, 1024, 768, "sprite_viewer");
        }

        // **메인 탭 뒤로 가려져 있어도 열면 앞으로 나온다**(D-159). 가려진 창에 탭만 더하면 두 번 누르기가
        // 아무 일도 하지 않은 것처럼 보였다(실제 에디터에서 그랬다).
        ImGui::SetWindowFocus(mainDock->Name);
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must bring the main dock forward");
        }
        Check(false == viewer->DockTabIsVisible, "the main dock tab now hides the viewer");
        Check(editor.OpenSpriteViewer(heroTexture), "the picture opens again");
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle on the viewer");
        }
        Check(viewer->DockTabIsVisible, "opening a picture brings the hidden viewer forward");

        // 탭을 닫으면 창도 사라지고 잡고 있던 스프라이트를 놓는다.
        JBro::AssetSystem* assets = editor.GetAssetSystem();
        JBro::AssetId heroSprite;
        for (std::size_t index = 0; index < editor.GetAssetRegistry().GetCount(); ++index)
        {
            const JBro::AssetRecord& record = editor.GetAssetRegistry().GetRecord(index);
            if (record.owner == heroTexture)
            {
                heroSprite = record.id;
            }
        }
        const JBro::AssetHandle held = assets->Find(heroSprite);
        const std::uint32_t heldCount = assets->GetReferenceCount(held);
        Check(heldCount >= 1, "the open tab holds the sprite");
        editor.CloseProject();
        Check(editor.GetSpriteViewerTabCount() == 0, "closing the project closes the viewer's tabs");

        editor.Shutdown();
        fs::remove_all(root, ignored);
    }

    // **파일 → 새 프로젝트**(D-160, 기존 루트 도크의 `MenuFileNewProject`). 폴더를 고르면 이름과 프레임워크를
    // 받는 팝업이 뜨고, 이름을 치고 Enter 를 누르면 그 폴더에 프로젝트가 서고 에디터가 그리로 넘어간다.
    // 2D 프로젝트를 연 채로 3D 를 골라 프레임워크까지 바뀌는지 본다.
    void TestNewProjectCreatesAndOpensIt()
    {
        namespace fs = std::filesystem;
        const fs::path root(TempPath("JBroNewProjectProbe").c_str());
        std::error_code ignored;
        fs::remove_all(root, ignored);
        fs::create_directories(root / "Current" / "Assets", ignored);
        fs::create_directories(root / "Parent", ignored);
        const JBro::String currentPath = TempPath("JBroNewProjectProbe\\Current\\Current.jproject");
        Check(WriteTextFile(currentPath,
            "Version: 1\n"
            "EngineVersion: 0.1.0\n"
            "Framework: 2D\n"
            "RootPath: .\n"
            "AssetDirectory: Assets\n"),
            "the test must be able to write its own project file");

        DialogProbe dialog;
        dialog.path = TempPath("JBroNewProjectProbe\\Parent");
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        config.fileDialog = &DialogProbe::Answer;
        config.fileDialogUser = &dialog;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; new project not verified" << std::endl;
            return;
        }
        JBro::ProjectFileError error;
        Check(editor.OpenProjectFile(currentPath.c_str(), error), "the current project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        // **시간이 흐르지 않은 프레임에 에디터가 멈추지 않는다**(D-160). 호스트의 첫 프레임이 0 초로 재어지면 UI 가
        // 프레임을 거절해 에디터가 켜지자마자 꺼졌다.
        Check(editor.Tick(0.0f), "a frame in which no time passed does not stop the editor");
        Check(editor.Tick(Frame), "the editor must settle");

        editor.RequestNewProject();
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must tick through the folder dialog");
        }
        Check(dialog.calls == 1 && dialog.pickFolder, "a new project first asks for a folder");
        Check(editor.IsPopupOpenById("new_project"), "then the popup asks for the name");
        {
            // **팝업은 화면 가운데에 선다.** 폭을 고정하고 높이를 내용에 맞추자 첫 프레임에 높이를 몰라 맨 위에
            // 붙었다(실제 에디터에서 그랬다). 가운데에 붙드는 몇 프레임이 지난 뒤에 잰다 - 그 뒤에도 가운데여야 한다.
            for (int frame = 0; frame < 6; ++frame)
            {
                Check(editor.Tick(Frame), "the popup must settle");
            }
            ImGuiWindow* popupWindow = nullptr;
            for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
            {
                if (window->Active && std::strstr(window->Name, "###popup_") != nullptr)
                {
                    popupWindow = window;
                }
            }
            Check(popupWindow != nullptr, "the popup must have a window");
            const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            const float middleY = popupWindow->Pos.y + popupWindow->Size.y * 0.5f;
            const float middleX = popupWindow->Pos.x + popupWindow->Size.x * 0.5f;
            Check(std::fabs(middleY - center.y) < 4.0f && std::fabs(middleX - center.x) < 4.0f,
                "the popup stands in the middle of the screen");
            Check(std::fabs(popupWindow->Size.x - 460.0f) < 1.0f, "at the width it asked for");
        }

        // 이름 칸은 팝업이 뜬 첫 프레임에 포커스를 받는다. 글자와 Enter 를 ImGui 에 바로 넣는다 -
        // 숨긴 창에 부친 키는 조합 상태를 읽는 길이 달라 믿을 수 없다(ClickAtWith 와 같은 이유).
        ImGuiIO& io = ImGui::GetIO();
        io.AddInputCharactersUTF8("Space Game");
        Check(editor.Tick(Frame), "the editor must take the name");
        // 첫 것은 팝업의 기본값(2D)으로 만든다. 3D 는 아래에서 팝업이 부르는 `CreateProject` 로 잰다.
        io.AddKeyEvent(ImGuiKey_Enter, true);
        Check(editor.Tick(Frame), "the editor must see Enter");
        io.AddKeyEvent(ImGuiKey_Enter, false);
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must switch to the new project");
        }
        Check(false == editor.IsPopupOpenById("new_project"), "the popup closes once the project is made");
        const fs::path made = root / "Parent" / "Space Game" / "Space Game.jproject";
        Check(fs::is_regular_file(made, ignored), "the project file stands in a folder of its name");
        Check(editor.HasOpenProject(), "and the editor has a project open");
        Check(editor.GetProjectFile().build.productName == "Space Game", "it is the new one");
        Check(editor.GetProjectFile().engineVersion == JBro::EngineVersionText,
            "written with this engine's version");
        Check(editor.GetCanvas() != nullptr, "and a canvas to edit");
        Check(editor.GetFrameworkKind() == JBro::FrameworkKind::Framework2D, "as a 2D project by default");

        // 3D 를 고르면 프레임워크까지 바뀐다.
        JBro::ProjectCreateFailure failure = JBro::ProjectCreateFailure::None;
        Check(false == editor.CreateProject(dialog.path.c_str(), "Space Game", JBro::FrameworkKind::Framework3D, &failure)
                && failure == JBro::ProjectCreateFailure::AlreadyExists,
            "the same name again is refused, saying why");
        Check(false == editor.CreateProject(dialog.path.c_str(), "a/b", JBro::FrameworkKind::Framework3D, &failure)
                && failure == JBro::ProjectCreateFailure::InvalidName,
            "and a name that cannot be a folder is refused as such");
        Check(editor.CreateProject(dialog.path.c_str(), "Deep Space", JBro::FrameworkKind::Framework3D, &failure),
            "a 3D project is made");
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must switch again");
        }
        Check(editor.GetFrameworkKind() == JBro::FrameworkKind::Framework3D,
            "and the editor now runs the 3D framework");

        editor.Shutdown();
        fs::remove_all(root, ignored);
    }

    // **밖의 그림을 가져온다**(D-156, 기존 `SpriteImporterWindow`). 에셋 폴더로 복사되고 스캔이
    // 등록하며 `.jmeta` 가 선다. 그림이면 바로 스프라이트 뷰어가 열린다. 같은 이름은 덮어쓰지 않는다.
    void TestImportingAPictureCopiesAndRegistersIt()
    {
        namespace fs = std::filesystem;
        const fs::path root(TempPath("JBroImportProbe").c_str());
        const fs::path outside(TempPath("JBroImportSource").c_str());
        std::error_code ignored;
        fs::remove_all(root, ignored);
        fs::remove_all(outside, ignored);
        fs::create_directories(root / "Assets", ignored);
        fs::create_directories(outside, ignored);
        {
            std::ofstream png(outside / "walk.png", std::ios::binary);
            png.write(reinterpret_cast<const char*>(TinyPng), sizeof(TinyPng));
            std::ofstream text(outside / "notes.txt", std::ios::binary);
            text << "not an asset";
        }
        const JBro::String projectPath = TempPath("JBroImportProbe\\Import.jproject");
        Check(WriteTextFile(projectPath,
            "Version: 1\n"
            "EngineVersion: 0.1.0\n"
            "Framework: 2D\n"
            "RootPath: .\n"
            "AssetDirectory: Assets\n"),
            "the test must be able to write its own project file");

        DialogProbe dialog;
        dialog.path = TempPath("JBroImportSource/walk.png");
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        config.fileDialog = &DialogProbe::Answer;
        config.fileDialogUser = &dialog;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; importing not verified" << std::endl;
            return;
        }
        JBro::ProjectFileError error;
        Check(editor.OpenProjectFile(projectPath.c_str(), error), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        Check(editor.Tick(Frame), "the editor must settle");

        editor.RequestImportAsset("art");
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must tick through the import");
        }
        Check(dialog.calls == 1 && false == dialog.save, "importing asks for a file to open");
        Check(fs::exists(root / "Assets" / "art" / "walk.png", ignored),
            "the picture is copied into the folder it was imported to");
        Check(fs::exists(outside / "walk.png", ignored), "and the original stays where it was");
        Check(fs::exists(root / "Assets" / "art" / "walk.png.jmeta", ignored),
            "the scan gives it a meta, so it has an id from the start");
        Check(editor.GetAssetRegistry().FindByPath("art/walk.png") != nullptr,
            "the registry knows it without a manual rescan");
        Check(editor.GetSpriteViewerTabCount() == 1,
            "a picture opens in the sprite viewer right away, to set up its slicing");

        // **같은 이름은 덮어쓰지 않는다.** 이미 있는 파일은 아이디를 들고 있다.
        const auto sizeBefore = fs::file_size(root / "Assets" / "art" / "walk.png", ignored);
        Check(false == editor.ImportAssetFile(dialog.path.c_str(), "art"),
            "importing over an existing asset is refused");
        Check(fs::file_size(root / "Assets" / "art" / "walk.png", ignored) == sizeBefore,
            "and the file there is untouched");
        // 에셋이 아닌 것은 복사하지 않는다 - 복사돼도 목록에 나오지 않는다.
        const JBro::String notes = TempPath("JBroImportSource/notes.txt");
        Check(false == editor.ImportAssetFile(notes.c_str(), "art"), "a file the engine cannot use is refused");
        Check(false == fs::exists(root / "Assets" / "art" / "notes.txt", ignored), "and not copied");

        editor.Shutdown();
        fs::remove_all(root, ignored);
        fs::remove_all(outside, ignored);
    }

    // **캔버스 뷰에서 누르면 맨 위 부모를 고르고, 두 번 누르면 그 안으로 들어간다**(D-157,
    // 기존 `CCanvasViewEditContext`). 조각 하나를 눌렀는데 조각만 고르면, 오브젝트를 통째로
    // 옮기려는 손짓이 조각 하나를 떼어 낸다.
    void TestTheCanvasViewPicksTheRootUntilYouStepInside()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 800;
        config.windowHeight = 600;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; stepping inside not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "StepInsideProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");

        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* body = canvas->CreateObject("Body");
        JBro::GameObject* arm = canvas->CreateObject("Arm");
        auto* bodyTransform = canvas->AttachComponent<JBro::Component::Transform2D>(body);
        auto* armTransform = canvas->AttachComponent<JBro::Component::Transform2D>(arm);
        Check(bodyTransform != nullptr && armTransform != nullptr, "both need transforms");
        arm->SetParent(body);
        // 팔은 몸에서 왼쪽 아래로 두 유닛이다. 둘의 집는 칸이 겹치지 않고, 몸을 고르면 서는
        // 기즈모의 손잡이(오른쪽·위)와도 겹치지 않는다 - 겹치면 누름이 손잡이로 간다.
        armTransform->position = JBro::Vec2{-2.0f, -2.0f};
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        // 그림의 왼쪽 위를 찾는다. 툴바 아래에서 그림의 누름 자리(`##canvas`)가 시작하는 줄이다.
        ImGuiWindow* view = ImGui::FindWindowByName("CanvasView");
        Check(view != nullptr, "the canvas view must have a window");
        const ImGuiID canvasId = LabelId(view->ID, "##canvas");
        const int probeX = static_cast<int>(view->Pos.x + view->Size.x * 0.5f);
        int top = -1;
        for (int y = static_cast<int>(view->Pos.y); y < static_cast<int>(view->Pos.y + view->Size.y); ++y)
        {
            PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(probeX, y));
            Check(editor.Tick(Frame), "the editor must tick while looking for the picture");
            if (ImGui::GetHoveredID() == canvasId)
            {
                top = y;
                break;
            }
        }
        Check(top >= 0, "the picture must be under the tool bar");
        // 월드 원점은 **그린 화면(텍스처)의 한가운데**다(D-150). 세로 절반이 5 유닛이다.
        const JBro::Extent2D drawn = editor.GetCanvasViewExtent();
        const float left = view->ContentRegionRect.Min.x;
        const float originX = left + static_cast<float>(drawn.width) * 0.5f;
        const float originY = static_cast<float>(top) + static_cast<float>(drawn.height) * 0.5f;
        const float pixelsPerUnit = static_cast<float>(drawn.height) * 0.5f / 5.0f;
        Spot onBody;
        onBody.x = static_cast<int>(originX);
        onBody.y = static_cast<int>(originY);
        Spot onArm;
        onArm.x = static_cast<int>(originX - pixelsPerUnit * 2.0f);
        onArm.y = static_cast<int>(originY + pixelsPerUnit * 2.0f);
        Spot empty;
        empty.x = static_cast<int>(originX + pixelsPerUnit * 3.0f);
        empty.y = static_cast<int>(originY + pixelsPerUnit * 3.0f);
        const auto waitOutDoubleClick = [&]() {
            // 다음 누름이 두 번 누르기로 읽히지 않게 시간을 둔다.
            for (int frame = 0; frame < 30; ++frame)
            {
                Check(editor.Tick(Frame), "the editor must wait out the double-click time");
            }
        };

        ClickAt(editor, hwnd, onArm);
        Check(editor.GetSelectedObject() == body, "clicking the arm selects the body while not inside it");
        waitOutDoubleClick();

        // 두 번 눌러 몸 안으로 들어간다. 그 뒤 팔을 한 번 누르면 팔이 골라진다.
        ClickAt(editor, hwnd, onArm);
        ClickAt(editor, hwnd, onArm);
        Check(editor.GetSelectedObject() == body, "a double-click steps inside the body and keeps it selected");
        waitOutDoubleClick();
        ClickAt(editor, hwnd, onArm);
        Check(editor.GetSelectedObject() == arm, "inside the body, clicking the arm selects the arm");
        // 고르기는 뗀 프레임의 뒤쪽에서 일어난다. 테두리가 새 선택을 두른 그림은 그다음 프레임이다.
        Check(editor.Tick(Frame), "the editor must draw the new selection");
        if (JBro::Renderer* renderer = editor.GetRenderer())
        {
            SaveScreenshot(*renderer, 800, 600, "inside");
        }
        waitOutDoubleClick();

        // 빈 곳을 두 번 누르면 나온다. 다시 팔을 누르면 몸이 골라진다.
        ClickAt(editor, hwnd, empty);
        ClickAt(editor, hwnd, empty);
        Check(editor.GetSelectedObject() == body, "double-clicking empty space steps out, selecting what was left");
        waitOutDoubleClick();
        ClickAt(editor, hwnd, onArm);
        Check(editor.GetSelectedObject() == body, "and outside again, the arm picks the body");

        editor.Shutdown();
    }

    // **새 오브젝트는 트랜스폼을 갖고 태어난다**(D-158). 실제 에디터를 띄워 보고서야 알았다 -
    // 메뉴로 만든 오브젝트에 트랜스폼이 없어 캔버스 뷰에 보이지도 않고 옮길 수도 없었다.
    void TestCreatedObjectsCarryTheFrameworkTransform()
    {
        for (const JBro::FrameworkKind kind : {JBro::FrameworkKind::Framework2D, JBro::FrameworkKind::Framework3D})
        {
            JBro::EditorApplication editor;
            JBro::EditorApplicationConfig config;
            config.windowVisible = false;
            if (false == editor.Initialize(config))
            {
                std::cout << "  [skip] no D3D12 device; object creation not verified" << std::endl;
                return;
            }
            JBro::ProjectDescriptor project;
            constexpr char name[] = "CreateProbe";
            project.name = {name, sizeof(name) - 1};
            project.framework = kind;
            Check(editor.OpenProject(project), "the probe project must open");
            JBro::GameObject* made = JBro::EditorActions::CreateObject(editor, nullptr);
            Check(made != nullptr, "the create action must make an object");
            JBro::Canvas* canvas = editor.GetCanvas();
            const bool has = kind == JBro::FrameworkKind::Framework3D
                ? canvas->FindComponentRaw<JBro::Component::Transform3D>(made) != nullptr
                : canvas->FindComponentRaw<JBro::Component::Transform2D>(made) != nullptr;
            Check(has, "a created object carries its framework's transform");
            Check(editor.GetCommands().Undo() && editor.GetCommands().Redo(), "creation undoes and redoes");
            JBro::GameObject* again = editor.GetSelectedObject();
            if (again == nullptr)
            {
                JBro::Array<JBro::GameObject*> roots;
                canvas->GetRootObjects(roots);
                again = roots.Size() > 0 ? roots[0] : nullptr;
            }
            const bool hasAgain = again != nullptr && (kind == JBro::FrameworkKind::Framework3D
                ? canvas->FindComponentRaw<JBro::Component::Transform3D>(again) != nullptr
                : canvas->FindComponentRaw<JBro::Component::Transform2D>(again) != nullptr);
            Check(hasAgain, "and the redone object has it too");
            editor.Shutdown();
        }
    }

    void TestPanelsGoThroughTheWidgetLayer()
    {
        namespace fs = std::filesystem;
        const fs::path panels("Modules/JBroEditor/Source/Panel");
        std::error_code ignored;
        if (false == fs::is_directory(panels, ignored))
        {
            std::cout << "  [skip] panel sources are not beside the test" << std::endl;
            return;
        }
        // 공용 위젯이 대신하는 원시 호출들이다. 배치(`SameLine`·`Separator`)와 그리기 목록은
        // 위젯이 아니라 여기 넣지 않는다.
        const char* forbidden[] = {
            "ImGui::Text(", "ImGui::TextUnformatted(", "ImGui::TextDisabled(", "ImGui::TextColored(",
            "ImGui::Button(", "ImGui::Checkbox(", "ImGui::MenuItem(", "ImGui::Selectable(",
            "ImGui::BeginCombo(", "ImGui::BeginPopupContextItem(", "ImGui::BeginPopupContextWindow(",
            "ImGui::BeginPopupModal(", "ImGui::OpenPopup(", "ImGui::EndPopup(",
            "ImGui::InvisibleButton(", "ImGui::Image(", "ImGui::CollapsingHeader(",
            "ImGui::TreeNodeEx(", "ImGui::TreeNode(", "ImGui::TreePop(", "ImGui::InputText("};
        std::size_t scanned = 0;
        for (const fs::directory_entry& entry : fs::directory_iterator(panels, ignored))
        {
            if (entry.path().extension() != ".cpp")
            {
                continue;
            }
            ++scanned;
            std::ifstream in(entry.path(), std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>());
            for (const char* token : forbidden)
            {
                if (text.find(token) != std::string::npos)
                {
                    std::cout << "  " << entry.path().filename().string() << " calls " << token
                        << std::endl;
                    Check(false, "a panel must draw through the shared widget layer");
                }
            }
        }
        Check(scanned >= 8, "the panel sources must actually have been read");
    }

    void TestPickingFollowsTheSpriteAssetSize()
    {
        namespace fs = std::filesystem;
        const fs::path root(TempPath("JBroPickSizeProbe").c_str());
        std::error_code ignored;
        fs::remove_all(root, ignored);
        fs::create_directories(root / "Assets", ignored);
        {
            std::ofstream png(root / "Assets" / "hero.png", std::ios::binary);
            png.write(reinterpret_cast<const char*>(TinyPng), sizeof(TinyPng));
        }
        const JBro::String projectPath = TempPath("JBroPickSizeProbe\\PickSize.jproject");
        Check(WriteTextFile(projectPath,
            "Version: 1\n"
            "EngineVersion: 0.1.0\n"
            "Framework: 2D\n"
            "RootPath: .\n"
            "AssetDirectory: Assets\n"),
            "the test must be able to write its own project file");

        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 800;
        config.windowHeight = 600;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; picking size not verified" << std::endl;
            return;
        }
        JBro::ProjectFileError error;
        Check(editor.OpenProjectFile(projectPath.c_str(), error), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");

        const JBro::AssetRegistry& registry = editor.GetAssetRegistry();
        JBro::AssetId spriteAsset;
        for (std::size_t index = 0; index < registry.GetCount(); ++index)
        {
            const JBro::AssetRecord& record = registry.GetRecord(index);
            if (record.type == JBro::AssetType::Sprite && record.relativePath == "hero.png")
            {
                spriteAsset = record.id;
            }
        }
        Check(false == spriteAsset.IsNull(), "the scan must have registered hero.png as a sprite");

        // 그림은 2x2 픽셀이고 기본 PPU 는 100 이라 0.02 유닛이다. 100 배로 키우면 2 유닛이 되어
        // 반폭이 1 유닛이다. 선언된 `size`(1x1)를 쓰던 예전 셈이라면 반폭이 50 유닛이라,
        // 두 칸은 화면에서 확실히 갈린다.
        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* object = canvas->CreateObject("Hero");
        auto* transform = canvas->AttachComponent<JBro::Component::Transform2D>(object);
        auto* sprite = canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(object);
        Check(transform != nullptr && sprite != nullptr, "the hero needs both components");
        transform->scale = JBro::Vec2{100.0f, 100.0f};
        sprite->spriteId = spriteAsset;
        // 해석은 커맨드가 돌 때 따라 도는데(D-115) 여기서는 값을 손으로 놓았다.
        // 다시 훑으면 해석도 함께 돈다.
        Check(editor.RescanAssets(), "the registry must rescan so the handle resolves");
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }
        Check(sprite->sprite.index != 0 || sprite->sprite.generation != 0,
            "the sprite id must resolve to a handle");

        ImGuiWindow* view = ImGui::FindWindowByName("CanvasView");
        Check(view != nullptr, "the canvas view must have a window");
        // 화면 한가운데가 월드 원점이고, 세로 절반이 5 유닛이다.
        const float centerX = view->Pos.x + view->Size.x * 0.5f;
        const float centerY = view->Pos.y + view->Size.y * 0.5f;
        const float pixelsPerUnit = view->Size.y * 0.5f / 5.0f;

        Spot inside;
        inside.x = static_cast<int>(centerX);
        inside.y = static_cast<int>(centerY);
        ClickAt(editor, hwnd, inside);
        Check(editor.GetSelectedObject() == object, "the middle of the picture picks it");

        // **고른 것의 테두리는 그림의 모양을 따른다**(D-149). 모양을 재는 데 한 프레임이
        // 더 걸리므로(프레임마다 몇 개로 막혀 있다) 몇 번 더 돌린 뒤에 묻는다.
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle on the selection");
        }
        {
            const JBro::AssetSystem* assets = editor.GetAssetSystem();
            Check(assets != nullptr, "the editor must have an asset system");
            const JBro::SpriteData* data = assets->GetSprite(sprite->sprite);
            Check(data != nullptr && false == data->frames.IsEmpty(),
                "the sprite must be loaded with a frame");
            const JBro::Array<JBro::EditorSpriteContours::Segment>* contour =
                editor.GetSpriteContour(data->texture, data->frames[0]);
            Check(contour != nullptr && contour->Size() >= 4,
                "the editor must have measured the picture's shape");

            // **테두리가 그림과 같은 자리에 있는가**(D-150). 화면을 읽어 둘의 사각형을
            // 견준다 - 그리는 쪽과 재는 쪽이 같은 셈을 쓰면, 둘이 함께 어긋나 있어도
            // 코드로는 드러나지 않는다.
            JBro::Renderer* pixels = editor.GetRenderer();
            Check(pixels != nullptr, "the editor must expose its renderer");
            JBro::Array<std::byte> frameImage;
            JBro::TextureReadback readback;
            ReadBackBufferInto(*pixels, 800, 600, frameImage, readback);
            // 그림은 순색 빨강·초록·파랑을 담고 있다. 격자의 축선(220,90,90 과 110,200,110)은
            // 순색이 아니라 걸리지 않는다.
            // 캔버스 뷰 창 안만 본다.
            const ImRect area(view->Pos, ImVec2(view->Pos.x + view->Size.x,
                view->Pos.y + view->Size.y));
            const PixelBox picture = MeasurePixels(frameImage, readback, area,
                [](const unsigned char* p) {
                    // 읽어 온 픽셀은 BGRA 다.
                    const int blue = p[0];
                    const int green = p[1];
                    const int red = p[2];
                    return (red > 180 && green < 60 && blue < 60)
                        || (green > 180 && red < 60 && blue < 60)
                        || (blue > 180 && red < 60 && green < 60);
                });
            // 선택 테두리의 색이다. 기즈모의 축은 빨강·초록·파랑이고 가운데 손잡이는 흰색이라
            // 이 색을 쓰는 것은 테두리뿐이다.
            const PixelBox outline = MeasurePixels(frameImage, readback, area,
                [](const unsigned char* p) {
                    return std::abs(static_cast<int>(p[2]) - 255) < 40
                        && std::abs(static_cast<int>(p[1]) - 168) < 40
                        && std::abs(static_cast<int>(p[0]) - 64) < 40;
                });
            Check(false == picture.IsEmpty(), "the picture must be on screen");
            Check(false == outline.IsEmpty(), "and so must the outline");
            std::cout << "  [measure] picture x " << picture.minX << ".." << picture.maxX
                << " y " << picture.minY << ".." << picture.maxY
                << " / outline x " << outline.minX << ".." << outline.maxX
                << " y " << outline.minY << ".." << outline.maxY << std::endl;
            // 선 두께(1.5px)와 가장자리의 반투명 픽셀만큼은 어긋난다. 그보다 크게 벌어지면
            // 겹쳐 그리는 좌표가 그림과 다른 기준으로 세어진 것이다.
            constexpr int Tolerance = 5;
            Check(std::abs(outline.minX - picture.minX) <= Tolerance
                    && std::abs(outline.maxX - picture.maxX) <= Tolerance
                    && std::abs(outline.minY - picture.minY) <= Tolerance
                    && std::abs(outline.maxY - picture.maxY) <= Tolerance,
                "the outline must sit on the picture, not beside it");
            std::cout << "  the sprite contour has " << contour->Size() << " segments" << std::endl;
            std::cout << "  [measure] frame " << data->frames[0].x << "," << data->frames[0].y
                << " " << data->frames[0].width << "x" << data->frames[0].height
                << " pivot " << data->frames[0].pivotX << "," << data->frames[0].pivotY
                << " ppu " << data->options.pixelsPerUnit << std::endl;
            if (JBro::Renderer* shotRenderer = editor.GetRenderer())
            {
                SaveScreenshot(*shotRenderer, 800, 600, "contour");
            }
        }

        // 두 유닛 옆은 **그림 밖**이다. 예전 셈으로는 아직 한참 안쪽이었다.
        Spot outside;
        outside.x = static_cast<int>(centerX + pixelsPerUnit * 2.0f);
        outside.y = static_cast<int>(centerY);
        ClickAt(editor, hwnd, outside);
        Check(editor.GetSelectedObject() == nullptr,
            "and two units to the side is outside the picture, so nothing is picked");

        editor.Shutdown();
        fs::remove_all(root, ignored);
    }

    void TestTheEditorSessionSurvivesReopening()
    {
        namespace fs = std::filesystem;
        const fs::path root(TempPath("JBroSessionProbe").c_str());
        std::error_code ignored;
        fs::remove_all(root, ignored);
        fs::create_directories(root / "Assets" / "scenes", ignored);
        const JBro::String projectPath = TempPath("JBroSessionProbe\\Session.jproject");
        Check(WriteTextFile(projectPath,
            "Version: 1\n"
            "EngineVersion: 0.1.0\n"
            "Framework: 2D\n"
            "RootPath: .\n"
            "AssetDirectory: Assets\n"),
            "the test must be able to write its own project file");

        float savedX = 0.0f;
        float savedY = 0.0f;
        float savedSize = 0.0f;
        float savedInspectorWidth = 0.0f;
        {
            JBro::EditorApplication editor;
            JBro::EditorApplicationConfig config;
            config.windowVisible = false;
            config.windowWidth = 800;
            config.windowHeight = 600;
            if (false == editor.Initialize(config))
            {
                std::cout << "  [skip] no D3D12 device; the session not verified" << std::endl;
                return;
            }
            JBro::ProjectFileError error;
            Check(editor.OpenProjectFile(projectPath.c_str(), error), "the probe project must open");
            Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
            for (int frame = 0; frame < 3; ++frame)
            {
                Check(editor.Tick(Frame), "the editor must settle");
            }

            JBro::Canvas* canvas = editor.GetCanvas();
            JBro::GameObject* object = canvas->CreateObject("Remembered");
            Check(canvas->AttachComponent<JBro::Component::Transform2D>(object) != nullptr,
                "the probe object needs a transform");
            const JBro::String canvasPath =
                TempPath("JBroSessionProbe\\Assets\\scenes\\Work.jcanvas");
            JBro::CanvasFileError canvasError;
            Check(editor.SaveCanvas(canvasPath.c_str(), canvasError), "the canvas must save");

            // 캔버스 뷰에서 휠을 돌려 보는 자리를 바꾼다. 기본값 그대로면 되살아난 것인지
            // 처음부터 그랬던 것인지 갈라지지 않는다.
            HWND hwnd = FindOwnEditorWindow();
            Check(hwnd != nullptr, "the editor window must be findable");
            ImGuiWindow* view = ImGui::FindWindowByName("CanvasView");
            Check(view != nullptr, "the canvas view must have a window");
            const int x = static_cast<int>(view->Pos.x + view->Size.x * 0.5f);
            const int y = static_cast<int>(view->Pos.y + view->Size.y * 0.5f);
            PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
            Check(editor.Tick(Frame), "the editor must tick");
            PostMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(x, y));
            for (int frame = 0; frame < 3; ++frame)
            {
                Check(editor.Tick(Frame), "the editor must settle after the wheel");
            }
            editor.GetCanvasViewCamera(savedX, savedY, savedSize);
            Check(savedSize > 0.0f, "the canvas view must report a camera");

            // **창 배치도 남아야 한다.** 인스펙터가 붙은 칸을 좁혀 두고, 다시 열었을 때
            // 그 폭이 살아 있는지 본다 - 기본 배치가 다시 돌면 원래 폭으로 되돌아간다.
            ImGuiWindow* inspector = ImGui::FindWindowByName("Inspector");
            Check(inspector != nullptr && inspector->DockNode != nullptr,
                "the inspector must be docked");
            const ImVec2 nodeSize = inspector->DockNode->Size;
            ImGui::DockBuilderSetNodeSize(inspector->DockNode->ID,
                ImVec2(nodeSize.x * 0.5f, nodeSize.y));
            for (int frame = 0; frame < 3; ++frame)
            {
                Check(editor.Tick(Frame), "the editor must settle after the resize");
            }
            inspector = ImGui::FindWindowByName("Inspector");
            Check(inspector != nullptr && inspector->DockNode != nullptr,
                "the inspector must still be docked");
            savedInspectorWidth = inspector->DockNode->Size.x;
            Check(savedInspectorWidth < nodeSize.x - 1.0f, "and the dock must have narrowed");

            // **설정·디버그 메뉴가 여는 창들이 실제로 있다**(D-151). 이름이 어긋나면 항목이
            // 잠긴 채로 서 있어, 메뉴는 보이는데 열리지 않는다.
            for (const char* title : {"ProjectSettings", "Profiler", "Stats", "Log"})
            {
                Check(editor.FindPanel(title) != nullptr, "every panel the menus name must exist");
            }

            // **프로젝트 저장**은 캔버스를 저장하고 세션을 적는다. 닫기 전에 파일에 가 있어야 한다.
            editor.RequestSaveProject();
            Check(editor.Tick(Frame), "the editor must tick through the save");
            {
                std::ifstream in(std::filesystem::path(projectPath.c_str()), std::ios::binary);
                const std::string text((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
                Check(text.find("LastOpenedCanvasPath: scenes/Work.jcanvas") != std::string::npos,
                    "save project writes the session without waiting for the project to close");
            }

            // 닫으면 적힌다.
            editor.CloseProject();
            editor.Shutdown();
        }

        // 파일에 실제로 적혔는가. 화면을 거치지 않고 파일로 확인한다.
        {
            std::ifstream in(std::filesystem::path(projectPath.c_str()), std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>());
            Check(text.find("LastOpenedCanvasPath: scenes/Work.jcanvas") != std::string::npos,
                "the project file names the canvas that was open, relative to the asset folder");
            Check(text.find("CanvasViewCameraSize:") != std::string::npos,
                "and the camera it was seen from");
            // 배치는 ImGui 의 형식 그대로 옆 파일에 있다. `.jproject` 안에 넣으면 여러 줄짜리
            // 덩어리가 YAML 한가운데 앉는다.
            const JBro::String layoutPath =
                TempPath("JBroSessionProbe\\Session.jproject.layout.ini");
            Check(std::filesystem::exists(std::filesystem::path(layoutPath.c_str())),
                "the window layout is written beside the project file");
        }

        {
            JBro::EditorApplication editor;
            JBro::EditorApplicationConfig config;
            config.windowVisible = false;
            config.windowWidth = 800;
            config.windowHeight = 600;
            if (false == editor.Initialize(config))
            {
                std::cout << "  [skip] no D3D12 device on the second open" << std::endl;
                return;
            }
            JBro::ProjectFileError error;
            Check(editor.OpenProjectFile(projectPath.c_str(), error), "the project must open again");
            Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
            for (int frame = 0; frame < 3; ++frame)
            {
                Check(editor.Tick(Frame), "the editor must settle");
            }

            // **보던 캔버스가 열려 있다.**
            JBro::Canvas* canvas = editor.GetCanvas();
            Check(canvas != nullptr && canvas->GetObjectCount() == 1,
                "the canvas from last time is open, with what was in it");
            float x = 0.0f;
            float y = 0.0f;
            float size = 0.0f;
            editor.GetCanvasViewCamera(x, y, size);
            Check(std::fabs(size - savedSize) < 0.001f,
                "and the canvas view starts where it was left");

            ImGuiWindow* inspector = ImGui::FindWindowByName("Inspector");
            Check(inspector != nullptr && inspector->DockNode != nullptr,
                "the inspector must be docked again");
            Check(std::fabs(inspector->DockNode->Size.x - savedInspectorWidth) < 2.0f,
                "and the dock keeps the width it was left at");

            editor.Shutdown();
        }
        fs::remove_all(root, ignored);
    }

    void TestTheStatsPanelShowsWhatTheCanvasHolds()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the stats panel not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "StatsProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");

        JBro::Canvas* canvas = editor.GetCanvas();
        for (int index = 0; index < 3; ++index)
        {
            JBro::GameObject* object = canvas->CreateObject("Counted");
            Check(canvas->AttachComponent<JBro::Component::Transform2D>(object) != nullptr,
                "each probe object needs a transform");
        }
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        // 통계는 아래 독의 탭이다. 앞으로 꺼내야 그려진다.
        ImGui::SetWindowFocus("Stats");
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle on the stats tab");
        }
        ImGuiWindow* stats = ImGui::FindWindowByName("Stats");
        Check(stats != nullptr && stats->DockTabIsVisible, "the stats tab must be in front");

        // 풀의 숫자는 캔버스가 내준다. 창이 그 값을 쓰는지는 화면 한 장으로 남긴다(§11.4).
        std::size_t live = 0;
        std::size_t capacity = 0;
        canvas->GetObjectPoolUsage(live, capacity);
        Check(live == 3, "the canvas holds the three objects that were made");
        if (JBro::Renderer* renderer = editor.GetRenderer())
        {
            SaveScreenshot(*renderer, 1024, 768, "stats");
        }

        editor.Shutdown();
    }

    void TestTheCanvasViewDrawsColliderShapes()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 640;
        config.windowHeight = 480;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; collider shapes not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "ColliderViewProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");

        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* body = canvas->CreateObject("Body");
        Check(canvas->AttachComponent<JBro::Component::Transform2D>(body) != nullptr,
            "the object needs a transform");
        JBro::Component::Collider2D* collider =
            canvas->AttachComponent<JBro::Component::Collider2D>(body);
        Check(collider != nullptr, "and a collider to show");
        collider->shape = JBro::Component::ColliderShape2D::Circle;
        collider->radius = 1.5f;

        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        ImGuiWindow* view = ImGui::FindWindowByName("CanvasView");
        Check(view != nullptr, "the canvas view must have a window");
        const char* collidersLabel =
            JBro::Loc::TextOr(JBro::LocKeys::CanvasViewColliders, "Colliders");
        Spot toggle;
        Check(FindItemAnywhereInWindow(editor, hwnd, view, LabelId(view->ID, collidersLabel), toggle),
            "the collider button must be on the canvas view tool bar");

        JBro::Renderer* renderer = editor.GetRenderer();
        Check(renderer != nullptr, "the editor must expose its renderer");
        JBro::Array<std::byte> without;
        JBro::Array<std::byte> with;
        JBro::TextureReadback readback;
        // 마우스는 단추 위에 그대로 둔 채 두 번 누른다. 자리를 옮기면 단추의 강조가
        // 달라져 그 픽셀까지 차이에 섞인다.
        ClickAt(editor, hwnd, toggle);
        Check(editor.Tick(Frame), "the editor must settle with the shapes hidden");
        ReadBackBufferInto(*renderer, 640, 480, without, readback);
        ClickAt(editor, hwnd, toggle);
        Check(editor.Tick(Frame), "the editor must settle with the shapes shown");
        ReadBackBufferInto(*renderer, 640, 480, with, readback);
        const std::size_t painted = CountDifferingPixels(without, with, readback, 640, 480);
        std::cout << "  the collider outline painted " << painted << " pixels" << std::endl;
        Check(painted > 100, "the collider outline must reach the screen");
        SaveScreenshot(*renderer, 640, 480, "colliders");

        editor.Shutdown();
    }

    void TestTheInspectorRenamesAndTogglesThroughCommands()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; the inspector header not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "InspectorHeaderProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");

        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* alpha = canvas->CreateObject("Alpha");
        JBro::GameObject* beta = canvas->CreateObject("Beta");
        Check(alpha != nullptr && beta != nullptr, "the probe objects must be made");
        editor.SetSelectedObject(alpha);
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        ImGuiWindow* inspector = ImGui::FindWindowByName("Inspector");
        Check(inspector != nullptr, "the inspector must have a window");
        const ImGuiID header = LabelId(inspector->ID, "##object");

        // ── 이름 ────────────────────────────────────────────────────────────
        Spot nameField;
        Check(FindItemAnywhereInWindow(editor, hwnd, inspector, LabelId(header, "##name"), nameField),
            "the name field must be in the inspector header");
        const std::size_t undoBefore = editor.GetCommands().GetUndoCount();
        ClickAt(editor, hwnd, nameField);
        Check(editor.Tick(Frame), "the field must take focus");
        // 커서를 끝에 두고 친다. 누른 자리에 따라 글자가 가운데 끼면 무엇이 붙었는지 흐려진다.
        PostMessageW(hwnd, WM_KEYDOWN, VK_END, 0);
        Check(editor.Tick(Frame), "the editor must tick");
        PostMessageW(hwnd, WM_KEYUP, VK_END, 0);
        Check(editor.Tick(Frame), "the editor must tick");
        for (const char* at = "One"; *at != '\0'; ++at)
        {
            PostMessageW(hwnd, WM_CHAR, static_cast<WPARAM>(*at), 0);
            Check(editor.Tick(Frame), "the editor must tick while typing");
        }
        Check(editor.GetCommands().GetUndoCount() == undoBefore,
            "nothing is recorded while the letters are still being typed");
        // 편집이 끝나야 값이 커맨드로 남는다. Enter 가 그 끝이다.
        PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
        Check(editor.Tick(Frame), "the editor must tick");
        PostMessageW(hwnd, WM_KEYUP, VK_RETURN, 0);
        Check(editor.Tick(Frame), "the editor must tick");
        Check(std::strcmp(alpha->GetTag(), "AlphaOne") == 0,
            "typing in the name field renames the object");
        // **친 글자 전체가 커맨드 하나다.** 글자마다 한 칸씩 쌓이면 되돌리기가 글자 수만큼 필요해진다.
        Check(editor.GetCommands().GetUndoCount() == undoBefore + 1,
            "and the three keystrokes are one command");
        Check(editor.GetCommands().Undo(), "the rename must undo");
        Check(std::strcmp(alpha->GetTag(), "Alpha") == 0, "back to the name it had");
        Check(editor.Tick(Frame), "the editor must tick after the undo");
        Check(editor.Tick(Frame), "and once more so the field reads the object again");

        // ── 활성 ────────────────────────────────────────────────────────────
        // 둘을 골라 둔다. 하나만 꺼지면 나머지는 화면에서 그대로라 무엇이 바뀌었는지 알 수 없다.
        editor.SetSelectedObject(alpha);
        editor.AddToSelection(beta);
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle on the pair");
        }
        Spot activeBox;
        Check(FindItemAnywhereInWindow(editor, hwnd, inspector, LabelId(header, "##active"), activeBox),
            "the active checkbox must be in the inspector header");
        const std::size_t undoActive = editor.GetCommands().GetUndoCount();
        ClickAt(editor, hwnd, activeBox);
        Check(editor.Tick(Frame), "the editor must tick after the toggle");
        Check(false == alpha->IsActiveSelf() && false == beta->IsActiveSelf(),
            "one click turns off everything that is selected");
        Check(editor.GetCommands().GetUndoCount() == undoActive + 1, "through one command");
        Check(editor.GetCommands().Undo(), "the toggle must undo");
        Check(alpha->IsActiveSelf() && beta->IsActiveSelf(), "and both come back on");

        editor.Shutdown();
    }

    void TestAssetFileOperationsCarryTheMeta()
    {
        // 이 파일의 다른 검사들과 같은 별칭이다. 그쪽은 제 함수 안에서 들여온다.
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / "JBroAssetOpsProbe";
        std::error_code errorCode;
        fs::remove_all(root, errorCode);
        fs::create_directories(root / "Assets" / "art", errorCode);
        {
            std::ofstream image(root / "Assets" / "art" / "hero.png", std::ios::binary);
            image.write(reinterpret_cast<const char*>(TinyPng), sizeof(TinyPng));
        }
        const JBro::String projectPath((root / "Probe.jproject").string().c_str());
        Check(WriteTextFile(projectPath,
            "Version: 1\n"
            "EngineVersion: 0.1.0\n"
            "Framework: 2D\n"
            "AssetDirectory: Assets\n"
            "ScriptOutputLibraryPath: \"\"\n"),
            "the test must be able to write its own project file");

        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        JBro::EditorApplication editor;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; asset file operations not verified" << std::endl;
            return;
        }
        JBro::ProjectFileError error;
        Check(editor.OpenProjectFile(projectPath.c_str(), error), "the probe project must open");
        Check(editor.GetAssetRegistry().FindByPath("art/hero.png") != nullptr,
            "the scan must have registered the image");
        // 메타는 스캔이 만든다(D-111). 그 파일이 따라다니는지가 이 검사의 요점이다.
        Check(fs::exists(root / "Assets" / "art" / "hero.png.jmeta", errorCode),
            "the scan must have written a meta beside it");

        // ── 폴더 만들기 ──────────────────────────────────────────────────
        Check(editor.CreateAssetFolder("", "sprites"), "a folder can be made at the root");
        Check(fs::is_directory(root / "Assets" / "sprites", errorCode), "and it is there");

        // ── 옮기기: 메타도 함께 ─────────────────────────────────────────
        Check(editor.MoveAsset("art/hero.png", "sprites"), "an asset can move to another folder");
        Check(fs::exists(root / "Assets" / "sprites" / "hero.png", errorCode), "the file moved");
        Check(fs::exists(root / "Assets" / "sprites" / "hero.png.jmeta", errorCode),
            "and so did its meta, or the asset would lose its id");
        Check(false == fs::exists(root / "Assets" / "art" / "hero.png", errorCode),
            "nothing is left behind");
        Check(editor.GetAssetRegistry().FindByPath("sprites/hero.png") != nullptr,
            "and the registry knows where it went");

        // ── 이름 바꾸기 ──────────────────────────────────────────────────
        Check(editor.RenameAsset("sprites/hero.png", "villain.png"), "an asset can be renamed");
        Check(fs::exists(root / "Assets" / "sprites" / "villain.png", errorCode), "under the new name");
        Check(fs::exists(root / "Assets" / "sprites" / "villain.png.jmeta", errorCode),
            "with its meta beside it");

        // **덮어쓰지 않는다.** 같은 이름이 이미 있으면 그 에셋을 잃는다.
        {
            std::ofstream other(root / "Assets" / "sprites" / "taken.png", std::ios::binary);
            other.write(reinterpret_cast<const char*>(TinyPng), sizeof(TinyPng));
        }
        Check(editor.RescanAssets(), "the folder must be rescannable");
        Check(false == editor.RenameAsset("sprites/villain.png", "taken.png"),
            "renaming onto a name that is taken must be refused");
        Check(fs::exists(root / "Assets" / "sprites" / "villain.png", errorCode),
            "and must leave the file where it was");

        // ── 지우기: 메타도 함께 ─────────────────────────────────────────
        Check(editor.DeleteAsset("sprites/villain.png"), "an asset can be deleted");
        Check(false == fs::exists(root / "Assets" / "sprites" / "villain.png", errorCode),
            "the file is gone");
        Check(false == fs::exists(root / "Assets" / "sprites" / "villain.png.jmeta", errorCode),
            "and the meta with it, or the next scan makes a meta with no file");

        editor.Shutdown();
        fs::remove_all(root, errorCode);
    }

    // 계층의 줄 하나가 차지한 Id.
    //
    // 줄마다 `PushID(&object)` 를 쌓고 트리 마디가 `"##node"` 로 선다. **펼친 마디는
    // 자기 Id 를 다시 쌓으므로**(`TreePushOverrideID`) 자식의 시드는 창이 아니라
    // 부모 줄의 Id 다 - 조상부터 내려오며 같은 순서로 쌓아야 같은 값이 나온다.
    //
    // 맨 위에는 **레이어**가 있다(D-135). 뿌리 오브젝트도 그 레이어 마디 아래에 선다.
    ImGuiID HierarchyRowId(const JBro::GameObject* object)
    {
        ImGuiWindow* window = ImGui::FindWindowByName("Hierarchy");
        Check(window != nullptr, "the hierarchy must have a window");
        const JBro::GameObject* chain[16] = {};
        std::size_t depth = 0;
        for (const JBro::GameObject* walk = object;
            walk != nullptr && depth < 16; walk = walk->GetParent())
        {
            chain[depth++] = walk;
        }
        // `ImGui::PushID(int)` 와 같은 계산이다. 레이어 줄은 아이디를 정수로 쌓는다.
        const int layerId = static_cast<int>(object->GetLayerId());
        ImGuiID seed = LabelId(PushedId(window->ID, layerId), "##layer");
        for (std::size_t step = depth; step > 0; --step)
        {
            const JBro::GameObject* at = chain[step - 1];
            // `ImGui::PushID(const void*)` 와 같은 계산이다.
            seed = LabelId(ImHashData(&at, sizeof(at), seed), "##node");
        }
        return seed;
    }

    // 계층 창을 위아래로 훑어 그 줄이 가리켜지는 자리를 찾는다.
    bool FindHierarchyRow(
        JBro::EditorApplication& editor, HWND hwnd, const JBro::GameObject* object, Spot& spot)
    {
        ImGuiWindow* window = ImGui::FindWindowByName("Hierarchy");
        Check(window != nullptr, "the hierarchy must have a window");
        const ImGuiID target = HierarchyRowId(object);
        const int x = static_cast<int>(window->Pos.x + window->Size.x * 0.5f);
        const int bottom = static_cast<int>(window->Pos.y + window->Size.y);
        for (int y = static_cast<int>(window->Pos.y); y < bottom; y += 2)
        {
            PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
            Check(editor.Tick(Frame), "the editor must tick while looking");
            if (ImGui::GetHoveredID() == target)
            {
                spot.x = x;
                spot.y = y;
                spot.disabled = false;
                return true;
            }
        }
        return false;
    }

    // 사용자가 화면에서 짚은 자리다: **계층에서 순서를 바꾸고 부모를 떼는 것**.
    // 명령 쪽 계약은 `EditorObjectCommandTests` 가 재고, 여기서는 마우스로 끌어
    // 놓는 손짓이 실제로 그 명령에 닿는지를 본다.
    void TestDraggingInTheHierarchyReordersAndUnparents()
    {
        JBro::EditorApplication editor;
        JBro::EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 1024;
        config.windowHeight = 768;
        if (false == editor.Initialize(config))
        {
            std::cout << "  [skip] no D3D12 device; hierarchy drags not verified" << std::endl;
            return;
        }
        JBro::ProjectDescriptor project;
        constexpr char name[] = "HierarchyDragProbe";
        project.name = {name, sizeof(name) - 1};
        Check(editor.OpenProject(project), "the probe project must open");
        Check(editor.EnableEditorUi({64, 48}), "the editor UI must turn on");
        HWND hwnd = FindOwnEditorWindow();
        Check(hwnd != nullptr, "the editor window must be findable");

        JBro::Canvas* canvas = editor.GetCanvas();
        JBro::GameObject* alpha = canvas->CreateObject("Alpha");
        JBro::GameObject* beta = canvas->CreateObject("Beta");
        JBro::GameObject* gamma = canvas->CreateObject("Gamma");
        for (int frame = 0; frame < 4; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle");
        }

        JBro::Array<JBro::GameObject*> roots;
        canvas->GetRootObjects(roots);
        Check(roots.Size() == 3 && roots[0] == alpha && roots[1] == beta && roots[2] == gamma,
            "the three roots start in the order they were made");

        Spot from;
        Spot to;
        Spot next;
        Check(FindHierarchyRow(editor, hwnd, gamma, from), "Gamma must have a row");
        Check(FindHierarchyRow(editor, hwnd, alpha, to), "Alpha must have a row");
        Check(FindHierarchyRow(editor, hwnd, beta, next), "Beta must have a row");
        // **줄 높이는 이웃한 두 줄의 간격으로 잰다.** 글꼴과 여백에 따라 달라지는
        // 값이라 상수로 두면 글꼴이 바뀌는 날 조용히 다른 띠를 겨냥하게 된다.
        const int pitch = next.y - to.y;
        Check(pitch > 4, "two rows must be more than a few pixels apart");

        // ── 형제로 끼우기: Alpha 줄의 **위쪽 띠**에 놓으면 그 앞에 간다. ───────
        Spot band = to;
        band.y = to.y + 1;
        const std::size_t undo = editor.GetCommands().GetUndoCount();
        DragTo(editor, hwnd, from, band);
        canvas->GetRootObjects(roots);
        Check(roots.Size() == 3, "nothing is lost by a drag");
        Check(roots[0] == gamma,
            "dropping on the upper band of a row must put the dragged one before it");
        Check(roots[1] == alpha && roots[2] == beta, "with the others sliding back");
        Check(editor.GetCommands().GetUndoCount() == undo + 1,
            "one drag must leave exactly one thing to undo");

        Check(editor.GetCommands().Undo(), "undo must run");
        canvas->GetRootObjects(roots);
        Check(roots[0] == alpha && roots[1] == beta && roots[2] == gamma,
            "and put the order back");

        // ── 자식으로 넣기: 줄 **가운데**에 놓으면 그 밑으로 들어간다. ──────────
        Check(FindHierarchyRow(editor, hwnd, gamma, from), "Gamma must still have a row");
        Check(FindHierarchyRow(editor, hwnd, alpha, to), "Alpha must still have a row");
        Spot middle = to;
        middle.y = to.y + pitch / 2;
        DragTo(editor, hwnd, from, middle);
        Check(gamma->GetParent() == alpha,
            "dropping on the middle of a row must make it a child");
        canvas->GetRootObjects(roots);
        Check(roots.Size() == 2, "and take it out of the roots");

        // ── 부모 해제: 남은 빈자리에 놓으면 뿌리로 올라간다. ───────────────────
        //
        // 이 자리가 죽어 있었다. `ImGui::Dummy` 에 음수 폭을 넘겨 사각형이 뒤집혀
        // 있었고, 그래서 부모를 뗄 방법이 끌어 놓기로는 없었다.
        for (int frame = 0; frame < 3; ++frame)
        {
            Check(editor.Tick(Frame), "the editor must settle after the reparent");
        }
        Check(FindHierarchyRow(editor, hwnd, gamma, from), "the child must have a row");
        ImGuiWindow* hierarchy = ImGui::FindWindowByName("Hierarchy");
        Check(hierarchy != nullptr, "the hierarchy must have a window");
        Spot blank;
        blank.x = static_cast<int>(hierarchy->Pos.x + hierarchy->Size.x * 0.5f);
        blank.y = static_cast<int>(hierarchy->Pos.y + hierarchy->Size.y - 20.0f);
        DragTo(editor, hwnd, from, blank);
        Check(gamma->GetParent() == nullptr,
            "dropping on the empty space below the tree must take the parent off");
        canvas->GetRootObjects(roots);
        Check(roots.Size() == 3, "and put it back among the roots");
        Check(roots[2] == gamma, "at the end, where it was dropped");

        (void)beta;
        editor.Shutdown();
    }
}

int RunEditorApplicationTests()
{
    TestEditorProjectSessions();
    TestTheEditorPaintsItsOwnScreen(JBro::GraphicsApi::D3D12);
    TestTheEditorPaintsItsOwnScreen(JBro::GraphicsApi::D3D11);
    TestTheEditorPaintsItsOwnScreen(JBro::GraphicsApi::Vulkan);
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
    TestDraggingTheGizmoMovesTheSelectionUnderOneUndo();
    TestPopupsOpenOneAtATimeAndCloseByHandle();
    TestSavingAsksForAPathOnceAndReportsFailure();
    TestMovingAComponentFromItsHeaderMenuCanBeUndone();
    TestCopyAndPasteMakeASiblingAndSelectIt();
    TestAStructElementOpensAndEditsEveryChosenList();
    TestDraggingAStructElementReordersEveryChosenList();
    TestFlagCountAndToneElementsEditByMouseOnEveryChosenList();
    TestTypingTheSameValueLeavesNothingToUndo();
    TestTheAssetFieldPicksARegisteredSprite();
    TestTheAssetBrowserSelectsAnAssetAndTheInspectorRewritesItsMeta();
    TestPlayingAndStoppingRestoresTheCanvas();
    TestBoxSelectInTheCanvasViewPicksWhatItTouches();
    TestTheCanvasViewDrawsInA3DProject();
    TestProjectSettingsAreWrittenBackToTheFile();
    TestDraggingAnAssetOntoTheFieldPicksIt();
    TestTheSpriteViewerDocksBesideTheMainDock();
    TestImportingAPictureCopiesAndRegistersIt();
    TestNewProjectCreatesAndOpensIt();
    TestTheCanvasViewPicksTheRootUntilYouStepInside();
    TestCreatedObjectsCarryTheFrameworkTransform();
    TestPanelsGoThroughTheWidgetLayer();
    TestPickingFollowsTheSpriteAssetSize();
    TestTheEditorSessionSurvivesReopening();
    TestTheStatsPanelShowsWhatTheCanvasHolds();
    TestTheCanvasViewDrawsColliderShapes();
    TestTheInspectorRenamesAndTogglesThroughCommands();
    TestTheAssetBrowserSelectsManyFilesAtOnce();
    TestAssetFileOperationsCarryTheMeta();
    TestDraggingInTheHierarchyReordersAndUnparents();
    TestCreatingAnObjectCanBeUndone();
    TestDeletingAnObjectCanBeUndoneWithItsValues();
    TestClosingTheWindowDoesNotTakeTheUiDownWithIt();
    TestEditorOpensAProjectFile();
    TestTheProjectFileDecidesTheFramework();
    TestEditorSavesAndOpensACanvas();
    TestCanvasWorkNeedsAnOpenProject();
    TestAProjectOpensWithoutItsScriptModule();
    std::cout << "Editor application tests passed.\n";
    return 0;
}

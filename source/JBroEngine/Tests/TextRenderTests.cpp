#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/CanvasFile.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Framework2D/BuiltinComponentProperties2D.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/ServiceContext.h>
#include <JBro/Framework2DSystem/BuiltinComponentTypes2D.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Framework2DSystem/System/Text2DSystem.h>
#include <JBro/Framework3D/Component/Camera3D.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Text3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3D/ServiceContext.h>
#include <JBro/Framework3DSystem/Framework3D.h>
#include <JBro/Framework3DSystem/Rendering/MeshLibrary.h>
#include <JBro/Framework3DSystem/System/Text3DSystem.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Reflection/ReflectedYaml.h>
#include <JBro/Runtime/TextStore.h>
#include <JBro/Task/TaskManager.h>
#include <JBro/Text/GlyphAtlas.h>
#include <JBro/Text/TextLayout.h>

#include "TestFontNotoSansKR.generated.h"
#include "TestFontNotoSansKRLatin.generated.h"

#include <cmath>
#include <chrono>
#include <thread>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

// 텍스트 2 단계(D-200, text-plan §5)의 테스트다: 글자 저장소와 코덱, 폰트 에셋, 복사, 그리고 GPU 로 그린 글자.
namespace
{
    namespace fs = std::filesystem;
    using namespace JBro;

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    String Utf8(const fs::path& path)
    {
        const std::u8string text = path.generic_u8string();
        return String(reinterpret_cast<const char*>(text.data()), text.size());
    }

    void WriteBytes(const fs::path& path, const void* data, std::size_t size)
    {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    bool TextIs(TextId id, const char* expected)
    {
        const ArrayView<const char> text = TextStore::Get().GetText(id);
        const std::size_t length = std::strlen(expected);
        return text.Size() == length && (length == 0 || std::memcmp(text.Data(), expected, length) == 0);
    }

    void TestTheStoreHandsOutSlots()
    {
        TextStore& store = TextStore::Get();
        const std::uint32_t baseline = store.GetLiveCount();
        TextId hello = store.Create("hello", 5);
        Check(hello.IsValid() && TextIs(hello, "hello") && store.GetRevision(hello) == 1, "a new slot holds its text at revision 1");
        Check(store.Set(hello, "안녕", std::strlen("안녕")) && TextIs(hello, "안녕") && store.GetRevision(hello) == 2,
            "setting the text bumps the revision");
        Check(store.GetLiveCount() == baseline + 1, "one slot is live");

        const TextId old = hello;
        store.Destroy(hello);
        Check(false == store.IsAlive(old) && store.GetText(old).Size() == 0 && store.GetRevision(old) == 0,
            "a destroyed slot reads as empty");
        Check(false == store.Set(old, "x", 1), "a destroyed id cannot be written");
        const TextId reused = store.Create("again", 5);
        Check(reused.index == old.index && reused.generation != old.generation, "the slot is reused with a new generation");
        Check(TextIs(reused, "again") && store.GetText(old).Size() == 0, "the old id does not see the new owner's text");

        TextId empty;
        store.Assign(empty, "made", 4);
        Check(empty.IsValid() && TextIs(empty, "made"), "assigning into an empty id makes a slot");
        const TextId before = empty;
        store.Assign(empty, "kept", 4);
        Check(empty.index == before.index && empty.generation == before.generation && TextIs(empty, "kept"),
            "assigning into a live id reuses its slot");
        store.Destroy(reused);
        store.Destroy(empty);
        Check(store.GetLiveCount() == baseline, "every slot is returned");
    }

    // 캔버스 파일의 YAML 은 값을 이스케이프하지 않는다. 코덱이 한 줄로 접은 것이 파일을 지나 그대로 돌아와야 한다.
    void TestTricky(Canvas& canvas, const char* sample)
    {
        GameObject* object = canvas.CreateObject("label");
        canvas.AttachComponent<Component::Transform2D>(object);
        auto* text = canvas.AttachComponent<Component::Text2D>(object);
        TextStore::Get().Assign(text->text, sample, std::strlen(sample));

        String saved;
        CanvasFileError error;
        Check(WriteCanvasText(canvas, saved, error), "a canvas with a text saves");
        Canvas reopened(CreateDefaultAllocator());
        if (false == ReadCanvasText(reopened, saved.c_str(), saved.size(), error))
        {
            std::cout << "  sample [" << sample << "] failed: " << error.message.c_str() << "\nfile:\n" << saved.c_str();
            Check(false, "the saved canvas reads back");
        }
        const Component::Text2D* copy = nullptr;
        reopened.ForEach<Component::Text2D>([&](Component::Text2D& found) { copy = &found; });
        Check(copy != nullptr, "the text component comes back");
        if (false == TextIs(copy->text, sample))
        {
            std::cout << "  sample [" << sample << "] came back as ["
                << String(TextStore::Get().GetText(copy->text).Data(), TextStore::Get().GetText(copy->text).Size()).c_str() << "]\n";
            Check(false, "the text survives the canvas file unchanged");
        }
        Check(copy->text.index != text->text.index, "the reopened text has its own slot");

        // 스냅숏 길(파일을 거치지 않고 글자로 떴다 다시 쓰기)도 같은 글자를 준다. 에디터 커맨드가 쓰는 그 함수를 지난다 -
        // 처음 판은 코덱을 손으로 불러 `required - 1` 을 넘겼고, 그래서 그 함수가 끝에 NUL 을 붙이는 것을 못 봤다(3 단계에서 드러났다).
        const ValueCodec& codec = GetTextIdCodec();
        String snapshotText;
        Check(ReflectedValueToText(codec, &text->text, snapshotText), "the codec writes the text");
        Check(snapshotText.empty() || snapshotText.back() != '\0', "the snapshot text carries no terminator");
        TextId snapshot;
        Check(codec.FromText(&snapshot, snapshotText.c_str(), snapshotText.size()) && TextIs(snapshot, sample),
            "the snapshot path round-trips");
        TextStore::Get().Destroy(snapshot);
        canvas.DestroyObject(object);
        canvas.FlushPendingDestroy();
    }

    void TestTheCodecSurvivesTheCanvasFile()
    {
        Component::RegisterBuiltinComponentProperties2D();
        Component::RegisterBuiltinComponentTypes2D();
        Canvas canvas(CreateDefaultAllocator());
        const char* samples[] =
        {
            "hello", "", "안녕하세요", "two\nlines", "a: b", "key: value: more", "  leading", "trailing  ",
            "\"quoted\"", "'single'", "'", "\"", "[]", "{}", "- dash", "# hash", "back\\slash", "tab\there",
            "crlf\r\nline", "C:\\path\\n", "mixed \"q\" and \\ and\nnew",
        };
        for (const char* sample : samples)
        {
            TestTricky(canvas, sample);
        }
    }

    void TestCopiesGetTheirOwnSlot()
    {
        Component::RegisterBuiltinComponentProperties2D();
        Component::RegisterBuiltinComponentTypes2D();
        const std::uint32_t baseline = TextStore::Get().GetLiveCount();
        {
            Canvas canvas(CreateDefaultAllocator());
            GameObject* a = canvas.CreateObject("a");
            GameObject* b = canvas.CreateObject("b");
            auto* first = canvas.AttachComponent<Component::Text2D>(a);
            auto* second = canvas.AttachComponent<Component::Text2D>(b);
            TextStore::Get().Assign(first->text, "original", 8);

            // 에디터의 복사·붙여넣기·되돌리기는 새 컴포넌트에 글자를 FromText 로 쓴다(ComponentSnapshot).
            const ValueCodec& codec = GetTextIdCodec();
            char buffer[64];
            std::size_t required = 0;
            Check(codec.ToText(&first->text, buffer, sizeof(buffer), required), "the source text is captured");
            Check(codec.FromText(&second->text, buffer, required - 1), "and written into the copy");
            Check(second->text.index != first->text.index, "the copy has its own slot");
            TextStore::Get().Assign(first->text, "changed", 7);
            Check(TextIs(second->text, "original"), "changing the source leaves the copy alone");

            // Assign 은 번호가 아니라 글자를 옮긴다. 같은 칸을 나눠 가진 둘(C++ 복사)이면 받는 쪽에 새 칸을 준다.
            TextId alias = first->text;
            codec.Assign(&alias, &first->text);
            Check(alias.index != first->text.index && TextIs(alias, "changed"), "assigning an alias gives it its own slot");
            TextStore::Get().Destroy(alias);
            Check(codec.Equals(&first->text, &first->text), "a text equals itself");
            Check(false == codec.Equals(&first->text, &second->text), "different texts are not equal");

            Check(TextStore::Get().GetLiveCount() == baseline + 2, "two texts hold two slots");
            canvas.DestroyObject(a);
            canvas.FlushPendingDestroy();
            Check(TextStore::Get().GetLiveCount() == baseline + 1, "destroying an object returns its text slot");
        }
        Check(TextStore::Get().GetLiveCount() == baseline, "tearing the canvas down returns the rest");
    }

    struct FontProject
    {
        WindowsPlatform platform;
        JMemoryContext memory;
        AssetRegistry registry;
        AssetSystem assets;
        fs::path root;
        AssetId fontId;
        // 한글이 없는 라틴 서브셋이다. 폴백 시험이 쓴다(3 단계).
        AssetId latinId;
        String metaPath;

        void Open(float pixelsPerUnit)
        {
            root = fs::temp_directory_path() / L"JBroTextProbe·글자";
            fs::remove_all(root);
            WriteBytes(root / "Fonts" / "sans.otf", TestFontNotoSansKR, sizeof(TestFontNotoSansKR));
            WriteBytes(root / "Fonts" / "latin.otf", TestFontNotoSansKRLatin, sizeof(TestFontNotoSansKRLatin));
            Check(platform.Initialize(memory), "the platform initializes");
            AssetScanOptions options;
            options.createMissingMeta = true;
            AssetScanReport report;
            Check(registry.Scan(platform, Utf8(root).c_str(), options, report), "the font folder scans");
            const AssetRecord* record = registry.FindByPath("Fonts/sans.otf");
            Check(record != nullptr && record->type == AssetType::Font, "an .otf registers as a Font");
            fontId = record->id;
            const AssetRecord* latin = registry.FindByPath("Fonts/latin.otf");
            Check(latin != nullptr && latin->type == AssetType::Font, "the latin subset registers as a Font");
            latinId = latin->id;
            metaPath = Utf8(root / "Fonts" / "sans.otf.jmeta");
            WriteOptions(pixelsPerUnit, TextureFilter::Default);
            // 라틴 폰트도 같은 PPU 다. 폴백 글자는 기본 폰트(라틴)의 PPU 로 그려지므로 둘이 다르면 칸이 어긋난다.
            WriteOptionsAt(Utf8(root / "Fonts" / "latin.otf.jmeta"), pixelsPerUnit, TextureFilter::Default);
            Check(assets.Initialize(memory), "the asset system initializes");
            assets.Bind(platform, registry, Utf8(root).c_str());
        }

        void WriteOptions(float pixelsPerUnit, TextureFilter filter)
        {
            WriteOptionsAt(metaPath, pixelsPerUnit, filter);
        }

        void WriteOptionsAt(const String& metaPath, float pixelsPerUnit, TextureFilter filter)
        {
            AssetMetaFile meta;
            AssetMetaError error;
            Check(LoadAssetMetaFile(platform, metaPath.c_str(), meta, error), "the font meta reads");
            meta.hasFontOptions = true;
            meta.fontOptions.pixelsPerUnit = pixelsPerUnit;
            meta.fontOptions.filter = filter;
            Check(SaveAssetMetaFile(platform, metaPath.c_str(), meta), "the font meta saves");
            AssetMetaFile reread;
            Check(LoadAssetMetaFile(platform, metaPath.c_str(), reread, error) && reread.hasFontOptions
                && reread.fontOptions.pixelsPerUnit == pixelsPerUnit && reread.fontOptions.filter == filter,
                "the Font block round-trips");
        }

        void Close()
        {
            assets.Shutdown();
            platform.Shutdown();
            fs::remove_all(root);
        }
    };

    void TestFontAssetsLoadAndReload()
    {
        FontProject project;
        project.Open(40.0f);
        AssetSystem& assets = project.assets;
        const AssetHandle font = assets.Load(project.fontId);
        const FontData* data = assets.GetFont(font);
        Check(data != nullptr && data->bytes.Size() == sizeof(TestFontNotoSansKR), "a font loads as its file bytes");
        Check(data->options.pixelsPerUnit == 40.0f, "with the meta's pixels per unit");
        Check(data->options.filter == TextureFilter::Nearest, "and Default becomes the project filter");
        Check(assets.GetTexture(font) == nullptr && assets.GetAudio(font) == nullptr, "a font handle is only a font");
        Check(assets.Load(project.fontId).index == font.index && assets.GetReferenceCount(font) == 2, "loading again shares it");

        project.WriteOptions(0.0f, TextureFilter::Linear);
        Check(assets.ReloadInPlace(project.fontId), "a loaded font reloads in place");
        data = assets.GetFont(font);
        Check(data != nullptr && data->dataGeneration == 2, "the handle lives on with a new generation");
        Check(data->options.pixelsPerUnit == DefaultPixelsPerUnit && data->options.filter == TextureFilter::Linear,
            "a zero PPU falls back to the default and the filter follows the meta");

        assets.Release(font);
        assets.Release(font);
        Check(assets.CollectUnused() == 1 && assets.GetFont(font) == nullptr, "an unused font is collected");
        project.Close();
    }

    // ── GPU ─────────────────────────────────────────────────────────────────────────────────────
    struct Gpu
    {
        WindowsPlatform& platform;
        D3D12RHIModule rhi;
        Renderer renderer;
        WindowHandle window;
        Array<std::byte> image;
        TextureReadback readback;
        bool ready = false;

        explicit Gpu(WindowsPlatform& owner, JMemoryContext memory)
            : platform(owner)
        {
            if (false == rhi.Initialize(memory))
            {
                return;
            }
            WindowDesc windowDesc;
            constexpr char title[] = "JBro text probe";
            windowDesc.title = {title, sizeof(title) - 1};
            windowDesc.width = 64;
            windowDesc.height = 64;
            windowDesc.visible = false;
            window = platform.OpenPlatformWindow(windowDesc);
            RendererConfig config;
            config.api = rhi.GetApi();
            config.surface = platform.CreateSurface(window);
            config.surfaceExtent = {64, 64};
            config.maxSpriteSubmissions = 64;
            config.presentMode = PresentMode::Immediate;
            Check(renderer.Initialize(rhi, config), "the renderer initializes");
            image.Resize(64 * 64 * 4);
            ready = true;
        }

        void Close()
        {
            if (false == ready)
            {
                rhi.Shutdown();
                return;
            }
            renderer.Shutdown();
            rhi.Shutdown();
            platform.ClosePlatformWindow(window);
            platform.PumpEvents();
        }

        template <typename TFramework>
        void Paint(TFramework& framework)
        {
            framework.Update(1.0f / 60.0f);
            Check(renderer.BeginFrame() == FrameStatus::Ready, "the frame begins");
            Check(framework.Render() == RenderResult::Submitted, "the framework submits");
            Check(renderer.EndFrame() == FrameStatus::Ready, "the frame presents");
            Check(renderer.ReadBackBuffer(image.Data(), image.Size(), readback), "the frame reads back");
        }

        float Red(std::uint32_t x, std::uint32_t y) const
        {
            const auto* pixel = reinterpret_cast<const unsigned char*>(
                image.Data() + static_cast<std::size_t>(y) * readback.rowPitch + static_cast<std::size_t>(x) * 4);
            return pixel[2] / 255.0f;
        }

        float Green(std::uint32_t x, std::uint32_t y) const
        {
            const auto* pixel = reinterpret_cast<const unsigned char*>(
                image.Data() + static_cast<std::size_t>(y) * readback.rowPitch + static_cast<std::size_t>(x) * 4);
            return pixel[1] / 255.0f;
        }

        float Blue(std::uint32_t x, std::uint32_t y) const
        {
            const auto* pixel = reinterpret_cast<const unsigned char*>(
                image.Data() + static_cast<std::size_t>(y) * readback.rowPitch + static_cast<std::size_t>(x) * 4);
            return pixel[0] / 255.0f;
        }
    };

    struct DarkBox
    {
        std::uint32_t count = 0;
        std::uint32_t minX = 64;
        std::uint32_t minY = 64;
        std::uint32_t maxX = 0;
        std::uint32_t maxY = 0;
    };

    DarkBox FindDark(const Gpu& gpu)
    {
        DarkBox box;
        for (std::uint32_t y = 0; y < 64; ++y)
        {
            for (std::uint32_t x = 0; x < 64; ++x)
            {
                if (gpu.Red(x, y) < 0.5f)
                {
                    ++box.count;
                    box.minX = std::min(box.minX, x);
                    box.minY = std::min(box.minY, y);
                    box.maxX = std::max(box.maxX, x);
                    box.maxY = std::max(box.maxY, y);
                }
            }
        }
        return box;
    }

    // 커널로 따로 잰, 글자 하나의 화면 사각형이다. 카메라가 64 픽셀에 2 유닛이고 폰트 PPU 가 32 라 글자 픽셀 = 화면 픽셀이다.
    struct ScreenRect
    {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
    };

    ScreenRect ExpectedGlyph(const char* utf8, std::uint32_t pixelSize)
    {
        Text::FontFace face;
        Check(face.Load(ArrayView<const std::byte>(reinterpret_cast<const std::byte*>(TestFontNotoSansKR), sizeof(TestFontNotoSansKR))),
            "the reference face loads");
        Text::TextLayout layout;
        Text::LayoutOptions options;
        options.fontSize = static_cast<float>(pixelSize);
        options.alignX = Text::AlignX::Center;
        options.alignY = Text::AlignY::Middle;
        const Text::FontFace* faces[] = { &face };
        Check(layout.Build(ArrayView<const char>(utf8, std::strlen(utf8)), faces, options) == Text::LayoutError::None
            && layout.GetGlyphs().Size() == 1, "the reference layout has one glyph");
        Text::GlyphBitmapBox box;
        Check(face.MeasureGlyphBitmap(layout.GetGlyphs()[0].glyph, static_cast<float>(pixelSize), box), "the reference glyph measures");
        ScreenRect rect;
        rect.left = 32.0f + layout.GetGlyphs()[0].x + static_cast<float>(box.left);
        rect.top = 32.0f - (layout.GetGlyphs()[0].y + static_cast<float>(box.top));
        rect.right = rect.left + static_cast<float>(box.width);
        rect.bottom = rect.top + static_cast<float>(box.height);
        return rect;
    }

    void TestTextDrawsCachesAndUploadsOnlyNewGlyphs()
    {
        FontProject project;
        project.Open(32.0f);
        Gpu gpu(project.platform, project.memory);
        if (false == gpu.ready)
        {
            std::cout << "  [skip] no D3D12 device; text rendering not verified" << std::endl;
            gpu.Close();
            project.Close();
            return;
        }
        {
            Framework2D framework;
            FrameworkContext context;
            context.memory = project.memory;
            context.assets = &project.assets;
            context.renderer = &gpu.renderer;
            Check(framework.Initialize(context), "the framework initializes");
            Check(framework.BindScriptContexts(), "the script contexts bind");
            Canvas* canvas = framework.GetCanvas();
            GameObject* cameraObject = canvas->CreateObject("camera");
            canvas->AttachComponent<Component::Transform2D>(cameraObject);
            auto* camera = canvas->AttachComponent<Component::Camera2D>(cameraObject);
            camera->primary = true;
            camera->orthographicSize = 1.0f;
            camera->clearColor = {1.0f, 1.0f, 1.0f, 1.0f};

            GameObject* labelObject = canvas->CreateObject("label");
            canvas->AttachComponent<Component::Transform2D>(labelObject);
            auto* label = canvas->AttachComponent<Component::Text2D>(labelObject);
            label->fontId = project.fontId;
            label->fontSize = 40.0f;
            label->alignX = Component::TextAlignX::Center;
            label->alignY = Component::TextAlignY::Middle;
            label->color = {0.0f, 0.0f, 0.0f, 1.0f};
            TextStore::Get().Assign(label->text, "A", 1);
            framework.BindCanvasAssets();
            Check(label->font.generation != 0, "the label's font resolves");

            auto* texts = canvas->GetSystems().FindSystem<System::Text2DSystem>();
            Check(texts != nullptr, "the framework runs a text system");

            // 1. 검은 A 가 커널이 잰 사각형 안에만 찍힌다.
            gpu.Paint(framework);
            const ScreenRect expected = ExpectedGlyph("A", 40);
            DarkBox dark = FindDark(gpu);
            Check(dark.count > 40, "the A is drawn in black on white");
            Check(static_cast<float>(dark.minX) >= expected.left - 1.0f && static_cast<float>(dark.maxX) <= expected.right + 1.0f
                && static_cast<float>(dark.minY) >= expected.top - 1.0f && static_cast<float>(dark.maxY) <= expected.bottom + 1.0f,
                "every dark pixel lies inside the glyph's cell");
            Check(static_cast<float>(dark.maxX - dark.minX) > (expected.right - expected.left) * 0.6f,
                "and the A spans most of its cell");
            float minX = 0.0f;
            float minY = 0.0f;
            float maxX = 0.0f;
            float maxY = 0.0f;
            Check(texts->GetLocalBounds(label->GetInstanceId(), minX, minY, maxX, maxY) && minX < 0.0f && maxX > 0.0f
                && minY < 0.0f && maxY > 0.0f, "the centred text reports a block around its origin");
            Check(texts->GetLibrary().GetPageTextureCount() == 1, "one atlas page is on the GPU");
            const std::uint64_t uploads = texts->GetLibrary().GetUploadCount();
            const std::uint64_t uploadedBefore = texts->GetLibrary().GetUploadedBytes();
            const std::uint64_t relayouts = texts->GetRelayoutCount();
            const std::uint32_t registered = gpu.renderer.GetTextureCount();

            // 2. 아무것도 바뀌지 않은 프레임은 레이아웃도 업로드도 없다.
            gpu.Paint(framework);
            gpu.Paint(framework);
            Check(texts->GetRelayoutCount() == relayouts, "an unchanged text is not laid out again");
            Check(texts->GetLibrary().GetUploadCount() == uploads, "a frame without new glyphs uploads nothing");
            Check(FindDark(gpu).count == dark.count, "and draws the same pixels");

            // 3. 스크립트 서비스로 글자를 바꾸면 다음 프레임에 보인다. 새 글자는 페이지를 한 번 올리고, 아는 글자로 바꾸면 올리지 않는다.
            const Service::Text2DService& service = GetFramework2DServices().Text2D;
            const Ref<Component::Text2D> ref = labelObject->GetScriptHandle().GetComponent<Component::Text2D>();
            Check(service.SetText(ref, "V"), "a script sets the text");
            char copied[8] = {};
            Check(service.GetTextLength(ref) == 1 && service.CopyText(ref, copied, sizeof(copied)) == 1 && copied[0] == 'V',
                "and reads it back");
            gpu.Paint(framework);
            Check(texts->GetRelayoutCount() == relayouts + 1, "the changed text is laid out once");
            Check(texts->GetLibrary().GetUploadCount() == uploads + 1, "a new glyph uploads its page once");
            // 올린 것은 V 의 칸을 감싼 사각형이지 페이지 전체(4 MB)가 아니다.
            Check(texts->GetLibrary().GetUploadedBytes() - uploadedBefore < 64 * 64 * 4,
                "and only the rectangle around the new cell goes up");
            const DarkBox vee = FindDark(gpu);
            Check(vee.count > 20 && (vee.minX != dark.minX || vee.minY != dark.minY || vee.count != dark.count), "the V replaces the A");
            Check(service.SetText(ref, "A"), "back to A");
            gpu.Paint(framework);
            Check(texts->GetLibrary().GetUploadCount() == uploads + 1, "a glyph already in the atlas uploads nothing");
            Check(FindDark(gpu).count == dark.count, "and the A is back pixel for pixel");
            Check(gpu.renderer.GetTextureCount() == registered, "no texture was registered for any of it");
            // 3-1. 새 글자 둘(T·o)이 한 프레임에 들어오면 올리는 사각형이 두 칸을 다 감싼다. 뒤의 칸(o)만 따로 그려 보면 올린 것이 보인다.
            Check(service.SetText(ref, "To"), "two new glyphs in one frame");
            gpu.Paint(framework);
            const std::uint64_t afterPair = texts->GetLibrary().GetUploadCount();
            Check(afterPair == uploads + 2, "the pair goes up in one upload");
            Check(service.SetText(ref, "o"), "the second of the pair alone");
            gpu.Paint(framework);
            Check(texts->GetLibrary().GetUploadCount() == afterPair && FindDark(gpu).count > 20,
                "the second cell of the pair was uploaded with the first");
            Check(service.SetText(ref, "A"), "back to A once more");
            gpu.Paint(framework);

            // 4. 색만 바꾸면 다시 레이아웃하지 않는다.
            const std::uint64_t beforeColour = texts->GetRelayoutCount();
            label->color = {1.0f, 0.0f, 0.0f, 1.0f};
            gpu.Paint(framework);
            Check(texts->GetRelayoutCount() == beforeColour, "a colour change reuses the layout");
            Check(FindDark(gpu).count == 0, "a red A on white has no dark red channel");
            label->color = {0.0f, 0.0f, 0.0f, 1.0f};

            // 4-1. 레이아웃 옵션(크기)을 바꾸면 다시 레이아웃하고 작게 그린다. 되돌리면 같은 픽셀이다.
            label->fontSize = 20.0f;
            gpu.Paint(framework);
            Check(texts->GetRelayoutCount() == beforeColour + 1, "a size change lays the text out again");
            const DarkBox small = FindDark(gpu);
            Check(small.count > 5 && small.count < dark.count / 2, "and draws it smaller");
            label->fontSize = 40.0f;
            gpu.Paint(framework);
            Check(FindDark(gpu).count == dark.count, "the original size draws the same pixels again");

            // 4-2. Clip 은 상자 밖의 글리프 조각을 잘라 낸다. 가운데 정렬이라 상자는 원점 둘레 16 x 40 픽셀(화면 x 24~40, y 12~52)이다.
            // 40 px 글자의 기준선은 상자 위에서 46 px 아래라 A 의 아랫부분과 양옆이 잘린다. (16 x 16 이면 A 전체가 상자 아래에 있어
            // 하나도 남지 않는다 - 그것도 맞는 동작이다.)
            // 같은 상자를 Wrap 으로 그린 것이 기준이다 - 상자가 블록 높이를 바꾸므로 상자 없는 A 와는 기준선이 다르다.
            label->boxSize = {16.0f, 40.0f};
            gpu.Paint(framework);
            const DarkBox boxed = FindDark(gpu);
            Check(boxed.maxY > 52 && (boxed.minX < 24 || boxed.maxX > 40), "unclipped, the boxed A overhangs its box");
            label->overflow = Component::TextOverflow::Clip;
            gpu.Paint(framework);
            const DarkBox clipped = FindDark(gpu);
            Check(clipped.count > 5 && clipped.count < boxed.count, "a clipped A keeps only part of its pixels");
            Check(clipped.minX >= 23 && clipped.maxX <= 40 && clipped.minY >= 11 && clipped.maxY <= 52,
                "and none outside the box around the origin");
            Check(clipped.maxY >= 50, "the cut runs along the box's bottom edge");
            label->overflow = Component::TextOverflow::Wrap;

            // 4-3. 자동 크기: 16 x 40 상자에 A 한 글자가 들어가는 가장 큰 정수 크기로 그린다. 줄 높이가 1.448 em 이라 높이로는 27 px 까지,
            // A 의 폭(0.608 em)으로는 26 px 까지다. 상자 없이는 뜻이 없어 켜도 그대로다.
            label->autoSize = true;
            label->minFontSize = 8.0f;
            label->maxFontSize = 200.0f;
            gpu.Paint(framework);
            const float fitted = texts->GetLaidOutFontSize(label->GetInstanceId());
            std::cout << "  [measure] auto size in a 16 x 40 box: " << fitted << " px" << std::endl;
            Check(fitted == std::floor(fitted) && fitted * 0.608f <= 16.0f + 0.01f && (fitted + 1.0f) * 0.608f > 16.0f,
                "auto size picks the largest whole size whose A fits the box width");
            Check(FindDark(gpu).count > 0 && FindDark(gpu).count < dark.count, "and draws the smaller A");
            // 자동 크기만 끄고 켜도 다시 레이아웃한다(상자는 그대로).
            label->autoSize = false;
            gpu.Paint(framework);
            Check(texts->GetLaidOutFontSize(label->GetInstanceId()) == 40.0f, "turning auto size off goes back to fontSize");
            label->autoSize = true;
            gpu.Paint(framework);
            Check(texts->GetLaidOutFontSize(label->GetInstanceId()) == fitted, "and on again fits the box again");
            label->boxSize = {0.0f, 0.0f};
            gpu.Paint(framework);
            Check(texts->GetLaidOutFontSize(label->GetInstanceId()) == 40.0f, "without a box auto size falls back to fontSize");
            label->autoSize = false;
            gpu.Paint(framework);
            Check(FindDark(gpu).count == dark.count, "unclipped again it is whole");

            // 5. 페이지가 넘치면 둘째 페이지가 생긴다. 첫 페이지의 A 는 그대로 그려진다.
            GameObject* crowdObject = canvas->CreateObject("crowd");
            auto* crowdTransform = canvas->AttachComponent<Component::Transform2D>(crowdObject);
            crowdTransform->position = {100.0f, 100.0f};
            auto* crowd = canvas->AttachComponent<Component::Text2D>(crowdObject);
            crowd->fontId = project.fontId;
            // 300 px 음절 칸은 250 px 남짓이라 1024² 한 장에 열여섯 개쯤 들어간다(200 px 로는 서른여섯이 한 장에 들어가 넘치지 않았다).
            crowd->fontSize = 300.0f;
            const char* many = "가나다라마바사아자차카타파하안녕세요한글계텍스트줄바꿈어절음";
            TextStore::Get().Assign(crowd->text, many, std::strlen(many));
            framework.BindCanvasAssets();
            gpu.Paint(framework);
            Check(texts->GetLibrary().GetPageTextureCount() >= 2, "thirty 300 px syllables need a second page");
            Check(FindDark(gpu).count == dark.count, "the first page's A still draws the same");
            Check(texts->GetCachedTextCount() == 2, "two texts are cached");
            canvas->DestroyObject(crowdObject);
            gpu.Paint(framework);
            Check(texts->GetCachedTextCount() == 1, "a destroyed text leaves the cache");

            // 5-1. 퇴출: 한도를 한 장으로 줄이면 다음 프레임에 두 장짜리 아틀라스를 비우고, 남은 A 만 다시 떠 한 장이 된다.
            Check(texts->GetLibrary().GetPageTextureCount() >= 2, "the crowd's pages are still there");
            texts->SetAtlasPageLimit(1);
            const std::uint32_t trims = texts->GetLibrary().GetTrimCount();
            gpu.Paint(framework);
            Check(texts->GetLibrary().GetTrimCount() == trims + 1, "an atlas past its limit is emptied");
            Check(texts->GetLibrary().GetPageTextureCount() == 1, "and the text on screen fills one page again");
            Check(FindDark(gpu).count == dark.count, "the A draws the same after being drawn again");
            // 5-2. 보이는 글자만으로 한도를 넘으면(곧바로 또 넘치면) 비우지 않고 그 폰트의 한도를 올린다 - 매 프레임 다시 뜨지 않는다.
            GameObject* again = canvas->CreateObject("crowd again");
            auto* againTransform = canvas->AttachComponent<Component::Transform2D>(again);
            againTransform->position = {100.0f, 100.0f};
            auto* crowdAgain = canvas->AttachComponent<Component::Text2D>(again);
            crowdAgain->fontId = project.fontId;
            crowdAgain->fontSize = 300.0f;
            TextStore::Get().Assign(crowdAgain->text, many, std::strlen(many));
            framework.BindCanvasAssets();
            gpu.Paint(framework);
            gpu.Paint(framework);
            const std::uint64_t relayoutsAfterThrash = texts->GetRelayoutCount();
            gpu.Paint(framework);
            Check(texts->GetLibrary().GetTrimCount() == trims + 1, "an atlas that refills at once is not emptied again");
            Check(texts->GetLibrary().GetPageTextureCount() >= 2 && texts->GetRelayoutCount() == relayoutsAfterThrash,
                "its limit rose instead, so the text is not drawn again every frame");
            canvas->DestroyObject(again);
            texts->SetAtlasPageLimit(TextLibrary::DefaultPageLimit);
            gpu.Paint(framework);

            // 5-3. 글자도 스프라이트 제출 상한(여기서는 64)을 나눠 쓴다. 넘친 글자 수를 세고 알린다. 줄이면 다시 0 이다.
            GameObject* longObject = canvas->CreateObject("long");
            auto* longTransform = canvas->AttachComponent<Component::Transform2D>(longObject);
            longTransform->position = {100.0f, 100.0f};
            auto* longText = canvas->AttachComponent<Component::Text2D>(longObject);
            longText->fontId = project.fontId;
            const std::string seventy(70, 'A');
            TextStore::Get().Assign(longText->text, seventy.c_str(), seventy.size());
            framework.BindCanvasAssets();
            // 넘친 프레임은 프레임워크가 "다 내지 못했다" 로 알린다. 그 결과를 보고 넘어간다.
            framework.Update(1.0f / 60.0f);
            Check(gpu.renderer.BeginFrame() == FrameStatus::Ready, "the frame begins");
            Check(framework.Render() != RenderResult::Submitted, "a frame that dropped glyphs says so");
            Check(gpu.renderer.EndFrame() == FrameStatus::Ready, "the frame presents");
            std::cout << "  [measure] glyphs past the 64 sprite limit: " << texts->GetDroppedGlyphCount() << std::endl;
            Check(texts->GetDroppedGlyphCount() == 70 + 1 - 64, "glyphs past the sprite submission limit are counted");
            canvas->DestroyObject(longObject);
            gpu.Paint(framework);
            Check(texts->GetDroppedGlyphCount() == 0, "and the count goes back to zero once the text fits");

            // 6. 폰트가 다시 로드되면(PPU 32 → 64) 글자가 다시 레이아웃되고 절반 크기로 그려진다.
            project.WriteOptions(64.0f, TextureFilter::Default);
            Check(project.assets.ReloadInPlace(project.fontId), "the font reloads in place");
            gpu.Paint(framework);
            const DarkBox half = FindDark(gpu);
            Check(half.count > 5 && half.count < dark.count / 2, "the reloaded font draws the A at half size");
            Check(half.maxY - half.minY < dark.maxY - dark.minY, "and shorter");

            framework.UnbindScriptContexts();
            framework.Shutdown();
        }
        Check(gpu.renderer.GetTextureCount() == 0, "shutting the framework down returns every atlas page");
        gpu.Close();
        project.Close();
    }

    // **프로젝트 폰트**(D-200 (6), text-plan §5 의 3 단계). `fontId` 가 빈 텍스트는 프로젝트의 첫 폰트로 그리고, 폰트에 없는 글자는
    // 목록에서 찾아 그 폰트의 아틀라스로 그린다. 목록을 바꾸면 다음 프레임에 다시 레이아웃되고, 같은 목록을 다시 주면 그대로다.
    // **미리 뜬 폰트는 새 글자에도 올리지 않는다**(text-plan §3.6). 완성형 벌을 켠 폰트는 열 때 글자를 모두 떠 두고, 첫 프레임에 페이지를
    // 한 번 올린다. 그 뒤 다른 음절로 바꿔도 레이아웃만 하고 올리지 않는다.
    void TestPrewarmedFontsUploadOnce()
    {
        FontProject project;
        project.Open(32.0f);
        {
            AssetMetaFile meta;
            AssetMetaError error;
            Check(LoadAssetMetaFile(project.platform, project.metaPath.c_str(), meta, error), "the font meta reads");
            meta.hasFontOptions = true;
            meta.fontOptions.prewarm = FontPrewarm::Ksx1001;
            meta.fontOptions.prewarmSize = 40;
            Check(SaveAssetMetaFile(project.platform, project.metaPath.c_str(), meta), "the font meta saves with a prewarm set");
        }
        Gpu gpu(project.platform, project.memory);
        if (false == gpu.ready)
        {
            std::cout << "  [skip] no D3D12 device; prewarm not verified" << std::endl;
            gpu.Close();
            project.Close();
            return;
        }
        {
            Framework2D framework;
            FrameworkContext context;
            context.memory = project.memory;
            context.assets = &project.assets;
            context.renderer = &gpu.renderer;
            Check(framework.Initialize(context), "the framework initializes");
            Canvas* canvas = framework.GetCanvas();
            GameObject* cameraObject = canvas->CreateObject("camera");
            canvas->AttachComponent<Component::Transform2D>(cameraObject);
            auto* camera = canvas->AttachComponent<Component::Camera2D>(cameraObject);
            camera->primary = true;
            camera->orthographicSize = 1.0f;
            GameObject* labelObject = canvas->CreateObject("label");
            canvas->AttachComponent<Component::Transform2D>(labelObject);
            auto* label = canvas->AttachComponent<Component::Text2D>(labelObject);
            label->fontId = project.fontId;
            label->fontSize = 40.0f;
            TextStore::Get().Assign(label->text, "\xED\x95\x9C", 3);
            framework.BindCanvasAssets();
            auto* texts = canvas->GetSystems().FindSystem<System::Text2DSystem>();

            gpu.Paint(framework);
            Check(texts->GetLibrary().GetPrewarmedGlyphCount(label->font) == 95 + 29, "opening the font prewarmed its glyphs");
            const std::uint64_t uploads = texts->GetLibrary().GetUploadCount();
            Check(uploads == 1, "the prewarmed page goes up once, with the first frame");
            TextStore::Get().Assign(label->text, "\xEA\xB8\x80\xEC\x9E\x90 ABC", 10);
            gpu.Paint(framework);
            Check(texts->GetLibrary().GetUploadCount() == uploads, "new letters that were prewarmed upload nothing");

            // 미리 채운 페이지는 퇴출 한도에 들지 않는다. 200 px 로 미리 채우면 한 장을 넘는데, 한도를 1 로 줘도 비우지 않는다.
            // (원본 Noto Sans KR 의 기본 SDF 벌은 9 페이지라 기본 한도 8 에서 첫 프레임에 비워졌다.)
            {
                AssetMetaFile meta;
                AssetMetaError error;
                Check(LoadAssetMetaFile(project.platform, project.metaPath.c_str(), meta, error), "the font meta reads again");
                meta.fontOptions.prewarmSize = 200;
                Check(SaveAssetMetaFile(project.platform, project.metaPath.c_str(), meta), "the font meta saves a large prewarm");
            }
            texts->SetAtlasPageLimit(1);
            const std::uint32_t trims = texts->GetLibrary().GetTrimCount();
            Check(project.assets.ReloadInPlace(project.fontId), "the font reloads with the large prewarm");
            gpu.Paint(framework);
            gpu.Paint(framework);
            Check(texts->GetLibrary().GetPageTextureCount() >= 2, "a 200 px prewarm fills more than one page");
            Check(texts->GetLibrary().GetTrimCount() == trims && texts->GetLibrary().GetPrewarmedGlyphCount(label->font) == 95 + 29,
                "and the prewarmed pages are not counted against the page limit");
            framework.Shutdown();
        }
        gpu.Close();
        project.Close();
    }

    // **워커에서 미리 뜬다**(text-plan §5 의 뒤의 것 - 비동기 래스터화). 태스크 관리자를 주면 폰트를 여는 프레임은 뜨기를 워커에 맡기고
    // 돌아온다. 끝난 덩어리는 태스크 관리자의 `Update` 가 메인 스레드에서 아틀라스에 넣고, 다 끝나면 동기로 뜬 것과 같은 수가 선다.
    // 그사이 화면의 글자는 제 자리에서 뜬 것이 먼저 서고, 태스크 쪽의 같은 칸은 버린다.
    void TestPrewarmRunsOnWorkers()
    {
        FontProject project;
        project.Open(32.0f);
        {
            AssetMetaFile meta;
            AssetMetaError error;
            Check(LoadAssetMetaFile(project.platform, project.metaPath.c_str(), meta, error), "the font meta reads");
            meta.hasFontOptions = true;
            meta.fontOptions.prewarm = FontPrewarm::Ksx1001;
            meta.fontOptions.renderMode = FontRenderMode::Sdf;
            Check(SaveAssetMetaFile(project.platform, project.metaPath.c_str(), meta), "the font meta saves");
        }
        Gpu gpu(project.platform, project.memory);
        if (false == gpu.ready)
        {
            std::cout << "  [skip] no D3D12 device; worker prewarm not verified" << std::endl;
            gpu.Close();
            project.Close();
            return;
        }
        TaskManager tasks;
        TaskManagerDesc desc;
        desc.workerCount = 2;
        Check(tasks.Initialize(desc) && tasks.UsesWorkers(), "a task manager with two workers starts");
        {
            Framework2D framework;
            FrameworkContext context;
            context.memory = project.memory;
            context.assets = &project.assets;
            context.renderer = &gpu.renderer;
            context.tasks = &tasks;
            Check(framework.Initialize(context), "the framework initializes");
            Canvas* canvas = framework.GetCanvas();
            GameObject* cameraObject = canvas->CreateObject("camera");
            canvas->AttachComponent<Component::Transform2D>(cameraObject);
            auto* camera = canvas->AttachComponent<Component::Camera2D>(cameraObject);
            camera->primary = true;
            camera->orthographicSize = 1.0f;
            camera->clearColor = {1.0f, 1.0f, 1.0f, 1.0f};
            GameObject* labelObject = canvas->CreateObject("label");
            canvas->AttachComponent<Component::Transform2D>(labelObject);
            auto* label = canvas->AttachComponent<Component::Text2D>(labelObject);
            label->fontId = project.fontId;
            label->fontSize = 40.0f;
            label->alignX = Component::TextAlignX::Center;
            label->alignY = Component::TextAlignY::Middle;
            label->color = {0.0f, 0.0f, 0.0f, 1.0f};
            TextStore::Get().Assign(label->text, "A", 1);
            framework.BindCanvasAssets();
            auto* texts = canvas->GetSystems().FindSystem<System::Text2DSystem>();

            gpu.Paint(framework);
            Check(FindDark(gpu).count > 40, "the A draws on the first frame, before the prewarm is done");
            int frames = 0;
            while (texts->GetLibrary().IsPrewarming(label->font) && frames < 2000)
            {
                tasks.Update();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                ++frames;
            }
            std::cout << "  [measure] worker prewarm finished after " << frames << " waits, "
                      << texts->GetLibrary().GetPrewarmedGlyphCount(label->font) << " glyphs placed" << std::endl;
            Check(false == texts->GetLibrary().IsPrewarming(label->font), "the worker prewarm finishes");
            // A 는 레이아웃이 먼저 떴으므로 태스크 쪽은 버린다. 나머지 공백을 뺀 ASCII 와 한글이 들어간다.
            Check(texts->GetLibrary().GetPrewarmedGlyphCount(label->font) == 95 + 29 - 1,
                "every prewarmed glyph but the one already drawn went in");
            const std::uint64_t uploads = texts->GetLibrary().GetUploadCount();
            gpu.Paint(framework);
            Check(texts->GetLibrary().GetUploadCount() == uploads + 1, "the page with the worker's glyphs goes up once");
            TextStore::Get().Assign(label->text, "\xED\x95\x9C", 3);
            gpu.Paint(framework);
            Check(texts->GetLibrary().GetUploadCount() == uploads + 1, "and a prewarmed syllable uploads nothing");

            // 미리 채우기가 도는 중에 폰트가 다른 모드로 다시 열리면 옛 모드의 결과는 새 아틀라스에 들어가지 않는다. 비트맵으로 다시 열어
            // 워커를 띄운 채 곧바로 SDF 로 되돌린다 - 끝나면 SDF 판의 수(화면의 `한` 을 뺀 것)만 선다. 비트맵 칸(키가 다르다)이 섞이면 는다.
            const auto reopenAs = [&](FontRenderMode mode) {
                AssetMetaFile meta;
                AssetMetaError error;
                Check(LoadAssetMetaFile(project.platform, project.metaPath.c_str(), meta, error), "the font meta reads again");
                meta.fontOptions.renderMode = mode;
                Check(SaveAssetMetaFile(project.platform, project.metaPath.c_str(), meta), "the font meta saves again");
                Check(project.assets.ReloadInPlace(project.fontId), "the font reloads in place");
                gpu.Paint(framework);
            };
            reopenAs(FontRenderMode::Bitmap);
            Check(texts->GetLibrary().IsPrewarming(label->font), "the bitmap prewarm is on the workers");
            reopenAs(FontRenderMode::Sdf);
            frames = 0;
            while (texts->GetLibrary().IsPrewarming(label->font) && frames < 2000)
            {
                tasks.Update();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                ++frames;
            }
            Check(false == texts->GetLibrary().IsPrewarming(label->font), "the prewarm after the reopen finishes");
            Check(texts->GetLibrary().GetPrewarmedGlyphCount(label->font) == 95 + 29 - 1,
                "nothing from the bitmap prewarm reached the reopened atlas");

            // 워커가 여러 장을 채우는 동안에도, 끝난 뒤에도 미리 채운 페이지로는 비우지 않는다. 200 px 거리장은 한 장을 넘고 한도는 1 이다.
            {
                AssetMetaFile meta;
                AssetMetaError error;
                Check(LoadAssetMetaFile(project.platform, project.metaPath.c_str(), meta, error), "the font meta reads for large cells");
                meta.fontOptions.sdfSize = 200;
                Check(SaveAssetMetaFile(project.platform, project.metaPath.c_str(), meta), "the font meta saves large cells");
            }
            texts->SetAtlasPageLimit(1);
            const std::uint32_t trims = texts->GetLibrary().GetTrimCount();
            Check(project.assets.ReloadInPlace(project.fontId), "the font reloads with large cells");
            frames = 0;
            gpu.Paint(framework);
            while (texts->GetLibrary().IsPrewarming(label->font) && frames < 2000)
            {
                tasks.Update();
                gpu.Paint(framework);
                ++frames;
            }
            gpu.Paint(framework);
            Check(texts->GetLibrary().GetPageTextureCount() >= 2, "the large worker prewarm fills more than one page");
            Check(texts->GetLibrary().GetTrimCount() == trims, "and neither its growth nor its pages trim the atlas");
            framework.Shutdown();
        }
        tasks.Shutdown();
        gpu.Close();
        project.Close();
    }

    // **픽셀 맞춤**(text-plan §4.2 의 `pixelSnap`). 40 px `A` 를 가운데 정렬하면 원점이 (-12.16, -17.44) 같은 소수 자리라, 선형 필터에서
    // 텍셀 사이를 샘플해 픽셀 값이 번진다. 켜면 원점이 정수 자리로 가서 정수 자리에 둔 `A`(왼쪽·기준선 정렬)와 **픽셀 값의 모음이 같다**
    // (자리만 옮겨졌다). 끈 판이 다르다는 것도 본다 - 그래야 이 비교가 번짐을 잡는다는 증거다.
    void TestPixelSnapLandsGlyphsOnWholePixels()
    {
        FontProject project;
        project.Open(32.0f);
        project.WriteOptions(32.0f, TextureFilter::Linear);
        Gpu gpu(project.platform, project.memory);
        if (false == gpu.ready)
        {
            std::cout << "  [skip] no D3D12 device; pixel snapping not verified" << std::endl;
            gpu.Close();
            project.Close();
            return;
        }
        {
            Framework2D framework;
            FrameworkContext context;
            context.memory = project.memory;
            context.assets = &project.assets;
            context.renderer = &gpu.renderer;
            Check(framework.Initialize(context), "the framework initializes");
            Canvas* canvas = framework.GetCanvas();
            GameObject* cameraObject = canvas->CreateObject("camera");
            canvas->AttachComponent<Component::Transform2D>(cameraObject);
            auto* camera = canvas->AttachComponent<Component::Camera2D>(cameraObject);
            camera->primary = true;
            camera->orthographicSize = 1.0f;
            camera->clearColor = {1.0f, 1.0f, 1.0f, 1.0f};
            GameObject* labelObject = canvas->CreateObject("label");
            canvas->AttachComponent<Component::Transform2D>(labelObject);
            auto* label = canvas->AttachComponent<Component::Text2D>(labelObject);
            label->fontId = project.fontId;
            label->fontSize = 40.0f;
            label->color = {0.0f, 0.0f, 0.0f, 1.0f};
            TextStore::Get().Assign(label->text, "A", 1);
            framework.BindCanvasAssets();

            // 픽셀 값(빨강 바이트)의 정렬한 모음이다. 흰 바탕은 빼지 않는다 - 모음의 크기가 늘 같다.
            const auto values = [&]() {
                Array<std::uint8_t> sorted;
                sorted.Resize(64 * 64);
                for (std::uint32_t y = 0; y < 64; ++y)
                {
                    for (std::uint32_t x = 0; x < 64; ++x)
                    {
                        sorted[y * 64 + x] = static_cast<std::uint8_t>(std::lround(gpu.Red(x, y) * 255.0f));
                    }
                }
                std::sort(sorted.Data(), sorted.Data() + sorted.Size());
                return sorted;
            };
            const auto same = [](const Array<std::uint8_t>& left, const Array<std::uint8_t>& right) {
                return left.Size() == right.Size() && std::equal(left.Data(), left.Data() + left.Size(), right.Data());
            };

            label->alignX = Component::TextAlignX::Left;
            label->alignY = Component::TextAlignY::Baseline;
            gpu.Paint(framework);
            const Array<std::uint8_t> whole = values();
            Check(FindDark(gpu).count > 40, "the A at a whole-pixel origin draws");

            label->alignX = Component::TextAlignX::Center;
            label->alignY = Component::TextAlignY::Middle;
            gpu.Paint(framework);
            Check(false == same(values(), whole), "a centred A between pixels is resampled");

            label->pixelSnap = true;
            gpu.Paint(framework);
            Check(same(values(), whole), "with pixelSnap the centred A has the whole-pixel A's values");
            framework.Shutdown();
        }
        gpu.Close();
        project.Close();
    }

    // **리치 텍스트를 그린다**(D-221). 검은 `AA` 의 뒤 글자에 `<color=#FF0000>` 을 주면 그 글자만 빨갛다. 텍스트 전체의 알파(0.5)는 태그
    // 색에도 곱해진다 - 흰 바탕 위 빨강 절반은 (1, 0.5, 0.5) 다. `<size>` 를 준 글자는 더 크게 그려진다. 끄면 태그가 글자로 보인다.
    void TestRichTextDrawsTaggedColourAndSize()
    {
        FontProject project;
        project.Open(32.0f);
        Gpu gpu(project.platform, project.memory);
        if (false == gpu.ready)
        {
            std::cout << "  [skip] no D3D12 device; rich text drawing not verified" << std::endl;
            gpu.Close();
            project.Close();
            return;
        }
        {
            Framework2D framework;
            FrameworkContext context;
            context.memory = project.memory;
            context.assets = &project.assets;
            context.renderer = &gpu.renderer;
            Check(framework.Initialize(context), "the framework initializes");
            Canvas* canvas = framework.GetCanvas();
            GameObject* cameraObject = canvas->CreateObject("camera");
            canvas->AttachComponent<Component::Transform2D>(cameraObject);
            auto* camera = canvas->AttachComponent<Component::Camera2D>(cameraObject);
            camera->primary = true;
            camera->orthographicSize = 1.0f;
            camera->clearColor = {1.0f, 1.0f, 1.0f, 1.0f};
            GameObject* labelObject = canvas->CreateObject("label");
            canvas->AttachComponent<Component::Transform2D>(labelObject);
            auto* label = canvas->AttachComponent<Component::Text2D>(labelObject);
            label->fontId = project.fontId;
            label->fontSize = 24.0f;
            label->alignX = Component::TextAlignX::Center;
            label->alignY = Component::TextAlignY::Middle;
            label->color = {0.0f, 0.0f, 0.0f, 1.0f};
            label->richText = true;
            const char* tagged = "A<color=#FF0000>A</color>";
            TextStore::Get().Assign(label->text, tagged, std::strlen(tagged));
            framework.BindCanvasAssets();

            // 빨강(R 높고 G 낮음)과 검정(R 낮음) 픽셀을 센다.
            const auto count = [&](std::uint32_t& red, std::uint32_t& black) {
                red = 0;
                black = 0;
                for (std::uint32_t y = 0; y < 64; ++y)
                {
                    for (std::uint32_t x = 0; x < 64; ++x)
                    {
                        const float r = gpu.Red(x, y);
                        const float g = gpu.Green(x, y);
                        red += r > 0.8f && g < 0.3f ? 1u : 0u;
                        black += r < 0.3f && g < 0.3f ? 1u : 0u;
                    }
                }
            };
            gpu.Paint(framework);
            std::uint32_t red = 0;
            std::uint32_t black = 0;
            count(red, black);
            Check(red > 20 && black > 20, "the tagged A is red and the other is black");

            // 텍스트 알파가 태그 색에도 곱해진다: 순 빨강 자리가 흰 바탕 위 빨강 절반(G 0.5)이 된다.
            label->color = {0.0f, 0.0f, 0.0f, 0.5f};
            gpu.Paint(framework);
            std::uint32_t halfRed = 0;
            for (std::uint32_t y = 0; y < 64; ++y)
            {
                for (std::uint32_t x = 0; x < 64; ++x)
                {
                    const float g = gpu.Green(x, y);
                    halfRed += gpu.Red(x, y) > 0.95f && g > 0.45f && g < 0.55f ? 1u : 0u;
                }
            }
            std::uint32_t fullRed = 0;
            count(fullRed, black);
            Check(halfRed > 20 && fullRed == 0, "the text's alpha multiplies the tag colour");
            label->color = {0.0f, 0.0f, 0.0f, 1.0f};

            // 크기: 태그 없는 `AA` 와 뒤 글자만 두 배인 것.
            const char* plain = "AA";
            TextStore::Get().Assign(label->text, plain, 2);
            gpu.Paint(framework);
            const DarkBox small = FindDark(gpu);
            const char* sized = "A<size=48>A</size>";
            TextStore::Get().Assign(label->text, sized, std::strlen(sized));
            gpu.Paint(framework);
            const DarkBox large = FindDark(gpu);
            Check(large.count > small.count * 2 && large.maxY - large.minY > small.maxY - small.minY,
                "the sized A draws larger and taller");

            // 비트맵은 `<size>` 도 정수로 뜬다: 12.6 은 13 과 같은 그림이다. 글자 열이 가운데 정렬이라, 전진 폭을 12.6 으로 재면 블록 폭이
            // 달라 모든 글자가 옮겨진다.
            const char* whole = "<size=13>AAAAAAAAAA</size>";
            TextStore::Get().Assign(label->text, whole, std::strlen(whole));
            gpu.Paint(framework);
            const Array<std::byte> at13 = gpu.image;
            const char* fractional = "<size=12.6>AAAAAAAAAA</size>";
            TextStore::Get().Assign(label->text, fractional, std::strlen(fractional));
            gpu.Paint(framework);
            Check(FindDark(gpu).count > 20 && gpu.image.Size() == at13.Size()
                    && std::memcmp(gpu.image.Data(), at13.Data(), at13.Size()) == 0,
                "a bitmap font rounds a tag size to whole pixels");

            // 끄면 태그가 글자로 보이고 빨강은 없다(글자 수가 늘어 블록이 넓다). 글자는 그대로 두고 필드만 끈다.
            TextStore::Get().Assign(label->text, tagged, std::strlen(tagged));
            gpu.Paint(framework);
            count(red, black);
            Check(red > 20, "the tagged text is red again");
            label->richText = false;
            gpu.Paint(framework);
            count(red, black);
            Check(red == 0, "without richText nothing is red");
            float minX = 0.0f;
            float minY = 0.0f;
            float maxX = 0.0f;
            float maxY = 0.0f;
            auto* texts = canvas->GetSystems().FindSystem<System::Text2DSystem>();
            Check(texts != nullptr && texts->GetLocalBounds(label->GetInstanceId(), minX, minY, maxX, maxY)
                    && maxX - minX > 1.5f, "and the tags lay out as letters");
            framework.Shutdown();
        }
        gpu.Close();
        project.Close();
    }

    // **3D 텍스트**(D-222). 카메라(z=3, 세로 60 도)가 64 x 64 백버퍼를 본다. 원점의 검은 `A`(40 px, PPU 32 라 1.25 유닛)가 그려진다.
    // - 앞(z=1.5)에 상자를 두면 가려진다(월드 텍스트는 메시 뒤에 깊이를 본다). 상자를 치우면 다시 보인다.
    // - Y 로 90 도 돌리면 판이 옆을 보아 사라지고, `Billboard` 면 회전과 무관하게 카메라를 봐 다시 보인다.
    // - 반투명 파랑(가까움)과 빨강(멀리)이 겹친 자리는 **뒤→앞** 합성이다: 파랑을 먼저 붙여도 겹친 자리는 파랑이 위(B > R)다.
    void TestText3DDrawsInTheWorld()
    {
        FontProject project;
        project.Open(32.0f);
        Gpu gpu(project.platform, project.memory);
        if (false == gpu.ready)
        {
            std::cout << "  [skip] no D3D12 device; 3D text not verified" << std::endl;
            gpu.Close();
            project.Close();
            return;
        }
        {
            Framework3D framework;
            FrameworkContext context;
            context.memory = project.memory;
            context.assets = &project.assets;
            context.renderer = &gpu.renderer;
            Check(framework.Initialize(context), "the 3D framework initializes");
            Canvas* canvas = framework.GetCanvas();
            GameObject* eye = canvas->CreateObject("eye");
            canvas->AttachComponent<Component::Transform3D>(eye)->position = {0.0f, 0.0f, 3.0f};
            auto* camera = canvas->AttachComponent<Component::Camera3D>(eye);
            camera->primary = true;
            camera->clearColor = {1.0f, 1.0f, 1.0f, 1.0f};
            GameObject* labelObject = canvas->CreateObject("label");
            auto* place = canvas->AttachComponent<Component::Transform3D>(labelObject);
            auto* label = canvas->AttachComponent<Component::Text3D>(labelObject);
            label->fontId = project.fontId;
            label->fontSize = 40.0f;
            label->color = {0.0f, 0.0f, 0.0f, 1.0f};
            TextStore::Get().Assign(label->text, "A", 1);
            framework.BindCanvasAssets();
            auto* texts = canvas->GetSystems().FindSystem<System::Text3DSystem>();
            Check(texts != nullptr, "the 3D framework runs a text system");

            gpu.Paint(framework);
            const DarkBox front = FindDark(gpu);
            Check(front.count > 20, "the A draws in the world");
            Check(front.minX < 32 && front.maxX > 32 && front.minY < 32 && front.maxY > 32, "centred on the object");
            float minX = 0.0f;
            float minY = 0.0f;
            float maxX = 0.0f;
            float maxY = 0.0f;
            Check(texts->GetLocalBounds(label->GetInstanceId(), minX, minY, maxX, maxY) && maxY - minY > 1.0f,
                "the block is over a unit tall (40 px at 32 px per unit)");

            // 앞의 상자가 가린다.
            GameObject* box = canvas->CreateObject("box");
            canvas->AttachComponent<Component::Transform3D>(box)->position = {0.0f, 0.0f, 1.5f};
            auto* mesh = canvas->AttachComponent<Component::MeshRenderer3D>(box);
            mesh->meshId = MeshLibrary::BuiltinCubeId();
            mesh->tint = {1.0f, 0.0f, 0.0f, 1.0f};
            gpu.Paint(framework);
            Check(FindDark(gpu).count == 0, "a cube in front hides the A");
            canvas->DestroyObject(box);
            gpu.Paint(framework);
            Check(FindDark(gpu).count == front.count, "without it the A is back pixel for pixel");

            // 옆으로 돌리면 사라지고, 빌보드면 다시 보인다.
            place->rotation = Quaternion{0.0f, std::sin(0.7853982f), 0.0f, std::cos(0.7853982f)};
            gpu.Paint(framework);
            Check(FindDark(gpu).count < 3, "turned 90 degrees about Y the plate is edge-on");
            label->facing = Component::TextFacing3D::Billboard;
            gpu.Paint(framework);
            Check(FindDark(gpu).count == front.count, "a billboard faces the camera whatever its rotation");
            label->facing = Component::TextFacing3D::Transform;
            place->rotation = Quaternion{};

            // 왼쪽 정렬이면 글자가 오브젝트 원점의 오른쪽에 선다(글자 사각형의 자리가 월드로 간다).
            label->alignX = Component::TextAlignX::Left;
            gpu.Paint(framework);
            const DarkBox left = FindDark(gpu);
            Check(left.count > 20 && left.minX >= 31, "left aligned, the A starts at the object's origin");
            label->alignX = Component::TextAlignX::Center;

            // 리치 텍스트: 태그 색에 텍스트 알파(0.5)가 곱해져 흰 바탕 위 빨강 절반이다.
            label->richText = true;
            label->color = {0.0f, 0.0f, 0.0f, 0.5f};
            const char* tagged = "<color=#FF0000>A</color>";
            TextStore::Get().Assign(label->text, tagged, std::strlen(tagged));
            gpu.Paint(framework);
            std::uint32_t halfRed = 0;
            std::uint32_t fullRed = 0;
            for (std::uint32_t y = 0; y < 64; ++y)
            {
                for (std::uint32_t x = 0; x < 64; ++x)
                {
                    const float g = gpu.Green(x, y);
                    halfRed += gpu.Red(x, y) > 0.95f && g > 0.45f && g < 0.55f ? 1u : 0u;
                    fullRed += gpu.Red(x, y) > 0.9f && g < 0.3f ? 1u : 0u;
                }
            }
            Check(halfRed > 10 && fullRed == 0, "a 3D tag colour takes the text's alpha");
            label->richText = false;
            TextStore::Get().Assign(label->text, "A", 1);

            // 뒤→앞: 가까운 파랑을 먼저 붙인다.
            label->color = {0.0f, 0.0f, 1.0f, 0.5f};
            place->position = {0.0f, 0.0f, 0.5f};
            GameObject* farObject = canvas->CreateObject("far");
            canvas->AttachComponent<Component::Transform3D>(farObject);
            auto* farLabel = canvas->AttachComponent<Component::Text3D>(farObject);
            farLabel->fontId = project.fontId;
            farLabel->fontSize = 40.0f;
            farLabel->color = {1.0f, 0.0f, 0.0f, 0.5f};
            TextStore::Get().Assign(farLabel->text, "A", 1);
            framework.BindCanvasAssets();
            gpu.Paint(framework);
            std::uint32_t overlap = 0;
            std::uint32_t wrongOrder = 0;
            for (std::uint32_t y = 0; y < 64; ++y)
            {
                for (std::uint32_t x = 0; x < 64; ++x)
                {
                    // 둘 다 덮은 자리는 초록이 0.25 로 떨어진다(하나만이면 0.5).
                    if (gpu.Green(x, y) < 0.3f)
                    {
                        ++overlap;
                        wrongOrder += gpu.Blue(x, y) > gpu.Red(x, y) ? 0u : 1u;
                    }
                }
            }
            std::cout << "  [measure] 3D text overlap pixels: " << overlap << std::endl;
            Check(overlap > 5 && wrongOrder == 0, "where the two overlap the nearer blue lies on top");
            Check(gpu.renderer.GetLastFrameStats().worldTextCount == 2, "two glyphs went to the renderer as world text");

            // 폰트를 못 찾는 텍스트는 그리지 않고 그렇다고 말한다(인스펙터 경고가 이것을 묻는다).
            GameObject* lostObject = canvas->CreateObject("lost");
            canvas->AttachComponent<Component::Transform3D>(lostObject);
            auto* lost = canvas->AttachComponent<Component::Text3D>(lostObject);
            lost->fontId = Uuid::FromName("a font that is not in the project");
            TextStore::Get().Assign(lost->text, "A", 1);
            framework.BindCanvasAssets();
            gpu.Paint(framework);
            Check(texts->IsMissingFont(lost->GetInstanceId()) && false == texts->IsMissingFont(label->GetInstanceId()),
                "a Text3D whose font is missing is reported, the others are not");

            // 스크립트 서비스(D-223): 3D 컨텍스트를 묶으면 `Text3DService` 가 호스트의 텍스트 시스템으로 글자를 바꾸고 읽는다.
            Check(framework.BindScriptContexts() && framework.GetScriptContextBlocks().size == 2,
                "the 3D framework hands out its system and service contexts");
            const Service::Text3DService& service = GetFramework3DServices().Text3D;
            const Ref<Component::Text3D> ref = labelObject->GetScriptHandle().GetComponent<Component::Text3D>();
            Check(service.SetText(ref, "Hi"), "a script sets a 3D text");
            char copied[8] = {};
            Check(service.GetTextLength(ref) == 2 && service.CopyText(ref, copied, sizeof(copied)) == 2
                    && copied[0] == 'H' && copied[1] == 'i',
                "and reads it back through the host");
            const std::uint64_t relayouts = texts->GetRelayoutCount();
            gpu.Paint(framework);
            Check(texts->GetRelayoutCount() > relayouts, "the next frame lays the new text out");
            framework.UnbindScriptContexts();
            Check(false == service.SetText(ref, "No"), "without the contexts the service does nothing");
            framework.Shutdown();
        }
        gpu.Close();
        project.Close();
    }

    struct ColourCount
    {
        std::uint32_t red = 0;
        std::uint32_t black = 0;
        std::uint32_t touched = 0; // 흰색이 아닌 픽셀
        std::uint32_t minX = 64;
        std::uint32_t minY = 64;
        std::uint32_t maxX = 0;
        std::uint32_t maxY = 0;
    };

    ColourCount CountColours(const Gpu& gpu)
    {
        ColourCount count;
        for (std::uint32_t y = 0; y < 64; ++y)
        {
            for (std::uint32_t x = 0; x < 64; ++x)
            {
                const auto* pixel = reinterpret_cast<const unsigned char*>(
                    gpu.image.Data() + static_cast<std::size_t>(y) * gpu.readback.rowPitch + static_cast<std::size_t>(x) * 4);
                const int b = pixel[0];
                const int g = pixel[1];
                const int r = pixel[2];
                if (r > 200 && g < 70 && b < 70)
                {
                    ++count.red;
                }
                if (r < 70 && g < 70 && b < 70)
                {
                    ++count.black;
                }
                if (r < 240 || g < 240 || b < 240)
                {
                    ++count.touched;
                    count.minX = std::min(count.minX, x);
                    count.minY = std::min(count.minY, y);
                    count.maxX = std::max(count.maxX, x);
                    count.maxY = std::max(count.maxY, y);
                }
            }
        }
        return count;
    }

    // **SDF 와 외곽선**(text-plan §5 의 4 단계 완료 조건). 폰트를 `Sdf` 로 들여와 빨간 `H` 에 검은 외곽선을 준다.
    // 외곽선 폭을 퍼짐보다 크게 줘도 글자 칸이 네모로 칠해지지 않고(퍼짐까지로 잘린다), 카메라를 두 배로 빼도 외곽선과 글자 굵기의
    // 비가 같으며(폭이 글자 픽셀이다), 반투명 글자의 채우기 자리가 외곽선과 겹쳐 진해지지 않는다. 크기를 조금씩 바꿔도 새 글리프를
    // 뜨지 않는다(거리장 한 벌을 키운다).
    void TestSdfTextKeepsItsOutlineInProportion()
    {
        FontProject project;
        project.Open(32.0f);
        {
            AssetMetaFile meta;
            AssetMetaError error;
            Check(LoadAssetMetaFile(project.platform, project.metaPath.c_str(), meta, error), "the font meta reads");
            meta.hasFontOptions = true;
            meta.fontOptions.renderMode = FontRenderMode::Sdf;
            Check(SaveAssetMetaFile(project.platform, project.metaPath.c_str(), meta), "the font meta saves as Sdf");
            AssetMetaFile reread;
            Check(LoadAssetMetaFile(project.platform, project.metaPath.c_str(), reread, error)
                    && reread.fontOptions.renderMode == FontRenderMode::Sdf && reread.fontOptions.sdfSize == 48
                    && reread.fontOptions.sdfSpread == 8,
                "the render mode round-trips with the default field size and spread");
        }
        Gpu gpu(project.platform, project.memory);
        if (false == gpu.ready)
        {
            std::cout << "  [skip] no D3D12 device; sdf text not verified" << std::endl;
            gpu.Close();
            project.Close();
            return;
        }
        {
            Framework2D framework;
            FrameworkContext context;
            context.memory = project.memory;
            context.assets = &project.assets;
            context.renderer = &gpu.renderer;
            Check(framework.Initialize(context), "the framework initializes");
            Canvas* canvas = framework.GetCanvas();
            GameObject* cameraObject = canvas->CreateObject("camera");
            canvas->AttachComponent<Component::Transform2D>(cameraObject);
            auto* camera = canvas->AttachComponent<Component::Camera2D>(cameraObject);
            camera->primary = true;
            camera->orthographicSize = 1.0f;
            camera->clearColor = {1.0f, 1.0f, 1.0f, 1.0f};

            GameObject* labelObject = canvas->CreateObject("label");
            canvas->AttachComponent<Component::Transform2D>(labelObject);
            auto* label = canvas->AttachComponent<Component::Text2D>(labelObject);
            label->fontId = project.fontId;
            label->fontSize = 40.0f;
            label->alignX = Component::TextAlignX::Center;
            label->alignY = Component::TextAlignY::Middle;
            label->color = {1.0f, 0.0f, 0.0f, 1.0f};
            label->outlineColor = {0.0f, 0.0f, 0.0f, 1.0f};
            label->outlineWidth = 3.0f;
            TextStore::Get().Assign(label->text, "H", 1);
            framework.BindCanvasAssets();
            Check(project.assets.GetFont(label->font) != nullptr
                    && project.assets.GetFont(label->font)->options.filter == TextureFilter::Linear,
                "an Sdf font always samples Linear");
            auto* texts = canvas->GetSystems().FindSystem<System::Text2DSystem>();

            // 1. 외곽선이 서고, 글자는 커널이 잰 칸 가운데에 빨갛게 선다.
            gpu.Paint(framework);
            const ColourCount near = CountColours(gpu);
            std::cout << "  [measure] sdf H: red " << near.red << ", black " << near.black << ", touched " << near.touched
                      << " in " << (near.maxX - near.minX + 1) << "x" << (near.maxY - near.minY + 1) << std::endl;
            Check(near.red > 100 && near.black > 60, "a red H with a black outline");
            const ScreenRect cell = ExpectedGlyph("H", 40);
            Check(static_cast<float>(near.minX) >= cell.left - 5.0f && static_cast<float>(near.maxX) <= cell.right + 5.0f,
                "the outlined H stays around its glyph cell");

            // 2. 퍼짐보다 굵은 외곽선(100 px)은 퍼짐까지로 잘린다 - 50 px 과 같은 그림이고, 칸 전체가 칠해지지 않는다.
            label->outlineWidth = 100.0f;
            gpu.Paint(framework);
            const ColourCount huge = CountColours(gpu);
            label->outlineWidth = 50.0f;
            gpu.Paint(framework);
            const ColourCount wide = CountColours(gpu);
            const std::uint32_t box = (huge.maxX - huge.minX + 1) * (huge.maxY - huge.minY + 1);
            std::cout << "  [measure] 100 px outline: black " << huge.black << ", touched " << huge.touched << " of a "
                      << box << " px box" << std::endl;
            Check(huge.black == wide.black && huge.touched == wide.touched, "an outline past the spread is cut at the spread");
            // 외곽선은 글자를 둥글게 넓힌 모양이라 그 외접 사각형의 네 모서리는 비어 있다. 칸이 네모로 칠해지면 모서리까지 찬다.
            const auto whiteAt = [&](std::uint32_t x, std::uint32_t y) {
                const auto* pixel = reinterpret_cast<const unsigned char*>(
                    gpu.image.Data() + static_cast<std::size_t>(y) * gpu.readback.rowPitch + static_cast<std::size_t>(x) * 4);
                return pixel[0] > 240 && pixel[1] > 240 && pixel[2] > 240;
            };
            Check(whiteAt(huge.minX, huge.minY) && whiteAt(huge.maxX, huge.minY) && whiteAt(huge.minX, huge.maxY)
                    && whiteAt(huge.maxX, huge.maxY),
                "and the glyph's quad is not filled into a box - the corners of the outline's bounds stay white");
            Check(huge.black > near.black, "the cut outline is still wider than a 3 px one");
            // 자르는 자리는 퍼짐의 한 칸 안쪽이다(48 px 거리장, 퍼짐 8 이면 7 칸 = 40 px 글자에서 5.83 px). 셰이더도 문턱을 0 위로 막지만,
            // 그것만으로는 외곽선이 퍼짐 끝까지 가서 가장자리가 흐리다.
            label->outlineWidth = 7.0f * 40.0f / 48.0f;
            gpu.Paint(framework);
            const ColourCount atLimit = CountColours(gpu);
            Check(atLimit.black == huge.black && atLimit.touched == huge.touched,
                "the widest outline is exactly the one at a field pixel inside the spread");

            // 폭은 글자 픽셀이다. 96 px 글자(거리장 한 칸이 글자 2 px)에 6 px 외곽선을 주고 카메라를 두 배로 빼면 화면에서 3 px 띠다.
            // 거리장 픽셀로 셌다면 12 px 이라 화면에서 6 px 이다.
            label->fontSize = 96.0f;
            label->outlineWidth = 6.0f;
            camera->orthographicSize = 2.0f;
            gpu.Paint(framework);
            std::uint32_t band = 0;
            {
                // 가운데 줄을 왼쪽부터 훑어 첫 빨강(왼쪽 기둥)까지의 검은 픽셀을 센다.
                for (std::uint32_t x = 0; x < 64; ++x)
                {
                    const auto* pixel = reinterpret_cast<const unsigned char*>(
                        gpu.image.Data() + static_cast<std::size_t>(32) * gpu.readback.rowPitch + static_cast<std::size_t>(x) * 4);
                    const bool black = pixel[2] < 70 && pixel[1] < 70 && pixel[0] < 70;
                    const bool red = pixel[2] > 200 && pixel[1] < 70;
                    if (red)
                    {
                        break;
                    }
                    if (black)
                    {
                        ++band;
                    }
                }
            }
            std::cout << "  [measure] 6 px outline on a 96 px H seen at half size: " << band << " screen px" << std::endl;
            Check(band >= 2 && band <= 4, "the outline width counts glyph pixels, not distance-field pixels");
            label->fontSize = 40.0f;
            camera->orthographicSize = 1.0f;

            // 3. 카메라를 두 배로 빼면 글자가 절반이 되고, 외곽선과 채우기의 비는 그대로다.
            label->outlineWidth = 3.0f;
            camera->orthographicSize = 2.0f;
            gpu.Paint(framework);
            const ColourCount far = CountColours(gpu);
            const float nearRatio = static_cast<float>(near.black) / static_cast<float>(near.red);
            const float farRatio = static_cast<float>(far.black) / static_cast<float>(far.red);
            std::cout << "  [measure] outline / fill: near " << nearRatio << ", two times further " << farRatio << std::endl;
            Check(far.red < near.red / 2 && far.red > near.red / 8, "the H is drawn smaller from further away");
            Check(std::fabs(farRatio - nearRatio) < nearRatio * 0.3f, "and its outline keeps the same share of it");
            camera->orthographicSize = 1.0f;

            // 4. 반투명이면 채우기 자리는 흰 바탕 위의 빨강 절반이다. 외곽선이 그 밑에 한 번 더 깔리면 초록·파랑이 반보다 어둡다.
            gpu.Paint(framework);
            Array<std::uint32_t> filled;
            for (std::uint32_t y = 0; y < 64; ++y)
            {
                for (std::uint32_t x = 0; x < 64; ++x)
                {
                    const auto* pixel = reinterpret_cast<const unsigned char*>(
                        gpu.image.Data() + static_cast<std::size_t>(y) * gpu.readback.rowPitch + static_cast<std::size_t>(x) * 4);
                    if (pixel[2] > 250 && pixel[1] < 5 && pixel[0] < 5)
                    {
                        filled.Add(y * 64 + x);
                    }
                }
            }
            Check(filled.Size() > 20, "the opaque H has solid red pixels");
            label->color = {1.0f, 0.0f, 0.0f, 0.5f};
            label->outlineColor = {0.0f, 0.0f, 0.0f, 0.5f};
            gpu.Paint(framework);
            std::uint32_t darker = 0;
            for (const std::uint32_t at : filled)
            {
                const auto* pixel = reinterpret_cast<const unsigned char*>(
                    gpu.image.Data() + static_cast<std::size_t>(at / 64) * gpu.readback.rowPitch + static_cast<std::size_t>(at % 64) * 4);
                if (pixel[1] < 115 || pixel[2] < 245)
                {
                    ++darker;
                }
            }
            Check(darker == 0, "a translucent fill is half red over white everywhere, never darkened by its own outline");
            label->color = {1.0f, 0.0f, 0.0f, 1.0f};
            label->outlineColor = {0.0f, 0.0f, 0.0f, 1.0f};

            // 5. 크기를 조금 바꾸면 다시 레이아웃하지만 새 글리프는 없다 - 올릴 것도 없다.
            gpu.Paint(framework);
            const std::uint64_t relayouts = texts->GetRelayoutCount();
            const std::uint64_t uploads = texts->GetLibrary().GetUploadCount();
            float minX = 0.0f;
            float minY = 0.0f;
            float maxX = 0.0f;
            float maxY = 0.0f;
            Check(texts->GetLocalBounds(label->GetInstanceId(), minX, minY, maxX, maxY), "the H has a block");
            const float width40 = maxX - minX;
            // 반올림한 픽셀이 같아도(40.2 → 40) 다시 레이아웃한다.
            label->fontSize = 40.2f;
            gpu.Paint(framework);
            Check(texts->GetRelayoutCount() == relayouts + 1, "a fraction of a pixel is a new size for an SDF text");
            label->fontSize = 40.7f;
            gpu.Paint(framework);
            label->fontSize = 57.3f;
            gpu.Paint(framework);
            // 소수 크기 그대로 레이아웃한다. 반올림한 57 이면 폭의 비가 57/40 이라 0.5 % 어긋난다.
            Check(texts->GetLocalBounds(label->GetInstanceId(), minX, minY, maxX, maxY)
                    && std::fabs((maxX - minX) / width40 - 57.3f / 40.0f) < 0.001f,
                "an SDF text is laid out at its exact size, not rounded to a pixel");
            Check(texts->GetRelayoutCount() == relayouts + 3, "each new size lays the text out again");
            Check(texts->GetLibrary().GetUploadCount() == uploads, "but a new size is not a new glyph, so nothing uploads");
            Check(CountColours(gpu).red > near.red, "and the bigger H is drawn bigger");

            framework.Shutdown();
        }
        gpu.Close();
        project.Close();
    }

    void TestProjectFontsDrawEmptyFontIdsAndFillInMissingLetters()
    {
        FontProject project;
        project.Open(32.0f);
        Gpu gpu(project.platform, project.memory);
        if (false == gpu.ready)
        {
            std::cout << "  [skip] no D3D12 device; project fonts not verified" << std::endl;
            gpu.Close();
            project.Close();
            return;
        }
        {
            Framework2D framework;
            FrameworkContext context;
            context.memory = project.memory;
            context.assets = &project.assets;
            context.renderer = &gpu.renderer;
            Check(framework.Initialize(context), "the framework initializes");
            Canvas* canvas = framework.GetCanvas();
            GameObject* cameraObject = canvas->CreateObject("camera");
            canvas->AttachComponent<Component::Transform2D>(cameraObject);
            auto* camera = canvas->AttachComponent<Component::Camera2D>(cameraObject);
            camera->primary = true;
            camera->orthographicSize = 1.0f;
            camera->clearColor = {1.0f, 1.0f, 1.0f, 1.0f};

            GameObject* labelObject = canvas->CreateObject("label");
            canvas->AttachComponent<Component::Transform2D>(labelObject);
            auto* label = canvas->AttachComponent<Component::Text2D>(labelObject);
            label->fontSize = 40.0f;
            label->alignX = Component::TextAlignX::Center;
            label->alignY = Component::TextAlignY::Middle;
            label->color = {0.0f, 0.0f, 0.0f, 1.0f};
            TextStore::Get().Assign(label->text, "A", 1);
            framework.BindCanvasAssets();
            auto* texts = canvas->GetSystems().FindSystem<System::Text2DSystem>();
            Check(texts != nullptr, "the framework runs a text system");

            // 1. 폰트 아이디도 프로젝트 폰트도 없으면 그리지 않는다.
            gpu.Paint(framework);
            Check(texts->IsMissingFont(label->GetInstanceId()), "a text with no font and no project font is missing one");
            Check(FindDark(gpu).count == 0, "and nothing is drawn");

            // 2. 프로젝트 폰트를 주면 그 첫 폰트로 그린다. 커널로 잰 A 의 칸 안이다.
            const AssetId sansOnly[] = { project.fontId };
            project.assets.SetProjectFonts(sansOnly);
            gpu.Paint(framework);
            Check(false == texts->IsMissingFont(label->GetInstanceId()), "the project's first font draws a text with no fontId");
            const ScreenRect expectedA = ExpectedGlyph("A", 40);
            const DarkBox a = FindDark(gpu);
            Check(a.count > 40 && static_cast<float>(a.minX) >= expectedA.left - 1.0f
                    && static_cast<float>(a.maxX) <= expectedA.right + 1.0f
                    && static_cast<float>(a.minY) >= expectedA.top - 1.0f
                    && static_cast<float>(a.maxY) <= expectedA.bottom + 1.0f,
                "the A lands in its cell");
            const std::uint64_t relayouts = texts->GetRelayoutCount();
            project.assets.SetProjectFonts(sansOnly);
            gpu.Paint(framework);
            Check(texts->GetRelayoutCount() == relayouts, "setting the same list again lays nothing out");

            // 3. 한글이 없는 라틴 폰트를 고르면 `한` 은 프로젝트 폰트(폴백)에서 온다 - 한글 폰트로 잰 칸 안에 그려진다.
            label->fontId = project.latinId;
            TextStore::Get().Assign(label->text, "\xED\x95\x9C", 3);
            framework.BindCanvasAssets();
            Check(label->font.generation != 0, "the latin font resolves");
            gpu.Paint(framework);
            const ScreenRect expectedHan = ExpectedGlyph("\xED\x95\x9C", 40);
            const DarkBox han = FindDark(gpu);
            Check(han.count > 60 && static_cast<float>(han.minX) >= expectedHan.left - 1.0f
                    && static_cast<float>(han.maxX) <= expectedHan.right + 1.0f
                    && static_cast<float>(han.minY) >= expectedHan.top - 1.0f
                    && static_cast<float>(han.maxY) <= expectedHan.bottom + 1.0f,
                "the missing letter is filled in from the project font, inside its cell");

            // 4. 목록을 비우면 다음 프레임에 다시 레이아웃되고, `한` 은 라틴 폰트의 .notdef(네모) 로 바뀐다.
            project.assets.SetProjectFonts({});
            gpu.Paint(framework);
            Check(false == texts->IsMissingFont(label->GetInstanceId()), "the latin font still draws");
            const DarkBox notdef = FindDark(gpu);
            std::cout << "  [measure] fallback han " << han.count << " px, latin notdef " << notdef.count << " px" << std::endl;
            Check(notdef.count > 0 && notdef.count != han.count, "without the fallback the letter becomes the notdef box");
            // 끄기 전에 목록을 되돌려 둔다. 텍스트 시스템이 든 프로젝트 폰트를 끌 때 놓는지 아래에서 잰다.
            project.assets.SetProjectFonts(sansOnly);
            gpu.Paint(framework);
            Check(project.assets.GetReferenceCount(project.assets.Find(project.fontId)) > 0,
                "the text system holds the project font while it runs");

            framework.Shutdown();
        }
        Check(gpu.renderer.GetTextureCount() == 0, "shutting the framework down returns every atlas page");
        Check(project.assets.GetReferenceCount(project.assets.Find(project.fontId)) == 0,
            "and the text system let go of the project fonts it loaded");
        gpu.Close();
        project.Close();
    }
}

int RunTextRenderTests()
{
    try
    {
        TestTheStoreHandsOutSlots();
        TestTheCodecSurvivesTheCanvasFile();
        TestCopiesGetTheirOwnSlot();
        TestFontAssetsLoadAndReload();
        TestTextDrawsCachesAndUploadsOnlyNewGlyphs();
        TestProjectFontsDrawEmptyFontIdsAndFillInMissingLetters();
        TestSdfTextKeepsItsOutlineInProportion();
        TestPrewarmedFontsUploadOnce();
        TestPrewarmRunsOnWorkers();
        TestPixelSnapLandsGlyphsOnWholePixels();
        TestRichTextDrawsTaggedColourAndSize();
        TestText3DDrawsInTheWorld();
    }
    catch (const std::exception&)
    {
        return 1;
    }
    std::cout << "text render tests passed\n";
    return 0;
}

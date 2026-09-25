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
#include <JBro/Graphics/Renderer.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/TextStore.h>
#include <JBro/Text/GlyphAtlas.h>
#include <JBro/Text/TextLayout.h>

#include "TestFontNotoSansKR.generated.h"

#include <cmath>
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

        // 스냅숏 길(파일을 거치지 않고 글자로 떴다 다시 쓰기)도 같은 글자를 준다.
        const ValueCodec& codec = GetTextIdCodec();
        char buffer[512];
        std::size_t required = 0;
        Check(codec.ToText(&text->text, buffer, sizeof(buffer), required), "the codec writes the text");
        TextId snapshot;
        Check(codec.FromText(&snapshot, buffer, required - 1) && TextIs(snapshot, sample), "the snapshot path round-trips");
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
        String metaPath;

        void Open(float pixelsPerUnit)
        {
            root = fs::temp_directory_path() / L"JBroTextProbe·글자";
            fs::remove_all(root);
            WriteBytes(root / "Fonts" / "sans.otf", TestFontNotoSansKR, sizeof(TestFontNotoSansKR));
            Check(platform.Initialize(memory), "the platform initializes");
            AssetScanOptions options;
            options.createMissingMeta = true;
            AssetScanReport report;
            Check(registry.Scan(platform, Utf8(root).c_str(), options, report), "the font folder scans");
            const AssetRecord* record = registry.FindByPath("Fonts/sans.otf");
            Check(record != nullptr && record->type == AssetType::Font, "an .otf registers as a Font");
            fontId = record->id;
            metaPath = Utf8(root / "Fonts" / "sans.otf.jmeta");
            WriteOptions(pixelsPerUnit, TextureFilter::Default);
            Check(assets.Initialize(memory), "the asset system initializes");
            assets.Bind(platform, registry, Utf8(root).c_str());
        }

        void WriteOptions(float pixelsPerUnit, TextureFilter filter)
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

        void Paint(Framework2D& framework)
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
            const DarkBox vee = FindDark(gpu);
            Check(vee.count > 20 && (vee.minX != dark.minX || vee.minY != dark.minY || vee.count != dark.count), "the V replaces the A");
            Check(service.SetText(ref, "A"), "back to A");
            gpu.Paint(framework);
            Check(texts->GetLibrary().GetUploadCount() == uploads + 1, "a glyph already in the atlas uploads nothing");
            Check(FindDark(gpu).count == dark.count, "and the A is back pixel for pixel");
            Check(gpu.renderer.GetTextureCount() == registered, "no texture was registered for any of it");

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
            label->boxSize = {0.0f, 0.0f};
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
    }
    catch (const std::exception&)
    {
        return 1;
    }
    std::cout << "text render tests passed\n";
    return 0;
}

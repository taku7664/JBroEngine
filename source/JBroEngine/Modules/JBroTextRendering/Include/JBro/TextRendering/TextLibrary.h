#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Text/FontFace.h>
#include <JBro/Text/GlyphAtlas.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/SafePtr.h>

#include <cstdint>

namespace JBro
{
    class AssetSystem;
    class Renderer;
    class TaskManager;

    // 폰트 에셋과 글리프 아틀라스, 그리고 아틀라스 페이지의 GPU 텍스처를 잇는다(D-200, text-plan §4.4). `SpriteLibrary` 와 같은 자리다.
    //
    // 폰트 에셋 하나에 face 하나(에셋 바이트의 **자기 사본**, `FontFace::Load`)와 아틀라스 하나다. 표는 에셋 핸들의 슬롯 번호로
    // 곧장 찍고 세대로 검증한다. 에셋이 in-place 재로드되면(`dataGeneration`) face 를 다시 열고 아틀라스와 페이지 텍스처를 버린다 -
    // 옛 글리프는 옛 바이트의 것이다.
    //
    // **렌더러 프레임 밖에서만 부른다.** 텍스트 시스템의 Update 가 그 자리다(`RegisterTexture`·`UpdateTexture` 가 프레임 안에서는 거절한다).
    struct FontView
    {
        const Text::FontFace* face = nullptr;
        Text::GlyphAtlas*     atlas = nullptr;
        float                 pixelsPerUnit = DefaultPixelsPerUnit;
        TextureFilter         filter = TextureFilter::Nearest; // 이미 정해진 값이다. `Default` 는 오지 않는다
        std::uint32_t         dataGeneration = 0;
        // 폰트 에셋의 임포트 옵션이다(4 단계). `Sdf` 면 `sdfSize` 한 크기로 거리장을 뜬다.
        FontRenderMode        renderMode = FontRenderMode::Bitmap;
        std::uint32_t         sdfSize = 48;
        std::uint32_t         sdfSpread = 8;
        // 아틀라스를 비울 때마다 오른다(퇴출, `TrimAtlases`). 이 값이 바뀐 폰트의 텍스트는 다시 레이아웃한다 - 옛 칸이 없다.
        std::uint32_t         atlasGeneration = 0;
    };

    class TextLibrary final
    {
    public:
        // tasks 가 있으면 미리 뜨기를 워커에 맡긴다(text-plan §5 의 뒤의 것 - 비동기 래스터화). 뜬 칸은 태스크가 끝나는 프레임에 메인
        // 스레드에서 아틀라스에 들어간다. 그사이의 텍스트는 제 글자를 그 자리에서 뜬다(같은 칸이 먼저 서면 태스크 쪽을 버린다).
        void Initialize(AssetSystem* assets, Renderer* renderer, TaskManager* tasks = nullptr);
        void Shutdown();

        // 폰트 에셋을 연다. 처음이거나 재로드됐으면 face 를 새로 열고 아틀라스를 비운다. 로드돼 있지 않거나 바이트가 폰트가
        // 아니면 거짓이다(같은 데이터 세대에는 다시 열어 보지 않는다). 받은 포인터는 이 라이브러리가 살아 있는 동안, 그리고
        // 그 폰트가 다시 열리기 전까지 유효하다.
        bool Acquire(AssetHandle font, FontView& view);
        // `font` 가 폰트 패밀리(D-224)면 네 칸의 Font 핸들(Regular·Bold·Italic·BoldItalic 순, 빈 칸은 빈 핸들)을 주고 참이다.
        // 패밀리가 아니면 거짓이다. 칸의 폰트는 패밀리가 잡고 있으므로 따로 놓지 않는다.
        bool GetFamilyFonts(AssetHandle font, AssetHandle (&slots)[4]) const;

        // 프로젝트 폰트 목록(`AssetSystem::GetProjectFonts`, D-200 (6))을 따라간다. 목록의 판번호가 바뀌었으면 옛 핸들을
        // 놓고 새 목록을 로드한다. 매 프레임 불러도 판번호가 같으면 아무것도 하지 않는다.
        void SyncProjectFonts();
        // 로드한 프로젝트 폰트다. 순서는 목록 그대로이고, 로드하지 못한 것은 빠진다.
        ArrayView<const AssetHandle> GetProjectFonts() const;

        // **글리프 퇴출**(text-plan §7). 폰트 하나의 아틀라스가 한도(페이지 수)를 넘으면 통째로 비운다 - 칸은 옮기지 않으므로 오래 안 쓴
        // 칸만 골라 뺄 수 없고, 비운 뒤 이번 프레임에 보이는 텍스트가 필요한 글자만 다시 뜬다. 비운 지 `ThrashFrames` 안에 또 넘치면
        // 보이는 글자만으로도 한도를 넘는 것이므로 비우지 않고 그 폰트의 한도를 두 배로 올린다(경고). 프레임 처음, 레이아웃 전에 부른다.
        void TrimAtlases(std::uint64_t frame);
        void SetPageLimit(std::uint32_t pages);
        std::uint32_t GetTrimCount() const;
        static constexpr std::uint32_t DefaultPageLimit = 8;
        static constexpr std::uint64_t ThrashFrames = 60;

        // 더러운 아틀라스 페이지를 올린다. 새 페이지는 등록하고 있던 페이지는 같은 핸들에 다시 쓴다. 올린 페이지 수다.
        std::uint32_t UploadDirtyPages();

        // 폰트의 page 번째 페이지 텍스처다. 아직 올리지 않았으면 빈 핸들이다.
        AssetHandle GetPageTexture(AssetHandle font, std::uint32_t page) const;

        // 지금까지 올린 페이지 수(등록과 다시 쓰기를 모두 센다)와 살아 있는 페이지 텍스처 수다. 테스트가 "새 글자가 없는 프레임에는
        // 올리지 않는다" 를 이것으로 잰다 - 렌더러에는 올린 횟수를 세는 자리가 없다.
        std::uint64_t GetUploadCount() const;
        // 지금까지 GPU 로 보낸 아틀라스 바이트다. 새 칸만 올리는지 테스트가 이것으로 잰다.
        std::uint64_t GetUploadedBytes() const;
        // 이 폰트를 (다시) 열 때 미리 뜬 칸 수다. 연 적이 없으면 0 이다. 워커에서 뜨는 중이면 지금까지 들어간 수다.
        std::uint32_t GetPrewarmedGlyphCount(AssetHandle font) const;
        // 워커에서 미리 뜨는 태스크가 남아 있는가.
        bool IsPrewarming(AssetHandle font) const;
        // 미리 뜨는 태스크 하나가 끝났다(메인 스레드, 태스크의 `OnFinished`). stamp 가 지금 폰트와 다르면(재로드·퇴출) 버린다.
        struct PrewarmResult
        {
            std::uint32_t            atlasGeneration = 0;
            std::uint32_t            dataGeneration = 0;
            std::uint32_t            pixelSize = 0;
            std::uint32_t            sdfSpread = 0;
            Array<Text::GlyphIndex>  glyphs;
            Array<Text::GlyphBitmapBox> boxes;
            Array<std::uint32_t>     offsets;
            Array<std::uint8_t>      pixels;
        };
        void FinishPrewarm(std::uint32_t slot, const PrewarmResult& result, bool completed);
        std::uint32_t GetPageTextureCount() const;

    private:
        struct FontEntry
        {
            AssetHandle           asset;
            std::uint32_t         dataGeneration = 0;
            std::uint32_t         failedGeneration = 0;
            Text::FontFace        face;
            Text::GlyphAtlas      atlas;
            Array<AssetHandle>    pageTextures;
            float                 pixelsPerUnit = DefaultPixelsPerUnit;
            TextureFilter         filter = TextureFilter::Nearest;
            FontRenderMode        renderMode = FontRenderMode::Bitmap;
            std::uint32_t         sdfSize = 48;
            std::uint32_t         sdfSpread = 8;
            // 폰트를 열 때 미리 뜬 칸 수다. 테스트와 로그가 쓴다.
            std::uint32_t         prewarmed = 0;
            std::uint32_t         atlasGeneration = 0;
            std::uint32_t         pageLimit = 0;     // 0 이면 라이브러리 한도다. 되풀이해 넘치면 두 배씩 오른다
            // 미리 채우기가 끝났을 때의 페이지 수다. 한도는 이 위에 더해 센다 - 기본 SDF(48, 퍼짐 8)의 KS X 1001 벌은 그것만으로
            // 9 페이지라, 한도 8 을 그대로 쓰면 첫 프레임에 비우고 다시 뜬 뒤 한도를 올렸다(원본 Noto Sans KR 실측).
            std::uint32_t         prewarmPages = 0;
            std::uint64_t         lastTrimFrame = 0; // 0 이면 비운 적이 없다
            std::uint32_t         prewarmTasksPending = 0; // 워커에서 도는 미리 뜨기 태스크 수
            FontPrewarm           prewarm = FontPrewarm::None;
            std::uint32_t         prewarmSize = 32;
        };

        void ReleasePages(FontEntry& entry);

        void ReleaseProjectFonts();
        void Prewarm(FontEntry& entry, std::uint32_t slot);
        // 이 폰트의 미리 뜨기 태스크가 모두 끝날 때까지 기다린다. face 를 다시 열거나 부수기 전에 부른다.
        void WaitForPrewarm(FontEntry& entry);

        AssetSystem* m_assets = nullptr;
        Renderer*    m_renderer = nullptr;
        TaskManager* m_tasks = nullptr;
        // 이 라이브러리가 로드해 든 프로젝트 폰트 핸들이다. 놓는 것도 이 라이브러리다.
        Array<AssetHandle> m_projectFonts;
        bool          m_projectFontsSynced = false;
        std::uint32_t m_projectFontsRevision = 0;
        // 폰트 에셋의 슬롯 번호로 찍는다. 원소가 옮겨 다니지 않게 따로 잡는다(FontView 가 face·atlas 를 가리킨다).
        Array<OwnerPtr<FontEntry>> m_fonts;
        std::uint64_t m_uploadCount = 0;
        std::uint64_t m_uploadedBytes = 0;
        std::uint32_t m_pageLimit = DefaultPageLimit;
        std::uint32_t m_trimCount = 0;
    };
}

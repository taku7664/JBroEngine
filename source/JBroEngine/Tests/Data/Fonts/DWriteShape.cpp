// DirectWrite 로 옛한글 자모를 모양 잡아 글리프 번호와 전진 폭을 찍는다(엔진 GSUB 읽기의 기대값).
#include <dwrite.h>
#include <cstdio>
#include <cwchar>
#include <JBro/Types/Int.h>
#pragma comment(lib, "dwrite.lib")

struct Source final : IDWriteTextAnalysisSource
{
    const wchar_t* text; UINT32 length;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 p, const WCHAR** t, UINT32* n) override
    { if (p >= length) { *t = nullptr; *n = 0; } else { *t = text + p; *n = length - p; } return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 p, const WCHAR** t, UINT32* n) override
    { if (p == 0 || p > length) { *t = nullptr; *n = 0; } else { *t = text; *n = p; } return S_OK; }
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override { return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT; }
    HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32, UINT32* n, const WCHAR** l) override { *n = length; *l = L"ko-KR"; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetNumberSubstitution(UINT32, UINT32* n, IDWriteNumberSubstitution** s) override { *n = length; *s = nullptr; return S_OK; }
};

struct Sink final : IDWriteTextAnalysisSink
{
    DWRITE_SCRIPT_ANALYSIS script{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE SetScriptAnalysis(UINT32, UINT32, const DWRITE_SCRIPT_ANALYSIS* a) override { script = *a; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetLineBreakpoints(UINT32, UINT32, const DWRITE_LINE_BREAKPOINT*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetBidiLevel(UINT32, UINT32, UINT8, UINT8) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetNumberSubstitution(UINT32, UINT32, IDWriteNumberSubstitution*) override { return S_OK; }
};

int wmain(int argc, wchar_t** argv)
{
    IDWriteFactory* factory = nullptr;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&factory));
    IDWriteFontFile* file = nullptr;
    factory->CreateFontFileReference(argv[1], nullptr, &file);
    IDWriteFontFace* face = nullptr;
    factory->CreateFontFace(DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &file, 0, DWRITE_FONT_SIMULATIONS_NONE, &face);
    IDWriteTextAnalyzer* analyzer = nullptr;
    factory->CreateTextAnalyzer(&analyzer);
    for (JBro::Int32 a = 2; a < argc; ++a)
    {
        // 인자는 16 진 코드포인트를 쉼표로 이은 것이다.
        wchar_t text[64] = {};
        UINT32 length = 0;
        for (wchar_t* token = wcstok(argv[a], L",", nullptr); token != nullptr; token = wcstok(nullptr, L",", nullptr))
        {
            text[length++] = static_cast<wchar_t>(wcstoul(token, nullptr, 16));
        }
        Source source; source.text = text; source.length = length;
        Sink sink;
        analyzer->AnalyzeScript(&source, 0, length, &sink);
        UINT16 clusters[64]; DWRITE_SHAPING_TEXT_PROPERTIES textProps[64];
        UINT16 glyphs[64]; DWRITE_SHAPING_GLYPH_PROPERTIES glyphProps[64]; UINT32 count = 0;
        HRESULT hr = analyzer->GetGlyphs(text, length, face, FALSE, FALSE, &sink.script, L"ko-KR", nullptr, nullptr, nullptr, 0, 64,
            clusters, textProps, glyphs, glyphProps, &count);
        FLOAT advances[64]; DWRITE_GLYPH_OFFSET offsets[64];
        analyzer->GetGlyphPlacements(text, clusters, textProps, length, glyphs, glyphProps, count, face, 2048.0f, FALSE, FALSE,
            &sink.script, L"ko-KR", nullptr, nullptr, 0, advances, offsets);
        std::wprintf(L"%ls hr=%08lx :", argv[a], static_cast<unsigned long>(hr));
        for (UINT32 i = 0; i < count; ++i)
        {
            std::wprintf(L" %u/%g", glyphs[i], advances[i]);
            if (offsets[i].advanceOffset != 0.0f || offsets[i].ascenderOffset != 0.0f)
            {
                std::wprintf(L"(%g,%g)", offsets[i].advanceOffset, offsets[i].ascenderOffset);
            }
        }
        std::wprintf(L"\n");
    }
    return 0;
}

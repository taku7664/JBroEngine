#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Types/String.h>
#include <JBro/Types/Uuid.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// 에셋을 가리키는 값 타입만 둔다. 로드·캐시를 소유하는 `AssetSystem` 은 엔진 계층(JBroAsset)이며
// 스크립트 표면에 나타나지 않는다(D-50). 컴포넌트 공개 필드가 참조하므로 이 헤더는 프렐류드를 탄다.

namespace JBro
{
    // 영속 식별자. 직렬화되는 값이다. **따로 만든 타입이 아니라 `Uuid` 그대로다**(D-111) - 아이디 역할에
    // 이 타입이 더 가질 것이 없고, 별개 타입으로 감싸면 변환과 코덱이 한 벌 더 생긴다. 임포트 때
    // `Uuid::Generate`, 빌트인은 `Uuid::FromName` 이다.
    using AssetId = Uuid;

    // 이번 실행에서의 위치. 저장하지 않는다.
    struct AssetHandle
    {
        UInt32 index = 0;
        UInt32 generation = 0;
    };

    // 에셋의 종류다. 파일에는 이름으로 적히므로(`AssetTypeRules`) 순서를 바꿔도 파일이 깨지지 않는다.
    // 이미지 파일 하나는 `Texture` 와 `Sprite` 둘로 등록된다(D-111) - Sprite 가 슬라이싱·피벗·PPU 를 갖고
    // Texture 를 가리키며, 3D 재질은 Texture 를 참조한다.
    enum class AssetType : std::uint8_t
    {
        Unknown,
        Texture,
        Sprite,
        Mesh,
        Material,
        Shader,
        Canvas,
        Prefab,
        Audio,
        Font,
        // 굵게·기울임 face 를 묶는 폰트 패밀리다(D-225). 본문(`.jfontfamily`)은 표지뿐이고 네 칸은 `.jmeta` 의 `FontFamily` 블록에 있다.
        FontFamily,
        // 게임 문자열 표다(D-226). 파일 하나가 로케일 하나이고(`키: 값` YAML), 로케일은 `.jmeta` 의 `StringTable` 블록에 있다.
        StringTable,
        // 레이어 에셋이다(D-287). 레이어 하나와 그 위의 오브젝트를 캔버스 파일과 같은 모양으로 적은 `.jlayer` 다. 에디터가 계층에 끌어 넣는다.
        Layer,
        // 소리 이펙트 하나다(D-306, 기존 `.jfx`). 본문은 `Effect: { Kind, Parameters }` YAML 이고 `AudioSource.audioEffectIds` 가 차례로 건다.
        AudioEffect
    };

    // 스프라이트 시트를 어떻게 자르는가다. 기존 엔진의 모델을 그대로 잇는다(asset-plan §2.7). `Automatic`(알파 기반
    // 자동 분할)은 기존 엔진도 예약만 하고 구현하지 않았으므로 두지 않는다.
    enum class SpriteSliceType : std::uint8_t
    {
        // 이미지 전체가 프레임 하나다.
        None,
        // 셀의 픽셀 크기로 균일하게 자른다.
        CellSize,
        // 행·열 개수로 균일하게 자른다.
        CellCount
    };

    // 스프라이트 PPU 의 기본이자, 0 이하·비유한으로 적힌 값을 대신하는 값이다(D-117·D-119). 한 곳에만 있다.
    inline constexpr Float DefaultPixelsPerUnit = 100.0f;

    // `.jmeta` 의 `Sprite.ImportOptions` 블록이다. 리플렉션으로 읽고 쓴다.
    struct SpriteImportOptions
    {
        SpriteSliceType sliceType = SpriteSliceType::None;
        // CellCount 일 때
        UInt32 rowCount = 1;
        UInt32 columnCount = 1;
        // CellSize 일 때(픽셀)
        UInt32 cellWidth = 32;
        UInt32 cellHeight = 32;
        // 공용 - 바깥 여백과 셀 사이 간격(픽셀)
        UInt32 marginX = 0;
        UInt32 marginY = 0;
        UInt32 gapX = 0;
        UInt32 gapY = 0;
        // 프레임 안의 피벗(0..1)
        Float pivotX = 0.5f;
        Float pivotY = 0.5f;
        // 한 유닛에 드는 픽셀 수다(D-117). 프로젝트 기본값은 없다 - 에셋이 전부 말한다. 0 이하나 비유한으로 적힌 값은
        // 로드 때 `DefaultPixelsPerUnit` 으로 바로잡는다.
        Float pixelsPerUnit = DefaultPixelsPerUnit;
    };

    // 텍스처를 어떻게 샘플링하는가(D-117). `Nearest` 는 텍셀 그대로(픽셀 아트), `Linear` 는 이웃과 섞는다.
    // `Default` 는 프로젝트의 `TextureFilter` 를 따른다 - 텍스처의 임포트 옵션에서만 뜻이 있다.
    enum class TextureFilter : std::uint8_t
    {
        Default,
        Nearest,
        Linear
    };

    // 텍스처 임포트 옵션이다. `.jmeta` 의 `Texture.ImportOptions` 로 저장된다(D-117).
    struct TextureImportOptions
    {
        TextureFilter filter = TextureFilter::Default;
    };

    // 오디오 에셋의 디코드 방식이다(`.jmeta` 의 `Audio.ImportOptions.mode`, D-197). 파일의 속성이라 에셋 몫이다(D-198).
    enum class AudioImportMode : std::uint8_t
    {
        // 로드 때 전부 PCM 으로 푼다. 짧은 효과음.
        Decompressed,
        // 압축된 바이트를 메모리에 두고 재생하며 푼다. 긴 배경음.
        Streaming,
        // 파일을 메모리에 올리지 않고 디스크에서 흘려 읽는다(D-203). 몇 분짜리 배경음·음성처럼 메모리에 두기 아까운 것.
        // 동시에 흘려 읽는 수가 정해져 있고(기본 8) 웹 빌드에서는 되지 않는다 - 그때는 `Streaming` 으로 둔다.
        StreamFromDisk
    };

    // `.jmeta` 의 `Audio.ImportOptions` 블록이다. **소리 하나의 재생 파라미터는 없다** - 볼륨·루프·거리·버스는 컴포넌트가
    // 유일한 원천이다(D-197·D-198). 파일의 속성과, 같은 소리가 여럿 울릴 때의 규칙(여러 컴포넌트가 한 클립을 나눠 쓰므로
    // 컴포넌트는 이것을 말할 수 없다, D-231)이 여기에 온다.
    struct AudioImportOptions
    {
        AudioImportMode mode = AudioImportMode::Decompressed;
        // 파일의 크기 보정(트림, 0..4, D-205)이다. 녹음마다 다른 크기를 여기서 한 번 맞추면 컴포넌트의 `volume` 은 연출에만 쓴다.
        Float gain = 1.0f;
        // 모노로 줄인다(채널 평균, D-231). 공간화하는 소리는 채널이 하나면 된다 - 메모리와 믹스가 채널 수만큼 준다.
        Bool mono = false;
        // 이 소리가 동시에 울릴 수 있는 수다(0 이면 제한 없음, D-231). 넘치면 가장 오래된 것을 짧게 줄여 끄고 새것을 튼다.
        Int32 maxInstances = 0;
        // 이 소리를 다시 틀 수 있기까지의 초다(0 이면 끔, D-231). 그 안에 온 재생은 버린다 - 한 프레임에 몰린 발소리·피격음.
        Float cooldown = 0.0f;
    };

    // `.jmeta` 의 `Font.ImportOptions` 블록이다(D-200, text-plan §4.1). 글자 크기는 컴포넌트의 몫이고(`Text2D::fontSize`,
    // 글자 픽셀), 여기에는 그 픽셀을 유닛으로 옮기는 비율과 아틀라스를 읽는 샘플러만 있다. 텍스처와 같은 규칙이다(D-119):
    // `pixelsPerUnit` 이 0 이하면 기본 100, `filter` 가 `Default` 면 프로젝트의 `TextureFilter`.
    // 렌더 모드(`Bitmap`·`Sdf`)는 SDF 가 서는 4 단계에서 온다 - 쓰이지 않는 옵션을 먼저 두지 않는다.
    // 글자를 어떻게 뜨는가(text-plan §3.5, D-200 (4)). `Bitmap` 은 글자 크기마다 커버리지를 뜨고 스프라이트 셰이더로 그린다 - 픽셀 폰트와
    // Nearest 의 기본이다. `Sdf` 는 `sdfSize` 한 크기로 거리장을 떠 어느 크기로도 키우고, 외곽선을 그릴 수 있다. 늘 Linear 로 샘플링한다.
    enum class FontRenderMode : std::uint8_t
    {
        Bitmap,
        Sdf
    };

    // 폰트를 열 때 미리 뜰 글자다(text-plan §3.6). `Ksx1001` 은 ASCII 와 완성형 한글 2,350 자다.
    enum class FontPrewarm : std::uint8_t
    {
        None,
        Ascii,
        Ksx1001
    };

    // ── 소리 이펙트 (D-306) ─────────────────────────────────────────────────────────────────────
    // 이펙트의 종류다. `.jfx` 에는 이름으로 적힌다(`Kind: Reverb`). 앞의 일곱은 기존 엔진 `EAudioEffectKind` 와 같은 이름이고,
    // 뒤의 셋은 버스 사슬(D-210)에 있던 DSP 를 소스에도 쓰게 한 것이다.
    enum class AudioEffectKind : std::uint8_t
    {
        Reverb,
        LowPass,
        HighPass,
        Echo,
        Distortion,
        Compressor,
        Limiter,
        Equalizer,
        Chorus,
        PitchShift,
        Count
    };

    inline constexpr UInt32 AudioEffectMaxParams = 6;

    // 파라미터 하나의 이름·기본값·범위다. 이름은 `.jfx` 의 키이고 편집 창이 그대로 보인다(필드는 필드 이름으로).
    struct AudioEffectParam
    {
        const char* name = nullptr;
        Float defaultValue = 0.0f;
        Float minValue = 0.0f;
        Float maxValue = 1.0f;
        // 편집 창이 값을 적는 꼴이다(`printf`, 단위까지, D-309). 비우면 소수 둘째 자리다.
        const char* format = nullptr;
        // 몇 자릿수에 걸친 값(주파수)이다. 편집 창의 슬라이더 눈금이 로그다.
        Bool logarithmic = false;
    };

    struct AudioEffectKindInfo
    {
        const char* name = nullptr;
        UInt32 paramCount = 0;
        AudioEffectParam params[AudioEffectMaxParams];
    };

    // **종류마다 파라미터 표가 여기 한 곳에 있다.** 기존 엔진은 에디터 표(`EffectParamSpecs`)와 재생 쪽이 따로 놀아 `q`·`delay`·
    // `drive` 같은 키가 저장만 되고 소리에 닿지 않았다. 앞의 일곱은 기존 표의 이름·기본값·범위 그대로다.
    inline constexpr AudioEffectKindInfo AudioEffectKinds[static_cast<std::size_t>(AudioEffectKind::Count)] =
    {
        {"Reverb", 5, {{"roomSize", 0.5f, 0.0f, 1.0f}, {"damping", 0.5f, 0.0f, 1.0f}, {"width", 1.0f, 0.0f, 1.0f},
                       {"wet", 0.3f, 0.0f, 1.0f}, {"dry", 0.7f, 0.0f, 1.0f}}},
        {"LowPass", 2, {{"cutoff", 1000.0f, 20.0f, 20000.0f, "%.0f Hz", true}, {"q", 0.707f, 0.1f, 10.0f, nullptr, true}}},
        {"HighPass", 2, {{"cutoff", 1000.0f, 20.0f, 20000.0f, "%.0f Hz", true}, {"q", 0.707f, 0.1f, 10.0f, nullptr, true}}},
        {"Echo", 3, {{"delay", 0.25f, 0.0f, 2.0f, "%.2f s"}, {"decay", 0.5f, 0.0f, 1.0f}, {"wet", 0.4f, 0.0f, 1.0f}}},
        {"Distortion", 2, {{"drive", 0.5f, 0.0f, 1.0f}, {"wet", 1.0f, 0.0f, 1.0f}}},
        {"Compressor", 2, {{"threshold", -12.0f, -60.0f, 0.0f, "%.1f dB"}, {"ratio", 4.0f, 1.0f, 20.0f, "%.1f : 1"}}},
        {"Limiter", 2, {{"threshold", -12.0f, -60.0f, 0.0f, "%.1f dB"}, {"ratio", 4.0f, 1.0f, 20.0f, "%.1f : 1"}}},
        {"Equalizer", 6, {{"lowGain", 0.0f, -24.0f, 24.0f, "%+.1f dB"}, {"lowHz", 200.0f, 20.0f, 1000.0f, "%.0f Hz", true},
                          {"midGain", 0.0f, -24.0f, 24.0f, "%+.1f dB"}, {"midHz", 1000.0f, 100.0f, 8000.0f, "%.0f Hz", true},
                          {"highGain", 0.0f, -24.0f, 24.0f, "%+.1f dB"}, {"highHz", 5000.0f, 1000.0f, 16000.0f, "%.0f Hz", true}}},
        {"Chorus", 3, {{"mix", 0.5f, 0.0f, 1.0f}, {"rate", 0.8f, 0.05f, 10.0f, "%.2f Hz", true}, {"depth", 3.0f, 0.0f, 8.0f, "%.1f ms"}}},
        {"PitchShift", 1, {{"semitones", 12.0f, -12.0f, 12.0f, "%+.1f st"}}},
    };

    // 모르는 종류는 Reverb 로 본다(기존 엔진과 같다).
    inline const AudioEffectKindInfo& GetAudioEffectKindInfo(AudioEffectKind kind)
    {
        const std::size_t index = static_cast<std::size_t>(kind);
        return AudioEffectKinds[index < static_cast<std::size_t>(AudioEffectKind::Count) ? index : 0];
    }

    // `.jfx` 하나의 값이다. `values` 는 그 종류의 파라미터 표 차례다(남는 칸은 0). 스크립트·DLL 경계를 넘는 POD 다.
    struct AudioEffectSettings
    {
        AudioEffectKind kind = AudioEffectKind::Reverb;
        Float values[AudioEffectMaxParams] = {0.5f, 0.5f, 1.0f, 0.3f, 0.7f, 0.0f};
    };

    // 그 종류의 기본값이다.
    inline AudioEffectSettings MakeAudioEffect(AudioEffectKind kind)
    {
        AudioEffectSettings settings;
        const AudioEffectKindInfo& info = GetAudioEffectKindInfo(kind);
        settings.kind = static_cast<std::size_t>(kind) < static_cast<std::size_t>(AudioEffectKind::Count) ? kind : AudioEffectKind::Reverb;
        for (UInt32 index = 0; index < AudioEffectMaxParams; ++index)
        {
            settings.values[index] = index < info.paramCount ? info.params[index].defaultValue : Float(0.0f);
        }
        return settings;
    }

    // 종류를 바꾼다. 같은 이름의 파라미터는 값을 잇고 나머지는 기본값이다(기존 `NormalizeEffectParams`).
    inline AudioEffectSettings ChangeAudioEffectKind(const AudioEffectSettings& from, AudioEffectKind kind)
    {
        AudioEffectSettings to = MakeAudioEffect(kind);
        const AudioEffectKindInfo& before = GetAudioEffectKindInfo(from.kind);
        const AudioEffectKindInfo& after = GetAudioEffectKindInfo(to.kind);
        for (UInt32 index = 0; index < after.paramCount; ++index)
        {
            for (UInt32 old = 0; old < before.paramCount; ++old)
            {
                const char* a = after.params[index].name;
                const char* b = before.params[old].name;
                while (*a != '\0' && *a == *b)
                {
                    ++a;
                    ++b;
                }
                if (*a == *b)
                {
                    to.values[index] = from.values[old];
                }
            }
        }
        return to;
    }

    struct FontImportOptions
    {
        Float          pixelsPerUnit = DefaultPixelsPerUnit;
        TextureFilter  filter = TextureFilter::Default;
        FontRenderMode renderMode = FontRenderMode::Bitmap;
        // `Sdf` 에서만 쓴다. 거리장을 뜨는 em 픽셀 크기와, 글리프 둘레에 두는 퍼짐(픽셀)이다. 외곽선은 퍼짐보다 굵어질 수 없다.
        // 로드가 8~256, 1~32 로 자른다.
        UInt32  sdfSize = 48;
        UInt32  sdfSpread = 8;
        FontPrewarm    prewarm = FontPrewarm::None;
        // `Bitmap` 을 미리 뜰 em 픽셀 크기다. 비트맵은 크기마다 따로 뜨므로 이 크기의 글자만 미리 선다. `Sdf` 는 `sdfSize` 로 뜬다.
        UInt32  prewarmSize = 32;
    };

    // 폰트 패밀리의 네 칸이다(D-225). 칸마다 Font 에셋의 아이디이고, 빈 칸은 그리는 쪽이 Regular 로 대신한다(가짜 굵게는 없다).
    // 필드 이름이 `...FontId` 라 인스펙터의 고르기가 Font 만 보인다.
    struct FontFamilyOptions
    {
        AssetId regularFontId;
        AssetId boldFontId;
        AssetId italicFontId;
        AssetId boldItalicFontId;
    };

    // 문자열 표의 옵션이다(D-226). 이 표가 어느 로케일의 것인가(`ko-KR`·`en-US` 처럼 프로젝트의 `Locales` 에 적은 이름).
    struct StringTableOptions
    {
        String locale;
    };

    // 시트의 한 칸이다. 픽셀 좌표는 왼쪽 위가 원점이다.
    struct SpriteFrame
    {
        UInt32 x = 0;
        UInt32 y = 0;
        UInt32 width = 0;
        UInt32 height = 0;
        Float pivotX = 0.5f;
        Float pivotY = 0.5f;
    };

    // 레지스트리가 아는 에셋 하나의 읽기 전용 요약이다. 경로는 에셋 폴더 기준 상대경로이고 구분자는 `/` 다.
    struct AssetMetadata
    {
        AssetId id;
        AssetType type = AssetType::Unknown;
        JStringView sourcePath;
    };

    namespace Asset
    {
        struct TextureAsset
        {
            AssetId id;
        };

        struct SpriteAsset
        {
            AssetId id;
        };

        struct MeshAsset
        {
            AssetId id;
        };

        struct MaterialAsset
        {
            AssetId id;
        };

        struct ShaderAsset
        {
            AssetId id;
        };

        struct AudioAsset
        {
            AssetId id;
        };
    }
}

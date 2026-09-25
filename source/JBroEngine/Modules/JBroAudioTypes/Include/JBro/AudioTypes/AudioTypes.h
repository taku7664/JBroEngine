#pragma once

#include <JBro/Core/Core.h>

#include <cstdint>
#include <type_traits>

// 오디오의 값 타입이다(D-197). 스크립트가 보는 층(Tier S)이라 컴포넌트·서비스가 이것을 쓰고, 엔진의 믹서(`JBroAudio`)도
// 같은 타입으로 말한다. **장치·믹서·miniaudio 는 여기 나타나지 않는다.**

namespace JBro
{
    // 믹서가 가진 보이스 하나의 자리다. 세대가 다르면 이미 끝난 보이스다 - 무엇을 해도 아무 일이 없다.
    // 스크립트에 나가지 않는다(D-197). 컴포넌트의 시스템 전용 필드가 든다.
    struct AudioVoiceHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        constexpr bool IsSet() const
        {
            return generation != 0;
        }
    };

    // 믹서에 등록된 클립 하나다. 에셋의 PCM·압축 바이트를 빌려 가리킨다.
    struct AudioClipHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        constexpr bool IsSet() const
        {
            return generation != 0;
        }
    };

    // 믹서의 버스 번호다. 0 은 Master, 1 은 에디터 미리 듣기이고 프로젝트 버스는 그 뒤다.
    using AudioBusId = std::uint8_t;
    inline constexpr AudioBusId AudioMasterBus = 0;
    inline constexpr AudioBusId AudioEditorPreviewBus = 1;
    inline constexpr AudioBusId AudioFirstProjectBus = 2;
    // 버스는 사람이 손으로 관리하는 카테고리라 이 정도면 넉넉하다(기존 엔진 `MAX_AUDIO_BUSES` 16 + 예약 둘).
    inline constexpr std::uint32_t AudioMaxBuses = 18;
    // 센드가 없다는 표지다.
    inline constexpr AudioBusId AudioNoBus = 0xFF;
    // 예약 이름이다. 빈 이름도 Master 다.
    inline constexpr const char* AudioMasterBusName = "Master";

    // 거리에 따라 줄어드는 곡선이다. miniaudio 의 `ma_attenuation_model` 과 같은 순서다.
    enum class AudioAttenuation : std::uint8_t
    {
        // 거리와 무관하다 - 방향(팬)만 적용된다.
        None,
        // 현실의 역제곱에 가깝다.
        Inverse,
        // 최소~최대 거리 사이를 직선으로 줄인다. 게임에서 다루기 쉽다.
        Linear,
        // 가까이서 급하게 줄어든다.
        Exponential
    };

    // 버스 하나의 이펙트 사슬이다(D-202·D-210). **버스마다 고정된 칸들**이고 차례는 저역 차단 → 고역 차단 → EQ → 디스토션 →
    // 코러스 → 피치 시프트 → 메아리 → 잔향 → 원음 양 → 컴프레서다. 기본값인 칸은 건너뛴다(비용이 없다).
    // 각 칸은 0 이면 꺼진다(`lowPassHz`·`highPassHz`·`echoMix`·`reverbMix`). 재생 중에 바꿔도 된다 - 값은 원자 변수로
    // 건너가고 오디오 스레드가 처리 앞에 한 번 읽는다. 쓰임: 일시 정지 화면에서 배경음을 먹먹하게(저역 통과), 동굴의 잔향.
    struct AudioBusEffects
    {
        // 이 위의 소리를 깎는다(Hz). 0 이면 끈다. 800 쯤이면 벽 너머처럼 들린다.
        float lowPassHz = 0.0f;
        // 이 아래의 소리를 깎는다(Hz). 0 이면 끈다. 라디오·전화 소리.
        float highPassHz = 0.0f;
        // 메아리의 간격(초, 0.01..2)·되먹임(0..0.95)·섞는 양(0..1, 0 이면 끔).
        float echoDelay = 0.25f;
        float echoFeedback = 0.35f;
        float echoMix = 0.0f;
        // 잔향의 방 크기(0..1)·고음 흡수(0..1)·섞는 양(0..1, 0 이면 끔).
        float reverbRoom = 0.6f;
        float reverbDamping = 0.5f;
        float reverbMix = 0.0f;
        // 필터를 거친 원음이 남는 양(0..1)이다. 메아리·잔향은 이것과 무관하게 더해진다. 센드를 받아 잔향만 내는 버스는 0 이다.
        float dry = 1.0f;

        // ── D-210 에서 더한 칸들. 모두 기본값이면 꺼져 있다 ──
        // 3 대역 EQ(dB, -24..24, 0 이면 그 대역은 끔). 낮은 선반·가운데 봉우리·높은 선반이고 주파수는 Hz 다.
        float eqLowHz = 200.0f;
        float eqLowGain = 0.0f;
        float eqMidHz = 1000.0f;
        float eqMidGain = 0.0f;
        float eqHighHz = 5000.0f;
        float eqHighGain = 0.0f;
        // 디스토션: 세기(0..1, 0 이면 끔)와 섞는 양(0..1). 무전기·확성기·거친 기타.
        float distortion = 0.0f;
        float distortionMix = 1.0f;
        // 코러스: 섞는 양(0..1, 0 이면 끔)·흔드는 빠르기(Hz)·깊이(ms, 0..8). 소리를 두껍게, 물속처럼.
        float chorusMix = 0.0f;
        float chorusRate = 0.8f;
        float chorusDepth = 3.0f;
        // 피치 시프트(반음, -12..12, 0 이면 끔). 빠르기는 그대로 두고 음높이만 옮긴다 - 괴물 목소리·다람쥐 목소리.
        float pitchShift = 0.0f;
        // 컴프레서: 비율(1 이면 끔, 1..20)·문턱(dB)·어택과 릴리스(초)·메이크업 게인(dB). 폭발음과 대사의 크기 차를 줄인다.
        float compRatio = 1.0f;
        float compThreshold = -18.0f;
        float compAttack = 0.01f;
        float compRelease = 0.15f;
        float compMakeup = 0.0f;

        bool operator==(const AudioBusEffects& other) const = default;
    };

    static_assert(std::is_trivially_copyable_v<AudioBusEffects>);
    static_assert(std::is_trivially_copyable_v<AudioVoiceHandle>);
    static_assert(std::is_trivially_copyable_v<AudioClipHandle>);
    static_assert(sizeof(AudioVoiceHandle) == 8);
}

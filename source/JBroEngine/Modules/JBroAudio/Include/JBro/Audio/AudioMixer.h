#pragma once

#include <JBro/AudioTypes/AudioTypes.h>
#include <JBro/Types/SafePtr.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    struct AudioMixerDesc
    {
        // 출력 형식이다. 장치가 이 형식으로 당겨 간다(`Render`).
        std::uint32_t sampleRate = 48000;
        std::uint32_t channels = 2;
        // 동시에 울릴 수 있는 보이스 수다(D-197, 기존 엔진 `MaxPolyphony` 와 같은 64). 다 차면 훔친다.
        std::uint32_t maxVoices = 64;
        // 등록할 수 있는 클립 수다. 에셋 하나가 클립 하나다.
        std::uint32_t maxClips = 1024;
    };

    // 클립의 자료 모양이다. 믹서는 **빌린다** - 등록한 쪽이 `UnregisterClip` 이 돌아올 때까지 메모리를 살려 둔다.
    enum class AudioClipEncoding : std::uint8_t
    {
        // f32 인터리브 PCM (`pcm`, `frameCount`, `sampleRate`, `channels`).
        Pcm,
        // 압축된 파일 바이트(`bytes`, `byteCount`). 보이스마다 디코더를 열어 재생하며 푼다.
        Encoded
    };

    struct AudioClipDesc
    {
        AudioClipEncoding encoding = AudioClipEncoding::Pcm;
        const float* pcm = nullptr;
        std::uint64_t frameCount = 0;
        std::uint32_t sampleRate = 0;
        std::uint32_t channels = 0;
        const void* bytes = nullptr;
        std::size_t byteCount = 0;
    };

    // 보이스를 시작할 때 한 번 정하는 값이다. **거리·감쇠·원뿔·도플러 계수는 여기에만 있다** - miniaudio 가 그것을
    // 원자 변수가 아닌 평범한 float 로 들어 재생 중에 쓰면 오디오 스레드와 경쟁하기 때문이다(D-198). 재생 중에 바꿀
    // 수 있는 것은 `Set*` 이다.
    struct AudioPlayDesc
    {
        AudioClipHandle clip;
        AudioBusId bus = AudioMasterBus;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        // 거짓이면 공간화하지 않는다(배경음·UI).
        bool spatial = false;
        AudioAttenuation attenuation = AudioAttenuation::Inverse;
        float minDistance = 1.0f;
        float maxDistance = 50.0f;
        float rolloff = 1.0f;
        // 0 이면 도플러를 끈다.
        float dopplerFactor = 0.0f;
        float position[3] = {0.0f, 0.0f, 0.0f};
        // 0..255. 보이스가 모자랄 때 낮은 것부터 훔친다. 같은 우선순위면 작게 들리는 것, 그다음 오래된 것이다.
        std::uint8_t priority = 128;
        // 0 보다 크면 그만큼 뒤에 시작한다(샘플 단위로 정확하다 - 기존 엔진의 PlayAt).
        float startDelaySeconds = 0.0f;
        float fadeInSeconds = 0.0f;
        // 누가 만든 보이스인지다. `StopAllWithTag` 로 한 무리를 멈춘다(플레이 중지·미리 듣기).
        std::uint32_t tag = 0;
    };

    // 오디오의 믹서다(D-197·D-198). 안에 miniaudio `ma_engine`(장치 없음)이 있고 **밖에는 번호만 나간다.**
    //
    // **메인 스레드 전용이다.** 예외는 `Render` 하나이고 출력 장치의 오디오 스레드가 부른다. 재생 중에 쓰는 값은
    // miniaudio 가 원자로 든 것뿐이라 잠금이 없다. 보이스·버스·클립의 파괴 순서는 이 객체 한 곳이 정한다 -
    // 호출자가 쥐는 오디오 객체가 없으므로 기존 엔진의 자식 수명 추적(단계 2)이 필요 없다.
    //
    // **정상 프레임은 힙을 건드리지 않는다**(§9). miniaudio 의 할당은 전부 이 믹서의 고정 할당기로 오고, 초기화 때
    // 보이스 수만큼 미리 채워 둔다. 예외는 하나다: Vorbis(OGG) 스트리밍을 시작하면 stb_vorbis 가 CRT 에서 할당한다
    // (miniaudio 가 그쪽에 할당기를 넘기지 않는다).
    class AudioMixer
    {
    public:
        struct Stats
        {
            std::uint32_t activeVoices = 0;
            std::uint32_t maxVoices = 0;
            std::uint32_t registeredClips = 0;
            // 시작 이후 누계다.
            std::uint64_t voicesStarted = 0;
            std::uint64_t voicesStolen = 0;
            std::uint64_t voicesRejected = 0;
            // 고정 할당기가 모자라 힙에서 새로 받은 횟수다. 초기화 뒤에 늘면 예열이 모자란 것이다.
            std::uint64_t allocatorGrowths = 0;
            // 마지막 `Render` 의 최대 절댓값(클리핑 전). 에디터 미터가 읽는다.
            float lastPeak = 0.0f;
            // 지금까지 당겨 간 프레임 수다. 장치가 실제로 돌고 있는지 소리 없이 알 수 있다.
            std::uint64_t renderedFrames = 0;
        };

        AudioMixer();
        ~AudioMixer();
        AudioMixer(const AudioMixer&) = delete;
        AudioMixer& operator=(const AudioMixer&) = delete;

        bool Initialize(const AudioMixerDesc& desc);
        // 모든 보이스를 멈추고 클립·버스를 내린다. 출력 장치를 먼저 멈춘 뒤에 부른다.
        void Shutdown();
        bool IsInitialized() const;
        std::uint32_t GetSampleRate() const;
        std::uint32_t GetChannels() const;

        // ── 오디오 스레드 ──────────────────────────────────────────────────────
        // 인터리브 f32 로 `frameCount` 프레임을 채운다. 초기화 전이면 0 으로 채운다.
        void Render(float* output, std::uint32_t frameCount);
        // 출력 장치에 넘기는 함수 포인터 모양이다(POD 경계, `IAudioOutput`).
        static void RenderCallback(void* user, float* output, std::uint32_t frameCount);

        // ── 메인 스레드 ────────────────────────────────────────────────────────
        // 프레임마다 한 번 부른다. 끝난 보이스를 거둔다.
        void Update();

        AudioClipHandle RegisterClip(const AudioClipDesc& desc);
        // 이 클립을 쓰는 보이스를 전부 멈춘 뒤에 돌아온다. 돌아온 뒤에는 클립의 메모리를 풀어도 된다.
        void UnregisterClip(AudioClipHandle clip);
        bool IsClipRegistered(AudioClipHandle clip) const;
        // 클립의 길이(초). 없으면 0.
        double GetClipDurationSeconds(AudioClipHandle clip) const;

        // 프로젝트 버스를 Master 아래에 만든다. 다 차면 Master 를 돌려준다.
        AudioBusId CreateBus(float volume);
        // 프로젝트 버스를 전부 없앤다. 그 버스의 보이스는 Master 로 옮긴다.
        void DestroyProjectBuses();
        std::uint32_t GetBusCount() const;
        void SetBusVolume(AudioBusId bus, float volume);
        float GetBusVolume(AudioBusId bus) const;
        void SetBusMuted(AudioBusId bus, bool muted);
        bool IsBusMuted(AudioBusId bus) const;
        // 버스의 이펙트 사슬(D-202). 재생 중에 바꿔도 된다 - 값은 원자 변수로 건너간다. 메아리·잔향을 처음 켤 때 그 버퍼를
        // 여기서(메인 스레드) 한 번 잡는다. 그 뒤로는 켜고 꺼도 할당이 없다.
        void SetBusEffects(AudioBusId bus, const AudioBusEffects& effects);
        AudioBusEffects GetBusEffects(AudioBusId bus) const;

        // 보이스가 모자라고 훔칠 것도 없으면 빈 핸들이다(`voicesRejected`).
        AudioVoiceHandle Play(const AudioPlayDesc& desc);
        // 0 이면 곧바로 멈추고 자리를 돌려준다. 양수면 그만큼 줄이다 멈춘다.
        void Stop(AudioVoiceHandle voice, float fadeOutSeconds = 0.0f);
        void StopAllWithTag(std::uint32_t tag);
        void StopAll();
        void Pause(AudioVoiceHandle voice);
        void Resume(AudioVoiceHandle voice);

        // 살아 있는가(재생·일시 정지·줄어드는 중). 끝났거나 멈췄거나 훔쳐졌으면 거짓이다.
        bool IsAlive(AudioVoiceHandle voice) const;
        bool IsPaused(AudioVoiceHandle voice) const;
        double GetPlaybackSeconds(AudioVoiceHandle voice) const;
        // 재생 위치를 옮긴다(초). miniaudio 가 목표만 원자로 적고 오디오 스레드가 옮긴다 - 재생 중에 불러도 된다.
        void Seek(AudioVoiceHandle voice, double seconds);

        // 재생 중에 바꿀 수 있는 값들이다(miniaudio 의 원자 변수).
        void SetVolume(AudioVoiceHandle voice, float volume);
        void SetPitch(AudioVoiceHandle voice, float pitch);
        void SetLooping(AudioVoiceHandle voice, bool loop);
        void SetPosition(AudioVoiceHandle voice, const float position[3]);
        void SetVelocity(AudioVoiceHandle voice, const float velocity[3]);
        void SetBus(AudioVoiceHandle voice, AudioBusId bus);

        // 리스너다. 2D 는 방향을 기본(-Z 앞, +Y 위)으로 두고 위치만 준다.
        void SetListener(const float position[3], const float forward[3], const float up[3]);
        void SetListenerVelocity(const float velocity[3]);
        void SetMasterVolume(float volume);
        float GetMasterVolume() const;

        // 오디오 시계(초). 예약 시작의 기준이다.
        double GetTimeSeconds() const;
        Stats GetStats() const;

    private:
        struct State;
        OwnerPtr<State> m_state;
    };
}

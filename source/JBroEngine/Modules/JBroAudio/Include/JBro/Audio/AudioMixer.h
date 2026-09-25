#pragma once

#include <JBro/AudioTypes/AudioTypes.h>
#include <JBro/Types/SafePtr.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    class AudioFileDecoder;

    // 디스크 스트리밍 클립의 파일을 연다(D-203). `decoder` 를 그 파일로 열고 참을 돌려준다. **스트리머 스레드가 부른다** -
    // 어느 스레드에서 불려도 되는 함수여야 한다(호스트는 `IPlatform::OpenFileStream` 으로 잇는다).
    using AudioStreamOpenCallback = bool (*)(void* user, const char* utf8Path, AudioFileDecoder& decoder);

    // 버스 사슬에 붙는 사용자 처리기다(D-206). **오디오 스레드에서 불린다** - 할당·잠금·파일 IO·로그를 하지 않고 곧 돌아와야
    // 한다. `frames` 는 인터리브 f32 이고 제자리에서 고친다. 스크립트 DLL 에는 열지 않는다(핫 리로드가 코드를 내리는 동안 오디오
    // 스레드가 부를 수 있다) - 엔진·호스트 코드가 쓰는 확장점이다.
    using AudioBusProcessCallback = void (*)(void* user, float* frames, std::uint32_t frameCount, std::uint32_t channels,
        std::uint32_t sampleRate);

    struct AudioMixerDesc
    {
        // 출력 형식이다. 장치가 이 형식으로 당겨 간다(`Render`).
        std::uint32_t sampleRate = 48000;
        std::uint32_t channels = 2;
        // 동시에 울릴 수 있는 보이스 수다(D-197, 기존 엔진 `MaxPolyphony` 와 같은 64). 다 차면 훔친다.
        std::uint32_t maxVoices = 64;
        // 등록할 수 있는 클립 수다. 에셋 하나가 클립 하나다.
        std::uint32_t maxClips = 1024;
        // 디스크 스트리밍(D-203). 여는 함수가 없으면 `File` 클립은 재생되지 않는다. 동시에 흘려 읽는 보이스는 `maxStreams`
        // 개이고 보이스마다 `streamBufferSeconds` 만큼을 미리 풀어 둔다 - 디스크가 그만큼 늦어도 끊기지 않는다.
        AudioStreamOpenCallback openStream = nullptr;
        void* openStreamUser = nullptr;
        std::uint32_t maxStreams = 8;
        float streamBufferSeconds = 1.0f;
    };

    // 클립의 자료 모양이다. 믹서는 **빌린다** - 등록한 쪽이 `UnregisterClip` 이 돌아올 때까지 메모리를 살려 둔다.
    enum class AudioClipEncoding : std::uint8_t
    {
        // f32 인터리브 PCM (`pcm`, `frameCount`, `sampleRate`, `channels`).
        Pcm,
        // 압축된 파일 바이트(`bytes`, `byteCount`). 보이스마다 디코더를 열어 재생하며 푼다.
        Encoded,
        // 디스크의 파일(`path`, UTF-8)이다(D-203). 스트리머 스레드가 조금씩 풀어 링 버퍼에 채우고 오디오 스레드는 그것만 읽는다.
        // 형식(`frameCount`·`sampleRate`·`channels`)은 등록할 때 알려 준다 - 임포트가 헤더를 읽어 둔 값이다.
        File
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
        const char* path = nullptr;
        // 파일의 크기 보정(트림, 0..4)이다(D-205). 보이스의 음량에 곱해진다 - 소리마다 다른 녹음 크기를 에셋에서 한 번 맞춘다.
        float gain = 1.0f;
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
        // 보이스 하나에만 거는 필터다(Hz, 0 이면 끔). 벽 너머의 소리(저역 통과)·무전기(고역 통과). 둘 다 0 이면 보이스가
        // 필터 노드를 거치지 않는다 - 쓰지 않는 보이스는 비용이 없다. 재생 중에는 `SetVoiceFilter` 로 바꾼다.
        float lowPassHz = 0.0f;
        float highPassHz = 0.0f;
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
            // 디스크 스트리밍(D-203). 흘려 읽는 보이스 수와, 링 버퍼가 비어 무음을 낸 블록의 누계다(0 이 아니면 디스크가 늦다).
            std::uint32_t activeStreams = 0;
            std::uint32_t maxStreams = 0;
            std::uint64_t streamUnderruns = 0;
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

        // 프로젝트 버스를 `parent` 아래에 만든다(기본 Master). 부모는 이미 있는 버스여야 하고 미리 듣기 버스는 부모가 될 수
        // 없다 - 아니면 Master 아래다. 다 차면 Master 를 돌려준다.
        AudioBusId CreateBus(float volume, AudioBusId parent = AudioMasterBus);
        AudioBusId GetBusParent(AudioBusId bus) const;
        // 프로젝트 버스를 전부 없앤다. 그 버스의 보이스는 Master 로 옮긴다.
        void DestroyProjectBuses();
        std::uint32_t GetBusCount() const;
        // 버스 음량이다. `fadeSeconds` 동안 곧게 옮겨 간다(D-205). 0 이어도 10 ms 에 걸쳐 옮긴다 - 음량·음소거·솔로가 바뀌는
        // 순간에 뚝 끊기는 소리(클릭)가 나지 않는다.
        void SetBusVolume(AudioBusId bus, float volume, float fadeSeconds = 0.0f);
        float GetBusVolume(AudioBusId bus) const;
        void SetBusMuted(AudioBusId bus, bool muted);
        bool IsBusMuted(AudioBusId bus) const;
        // 버스의 이펙트 사슬(D-202). 재생 중에 바꿔도 된다 - 값은 원자 변수로 건너간다. 메아리·잔향을 처음 켤 때 그 버퍼를
        // 여기서(메인 스레드) 한 번 잡는다. 그 뒤로는 켜고 꺼도 할당이 없다.
        void SetBusEffects(AudioBusId bus, const AudioBusEffects& effects);
        AudioBusEffects GetBusEffects(AudioBusId bus) const;
        // 버스의 출력(음량·이펙트를 거친 뒤)을 `level` 만큼 다른 버스에도 보낸다. 쓰임: 여러 버스가 잔향 버스 하나를 나눠
        // 쓴다(받는 쪽은 `dry` 0 에 `reverbMix` 1). `target` 이 `AudioNoBus` 거나 `level` 이 0 이면 끊는다. 프로젝트 버스만
        // 보낼 수 있다. 되돌아오는 길(받는 버스가 부모·센드를 따라 보내는 버스에 닿는다)이 생기면 거절하고 거짓이다.
        bool SetBusSend(AudioBusId bus, AudioBusId target, float level);
        AudioBusId GetBusSendTarget(AudioBusId bus) const;
        float GetBusSendLevel(AudioBusId bus) const;
        // 믹싱할 때 한 버스만 듣는다. 솔로가 하나라도 있으면 솔로 버스와 그 자식·조상·센드를 받는 버스만 들리고 나머지
        // 프로젝트 버스는 0 이다. 음량·음소거 값은 그대로 두고 들리는 크기만 바꾼다 - 풀면 전과 같다.
        void SetBusSolo(AudioBusId bus, bool solo);
        bool IsBusSolo(AudioBusId bus) const;
        // 버스가 마지막으로 낸 블록의 최대 절댓값(음량·이펙트 뒤)이다. 미터가 읽는다.
        float GetBusPeak(AudioBusId bus) const;
        // 더킹(D-205): `trigger` 버스에 소리가 있는 동안 이 버스를 `amount`(0..1) 만큼 줄인다. 대사가 나오면 배경음이 물러선다.
        // 20 ms 에 걸쳐 줄고 `releaseSeconds` 에 걸쳐 돌아온다. `trigger` 가 `AudioNoBus` 거나 `amount` 가 0 이면 끈다.
        void SetBusDucking(AudioBusId bus, AudioBusId trigger, float amount, float releaseSeconds);
        // 버스 사슬의 끝(잔향 뒤, 음량 앞)에 사용자 처리기를 건다(D-206). null 이면 뗀다. **돌아온 뒤에는 옛 처리기가 다시 불리지
        // 않는다** - 그 코드와 `user` 를 곧 내려도 된다. 오디오 스레드가 옛 것을 부르는 중이면 그 한 번이 끝날 때까지 기다린다.
        void SetBusProcessor(AudioBusId bus, AudioBusProcessCallback callback, void* user);
        // 버스 컴프레서가 지난 블록에서 줄인 가장 큰 양(dB, 0 이상)이다(D-210). 미터가 읽는다.
        float GetBusGainReduction(AudioBusId bus) const;
        AudioBusId GetBusDuckTrigger(AudioBusId bus) const;
        float GetBusDuckAmount(AudioBusId bus) const;

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
        // 보이스의 필터다(Hz, 0 이면 끔). 켜면 보이스가 제 필터 노드를 거쳐 버스로 가고, 둘 다 끄면 곧장 버스로 간다.
        void SetVoiceFilter(AudioVoiceHandle voice, float lowPassHz, float highPassHz);

        // 리스너다. 2D 는 방향을 기본(-Z 앞, +Y 위)으로 두고 위치만 준다.
        void SetListener(const float position[3], const float forward[3], const float up[3]);
        void SetListenerVelocity(const float velocity[3]);
        void SetMasterVolume(float volume);
        float GetMasterVolume() const;
        // 장치로 나가기 직전의 이득이다. `seconds` 동안 곧게 옮겨 가므로 뚝 끊기는 소리(클릭)가 없다. 창이 포커스를 잃었을
        // 때 줄이는 정책이 이것을 쓴다 - 게임이 정한 Master 음량과 따로 논다.
        void SetOutputGain(float gain, float seconds);
        float GetOutputGain() const;
        // 출력 리미터(D-210, 기본 켬). 여러 버스가 겹쳐 `ceiling` 을 넘으면 그 순간에 줄이고 0.1 초에 걸쳐 되돌린다 - 잘라 내어
        // 찌그러지는 것보다 낫다. 끄면 1 에서 잘라 낸다. `Stats::lastPeak` 는 리미터 앞의 값이라 넘친 것이 보인다.
        void SetOutputLimiter(bool enabled, float ceiling = 0.98f);
        bool IsOutputLimiterEnabled() const;

        // 장치로 나간 마지막 `count` 샘플(채널 평균, 최대 `RecentCapacity`)을 옛것부터 복사하고 복사한 수를 돌려준다.
        // 오디오 스레드가 쓰는 도중에 읽으므로 한두 샘플이 어긋날 수 있다 - 화면에 그리는 데만 쓴다.
        static constexpr std::uint32_t RecentCapacity = 4096;
        std::uint32_t CopyRecentOutput(float* mono, std::uint32_t count) const;
        // 최근 출력의 스펙트럼을 `bandCount` 칸(로그 간격, 30 Hz..나이퀴스트)으로 채운다. 값은 0..1 이고 -72 dB 가 0,
        // 0 dB(최대 크기의 사인파)가 1 이다. 메인 스레드에서 2048 점 FFT 를 한 번 돈다. 할당하지 않는다.
        void ComputeSpectrum(float* bands, std::uint32_t bandCount) const;

        // 오디오 시계(초). 예약 시작의 기준이다.
        double GetTimeSeconds() const;
        Stats GetStats() const;

    private:
        struct State;
        OwnerPtr<State> m_state;
    };
}

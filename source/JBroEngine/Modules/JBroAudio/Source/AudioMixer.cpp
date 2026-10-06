#include <JBro/Audio/AudioMixer.h>

#include <JBro/Asset/AudioDecoder.h>
#include <JBro/Core/Log.h>
#include <JBro/Types/Allocator.h>
#include <JBro/Types/Array.h>

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        // miniaudio 의 할당을 받는 고정 할당기다. 크기를 2 의 거듭제곱 칸으로 올려 칸마다 빈 목록을 둔다. 한 번 힙에서
        // 받은 블록은 믹서가 내려갈 때까지 돌려주지 않고 다시 쓴다 - 그래서 예열 뒤의 정상 프레임은 힙을 건드리지 않는다.
        //
        // 잠금이 있다. miniaudio 는 메인 스레드에서만 할당하지만(보이스 시작·끝), 디코더가 오디오 스레드에서 할당하는
        // 경로가 없다고 원문으로 확인하지 못했으므로 둘이 부딪혀도 망가지지 않게 한다. 경합이 없으면 원자 연산 하나다.
        class FixedAllocator
        {
        public:
            static constexpr std::size_t HeaderSize = 16;
            static constexpr UInt32 MinShift = 6;
            static constexpr UInt32 MaxShift = 24;
            static constexpr UInt32 ClassCount = MaxShift - MinShift + 1;
            static constexpr UInt32 DirectClass = 0xFFu;

            ~FixedAllocator()
            {
                ReleaseAll();
            }

            void* Allocate(std::size_t size)
            {
                const UInt32 sizeClass = ClassOf(size + HeaderSize);
                Lock();
                void* block = nullptr;
                if (sizeClass != DirectClass && m_free[sizeClass] != nullptr)
                {
                    block = m_free[sizeClass];
                    std::memcpy(&m_free[sizeClass], block, sizeof(void*));
                }
                Unlock();
                if (block == nullptr)
                {
                    const std::size_t bytes = sizeClass == DirectClass ? size + HeaderSize : BlockBytes(sizeClass);
                    block = HeapAllocator{}.Allocate(bytes, HeaderSize);
                    if (block == nullptr)
                    {
                        return nullptr;
                    }
                    Lock();
                    ++m_growths;
                    if (sizeClass != DirectClass)
                    {
                        m_owned.Add(block);
                    }
                    Unlock();
                }
                Header* header = static_cast<Header*>(block);
                header->sizeClass = sizeClass;
                header->bytes = sizeClass == DirectClass ? size + HeaderSize : BlockBytes(sizeClass);
                return static_cast<std::byte*>(block) + HeaderSize;
            }

            void Free(void* memory)
            {
                if (memory == nullptr)
                {
                    return;
                }
                void* block = static_cast<std::byte*>(memory) - HeaderSize;
                const Header* header = static_cast<const Header*>(block);
                if (header->sizeClass == DirectClass)
                {
                    HeapAllocator{}.Deallocate(block, header->bytes, HeaderSize);
                    return;
                }
                const UInt32 sizeClass = header->sizeClass;
                Lock();
                std::memcpy(block, &m_free[sizeClass], sizeof(void*));
                m_free[sizeClass] = block;
                Unlock();
            }

            void* Reallocate(void* memory, std::size_t size)
            {
                if (memory == nullptr)
                {
                    return Allocate(size);
                }
                const Header* header = reinterpret_cast<const Header*>(static_cast<std::byte*>(memory) - HeaderSize);
                const std::size_t usable = header->bytes - HeaderSize;
                if (size <= usable)
                {
                    return memory;
                }
                void* replacement = Allocate(size);
                if (replacement == nullptr)
                {
                    return nullptr;
                }
                std::memcpy(replacement, memory, usable);
                Free(memory);
                return replacement;
            }

            UInt64 GetGrowths() const
            {
                return m_growths;
            }

            void ReleaseAll()
            {
                for (void* block : m_owned)
                {
                    const Header* header = static_cast<const Header*>(block);
                    HeapAllocator{}.Deallocate(block, header->bytes, HeaderSize);
                }
                m_owned.Reset();
                for (void*& head : m_free)
                {
                    head = nullptr;
                }
            }

            void Reserve(std::size_t blocks)
            {
                m_owned.Reserve(blocks);
            }

            static void* OnMalloc(std::size_t size, void* user)
            {
                return static_cast<FixedAllocator*>(user)->Allocate(size);
            }

            static void* OnRealloc(void* memory, std::size_t size, void* user)
            {
                return static_cast<FixedAllocator*>(user)->Reallocate(memory, size);
            }

            static void OnFree(void* memory, void* user)
            {
                static_cast<FixedAllocator*>(user)->Free(memory);
            }

            ma_allocation_callbacks Callbacks()
            {
                ma_allocation_callbacks callbacks = {};
                callbacks.pUserData = this;
                callbacks.onMalloc = &OnMalloc;
                callbacks.onRealloc = &OnRealloc;
                callbacks.onFree = &OnFree;
                return callbacks;
            }

        private:
            struct Header
            {
                UInt32 sizeClass;
                UInt32 reserved;
                std::size_t bytes;
            };
            static_assert(sizeof(Header) <= HeaderSize);

            static UInt32 ClassOf(std::size_t bytes)
            {
                UInt32 shift = MinShift;
                while (shift <= MaxShift && (std::size_t{1} << shift) < bytes)
                {
                    ++shift;
                }
                return shift > MaxShift ? DirectClass : shift - MinShift;
            }

            static std::size_t BlockBytes(UInt32 sizeClass)
            {
                return std::size_t{1} << (sizeClass + MinShift);
            }

            void Lock()
            {
                while (m_lock.test_and_set(std::memory_order_acquire))
                {
                }
            }

            void Unlock()
            {
                m_lock.clear(std::memory_order_release);
            }

            std::atomic_flag m_lock = ATOMIC_FLAG_INIT;
            void* m_free[ClassCount] = {};
            Array<void*> m_owned;
            UInt64 m_growths = 0;
        };

        Float Clamp01(Float value)
        {
            if (!(value > 0.0f))
            {
                return 0.0f;
            }
            return value > 1.0f ? Float(1.0f) : value;
        }

        Float SafePositive(Float value, Float fallback)
        {
            return std::isfinite(value) && value > 0.0f ? value : fallback;
        }

        ma_attenuation_model ToMiniaudio(AudioAttenuation attenuation)
        {
            switch (attenuation)
            {
            case AudioAttenuation::None:
                return ma_attenuation_model_none;
            case AudioAttenuation::Linear:
                return ma_attenuation_model_linear;
            case AudioAttenuation::Exponential:
                return ma_attenuation_model_exponential;
            case AudioAttenuation::Inverse:
            default:
                return ma_attenuation_model_inverse;
            }
        }
    }

    namespace
    {
        constexpr Float Tau = 6.28318530717958647692f;

        // RBJ 쿡북의 2 차 필터다. 계수는 오디오 스레드가 목표가 바뀐 것을 보고 다시 짓는다 - miniaudio 필터의 `reinit` 은
        // 스레드 안전하지 않다(D-198).
        struct Biquad
        {
            Float b0 = 1.0f;
            Float b1 = 0.0f;
            Float b2 = 0.0f;
            Float a1 = 0.0f;
            Float a2 = 0.0f;
            float z1[2] = {0.0f, 0.0f};
            float z2[2] = {0.0f, 0.0f};

            void Configure(Bool highPass, Float cutoff, Float sampleRate)
            {
                const Float nyquist = sampleRate * 0.5f;
                if (cutoff < 10.0f)
                {
                    cutoff = 10.0f;
                }
                if (cutoff > nyquist * 0.95f)
                {
                    cutoff = nyquist * 0.95f;
                }
                const Float omega = Tau * cutoff / sampleRate;
                const Float cosine = std::cos(omega);
                const Float alpha = std::sin(omega) / (2.0f * 0.70710678f);
                const Float a0 = 1.0f + alpha;
                if (highPass)
                {
                    b0 = (1.0f + cosine) * 0.5f / a0;
                    b1 = -(1.0f + cosine) / a0;
                }
                else
                {
                    b0 = (1.0f - cosine) * 0.5f / a0;
                    b1 = (1.0f - cosine) / a0;
                }
                b2 = b0;
                a1 = -2.0f * cosine / a0;
                a2 = (1.0f - alpha) / a0;
            }

            void SetNormalized(Float nb0, Float nb1, Float nb2, Float na0, Float na1, Float na2)
            {
                b0 = nb0 / na0;
                b1 = nb1 / na0;
                b2 = nb2 / na0;
                a1 = na1 / na0;
                a2 = na2 / na0;
            }

            // RBJ 쿡북의 선반(기울기 1)이다. `high` 면 높은 선반이다(D-210).
            void ConfigureShelf(Bool high, Float frequency, Float gainDb, Float sampleRate)
            {
                const Float nyquist = sampleRate * 0.5f;
                frequency = frequency < 10.0f ? Float(10.0f) : (frequency > nyquist * 0.95f ? Float(nyquist * 0.95f) : frequency);
                const Float amplitude = std::pow(10.0f, gainDb / 40.0f);
                const Float omega = Tau * frequency / sampleRate;
                const Float cosine = std::cos(omega);
                const Float alpha = std::sin(omega) * 0.5f * 1.41421356f;
                const Float root = 2.0f * std::sqrt(amplitude) * alpha;
                const Float up = amplitude + 1.0f;
                const Float down = amplitude - 1.0f;
                if (high)
                {
                    SetNormalized(amplitude * (up + down * cosine + root), -2.0f * amplitude * (down + up * cosine),
                        amplitude * (up + down * cosine - root), up - down * cosine + root, 2.0f * (down - up * cosine),
                        up - down * cosine - root);
                }
                else
                {
                    SetNormalized(amplitude * (up - down * cosine + root), 2.0f * amplitude * (down - up * cosine),
                        amplitude * (up - down * cosine - root), up + down * cosine + root, -2.0f * (down + up * cosine),
                        up + down * cosine - root);
                }
            }

            // RBJ 쿡북의 봉우리(Q 1)다.
            void ConfigurePeak(Float frequency, Float gainDb, Float sampleRate)
            {
                const Float nyquist = sampleRate * 0.5f;
                frequency = frequency < 10.0f ? Float(10.0f) : (frequency > nyquist * 0.95f ? Float(nyquist * 0.95f) : frequency);
                const Float amplitude = std::pow(10.0f, gainDb / 40.0f);
                const Float omega = Tau * frequency / sampleRate;
                const Float cosine = std::cos(omega);
                const Float alpha = std::sin(omega) * 0.5f;
                SetNormalized(1.0f + alpha * amplitude, -2.0f * cosine, 1.0f - alpha * amplitude, 1.0f + alpha / amplitude,
                    -2.0f * cosine, 1.0f - alpha / amplitude);
            }

            Float Process(UInt32 channel, Float input)
            {
                const Float output = b0 * input + z1[channel];
                z1[channel] = b1 * input - a1 * output + z2[channel];
                z2[channel] = b2 * input - a2 * output;
                return output;
            }
        };

        // Freeverb(Jezar, 퍼블릭 도메인)의 짜임이다. 기존 엔진의 잔향도 이것이었다. 줄 길이는 44.1 kHz 기준 값을 샘플 레이트로
        // 늘인다. 오른쪽 채널은 23 샘플 벌린다.
        struct Reverb
        {
            static constexpr Int32 Combs = 8;
            static constexpr Int32 Allpasses = 4;
            static constexpr Int32 MaxComb = 3700;
            static constexpr Int32 MaxAllpass = 1300;
            float comb[2][Combs][MaxComb] = {};
            float combStore[2][Combs] = {};
            Int32 combLength[2][Combs] = {};
            Int32 combIndex[2][Combs] = {};
            float allpass[2][Allpasses][MaxAllpass] = {};
            Int32 allpassLength[2][Allpasses] = {};
            Int32 allpassIndex[2][Allpasses] = {};

            // 옛 꼬리를 지운다. 꺼졌던 잔향을 다시 켤 때 오디오 스레드가 부른다(D-240).
            void Clear()
            {
                std::memset(comb, 0, sizeof(comb));
                std::memset(combStore, 0, sizeof(combStore));
                std::memset(allpass, 0, sizeof(allpass));
            }

            void Prepare(UInt32 sampleRate)
            {
                static const Int32 combTuning[Combs] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
                static const Int32 allpassTuning[Allpasses] = {556, 441, 341, 225};
                const Float scale = static_cast<float>(sampleRate) / 44100.0f;
                for (Int32 channel = 0; channel < 2; ++channel)
                {
                    const Int32 spread = channel == 0 ? 0 : 23;
                    for (Int32 index = 0; index < Combs; ++index)
                    {
                        Int32 length = static_cast<int>(static_cast<float>(combTuning[index] + spread) * scale);
                        combLength[channel][index] = length < MaxComb ? (length > 1 ? length : Int32(1)) : MaxComb;
                    }
                    for (Int32 index = 0; index < Allpasses; ++index)
                    {
                        Int32 length = static_cast<int>(static_cast<float>(allpassTuning[index] + spread) * scale);
                        allpassLength[channel][index] = length < MaxAllpass ? (length > 1 ? length : Int32(1)) : MaxAllpass;
                    }
                }
            }

            Float Process(Int32 channel, Float input, Float feedback, Float damping)
            {
                Float output = 0.0f;
                const Float scaled = input * 0.015f;
                for (Int32 index = 0; index < Combs; ++index)
                {
                    float* line = comb[channel][index];
                    Int32& at = combIndex[channel][index];
                    const Float delayed = line[at];
                    float& store = combStore[channel][index];
                    store = delayed * (1.0f - damping) + store * damping;
                    line[at] = scaled + store * feedback;
                    if (++at >= combLength[channel][index])
                    {
                        at = 0;
                    }
                    output += delayed;
                }
                for (Int32 index = 0; index < Allpasses; ++index)
                {
                    float* line = allpass[channel][index];
                    Int32& at = allpassIndex[channel][index];
                    const Float delayed = line[at];
                    line[at] = output + delayed * 0.5f;
                    output = delayed - output;
                    if (++at >= allpassLength[channel][index])
                    {
                        at = 0;
                    }
                }
                return output;
            }
        };

        // 버스의 이펙트 노드다. miniaudio 노드 그래프에서 버스 그룹과 그 부모 사이에 선다. 목표 값은 메인 스레드가 원자로 쓰고,
        // 오디오 스레드는 처리 앞에 읽어 바뀐 것만 반영한다. 메아리·잔향 버퍼는 메인 스레드가 처음 켤 때 잡아 원자 포인터로 넘긴다.
        struct BusEffectNode
        {
            ma_node_base base;
            UInt32 channels = 2;
            UInt32 sampleRate = 48000;
            std::atomic<float> lowPass{0.0f};
            std::atomic<float> highPass{0.0f};
            std::atomic<float> echoDelay{0.25f};
            std::atomic<float> echoFeedback{0.35f};
            std::atomic<float> echoMix{0.0f};
            std::atomic<float> reverbRoom{0.6f};
            std::atomic<float> reverbDamping{0.5f};
            std::atomic<float> reverbMix{0.0f};
            std::atomic<float> dry{1.0f};
            std::atomic<float> peak{0.0f};
            // 버스 음량(D-205). 목표와 프레임당 변화량은 메인 스레드가 쓰고, 지금 값은 오디오 스레드만 만진다.
            std::atomic<float> gainTarget{1.0f};
            std::atomic<float> gainRate{1.0f};
            Float gainCurrent = 1.0f;
            // 블록 끝의 지금 음량이다. 페이드 중에 새 페이드를 걸 때 메인 스레드가 남은 거리를 잰다(D-240).
            std::atomic<float> gainNow{1.0f};
            // 더킹. 다른 버스 노드의 봉우리(지난 블록)를 읽는다. 노드는 믹서의 고정 배열에 있어 버스를 내려도 메모리는 산다.
            std::atomic<const void*> duckSource{nullptr};
            std::atomic<float> duckAmount{0.0f};
            std::atomic<float> duckRelease{0.3f};
            Float duckCurrent = 1.0f;
            // 사용자 처리기(D-206). 오디오 스레드는 `inProcessor` 를 먼저 세우고 처리기를 읽는다. 메인 스레드는 처리기를 바꾼
            // 뒤 `inProcessor` 가 내려갈 때까지 기다린다 - 그래서 돌아온 뒤에는 옛 처리기가 불리지 않는다.
            std::atomic<AudioBusProcessCallback> processor{nullptr};
            std::atomic<void*> processorUser{nullptr};
            std::atomic<bool> inProcessor{false};
            std::atomic<float*> echoBuffer{nullptr};
            std::atomic<Reverb*> reverb{nullptr};
            UInt32 echoCapacity = 0;
            // D-210 의 칸들. 뜻은 `AudioBusEffects` 의 같은 이름이다.
            std::atomic<float> eqLowHz{200.0f};
            std::atomic<float> eqLowGain{0.0f};
            std::atomic<float> eqMidHz{1000.0f};
            std::atomic<float> eqMidGain{0.0f};
            std::atomic<float> eqHighHz{5000.0f};
            std::atomic<float> eqHighGain{0.0f};
            std::atomic<float> distortion{0.0f};
            std::atomic<float> distortionMix{1.0f};
            std::atomic<float> chorusMix{0.0f};
            std::atomic<float> chorusRate{0.8f};
            std::atomic<float> chorusDepth{3.0f};
            std::atomic<float> pitchShift{0.0f};
            std::atomic<float> compRatio{1.0f};
            std::atomic<float> compThreshold{-18.0f};
            std::atomic<float> compAttack{0.01f};
            std::atomic<float> compRelease{0.15f};
            std::atomic<float> compMakeup{0.0f};
            std::atomic<float> compReduction{0.0f};
            // 코러스·피치 시프트의 지연선이다. 메아리처럼 처음 켤 때 메인 스레드가 잡는다.
            std::atomic<float*> chorusBuffer{nullptr};
            UInt32 chorusCapacity = 0;
            std::atomic<float*> pitchBuffer{nullptr};
            UInt32 pitchCapacity = 0;
            // 아래는 오디오 스레드만 만진다.
            Float appliedLowPass = -1.0f;
            Float appliedHighPass = -1.0f;
            Biquad lowFilter;
            Biquad highFilter;
            UInt32 echoWrite = 0;
            float appliedEq[6] = {-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
            Biquad eqLow;
            Biquad eqMid;
            Biquad eqHigh;
            UInt32 chorusWrite = 0;
            Float chorusPhase = 0.0f;
            UInt32 pitchWrite = 0;
            Float pitchPhase = 0.0f;
            Float compEnvelope = 0.0f;
            // 지난 블록에 켜져 있었는가(D-240). 꺼졌다 켜지면 지연선을 비운다 - 끈 동안 멈춰 있던 옛 소리가 다시 나지 않게.
            Bool echoWasOn = false;
            Bool reverbWasOn = false;
            Bool chorusWasOn = false;
            Bool pitchWasOn = false;
        };

        static constexpr Float ChorusSeconds = 0.05f;
        static constexpr Float PitchWindowSeconds = 0.05f;

        // 지연선을 뒤로 `delay` 프레임(소수) 읽는다. 두 샘플 사이는 곧게 잇는다.
        Float ReadDelay(const float* line, UInt32 capacity, UInt32 channels, UInt32 channel,
            UInt32 write, Float delay)
        {
            Float position = static_cast<float>(write) - delay;
            while (position < 0.0f)
            {
                position += static_cast<float>(capacity);
            }
            const UInt32 first = static_cast<std::uint32_t>(position) % capacity;
            const UInt32 second = (first + 1) % capacity;
            const Float fraction = position - std::floor(position);
            return line[first * channels + channel] * (1.0f - fraction) + line[second * channels + channel] * fraction;
        }

        void RunEqBand(Biquad& filter, float& appliedHz, float& appliedGain, Float hz, Float gain, Int32 kind, Float rate,
            float* out, std::size_t samples, UInt32 channels)
        {
            if (gain == 0.0f)
            {
                return;
            }
            if (hz != appliedHz || gain != appliedGain)
            {
                if (kind == 1)
                {
                    filter.ConfigurePeak(hz, gain, rate);
                }
                else
                {
                    filter.ConfigureShelf(kind == 2, hz, gain, rate);
                }
                appliedHz = hz;
                appliedGain = gain;
            }
            for (std::size_t index = 0; index < samples; ++index)
            {
                out[index] = filter.Process(static_cast<std::uint32_t>(index % channels) & 1u, out[index]);
            }
        }

        static constexpr Float MaxEchoSeconds = 2.0f;

        Float Clamped(Float value, Float low, Float high)
        {
            if (!(value >= low))
            {
                return low;
            }
            return value > high ? high : value;
        }

        void ScrubNonFinite(float* samples, std::size_t count)
        {
            for (std::size_t index = 0; index < count; ++index)
            {
                if (false == std::isfinite(samples[index]))
                {
                    samples[index] = 0.0f;
                }
            }
        }

        void ProcessBusEffects(ma_node* node, const float** framesIn, ma_uint32* frameCountIn, float** framesOut,
            ma_uint32* frameCountOut)
        {
            BusEffectNode& self = *reinterpret_cast<BusEffectNode*>(node);
            const UInt32 frames = *frameCountOut;
            const UInt32 channels = self.channels;
            float* out = framesOut[0];
            const std::size_t samples = static_cast<std::size_t>(frames) * channels;
            // 입력이 없는 프레임도 돈다(`MA_NODE_FLAG_ALLOW_NULL_INPUT`) - 메아리와 잔향의 꼬리가 소리가 멎은 뒤에도 운다.
            if (framesIn != nullptr && framesIn[0] != nullptr && frameCountIn != nullptr && *frameCountIn > 0)
            {
                const std::size_t available = static_cast<std::size_t>(*frameCountIn < frames ? UInt32(*frameCountIn) : frames) * channels;
                std::memcpy(out, framesIn[0], sizeof(float) * available);
                if (available < samples)
                {
                    std::memset(out + available, 0, sizeof(float) * (samples - available));
                }
            }
            else
            {
                std::memset(out, 0, sizeof(float) * samples);
            }
            // 유한하지 않은 샘플(깨진 클립·자식 버스의 처리기)은 0 으로 바꾼다(D-240). 필터·메아리·컴프레서의 상태에 한 번 들어가면
            // 그 버스가 영원히 먹통이 된다.
            ScrubNonFinite(out, samples);
            const Float rate = static_cast<float>(self.sampleRate);

            const Float highPass = self.highPass.load(std::memory_order_relaxed);
            if (highPass > 0.0f)
            {
                if (highPass != self.appliedHighPass)
                {
                    self.highFilter.Configure(true, highPass, rate);
                    self.appliedHighPass = highPass;
                }
                for (std::size_t index = 0; index < samples; ++index)
                {
                    out[index] = self.highFilter.Process(static_cast<std::uint32_t>(index % channels) & 1u, out[index]);
                }
            }
            const Float lowPass = self.lowPass.load(std::memory_order_relaxed);
            if (lowPass > 0.0f)
            {
                if (lowPass != self.appliedLowPass)
                {
                    self.lowFilter.Configure(false, lowPass, rate);
                    self.appliedLowPass = lowPass;
                }
                for (std::size_t index = 0; index < samples; ++index)
                {
                    out[index] = self.lowFilter.Process(static_cast<std::uint32_t>(index % channels) & 1u, out[index]);
                }
            }

            // 3 대역 EQ(D-210). 대역마다 이득이 0 이면 건너뛴다.
            RunEqBand(self.eqLow, self.appliedEq[0], self.appliedEq[1], self.eqLowHz.load(std::memory_order_relaxed),
                Clamped(self.eqLowGain.load(std::memory_order_relaxed), -24.0f, 24.0f), 0, rate, out, samples, channels);
            RunEqBand(self.eqMid, self.appliedEq[2], self.appliedEq[3], self.eqMidHz.load(std::memory_order_relaxed),
                Clamped(self.eqMidGain.load(std::memory_order_relaxed), -24.0f, 24.0f), 1, rate, out, samples, channels);
            RunEqBand(self.eqHigh, self.appliedEq[4], self.appliedEq[5], self.eqHighHz.load(std::memory_order_relaxed),
                Clamped(self.eqHighGain.load(std::memory_order_relaxed), -24.0f, 24.0f), 2, rate, out, samples, channels);

            // 디스토션: tanh 로 둥글게 자른다. 가득 찬 소리는 가득 찬 채로 두도록 tanh(이득) 으로 나눈다.
            const Float drive = Clamped(self.distortion.load(std::memory_order_relaxed), 0.0f, 1.0f);
            if (drive > 0.0f)
            {
                const Float mix = Clamped(self.distortionMix.load(std::memory_order_relaxed), 0.0f, 1.0f);
                const Float pre = 1.0f + drive * 24.0f;
                const Float normalize = 1.0f / std::tanh(pre);
                for (std::size_t index = 0; index < samples; ++index)
                {
                    const Float shaped = std::tanh(pre * out[index]) * normalize;
                    out[index] += (shaped - out[index]) * mix;
                }
            }

            // 코러스: 12 ms 에서 깊이만큼 흔들리는 지연을 섞는다. 오른쪽은 흔들림을 1/4 주기 늦춰 넓게 들린다.
            const Float chorusMix = Clamped(self.chorusMix.load(std::memory_order_relaxed), 0.0f, 1.0f);
            float* chorus = self.chorusBuffer.load(std::memory_order_acquire);
            const Bool chorusOn = chorusMix > 0.0f && chorus != nullptr && self.chorusCapacity > 2;
            if (chorusOn && false == self.chorusWasOn)
            {
                std::memset(chorus, 0, sizeof(float) * static_cast<std::size_t>(self.chorusCapacity) * channels);
                self.chorusWrite = 0;
            }
            self.chorusWasOn = chorusOn;
            if (chorusOn)
            {
                const Float step = Clamped(self.chorusRate.load(std::memory_order_relaxed), 0.05f, 10.0f) / rate;
                const Float depth = Clamped(self.chorusDepth.load(std::memory_order_relaxed), 0.0f, 8.0f) * 0.001f * rate;
                const Float base = 0.012f * rate;
                for (UInt32 frame = 0; frame < frames; ++frame)
                {
                    for (UInt32 channel = 0; channel < channels; ++channel)
                    {
                        chorus[self.chorusWrite * channels + channel] = out[frame * channels + channel];
                    }
                    for (UInt32 channel = 0; channel < channels; ++channel)
                    {
                        const Float lfo = std::sin(Tau * (self.chorusPhase + (channel == 1 ? 0.25f : 0.0f)));
                        const Float delayed = ReadDelay(chorus, self.chorusCapacity, channels, channel, self.chorusWrite,
                            base + depth * (0.5f + 0.5f * lfo));
                        float& sample = out[frame * channels + channel];
                        sample = sample * (1.0f - 0.5f * chorusMix) + delayed * 0.5f * chorusMix;
                    }
                    self.chorusWrite = (self.chorusWrite + 1) % self.chorusCapacity;
                    self.chorusPhase += step;
                    if (self.chorusPhase >= 1.0f)
                    {
                        self.chorusPhase -= 1.0f;
                    }
                }
            }

            // 피치 시프트: 50 ms 창의 두 탭이 지연을 줄이거나 늘리며 읽고, 삼각 창으로 번갈아 섞는다. 빠르기는 그대로다.
            const Float semitones = Clamped(self.pitchShift.load(std::memory_order_relaxed), -12.0f, 12.0f);
            float* pitch = self.pitchBuffer.load(std::memory_order_acquire);
            const Bool pitchOn = semitones != 0.0f && pitch != nullptr && self.pitchCapacity > 2;
            if (pitchOn && false == self.pitchWasOn)
            {
                std::memset(pitch, 0, sizeof(float) * static_cast<std::size_t>(self.pitchCapacity) * channels);
                self.pitchWrite = 0;
                self.pitchPhase = 0.0f;
            }
            self.pitchWasOn = pitchOn;
            if (pitchOn)
            {
                const Float window = PitchWindowSeconds * rate;
                const Float step = (1.0f - std::pow(2.0f, semitones / 12.0f)) / window;
                for (UInt32 frame = 0; frame < frames; ++frame)
                {
                    for (UInt32 channel = 0; channel < channels; ++channel)
                    {
                        pitch[self.pitchWrite * channels + channel] = out[frame * channels + channel];
                    }
                    const Float first = self.pitchPhase;
                    const Float second = first + 0.5f >= 1.0f ? first - 0.5f : first + 0.5f;
                    const Float firstGain = 1.0f - std::fabs(2.0f * first - 1.0f);
                    const Float secondGain = 1.0f - std::fabs(2.0f * second - 1.0f);
                    for (UInt32 channel = 0; channel < channels; ++channel)
                    {
                        const Float a = ReadDelay(pitch, self.pitchCapacity, channels, channel, self.pitchWrite, 1.0f + first * window);
                        const Float b = ReadDelay(pitch, self.pitchCapacity, channels, channel, self.pitchWrite, 1.0f + second * window);
                        out[frame * channels + channel] = a * firstGain + b * secondGain;
                    }
                    self.pitchWrite = (self.pitchWrite + 1) % self.pitchCapacity;
                    self.pitchPhase += step;
                    while (self.pitchPhase >= 1.0f)
                    {
                        self.pitchPhase -= 1.0f;
                    }
                    while (self.pitchPhase < 0.0f)
                    {
                        self.pitchPhase += 1.0f;
                    }
                }
            }

            // 메아리·잔향·원음 양은 한 프레임씩 함께 돈다: 결과 = 원음 × dry + 메아리 + 잔향. 잔향은 메아리가 섞인 소리를 받는다.
            const Float echoMix = self.echoMix.load(std::memory_order_relaxed);
            float* echo = self.echoBuffer.load(std::memory_order_acquire);
            const Bool echoOn = echoMix > 0.0f && echo != nullptr && self.echoCapacity > 1;
            const Float reverbMix = self.reverbMix.load(std::memory_order_relaxed);
            Reverb* reverb = self.reverb.load(std::memory_order_acquire);
            const Bool reverbOn = reverbMix > 0.0f && reverb != nullptr;
            if (echoOn && false == self.echoWasOn)
            {
                std::memset(echo, 0, sizeof(float) * static_cast<std::size_t>(self.echoCapacity) * channels);
                self.echoWrite = 0;
            }
            if (reverbOn && false == self.reverbWasOn)
            {
                reverb->Clear();
            }
            self.echoWasOn = echoOn;
            self.reverbWasOn = reverbOn;
            const Float dry = Clamped(self.dry.load(std::memory_order_relaxed), 0.0f, 1.0f);
            if (echoOn || reverbOn || dry != 1.0f)
            {
                const Float feedback = Clamped(self.echoFeedback.load(std::memory_order_relaxed), 0.0f, 0.95f);
                UInt32 delay = 0;
                // 메아리 칸의 크기는 메아리를 켤 때만 읽는다 - 메인 스레드가 처음 잡을 때 쓰는 값이다.
                if (echoOn)
                {
                    delay = static_cast<std::uint32_t>(Clamped(self.echoDelay.load(std::memory_order_relaxed), 0.01f, MaxEchoSeconds) * rate);
                    if (delay >= self.echoCapacity)
                    {
                        delay = self.echoCapacity - 1;
                    }
                }
                const Float room = 0.7f + Clamped(self.reverbRoom.load(std::memory_order_relaxed), 0.0f, 1.0f) * 0.28f;
                const Float damping = Clamped(self.reverbDamping.load(std::memory_order_relaxed), 0.0f, 1.0f) * 0.4f;
                for (UInt32 frame = 0; frame < frames; ++frame)
                {
                    float source[2] = {0.0f, 0.0f};
                    float echoed[2] = {0.0f, 0.0f};
                    const UInt32 read = echoOn ? (self.echoWrite + self.echoCapacity - delay) % self.echoCapacity : UInt32(0);
                    for (UInt32 channel = 0; channel < channels; ++channel)
                    {
                        source[channel] = out[frame * channels + channel];
                        if (echoOn)
                        {
                            const Float delayed = echo[read * channels + channel];
                            echo[self.echoWrite * channels + channel] = source[channel] + delayed * feedback;
                            echoed[channel] = delayed * echoMix;
                        }
                    }
                    if (echoOn)
                    {
                        self.echoWrite = (self.echoWrite + 1) % self.echoCapacity;
                    }
                    float wet[2] = {0.0f, 0.0f};
                    if (reverbOn)
                    {
                        const Float left = source[0] + echoed[0];
                        const Float right = channels > 1 ? Float(source[1] + echoed[1]) : left;
                        const Float input = (left + right) * 0.5f;
                        wet[0] = reverb->Process(0, input, room, damping) * reverbMix * 3.0f;
                        wet[1] = reverb->Process(1, input, room, damping) * reverbMix * 3.0f;
                    }
                    for (UInt32 channel = 0; channel < channels; ++channel)
                    {
                        out[frame * channels + channel] = source[channel] * dry + echoed[channel] + wet[channel];
                    }
                }
            }

            // 컴프레서(D-210): 채널의 최대 크기를 어택·릴리스로 따라가고 문턱을 넘은 만큼을 비율로 줄인다.
            const Float compRatio = Clamped(self.compRatio.load(std::memory_order_relaxed), 1.0f, 20.0f);
            if (compRatio > 1.001f)
            {
                const Float threshold = Clamped(self.compThreshold.load(std::memory_order_relaxed), -60.0f, 0.0f);
                const Float attack = std::exp(-1.0f / (Clamped(self.compAttack.load(std::memory_order_relaxed), 0.0005f, 0.5f) * rate));
                const Float release = std::exp(-1.0f / (Clamped(self.compRelease.load(std::memory_order_relaxed), 0.005f, 2.0f) * rate));
                const Float makeup = std::pow(10.0f, Clamped(self.compMakeup.load(std::memory_order_relaxed), 0.0f, 24.0f) / 20.0f);
                const Float slope = 1.0f - 1.0f / compRatio;
                Float envelope = self.compEnvelope;
                Float deepest = 0.0f;
                for (UInt32 frame = 0; frame < frames; ++frame)
                {
                    Float level = 0.0f;
                    for (UInt32 channel = 0; channel < channels; ++channel)
                    {
                        level = std::fmax(level, std::fabs(out[frame * channels + channel]));
                    }
                    envelope = level > envelope ? level + attack * (envelope - level) : level + release * (envelope - level);
                    Float gain = makeup;
                    if (envelope > 1e-6f)
                    {
                        const Float over = 20.0f * std::log10(envelope) - threshold;
                        if (over > 0.0f)
                        {
                            const Float reduction = over * slope;
                            deepest = std::fmax(deepest, reduction);
                            gain *= std::pow(10.0f, -reduction / 20.0f);
                        }
                    }
                    for (UInt32 channel = 0; channel < channels; ++channel)
                    {
                        out[frame * channels + channel] *= gain;
                    }
                }
                self.compEnvelope = envelope;
                self.compReduction.store(deepest, std::memory_order_relaxed);
            }
            else
            {
                self.compReduction.store(0.0f, std::memory_order_relaxed);
            }

            self.inProcessor.store(true, std::memory_order_seq_cst);
            if (const AudioBusProcessCallback processor = self.processor.load(std::memory_order_seq_cst))
            {
                processor(self.processorUser.load(std::memory_order_seq_cst), out, frames, channels, self.sampleRate);
                // 처리기가 낸 값도 부모 버스로 올라간다 - 같은 까닭으로 거른다.
                ScrubNonFinite(out, samples);
            }
            self.inProcessor.store(false, std::memory_order_seq_cst);

            // 음량과 더킹은 사슬의 끝에서 곱한다 - 음소거하면 메아리·잔향의 꼬리도 함께 멎는다. 둘 다 샘플마다 곧게 옮겨 간다.
            const Float gainTarget = self.gainTarget.load(std::memory_order_relaxed);
            Float duckTarget = 1.0f;
            const BusEffectNode* duckSource = static_cast<const BusEffectNode*>(self.duckSource.load(std::memory_order_acquire));
            const Float duckAmount = Clamped(self.duckAmount.load(std::memory_order_relaxed), 0.0f, 1.0f);
            if (duckSource != nullptr && duckAmount > 0.0f && duckSource->peak.load(std::memory_order_relaxed) > 0.01f)
            {
                duckTarget = 1.0f - duckAmount;
            }
            if (self.gainCurrent != gainTarget || gainTarget != 1.0f || self.duckCurrent != duckTarget || duckTarget != 1.0f)
            {
                const Float gainRate = self.gainRate.load(std::memory_order_relaxed);
                const Float attack = 1.0f / (0.02f * rate);
                const Float release = 1.0f / (Clamped(self.duckRelease.load(std::memory_order_relaxed), 0.01f, 10.0f) * rate);
                Float gain = self.gainCurrent;
                Float duck = self.duckCurrent;
                for (UInt32 frame = 0; frame < frames; ++frame)
                {
                    if (gain < gainTarget)
                    {
                        gain = gain + gainRate < gainTarget ? gain + gainRate : gainTarget;
                    }
                    else if (gain > gainTarget)
                    {
                        gain = gain - gainRate > gainTarget ? gain - gainRate : gainTarget;
                    }
                    if (duck > duckTarget)
                    {
                        duck = duck - attack > duckTarget ? duck - attack : duckTarget;
                    }
                    else if (duck < duckTarget)
                    {
                        duck = duck + release < duckTarget ? duck + release : duckTarget;
                    }
                    const Float scale = gain * duck;
                    for (UInt32 channel = 0; channel < channels; ++channel)
                    {
                        out[frame * channels + channel] *= scale;
                    }
                }
                self.gainCurrent = gain;
                self.duckCurrent = duck;
            }
            self.gainNow.store(self.gainCurrent, std::memory_order_relaxed);

            Float peak = 0.0f;
            for (std::size_t index = 0; index < samples; ++index)
            {
                const Float magnitude = std::fabs(out[index]);
                if (magnitude > peak)
                {
                    peak = magnitude;
                }
            }
            self.peak.store(peak, std::memory_order_relaxed);
            // 둘째 출력은 센드다. 같은 소리를 내고 miniaudio 가 그 출력의 음량(센드 양)을 곱한다.
            if (framesOut[1] != nullptr)
            {
                std::memcpy(framesOut[1], out, sizeof(float) * samples);
            }
        }

        // 보이스 하나의 필터 노드다. 필터를 켠 보이스만 소리 → 이 노드 → 버스로 이어진다. 계수는 오디오 스레드가 짓고,
        // 보이스를 새로 시작하면 `reset` 으로 지난 소리의 필터 상태를 비운다.
        struct VoiceFilterNode
        {
            ma_node_base base;
            UInt32 channels = 2;
            UInt32 sampleRate = 48000;
            std::atomic<float> lowPass{0.0f};
            std::atomic<float> highPass{0.0f};
            std::atomic<bool> reset{false};
            Float appliedLowPass = -1.0f;
            Float appliedHighPass = -1.0f;
            Biquad lowFilter;
            Biquad highFilter;
        };

        void ProcessVoiceFilter(ma_node* node, const float** framesIn, ma_uint32* frameCountIn, float** framesOut,
            ma_uint32* frameCountOut)
        {
            VoiceFilterNode& self = *reinterpret_cast<VoiceFilterNode*>(node);
            const UInt32 channels = self.channels;
            UInt32 frames = *frameCountOut;
            if (frameCountIn != nullptr && *frameCountIn < frames)
            {
                frames = *frameCountIn;
            }
            *frameCountOut = frames;
            if (frameCountIn != nullptr)
            {
                *frameCountIn = frames;
            }
            const std::size_t samples = static_cast<std::size_t>(frames) * channels;
            float* out = framesOut[0];
            std::memcpy(out, framesIn[0], sizeof(float) * samples);
            if (self.reset.exchange(false, std::memory_order_acquire))
            {
                self.lowFilter = Biquad{};
                self.highFilter = Biquad{};
                self.appliedLowPass = -1.0f;
                self.appliedHighPass = -1.0f;
            }
            const Float rate = static_cast<float>(self.sampleRate);
            const Float highPass = self.highPass.load(std::memory_order_relaxed);
            if (highPass > 0.0f)
            {
                if (highPass != self.appliedHighPass)
                {
                    self.highFilter.Configure(true, highPass, rate);
                    self.appliedHighPass = highPass;
                }
                for (std::size_t index = 0; index < samples; ++index)
                {
                    out[index] = self.highFilter.Process(static_cast<std::uint32_t>(index % channels) & 1u, out[index]);
                }
            }
            const Float lowPass = self.lowPass.load(std::memory_order_relaxed);
            if (lowPass > 0.0f)
            {
                if (lowPass != self.appliedLowPass)
                {
                    self.lowFilter.Configure(false, lowPass, rate);
                    self.appliedLowPass = lowPass;
                }
                for (std::size_t index = 0; index < samples; ++index)
                {
                    out[index] = self.lowFilter.Process(static_cast<std::uint32_t>(index % channels) & 1u, out[index]);
                }
            }
        }

        ma_node_vtable g_voiceFilterVtable = {&ProcessVoiceFilter, nullptr, 1, 1, 0};

        // 디스크 스트리밍의 자리 하나다(D-203). 스트리머 스레드가 파일을 풀어 링에 쓰고(생산), 오디오 스레드가 읽는다(소비) -
        // 한 명씩이라 잠금 없이 원자 계수기 둘(`written`·`consumed`, 프레임 누계)로 된다. 메인 스레드는 `phase` 로만 말한다:
        // Idle 에서 Opening 으로 넘기고, 보이스를 내린 뒤 Closing 으로 넘긴다. Closing → Idle 은 스트리머만 한다.
        //
        // 위치를 옮기면(시크) 스트리머가 디코더를 옮긴 뒤 "여기부터 새 소리" 자리(`epochWritten`)와 번호(`epoch`)를 올린다.
        // 오디오 스레드는 번호가 바뀐 것을 보면 그 자리로 건너뛴다 - 옛 소리는 버려진다.
        enum class StreamPhase : std::uint32_t
        {
            Idle,
            Opening,
            Running,
            Closing
        };

        struct StreamSlot
        {
            // miniaudio 데이터 소스여야 해서 맨 앞이다.
            ma_data_source_base base;
            std::atomic<std::uint32_t> phase{static_cast<std::uint32_t>(StreamPhase::Idle)};
            char path[1024] = {};
            UInt32 channels = 0;
            UInt32 sampleRate = 0;
            UInt64 length = 0;
            std::atomic<bool> loop{false};
            OwnerPtr<Array<float>> ring;
            UInt32 capacity = 0;
            std::atomic<std::uint64_t> written{0};
            std::atomic<std::uint64_t> consumed{0};
            std::atomic<std::uint32_t> epoch{0};
            std::atomic<std::uint64_t> epochWritten{0};
            std::atomic<std::uint64_t> epochFrame{0};
            std::atomic<std::int64_t> seekRequest{-1};
            std::atomic<bool> ended{false};
            std::atomic<std::uint64_t> endWritten{0};
            std::atomic<std::uint64_t> cursor{0};
            // 스트리머가 처음 채운 뒤 참이다. 그 전의 무음은 끊김으로 세지 않는다.
            std::atomic<bool> ready{false};
            std::atomic<std::uint64_t> underruns{0};
            Bool baseReady = false;
            // 오디오 스레드만 만진다.
            UInt32 seenEpoch = 0;
            // 스트리머만 만진다.
            AudioFileDecoder decoder;
            Bool failed = false;
        };

        StreamSlot& SlotOf(ma_data_source* source)
        {
            return *reinterpret_cast<StreamSlot*>(source);
        }

        ma_result StreamRead(ma_data_source* source, void* output, ma_uint64 frameCount, ma_uint64* framesRead)
        {
            StreamSlot& slot = SlotOf(source);
            float* out = static_cast<float*>(output);
            const UInt32 channels = slot.channels;
            // `written` 을 먼저 읽는다(D-240). 스트리머는 새 세대를 알린 뒤에 새 자리의 프레임을 쓰므로, 새 프레임을 품은 `written` 을
            // 보았으면 새 세대도 보인다. 거꾸로 읽으면 옛 세대로 새 프레임을 읽고, 다음에 `consumed` 가 뒤로 가 링을 넘친다.
            const UInt64 write = slot.written.load(std::memory_order_acquire);
            const UInt32 epoch = slot.epoch.load(std::memory_order_acquire);
            if (epoch != slot.seenEpoch)
            {
                slot.consumed.store(slot.epochWritten.load(std::memory_order_relaxed), std::memory_order_release);
                slot.cursor.store(slot.epochFrame.load(std::memory_order_relaxed), std::memory_order_relaxed);
                slot.seenEpoch = epoch;
            }
            const UInt64 read = slot.consumed.load(std::memory_order_relaxed);
            const float* ring = slot.ring.Get() != nullptr ? slot.ring->Data() : nullptr;
            // 새 세대의 시작이 먼저 읽은 `written` 보다 뒤일 수 있다 - 그때는 이번에 읽을 것이 없다.
            const UInt64 available = write > read ? write - read : UInt64(0);
            UInt64 count = available < frameCount ? available : UInt64(frameCount);
            if (ring == nullptr || slot.capacity == 0)
            {
                count = 0;
            }
            for (UInt64 done = 0; done < count;)
            {
                const UInt64 at = (read + done) % slot.capacity;
                const UInt64 run = count - done < slot.capacity - at ? count - done : slot.capacity - at;
                std::memcpy(out + done * channels, ring + at * channels, sizeof(float) * run * channels);
                done += run;
            }
            slot.consumed.store(read + count, std::memory_order_release);
            UInt64 cursor = slot.cursor.load(std::memory_order_relaxed) + count;
            if (slot.length > 0 && cursor >= slot.length)
            {
                cursor = slot.loop.load(std::memory_order_relaxed) ? cursor % slot.length : slot.length;
            }
            slot.cursor.store(cursor, std::memory_order_relaxed);
            if (count < frameCount)
            {
                if (slot.ended.load(std::memory_order_acquire) && read + count >= slot.endWritten.load(std::memory_order_relaxed))
                {
                    *framesRead = count;
                    return count == 0 ? MA_AT_END : MA_SUCCESS;
                }
                // 링이 비었다 - 디스크가 늦다. 무음으로 채워 소리가 끝난 것으로 보이지 않게 한다.
                std::memset(out + count * channels, 0, sizeof(float) * static_cast<std::size_t>(frameCount - count) * channels);
                if (slot.ready.load(std::memory_order_relaxed))
                {
                    slot.underruns.fetch_add(1, std::memory_order_relaxed);
                }
            }
            *framesRead = frameCount;
            return MA_SUCCESS;
        }

        ma_result StreamSeek(ma_data_source* source, ma_uint64 frame)
        {
            SlotOf(source).seekRequest.store(static_cast<std::int64_t>(frame), std::memory_order_release);
            return MA_SUCCESS;
        }

        ma_result StreamFormat(ma_data_source* source, ma_format* format, ma_uint32* channels, ma_uint32* sampleRate,
            ma_channel* channelMap, size_t channelMapCapacity)
        {
            const StreamSlot& slot = SlotOf(source);
            *format = ma_format_f32;
            *channels = slot.channels;
            *sampleRate = slot.sampleRate;
            if (channelMap != nullptr)
            {
                ma_channel_map_init_standard(ma_standard_channel_map_default, channelMap, channelMapCapacity, slot.channels);
            }
            return MA_SUCCESS;
        }

        ma_result StreamCursor(ma_data_source* source, ma_uint64* cursor)
        {
            *cursor = SlotOf(source).cursor.load(std::memory_order_relaxed);
            return MA_SUCCESS;
        }

        ma_result StreamLength(ma_data_source* source, ma_uint64* length)
        {
            *length = SlotOf(source).length;
            return *length > 0 ? MA_SUCCESS : MA_NOT_IMPLEMENTED;
        }

        // 되풀이는 스트리머가 한다(끝에서 파일을 처음으로 돌린다). 그래서 이 소스는 되풀이하는 동안 끝을 알리지 않는다.
        ma_result StreamSetLooping(ma_data_source* source, ma_bool32 looping)
        {
            SlotOf(source).loop.store(looping != MA_FALSE, std::memory_order_relaxed);
            return MA_SUCCESS;
        }

        ma_data_source_vtable g_streamVtable = {&StreamRead, &StreamSeek, &StreamFormat, &StreamCursor, &StreamLength,
            &StreamSetLooping, 0};

        Float PositiveOrZero(Float value)
        {
            return std::isfinite(value) && value > 0.0f ? value : Float(0.0f);
        }

        // 제자리 반복 FFT(기수 2)다. 스펙트럼 창이 쓴다 - 메인 스레드, 할당 없음.
        void Fft(float* real, float* imag, UInt32 size)
        {
            for (UInt32 index = 1, reversed = 0; index < size; ++index)
            {
                UInt32 bit = size >> 1;
                for (; (reversed & bit) != 0; bit >>= 1)
                {
                    reversed ^= bit;
                }
                reversed ^= bit;
                if (index < reversed)
                {
                    const Float swapReal = real[index];
                    real[index] = real[reversed];
                    real[reversed] = swapReal;
                    const Float swapImag = imag[index];
                    imag[index] = imag[reversed];
                    imag[reversed] = swapImag;
                }
            }
            for (UInt32 length = 2; length <= size; length <<= 1)
            {
                const Float angle = -Tau / static_cast<float>(length);
                const Float stepReal = std::cos(angle);
                const Float stepImag = std::sin(angle);
                for (UInt32 start = 0; start < size; start += length)
                {
                    Float twiddleReal = 1.0f;
                    Float twiddleImag = 0.0f;
                    for (UInt32 offset = 0; offset < length / 2; ++offset)
                    {
                        const UInt32 even = start + offset;
                        const UInt32 odd = even + length / 2;
                        const Float oddReal = real[odd] * twiddleReal - imag[odd] * twiddleImag;
                        const Float oddImag = real[odd] * twiddleImag + imag[odd] * twiddleReal;
                        real[odd] = real[even] - oddReal;
                        imag[odd] = imag[even] - oddImag;
                        real[even] += oddReal;
                        imag[even] += oddImag;
                        const Float nextReal = twiddleReal * stepReal - twiddleImag * stepImag;
                        twiddleImag = twiddleReal * stepImag + twiddleImag * stepReal;
                        twiddleReal = nextReal;
                    }
                }
            }
        }

        // 출력은 둘이다: 0 은 부모 버스, 1 은 센드(끊겨 있으면 miniaudio 가 읽지 않는다).
        ma_node_vtable g_busEffectVtable = {
            &ProcessBusEffects,
            nullptr,
            1,
            2,
            MA_NODE_FLAG_CONTINUOUS_PROCESSING | MA_NODE_FLAG_ALLOW_NULL_INPUT};
    }

    struct AudioMixer::State
    {
        static constexpr UInt32 NoStream = 0xFFFFFFFFu;

        enum class VoiceState : std::uint8_t
        {
            Free,
            Playing,
            Paused,
            Stopping
        };

        struct Voice
        {
            ma_sound sound = {};
            ma_audio_buffer_ref buffer = {};
            ma_decoder decoder = {};
            Bool usesDecoder = false;
            UInt32 generation = 1;
            VoiceState state = VoiceState::Free;
            AudioClipHandle clip;
            AudioBusId bus = AudioMasterBus;
            std::uint8_t priority = 0;
            Bool looping = false;
            Float volume = 1.0f;
            // 클립의 트림(D-205). 실제로 거는 음량은 `volume * trim` 이다.
            Float trim = 1.0f;
            UInt64 startSerial = 0;
            UInt32 tag = 0;
            // 보이스마다 하나, 초기화 때 만들어 둔다. 필터를 켠 동안만 소리와 버스 사이에 선다(`filterRouted`).
            OwnerPtr<VoiceFilterNode> filter;
            Bool filterReady = false;
            // 디스크 스트리밍 보이스면 그 자리 번호다.
            UInt32 streamSlot = NoStream;
            Bool filterRouted = false;
            // 들리는 크기를 메인 스레드에서 잰다(D-235). 공간화 값은 시작 때와 `SetPosition` 이 적는다.
            Float pitch = 1.0f;
            Bool spatial = false;
            AudioAttenuation attenuation = AudioAttenuation::Inverse;
            Float minDistance = 1.0f;
            Float maxDistance = 50.0f;
            Float rolloff = 1.0f;
            float position[3] = {0.0f, 0.0f, 0.0f};
            // 가상 보이스(D-235). 가상이면 `ma_sound` 는 멈춰 있고 재생 위치는 `parkCursor`(클립 프레임)에 `parkTime`(믹서 프레임)
            // 부터 흐른 시간 × 피치를 더한 값이다. `lastSwitch` 는 마지막으로 바뀐 믹서 시각이다.
            Bool parked = false;
            Bool everSwitched = false;
            double parkCursor = 0.0;
            UInt64 parkTime = 0;
            UInt64 lastSwitch = 0;
        };

        struct Bus
        {
            ma_sound_group group = {};
            BusEffectNode effects;
            Bool effectsReady = false;
            OwnerPtr<Array<float>> echoStorage;
            OwnerPtr<Reverb> reverbStorage;
            OwnerPtr<Array<float>> chorusStorage;
            OwnerPtr<Array<float>> pitchStorage;
            AudioBusEffects settings;
            Bool used = false;
            Bool muted = false;
            Bool solo = false;
            Float volume = 1.0f;
            // 음소거·솔로를 반영해 그룹에 실제로 건 값이다. 보이스를 훔칠 때 들리는 크기로 쓴다.
            Float effectiveGain = 1.0f;
            AudioBusId parent = AudioMasterBus;
            AudioBusId sendTarget = AudioNoBus;
            Float sendLevel = 0.0f;
            AudioBusId duckTrigger = AudioNoBus;
        };

        struct Clip
        {
            AudioClipDesc desc;
            UInt32 generation = 1;
            Bool used = false;
            // 마지막으로 시작한 믹서 시각(PCM 프레임)이다. 쿨다운이 본다(D-231).
            Bool started = false;
            UInt64 lastStartFrame = 0;
        };

        AudioMixerDesc desc;
        Bool initialized = false;
        FixedAllocator allocator;
        ma_allocation_callbacks callbacks = {};
        ma_engine engine = {};
        Bus buses[AudioMaxBuses];
        UInt32 busCount = 0;
        // 크기는 초기화 때 한 번 정하고 다시 늘리지 않는다 - 노드 그래프가 `ma_sound` 의 주소를 들고 있다.
        Array<Voice> voices;
        Array<UInt32> freeVoices;
        Array<Clip> clips;
        Array<UInt32> freeClips;
        UInt64 serial = 0;
        UInt64 voicesStarted = 0;
        UInt64 voicesStolen = 0;
        UInt64 voicesRejected = 0;
        UInt64 voicesCulled = 0;
        UInt64 voicesThrottled = 0;
        UInt64 voicesReplaced = 0;
        UInt64 voicesVirtualized = 0;
        UInt64 voicesRealized = 0;
        // 가상 보이스의 순위를 매기는 칸이다. 초기화 때 보이스 수만큼 잡아 두어 매 프레임 할당하지 않는다.
        Array<UInt32> rankOrder;
        Array<float> rankLoudness;
        std::atomic<float> peak{0.0f};
        std::atomic<std::uint64_t> renderedFrames{0};
        Float masterVolume = 1.0f;
        // 출력 이득(포커스 정책). 목표와 프레임당 변화량은 메인 스레드가 쓰고 지금 값은 오디오 스레드만 만진다.
        std::atomic<float> outputGainTarget{1.0f};
        std::atomic<float> outputGainRate{1.0f};
        Float outputGainCurrent = 1.0f;
        // 출력 리미터(D-210). 켬·천장은 메인 스레드가, 지금 이득은 오디오 스레드가 든다.
        std::atomic<bool> limiterEnabled{true};
        std::atomic<float> limiterCeiling{0.98f};
        Float limiterGain = 1.0f;
        // 최근 출력(채널 평균)이다. 오디오 스레드가 쓰고 메인 스레드는 복사만 한다.
        float recent[RecentCapacity] = {};
        std::atomic<std::uint32_t> recentWrite{0};
        static constexpr UInt32 FftSize = 2048;
        // 스펙트럼 계산의 작업 칸이다. `const` 조회에서 쓰므로 mutable 이다(메인 스레드 전용).
        mutable float fftReal[FftSize] = {};
        mutable float fftImag[FftSize] = {};
        // 디스크 스트리밍(D-203). 자리는 초기화 때 만들고(데이터 소스), 링은 그 자리를 처음 쓸 때 잡는다.
        Array<OwnerPtr<StreamSlot>> streams;
        std::thread streamer;
        std::atomic<bool> streamerRunning{false};
        Bool warnedStreams = false;

        // 스트리머 스레드다. 자리를 돌며 열고, 옮기고, 채우고, 닫는다. 할 일이 없으면 2 ms 쉰다.
        void RunStreamer()
        {
            while (streamerRunning.load(std::memory_order_acquire))
            {
                Bool busy = false;
                for (OwnerPtr<StreamSlot>& owned : streams)
                {
                    busy = ServiceStream(*owned) || busy;
                }
                if (false == busy)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            }
        }

        Bool ServiceStream(StreamSlot& slot)
        {
            const StreamPhase phase = static_cast<StreamPhase>(slot.phase.load(std::memory_order_acquire));
            if (phase == StreamPhase::Opening)
            {
                Bool opened = desc.openStream != nullptr && desc.openStream(desc.openStreamUser, slot.path, slot.decoder);
                if (opened)
                {
                    // 클립이 채널 1 을 말하면 모노 에셋이다(D-231) - 평균해 읽는다.
                    if (slot.channels == 1)
                    {
                        slot.decoder.SetMono();
                    }
                    const AudioFormat format = slot.decoder.GetFormat();
                    opened = format.channels == slot.channels && format.sampleRate == slot.sampleRate;
                }
                slot.failed = false == opened;
                if (slot.failed)
                {
                    // 열지 못한 파일은 곧 끝난 소리다 - 보이스가 다음 갱신에서 거둬진다.
                    slot.decoder.Close();
                    slot.endWritten.store(slot.written.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    slot.ended.store(true, std::memory_order_release);
                }
                else
                {
                    FillStream(slot);
                }
                slot.ready.store(true, std::memory_order_relaxed);
                std::uint32_t expected = static_cast<std::uint32_t>(StreamPhase::Opening);
                if (false == slot.phase.compare_exchange_strong(expected, static_cast<std::uint32_t>(StreamPhase::Running),
                        std::memory_order_acq_rel))
                {
                    CloseStream(slot);
                }
                return true;
            }
            if (phase == StreamPhase::Running)
            {
                Bool busy = false;
                const Int64 seek = slot.seekRequest.exchange(-1, std::memory_order_acq_rel);
                if (seek >= 0 && false == slot.failed)
                {
                    // 옮긴 뒤 첫 채움 전의 무음은 디스크가 늦은 것이 아니다 - 끊김으로 세지 않는다(D-240).
                    slot.ready.store(false, std::memory_order_relaxed);
                    slot.decoder.Seek(static_cast<std::uint64_t>(seek));
                    slot.ended.store(false, std::memory_order_relaxed);
                    slot.epochWritten.store(slot.written.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    slot.epochFrame.store(static_cast<std::uint64_t>(seek), std::memory_order_relaxed);
                    slot.epoch.fetch_add(1, std::memory_order_release);
                    busy = true;
                }
                if (false == slot.failed && false == slot.ended.load(std::memory_order_relaxed))
                {
                    busy = FillStream(slot) || busy;
                }
                slot.ready.store(true, std::memory_order_relaxed);
                return busy;
            }
            if (phase == StreamPhase::Closing)
            {
                CloseStream(slot);
                return true;
            }
            return false;
        }

        void CloseStream(StreamSlot& slot)
        {
            slot.decoder.Close();
            slot.failed = false;
            slot.phase.store(static_cast<std::uint32_t>(StreamPhase::Idle), std::memory_order_release);
        }

        // 링의 빈자리를 채운다. 되풀이면 끝에서 처음으로 돌리고, 아니면 끝을 알린다. 채웠으면 참이다.
        Bool FillStream(StreamSlot& slot)
        {
            float* ring = slot.ring->Data();
            const UInt32 channels = slot.channels;
            UInt64 write = slot.written.load(std::memory_order_relaxed);
            Bool filled = false;
            Int32 emptyReads = 0;
            for (;;)
            {
                const UInt64 read = slot.consumed.load(std::memory_order_acquire);
                const UInt64 used = write - read;
                if (used >= slot.capacity)
                {
                    break;
                }
                const UInt64 free = slot.capacity - used;
                // 조금씩 자주 쓰기보다 덩어리로 쓴다 - 링의 1/8 이 비기 전에는 기다린다(처음 채울 때는 예외).
                if (filled == false && write != 0 && free < slot.capacity / 8)
                {
                    break;
                }
                const UInt64 at = write % slot.capacity;
                UInt64 run = free < slot.capacity - at ? free : slot.capacity - at;
                run = run > 4096 ? UInt64(4096) : run;
                const UInt64 got = slot.decoder.Read(ring + at * channels, run);
                if (got == 0)
                {
                    if (slot.loop.load(std::memory_order_relaxed) && ++emptyReads < 2 && slot.decoder.Seek(0))
                    {
                        continue;
                    }
                    slot.endWritten.store(write, std::memory_order_relaxed);
                    slot.ended.store(true, std::memory_order_release);
                    break;
                }
                emptyReads = 0;
                write += got;
                slot.written.store(write, std::memory_order_release);
                filled = true;
            }
            return filled;
        }

        void StartStreamer()
        {
            if (false == streamerRunning.load(std::memory_order_relaxed))
            {
                streamerRunning.store(true, std::memory_order_release);
                streamer = std::thread([this] { RunStreamer(); });
            }
        }

        void StopStreamer()
        {
            if (streamerRunning.exchange(false, std::memory_order_acq_rel))
            {
                streamer.join();
            }
            for (OwnerPtr<StreamSlot>& owned : streams)
            {
                owned->decoder.Close();
                owned->phase.store(static_cast<std::uint32_t>(StreamPhase::Idle), std::memory_order_relaxed);
            }
        }

        Voice* Resolve(AudioVoiceHandle handle)
        {
            if (false == initialized || handle.index >= voices.Size())
            {
                return nullptr;
            }
            Voice& voice = voices[handle.index];
            if (voice.generation != handle.generation || voice.state == VoiceState::Free)
            {
                return nullptr;
            }
            return &voice;
        }

        const Voice* Resolve(AudioVoiceHandle handle) const
        {
            return const_cast<State*>(this)->Resolve(handle);
        }

        const Clip* ResolveClip(AudioClipHandle handle) const
        {
            if (false == initialized || handle.index >= clips.Size())
            {
                return nullptr;
            }
            const Clip& clip = clips[handle.index];
            return clip.used && clip.generation == handle.generation ? &clip : nullptr;
        }

        Bool IsBusValid(AudioBusId bus) const
        {
            return bus < AudioMaxBuses && buses[bus].used;
        }

        // 보이스를 곧바로 내리고 자리를 돌려준다. `ma_sound_uninit` 은 오디오 스레드가 이 노드를 다 읽을 때까지
        // 기다린 뒤 돌아온다(miniaudio 7.2 절) - 그래서 돌아온 뒤에는 클립 메모리를 풀어도 된다.
        void ReleaseVoice(UInt32 index)
        {
            Voice& voice = voices[index];
            if (voice.state == VoiceState::Free)
            {
                return;
            }
            ma_sound_uninit(&voice.sound);
            if (voice.usesDecoder)
            {
                ma_decoder_uninit(&voice.decoder);
                voice.usesDecoder = false;
            }
            if (voice.filterRouted)
            {
                ma_node_detach_output_bus(reinterpret_cast<ma_node*>(voice.filter.Get()), 0);
                voice.filterRouted = false;
            }
            if (voice.streamSlot != NoStream)
            {
                // 오디오 스레드는 이미 이 소스를 읽지 않는다(`ma_sound_uninit`). 닫기는 스트리머가 한다.
                streams[voice.streamSlot]->phase.store(static_cast<std::uint32_t>(StreamPhase::Closing), std::memory_order_release);
                voice.streamSlot = NoStream;
            }
            voice.state = VoiceState::Free;
            voice.clip = {};
            voice.parked = false;
            voice.everSwitched = false;
            ++voice.generation;
            if (voice.generation == 0)
            {
                voice.generation = 1;
            }
            freeVoices.Add(index);
        }

        // 지금 들리는 크기다. 공간화한 보이스는 거리 감쇠를 곱한다(D-235 - 전에는 거리를 보지 않아 먼 소리와 가까운 소리가
        // 같았다). 가상 보이스는 들리지 않으므로 0 이다 - 자리가 모자랄 때 먼저 훔쳐진다.
        Float Audibility(const Voice& voice) const
        {
            if (voice.parked)
            {
                return 0.0f;
            }
            return Loudness(voice);
        }

        // 가상이든 아니든 실제로 섞으면 들릴 크기다. 가상 보이스의 순위를 매긴다(D-235).
        Float Loudness(const Voice& voice) const
        {
            const Float gain = voice.volume * voice.trim * buses[voice.bus].effectiveGain;
            if (false == voice.spatial)
            {
                return gain;
            }
            return gain * DistanceGain(voice.attenuation, voice.minDistance, voice.maxDistance, voice.rolloff, voice.position);
        }

        Bool HasIdleStream() const
        {
            for (const OwnerPtr<StreamSlot>& slot : streams)
            {
                if (slot->baseReady && slot->phase.load(std::memory_order_acquire) == static_cast<std::uint32_t>(StreamPhase::Idle))
                {
                    return true;
                }
            }
            return false;
        }

        Bool VirtualizationEnabled() const
        {
            return desc.maxAudibleVoices > 0 && desc.maxAudibleVoices < desc.maxVoices;
        }

        // 가상이 될 수 있는 보이스다: 재생 중인 루프이고 디스크 스트리밍이 아니다.
        static Bool CanPark(const Voice& voice)
        {
            return voice.state == VoiceState::Playing && voice.looping && voice.streamSlot == NoStream;
        }

        // 실제로 섞고 있는 보이스다(줄여 끄는 중인 것 포함).
        static Bool IsMixing(const Voice& voice)
        {
            return (voice.state == VoiceState::Playing || voice.state == VoiceState::Stopping) && false == voice.parked;
        }

        UInt32 CountMixing() const
        {
            UInt32 count = 0;
            for (const Voice& voice : voices)
            {
                count += IsMixing(voice) ? 1u : 0u;
            }
            return count;
        }

        UInt64 Now() const
        {
            return ma_engine_get_time_in_pcm_frames(&engine);
        }

        // 가상 보이스의 지금 재생 위치(클립 프레임)다. 멈춘 동안은 흐르지 않고, 루프이므로 길이로 감는다.
        double VirtualCursor(const Voice& voice, UInt64 now) const
        {
            const Clip* clip = ResolveClip(voice.clip);
            if (clip == nullptr || clip->desc.sampleRate == 0)
            {
                return voice.parkCursor;
            }
            double cursor = voice.parkCursor;
            if (voice.state == VoiceState::Playing && now > voice.parkTime)
            {
                cursor += static_cast<double>(now - voice.parkTime) * static_cast<double>(voice.pitch)
                    * static_cast<double>(clip->desc.sampleRate) / static_cast<double>(desc.sampleRate);
            }
            const double length = static_cast<double>(clip->desc.frameCount);
            if (length > 0.0 && cursor >= length)
            {
                cursor = std::fmod(cursor, length);
            }
            return cursor;
        }

        // 가상으로 돌린다: 지금 위치를 기준으로 적고 20 ms 에 줄여 멈춘다. 자리·핸들·디코더는 그대로다.
        void Park(Voice& voice, UInt64 now)
        {
            ma_uint64 cursor = 0;
            ma_sound_get_cursor_in_pcm_frames(&voice.sound, &cursor);
            voice.parkCursor = static_cast<double>(cursor);
            voice.parkTime = now;
            // 아직 시작 지연 중이면 처음부터, 지연이 끝나는 때부터 센다(D-240) - 되살렸을 때 앞부분을 건너뛰지 않게.
            const ma_uint64 startsAt = ma_node_get_state_time(&voice.sound, ma_node_state_started);
            if (startsAt > now)
            {
                voice.parkCursor = 0.0;
                voice.parkTime = startsAt;
            }
            voice.lastSwitch = now;
            voice.everSwitched = true;
            voice.parked = true;
            ma_sound_stop_with_fade_in_milliseconds(&voice.sound, 20);
            ++voicesVirtualized;
        }

        // 실제로 되돌린다: 센 자리로 옮기고 20 ms 페이드인으로 튼다. 옮기기는 목표만 적는다(miniaudio `seekTarget`).
        void Unpark(Voice& voice, UInt64 now)
        {
            const double cursor = VirtualCursor(voice, now);
            ma_sound_stop(&voice.sound);
            ma_sound_reset_stop_time_and_fade(&voice.sound);
            ma_sound_seek_to_pcm_frame(&voice.sound, static_cast<ma_uint64>(cursor));
            ma_sound_set_fade_in_milliseconds(&voice.sound, 0.0f, 1.0f, 20);
            voice.parked = false;
            voice.lastSwitch = now;
            voice.everSwitched = true;
            ma_sound_start(&voice.sound);
            ++voicesRealized;
        }

        // 섞는 수가 찼을 때 한 번짜리에게 자리를 내줄 루프다: 가장 약한 실제 루프(우선순위가 낮은 것, 같으면 작게 들리는 것).
        // 머무는 시간은 보지 않는다 - 새 소리가 우선이다. 새 소리보다 우선순위가 높은 루프는 밀어내지 않는다(D-240) - 훔치기(`PickVictim`)와 같은 규칙이다. `except` 는 이번 재생이
        // 이미 훔치기로 고른 보이스다.
        UInt32 FindWeakestLoop(std::uint8_t incomingPriority, UInt32 except) const
        {
            UInt32 weakest = static_cast<std::uint32_t>(-1);
            Float weakestLoudness = 0.0f;
            for (UInt32 index = 0; index < voices.Size(); ++index)
            {
                const Voice& voice = voices[index];
                if (index == except || false == CanPark(voice) || voice.parked || voice.priority > incomingPriority)
                {
                    continue;
                }
                const Float loudness = Loudness(voice);
                if (weakest == static_cast<std::uint32_t>(-1) || voice.priority < voices[weakest].priority
                    || (voice.priority == voices[weakest].priority && loudness < weakestLoudness))
                {
                    weakest = index;
                    weakestLoudness = loudness;
                }
            }
            return weakest;
        }

        // 프레임마다 루프에 섞는 자리를 나눠 준다(D-235). 한 번짜리·스트리밍이 쓰고 남은 자리를 우선순위 → 들리는 크기 순으로
        // 준다. -60 dB 밑은 자리가 남아도 가상이다. 바뀐 지 `AudioVirtualDwellSeconds` 가 안 된 것은 그대로 둔다.
        void Virtualize()
        {
            if (false == VirtualizationEnabled())
            {
                return;
            }
            const UInt64 now = Now();
            const UInt64 dwell = static_cast<std::uint64_t>(AudioVirtualDwellSeconds * static_cast<float>(desc.sampleRate));
            const auto settled = [now, dwell](const Voice& voice) {
                // 아직 시작 지연 중인 가상 보이스도 그대로 둔다 - 되살리면 지연보다 먼저 울린다.
                return (voice.everSwitched && now - voice.lastSwitch < dwell) || (voice.parked && voice.parkTime > now);
            };
            UInt32 required = 0;
            UInt32 candidates = 0;
            for (UInt32 index = 0; index < voices.Size(); ++index)
            {
                Voice& voice = voices[index];
                // 가상인 채 루프가 풀렸으면 곧바로 되돌린다 - 끝까지 울고 거둬져야 한다.
                if (voice.parked && voice.state == VoiceState::Playing && false == voice.looping)
                {
                    Unpark(voice, now);
                }
                if (CanPark(voice))
                {
                    rankOrder[candidates] = index;
                    rankLoudness[index] = Loudness(voice);
                    ++candidates;
                }
                else if (IsMixing(voice))
                {
                    ++required;
                }
            }
            std::sort(rankOrder.Data(), rankOrder.Data() + candidates, [this](UInt32 a, UInt32 b) {
                const Voice& left = voices[a];
                const Voice& right = voices[b];
                if (left.priority != right.priority)
                {
                    return left.priority > right.priority;
                }
                if (rankLoudness[a] != rankLoudness[b])
                {
                    return rankLoudness[a] > rankLoudness[b];
                }
                return left.startSerial < right.startSerial;
            });
            const UInt32 budget = desc.maxAudibleVoices > required ? desc.maxAudibleVoices - required : UInt32(0);
            // 머무느라 가상이 못 된 루프가 자리를 쥐고 있으면 그만큼 순위의 자리가 준다 - 섞는 수를 넘지 않는다.
            UInt32 used = 0;
            for (UInt32 rank = 0; rank < candidates; ++rank)
            {
                const Voice& voice = voices[rankOrder[rank]];
                if (settled(voice) && false == voice.parked)
                {
                    ++used;
                }
            }
            for (UInt32 rank = 0; rank < candidates; ++rank)
            {
                Voice& voice = voices[rankOrder[rank]];
                if (settled(voice))
                {
                    continue;
                }
                const Bool wantReal = used < budget && rankLoudness[rankOrder[rank]] >= CullGain;
                if (wantReal)
                {
                    ++used;
                    if (voice.parked)
                    {
                        Unpark(voice, now);
                    }
                }
                else if (false == voice.parked)
                {
                    Park(voice, now);
                }
            }
        }

        // 보이스를 버스에 잇는다. 필터를 켠 보이스는 소리 → 필터 → 버스, 아니면 소리 → 버스다. 재생 중에도 스레드 안전하다.
        void RouteVoice(Voice& voice, AudioBusId bus, Bool filtered)
        {
            ma_node* group = reinterpret_cast<ma_node*>(&buses[bus].group);
            ma_node* filter = reinterpret_cast<ma_node*>(voice.filter.Get());
            if (filtered && voice.filterReady)
            {
                ma_node_attach_output_bus(filter, 0, group, 0);
                if (false == voice.filterRouted)
                {
                    ma_node_attach_output_bus(&voice.sound, 0, filter, 0);
                    voice.filterRouted = true;
                }
            }
            else
            {
                ma_node_attach_output_bus(&voice.sound, 0, group, 0);
                if (voice.filterRouted)
                {
                    ma_node_detach_output_bus(filter, 0);
                    voice.filterRouted = false;
                }
            }
            voice.bus = bus;
        }

        // `from` 에서 부모와 센드를 따라가면 `to` 에 닿는가. 센드를 이을 때 되돌아오는 길을 막는다.
        Bool Reaches(AudioBusId from, AudioBusId to) const
        {
            AudioBusId stack[AudioMaxBuses * 2 + 1];
            Bool seen[AudioMaxBuses] = {};
            UInt32 top = 0;
            stack[top++] = from;
            while (top > 0)
            {
                const AudioBusId bus = stack[--top];
                if (bus == to)
                {
                    return true;
                }
                if (bus >= AudioMaxBuses || seen[bus] || false == buses[bus].used)
                {
                    continue;
                }
                seen[bus] = true;
                if (bus != AudioMasterBus && bus != AudioEditorPreviewBus)
                {
                    stack[top++] = buses[bus].parent;
                }
                if (buses[bus].sendTarget != AudioNoBus)
                {
                    stack[top++] = buses[bus].sendTarget;
                }
            }
            return false;
        }

        Bool IsDescendantOf(AudioBusId bus, AudioBusId ancestor) const
        {
            for (UInt32 guard = 0; guard < AudioMaxBuses && bus >= AudioFirstProjectBus && buses[bus].used; ++guard)
            {
                bus = buses[bus].parent;
                if (bus == ancestor)
                {
                    return true;
                }
            }
            return false;
        }

        // 음소거·솔로를 반영해 모든 버스의 음량 목표를 다시 건다. 버스는 열여덟 개뿐이라 매번 전부 센다. `fadingBus` 는
        // `fadeSeconds` 에 걸쳐, 나머지는 10 ms 에 걸쳐 옮긴다(D-205).
        void ApplyBusGains(AudioBusId fadingBus = AudioNoBus, Float fadeSeconds = 0.0f)
        {
            Bool audible[AudioMaxBuses] = {};
            Bool anySolo = false;
            for (UInt32 bus = AudioFirstProjectBus; bus < AudioMaxBuses; ++bus)
            {
                anySolo = anySolo || (buses[bus].used && buses[bus].solo);
            }
            for (UInt32 bus = 0; bus < AudioMaxBuses; ++bus)
            {
                audible[bus] = false == anySolo || bus < AudioFirstProjectBus;
            }
            if (anySolo)
            {
                // 솔로 버스와 그 자식, 그들이 센드로 보내는 버스(되풀이), 그리고 그 모두의 조상이다.
                for (UInt32 bus = AudioFirstProjectBus; bus < AudioMaxBuses; ++bus)
                {
                    if (false == buses[bus].used)
                    {
                        continue;
                    }
                    for (UInt32 solo = AudioFirstProjectBus; solo < AudioMaxBuses; ++solo)
                    {
                        if (buses[solo].used && buses[solo].solo
                            && (solo == bus || IsDescendantOf(static_cast<AudioBusId>(bus), static_cast<AudioBusId>(solo))))
                        {
                            audible[bus] = true;
                        }
                    }
                }
                for (UInt32 pass = 0; pass < AudioMaxBuses; ++pass)
                {
                    for (UInt32 bus = AudioFirstProjectBus; bus < AudioMaxBuses; ++bus)
                    {
                        const AudioBusId target = buses[bus].sendTarget;
                        if (audible[bus] && buses[bus].used && target != AudioNoBus && target < AudioMaxBuses)
                        {
                            audible[target] = true;
                        }
                    }
                }
                for (UInt32 bus = AudioFirstProjectBus; bus < AudioMaxBuses; ++bus)
                {
                    if (false == audible[bus] || false == buses[bus].used)
                    {
                        continue;
                    }
                    AudioBusId up = buses[bus].parent;
                    for (UInt32 guard = 0; guard < AudioMaxBuses && up < AudioMaxBuses; ++guard)
                    {
                        audible[up] = true;
                        if (up < AudioFirstProjectBus)
                        {
                            break;
                        }
                        up = buses[up].parent;
                    }
                }
            }
            for (UInt32 bus = 0; bus < AudioMaxBuses; ++bus)
            {
                Bus& target = buses[bus];
                if (false == target.used)
                {
                    continue;
                }
                const Float gain = target.muted || false == audible[bus] ? Float(0.0f) : target.volume;
                if (gain != target.effectiveGain)
                {
                    const Float seconds = bus == fadingBus && fadeSeconds > 0.01f ? fadeSeconds : Float(0.01f);
                    Float distance = std::fabs(gain - target.effects.gainNow.load(std::memory_order_relaxed));
                    distance = distance > 0.0f ? distance : Float(1.0f);
                    target.effects.gainRate.store(distance / (seconds * static_cast<float>(desc.sampleRate)),
                        std::memory_order_relaxed);
                    target.effects.gainTarget.store(gain, std::memory_order_relaxed);
                    target.effectiveGain = gain;
                }
            }
        }

        // 이 밑이면 들리지 않는 것으로 본다(-60 dB, D-231).
        static constexpr Float CullGain = 0.001f;

        // 듣는 자리에서의 거리 감쇠다(D-231). miniaudio 의 감쇠 공식(`ma_attenuation_*`)을 그대로 따른다 - 최대 거리
        // 밖은 최대 거리의 값이므로 역·지수 감쇠는 멀어도 0 이 되지 않는다. 원뿔은 더 줄일 뿐이라 보지 않는다(보수적).
        Float DistanceGain(AudioAttenuation attenuation, Float minDistance, Float maxDistance, Float rolloff,
            const float position[3]) const
        {
            if (attenuation == AudioAttenuation::None)
            {
                return 1.0f;
            }
            const ma_vec3f listener = ma_engine_listener_get_position(&engine, 0);
            const Float dx = position[0] - listener.x;
            const Float dy = position[1] - listener.y;
            const Float dz = position[2] - listener.z;
            const Float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (false == std::isfinite(distance) || minDistance >= maxDistance)
            {
                return 1.0f;
            }
            const Float clamped = distance < minDistance ? minDistance : (distance > maxDistance ? maxDistance : distance);
            Float gain = 1.0f;
            if (attenuation == AudioAttenuation::Linear)
            {
                gain = 1.0f - rolloff * (clamped - minDistance) / (maxDistance - minDistance);
            }
            else if (attenuation == AudioAttenuation::Exponential)
            {
                gain = std::pow(clamped / minDistance, -rolloff);
            }
            else
            {
                gain = minDistance / (minDistance + rolloff * (clamped - minDistance));
            }
            return gain > 0.0f ? gain : Float(0.0f);
        }

        // 시작하는 자리에서 들리지 않는가(D-231). 공간화한 한 번짜리만 거른다 - 루프는 가상 보이스(D-235)가 맡는다.
        Bool IsInaudibleAtStart(const AudioPlayDesc& play, Float trim, Float minDistance, Float maxDistance) const
        {
            if (false == play.spatial || play.loop)
            {
                return false;
            }
            const Float gain = DistanceGain(play.attenuation, minDistance, maxDistance, SafePositive(play.rolloff, 1.0f),
                play.position);
            return Clamp01(play.volume) * trim * gain < CullGain;
        }

        // 훔칠 보이스다. 우선순위가 가장 낮은 것, 같으면 작게 들리는 것, 같으면 가장 오래된 것이다. 새 보이스보다
        // 우선순위가 높은 것만 남았으면 훔치지 않는다. 결정적이다 - 같은 상태에서 늘 같은 것을 고른다.
        // `mixingOnly` 면 실제로 섞는 보이스만 본다 - 섞는 수가 찼을 때(D-235).
        UInt32 PickVictim(std::uint8_t incomingPriority, Bool mixingOnly = false,
            UInt32 except = static_cast<std::uint32_t>(-1)) const
        {
            UInt32 best = static_cast<std::uint32_t>(-1);
            for (UInt32 index = 0; index < voices.Size(); ++index)
            {
                const Voice& voice = voices[index];
                if (index == except || voice.state == VoiceState::Free || voice.priority > incomingPriority
                    || (mixingOnly && false == IsMixing(voice)))
                {
                    continue;
                }
                if (best == static_cast<std::uint32_t>(-1))
                {
                    best = index;
                    continue;
                }
                const Voice& current = voices[best];
                if (voice.priority != current.priority)
                {
                    if (voice.priority < current.priority)
                    {
                        best = index;
                    }
                    continue;
                }
                const Float audibility = Audibility(voice);
                const Float currentAudibility = Audibility(current);
                if (audibility != currentAudibility)
                {
                    if (audibility < currentAudibility)
                    {
                        best = index;
                    }
                    continue;
                }
                if (voice.startSerial < current.startSerial)
                {
                    best = index;
                }
            }
            return best;
        }

        // `parentBus` 가 `AudioNoBus` 면 엔드포인트에 곧장 잇는다(Master·미리 듣기).
        // 버스 자리를 다시 쓸 때 옛 버스가 남긴 것을 비운다(D-240). 노드가 그래프에 붙기 전이라 오디오 스레드가 읽지 않는다.
        static void ResetBusEffects(BusEffectNode& node)
        {
            const AudioBusEffects defaults;
            node.lowPass.store(defaults.lowPassHz, std::memory_order_relaxed);
            node.highPass.store(defaults.highPassHz, std::memory_order_relaxed);
            node.echoDelay.store(defaults.echoDelay, std::memory_order_relaxed);
            node.echoFeedback.store(defaults.echoFeedback, std::memory_order_relaxed);
            node.echoMix.store(defaults.echoMix, std::memory_order_relaxed);
            node.reverbRoom.store(defaults.reverbRoom, std::memory_order_relaxed);
            node.reverbDamping.store(defaults.reverbDamping, std::memory_order_relaxed);
            node.reverbMix.store(defaults.reverbMix, std::memory_order_relaxed);
            node.dry.store(defaults.dry, std::memory_order_relaxed);
            node.eqLowHz.store(defaults.eqLowHz, std::memory_order_relaxed);
            node.eqLowGain.store(defaults.eqLowGain, std::memory_order_relaxed);
            node.eqMidHz.store(defaults.eqMidHz, std::memory_order_relaxed);
            node.eqMidGain.store(defaults.eqMidGain, std::memory_order_relaxed);
            node.eqHighHz.store(defaults.eqHighHz, std::memory_order_relaxed);
            node.eqHighGain.store(defaults.eqHighGain, std::memory_order_relaxed);
            node.distortion.store(defaults.distortion, std::memory_order_relaxed);
            node.distortionMix.store(defaults.distortionMix, std::memory_order_relaxed);
            node.chorusMix.store(defaults.chorusMix, std::memory_order_relaxed);
            node.chorusRate.store(defaults.chorusRate, std::memory_order_relaxed);
            node.chorusDepth.store(defaults.chorusDepth, std::memory_order_relaxed);
            node.pitchShift.store(defaults.pitchShift, std::memory_order_relaxed);
            node.compRatio.store(defaults.compRatio, std::memory_order_relaxed);
            node.compThreshold.store(defaults.compThreshold, std::memory_order_relaxed);
            node.compAttack.store(defaults.compAttack, std::memory_order_relaxed);
            node.compRelease.store(defaults.compRelease, std::memory_order_relaxed);
            node.compMakeup.store(defaults.compMakeup, std::memory_order_relaxed);
            node.compReduction.store(0.0f, std::memory_order_relaxed);
            node.appliedLowPass = -1.0f;
            node.appliedHighPass = -1.0f;
            node.lowFilter = {};
            node.highFilter = {};
            node.eqLow = {};
            node.eqMid = {};
            node.eqHigh = {};
            for (float& applied : node.appliedEq)
            {
                applied = -1.0f;
            }
            node.echoWrite = 0;
            node.chorusWrite = 0;
            node.chorusPhase = 0.0f;
            node.pitchWrite = 0;
            node.pitchPhase = 0.0f;
            node.compEnvelope = 0.0f;
            node.echoWasOn = false;
            node.reverbWasOn = false;
            node.chorusWasOn = false;
            node.pitchWasOn = false;
        }

        Bool InitBus(Bus& bus, AudioBusId parentBus, Float volume)
        {
            ma_sound_group* parent = parentBus == AudioNoBus ? nullptr : &buses[parentBus].group;
            if (ma_sound_group_init(&engine, 0, parent, &bus.group) != MA_SUCCESS)
            {
                return false;
            }
            // 이펙트 노드를 그룹과 부모 사이에 끼운다: 그룹 → 이펙트 → 부모(또는 엔드포인트). 둘째 출력은 센드다.
            const ma_uint32 channels = desc.channels;
            const ma_uint32 outputChannels[2] = {desc.channels, desc.channels};
            ma_node_config config = ma_node_config_init();
            config.vtable = &g_busEffectVtable;
            config.pInputChannels = &channels;
            config.pOutputChannels = outputChannels;
            bus.effects.channels = desc.channels;
            bus.effects.sampleRate = desc.sampleRate;
            bus.effectsReady = ma_node_init(ma_engine_get_node_graph(&engine), &config, &callbacks,
                reinterpret_cast<ma_node*>(&bus.effects)) == MA_SUCCESS;
            if (bus.effectsReady)
            {
                ma_node* target = parent != nullptr ? reinterpret_cast<ma_node*>(parent)
                                                    : ma_engine_get_endpoint(&engine);
                ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&bus.effects), 0, target, 0);
                ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&bus.group), 0,
                    reinterpret_cast<ma_node*>(&bus.effects), 0);
            }
            ResetBusEffects(bus.effects);
            bus.settings = {};
            bus.used = true;
            bus.muted = false;
            bus.solo = false;
            bus.volume = Clamp01(volume);
            bus.effectiveGain = bus.volume;
            bus.parent = parentBus == AudioNoBus ? AudioMasterBus : parentBus;
            bus.sendTarget = AudioNoBus;
            bus.sendLevel = 0.0f;
            bus.effects.peak.store(0.0f, std::memory_order_relaxed);
            // 음량은 이펙트 노드가 건다(D-205). 아직 오디오 스레드가 이 노드를 읽지 않으므로 지금 값을 곧바로 둔다.
            bus.effects.gainCurrent = bus.volume;
            bus.effects.gainNow.store(bus.volume, std::memory_order_relaxed);
            bus.effects.gainTarget.store(bus.volume, std::memory_order_relaxed);
            bus.effects.duckSource.store(nullptr, std::memory_order_relaxed);
            bus.effects.duckAmount.store(0.0f, std::memory_order_relaxed);
            bus.effects.duckCurrent = 1.0f;
            bus.duckTrigger = AudioNoBus;
            return true;
        }

        void UninitBus(Bus& bus)
        {
            if (bus.used)
            {
                ma_sound_group_uninit(&bus.group);
                if (bus.effectsReady)
                {
                    // 오디오 스레드가 이 노드를 다 읽을 때까지 기다린 뒤 돌아온다 - 그 뒤에 버퍼를 푼다.
                    ma_node_uninit(reinterpret_cast<ma_node*>(&bus.effects), &callbacks);
                    bus.effectsReady = false;
                }
                bus.effects.echoBuffer.store(nullptr, std::memory_order_release);
                bus.effects.reverb.store(nullptr, std::memory_order_release);
                bus.echoStorage = nullptr;
                bus.reverbStorage = nullptr;
                bus.effects.chorusBuffer.store(nullptr, std::memory_order_release);
                bus.effects.pitchBuffer.store(nullptr, std::memory_order_release);
                bus.chorusStorage = nullptr;
                bus.pitchStorage = nullptr;
                bus.used = false;
                bus.solo = false;
                bus.sendTarget = AudioNoBus;
                bus.duckTrigger = AudioNoBus;
                bus.effects.processor.store(nullptr, std::memory_order_seq_cst);
                bus.effects.processorUser.store(nullptr, std::memory_order_seq_cst);
            }
        }

        // 보이스 수만큼 소리를 한꺼번에 만들었다 지워 고정 할당기의 빈 목록을 채운다. miniaudio 의 노드 힙 크기는
        // 채널 수와 공간화 여부에 따라 달라지므로 넷을 차례로 돈다.
        void Prewarm()
        {
            static const float silence[2] = {0.0f, 0.0f};
            Array<ma_audio_buffer_ref> buffers;
            Array<ma_sound> sounds;
            buffers.Resize(desc.maxVoices);
            sounds.Resize(desc.maxVoices);
            for (UInt32 channels = 1; channels <= 2; ++channels)
            {
                for (Int32 spatial = 0; spatial < 2; ++spatial)
                {
                    UInt32 made = 0;
                    for (; made < desc.maxVoices; ++made)
                    {
                        if (ma_audio_buffer_ref_init(ma_format_f32, channels, silence, 1, &buffers[made]) != MA_SUCCESS)
                        {
                            break;
                        }
                        ma_sound_config config = ma_sound_config_init_2(&engine);
                        config.pDataSource = &buffers[made];
                        config.pInitialAttachment = reinterpret_cast<ma_node*>(&buses[AudioMasterBus].group);
                        config.flags = spatial != 0 ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;
                        if (ma_sound_init_ex(&engine, &config, &sounds[made]) != MA_SUCCESS)
                        {
                            break;
                        }
                    }
                    for (UInt32 index = 0; index < made; ++index)
                    {
                        ma_sound_uninit(&sounds[index]);
                    }
                }
            }
        }
    };

    AudioMixer::AudioMixer() = default;

    AudioMixer::~AudioMixer()
    {
        Shutdown();
    }

    Bool AudioMixer::Initialize(const AudioMixerDesc& desc)
    {
        if (m_state && m_state->initialized)
        {
            return false;
        }
        if (desc.sampleRate == 0 || desc.channels == 0 || desc.channels > 2 || desc.maxVoices == 0)
        {
            Log::Write(LogLevel::Error, "audio", "mixer: bad format %u Hz, %u channels, %u voices",
                desc.sampleRate, desc.channels, desc.maxVoices);
            return false;
        }
        m_state = MakeOwnerPtr<State>();
        State& state = *m_state;
        state.desc = desc;
        state.callbacks = state.allocator.Callbacks();

        ma_engine_config config = ma_engine_config_init();
        config.noDevice = MA_TRUE;
        config.channels = desc.channels;
        config.sampleRate = desc.sampleRate;
        config.listenerCount = 1;
        config.allocationCallbacks = state.callbacks;
        if (ma_engine_init(&config, &state.engine) != MA_SUCCESS)
        {
            Log::Write(LogLevel::Error, "audio", "mixer: ma_engine_init failed");
            m_state = nullptr;
            return false;
        }
        if (false == state.InitBus(state.buses[AudioMasterBus], AudioNoBus, 1.0f)
            || false == state.InitBus(state.buses[AudioEditorPreviewBus], AudioNoBus, 1.0f))
        {
            Log::Write(LogLevel::Error, "audio", "mixer: could not create the master bus");
            state.UninitBus(state.buses[AudioEditorPreviewBus]);
            state.UninitBus(state.buses[AudioMasterBus]);
            ma_engine_uninit(&state.engine);
            m_state = nullptr;
            return false;
        }
        state.busCount = AudioFirstProjectBus;

        state.voices.Resize(desc.maxVoices);
        for (State::Voice& voice : state.voices)
        {
            const ma_uint32 channels = desc.channels;
            ma_node_config filterConfig = ma_node_config_init();
            filterConfig.vtable = &g_voiceFilterVtable;
            filterConfig.pInputChannels = &channels;
            filterConfig.pOutputChannels = &channels;
            voice.filter = MakeOwnerPtr<VoiceFilterNode>();
            voice.filter->channels = desc.channels;
            voice.filter->sampleRate = desc.sampleRate;
            voice.filterReady = ma_node_init(ma_engine_get_node_graph(&state.engine), &filterConfig, &state.callbacks,
                reinterpret_cast<ma_node*>(voice.filter.Get())) == MA_SUCCESS;
        }
        state.rankOrder.Resize(desc.maxVoices);
        state.rankLoudness.Resize(desc.maxVoices);
        state.freeVoices.Reserve(desc.maxVoices);
        for (UInt32 index = desc.maxVoices; index > 0; --index)
        {
            state.freeVoices.Add(index - 1);
        }
        state.clips.Reserve(desc.maxClips);
        state.freeClips.Reserve(desc.maxClips);
        state.streams.Reserve(desc.maxStreams);
        for (UInt32 index = 0; index < desc.maxStreams; ++index)
        {
            OwnerPtr<StreamSlot> slot = MakeOwnerPtr<StreamSlot>();
            ma_data_source_config sourceConfig = ma_data_source_config_init();
            sourceConfig.vtable = &g_streamVtable;
            slot->baseReady = ma_data_source_init(&sourceConfig, &slot->base) == MA_SUCCESS;
            state.streams.Add(std::move(slot));
        }
        state.allocator.Reserve(static_cast<std::size_t>(desc.maxVoices) * 16u + 64u);
        state.initialized = true;
        state.Prewarm();
        return true;
    }

    void AudioMixer::Shutdown()
    {
        if (!m_state)
        {
            return;
        }
        State& state = *m_state;
        if (state.initialized)
        {
            for (UInt32 index = 0; index < state.voices.Size(); ++index)
            {
                state.ReleaseVoice(index);
            }
            for (State::Voice& voice : state.voices)
            {
                if (voice.filterReady)
                {
                    ma_node_uninit(reinterpret_cast<ma_node*>(voice.filter.Get()), &state.callbacks);
                    voice.filterReady = false;
                }
            }
            state.StopStreamer();
            for (OwnerPtr<StreamSlot>& slot : state.streams)
            {
                if (slot->baseReady)
                {
                    ma_data_source_uninit(&slot->base);
                    slot->baseReady = false;
                }
            }
            for (UInt32 bus = AudioMaxBuses; bus > AudioFirstProjectBus; --bus)
            {
                state.UninitBus(state.buses[bus - 1]);
            }
            state.UninitBus(state.buses[AudioEditorPreviewBus]);
            state.UninitBus(state.buses[AudioMasterBus]);
            ma_engine_uninit(&state.engine);
            state.initialized = false;
        }
        m_state = nullptr;
    }

    Bool AudioMixer::IsInitialized() const
    {
        return m_state && m_state->initialized;
    }

    UInt32 AudioMixer::GetSampleRate() const
    {
        return IsInitialized() ? m_state->desc.sampleRate : UInt32(0);
    }

    UInt32 AudioMixer::GetChannels() const
    {
        return IsInitialized() ? m_state->desc.channels : UInt32(0);
    }

    void AudioMixer::Render(float* output, UInt32 frameCount)
    {
        if (output == nullptr || frameCount == 0)
        {
            return;
        }
        State* state = m_state.Get();
        if (state == nullptr || false == state->initialized)
        {
            const UInt32 channels = state != nullptr ? state->desc.channels : UInt32(2);
            std::memset(output, 0, sizeof(float) * frameCount * channels);
            return;
        }
        ma_uint64 read = 0;
        if (ma_engine_read_pcm_frames(&state->engine, output, frameCount, &read) != MA_SUCCESS)
        {
            read = 0;
        }
        const std::size_t samples = static_cast<std::size_t>(frameCount) * state->desc.channels;
        const std::size_t filled = static_cast<std::size_t>(read) * state->desc.channels;
        if (filled < samples)
        {
            std::memset(output + filled, 0, sizeof(float) * (samples - filled));
        }
        // 출력 이득(포커스 정책)을 곧게 옮기며 곱한다. 목표에 닿아 1 이면 건너뛴다.
        const UInt32 channels = state->desc.channels;
        const Float gainTarget = state->outputGainTarget.load(std::memory_order_relaxed);
        if (state->outputGainCurrent != gainTarget || gainTarget != 1.0f)
        {
            const Float rate = state->outputGainRate.load(std::memory_order_relaxed);
            Float gain = state->outputGainCurrent;
            for (UInt32 frame = 0; frame < frameCount; ++frame)
            {
                if (gain < gainTarget)
                {
                    gain = gain + rate < gainTarget ? gain + rate : gainTarget;
                }
                else if (gain > gainTarget)
                {
                    gain = gain - rate > gainTarget ? gain - rate : gainTarget;
                }
                for (UInt32 channel = 0; channel < channels; ++channel)
                {
                    output[frame * channels + channel] *= gain;
                }
            }
            state->outputGainCurrent = gain;
        }
        // 버스를 더하면 1 을 넘을 수 있다. 장치에 넘기 전에 자른다 - 넘친 값은 장치마다 다르게 깨진다.
        Float peak = 0.0f;
        for (std::size_t index = 0; index < samples; ++index)
        {
            Float sample = output[index];
            if (!std::isfinite(sample))
            {
                sample = 0.0f;
            }
            const Float magnitude = std::fabs(sample);
            if (magnitude > peak)
            {
                peak = magnitude;
            }
            output[index] = sample;
        }
        // 리미터: 프레임의 가장 큰 채널이 천장을 넘으면 그 순간에 줄이고, 0.1 초에 걸쳐 1 로 되돌린다. 넘는 일이 없으면
        // 이득은 1 이고 이 고리를 건너뛴다.
        if (state->limiterEnabled.load(std::memory_order_relaxed) && (peak > state->limiterCeiling.load(std::memory_order_relaxed)
            || state->limiterGain < 1.0f))
        {
            const Float ceiling = state->limiterCeiling.load(std::memory_order_relaxed);
            const Float release = 1.0f - std::exp(-1.0f / (0.1f * static_cast<float>(state->desc.sampleRate)));
            Float gain = state->limiterGain;
            for (UInt32 frame = 0; frame < frameCount; ++frame)
            {
                Float loudest = 0.0f;
                for (UInt32 channel = 0; channel < channels; ++channel)
                {
                    loudest = std::fmax(loudest, std::fabs(output[frame * channels + channel]));
                }
                const Float allowed = loudest > ceiling ? ceiling / loudest : Float(1.0f);
                gain = gain + (1.0f - gain) * release;
                gain = allowed < gain ? allowed : gain;
                for (UInt32 channel = 0; channel < channels; ++channel)
                {
                    output[frame * channels + channel] *= gain;
                }
            }
            state->limiterGain = gain;
        }
        // 마지막 안전판이다. 리미터를 껐거나 천장이 1 이상이면 여기서 잘린다.
        for (std::size_t index = 0; index < samples; ++index)
        {
            float& sample = output[index];
            sample = sample > 1.0f ? Float(1.0f) : (sample < -1.0f ? Float(-1.0f) : Float(sample));
        }
        // 스펙트럼 창이 읽을 최근 출력이다(채널 평균).
        UInt32 write = state->recentWrite.load(std::memory_order_relaxed);
        const Float inverseChannels = 1.0f / static_cast<float>(channels);
        for (UInt32 frame = 0; frame < frameCount; ++frame)
        {
            Float mono = 0.0f;
            for (UInt32 channel = 0; channel < channels; ++channel)
            {
                mono += output[frame * channels + channel];
            }
            state->recent[write % RecentCapacity] = mono * inverseChannels;
            ++write;
        }
        state->recentWrite.store(write, std::memory_order_release);
        state->peak.store(peak, std::memory_order_relaxed);
        state->renderedFrames.fetch_add(frameCount, std::memory_order_relaxed);
    }

    void AudioMixer::RenderCallback(void* user, float* output, UInt32 frameCount)
    {
        static_cast<AudioMixer*>(user)->Render(output, frameCount);
    }

    void AudioMixer::Update()
    {
        if (false == IsInitialized())
        {
            return;
        }
        State& state = *m_state;
        for (UInt32 index = 0; index < state.voices.Size(); ++index)
        {
            State::Voice& voice = state.voices[index];
            if (voice.state == State::VoiceState::Playing)
            {
                // 루프도 본다(D-240) - 제대로 도는 루프는 끝에 닿지 않지만, 열지 못한 디스크 스트림은 루프인 채 끝난다.
                if (false == voice.parked && ma_sound_at_end(&voice.sound))
                {
                    state.ReleaseVoice(index);
                }
            }
            else if (voice.state == State::VoiceState::Stopping)
            {
                if (false == ma_sound_is_playing(&voice.sound))
                {
                    state.ReleaseVoice(index);
                }
            }
        }
        state.Virtualize();
    }

    AudioClipHandle AudioMixer::RegisterClip(const AudioClipDesc& desc)
    {
        if (false == IsInitialized())
        {
            return {};
        }
        const Bool pcmValid = desc.encoding == AudioClipEncoding::Pcm && desc.pcm != nullptr && desc.frameCount > 0
            && desc.channels > 0 && desc.channels <= 8 && desc.sampleRate > 0;
        const Bool encodedValid = desc.encoding == AudioClipEncoding::Encoded && desc.bytes != nullptr && desc.byteCount > 0;
        const Bool fileValid = desc.encoding == AudioClipEncoding::File && desc.path != nullptr && desc.path[0] != '\0'
            && std::strlen(desc.path) < sizeof(StreamSlot::path) && desc.channels > 0 && desc.channels <= 8 && desc.sampleRate > 0;
        if (false == pcmValid && false == encodedValid && false == fileValid)
        {
            return {};
        }
        State& state = *m_state;
        UInt32 index = 0;
        if (false == state.freeClips.IsEmpty())
        {
            index = state.freeClips.Last();
            state.freeClips.RemoveAt(state.freeClips.Size() - 1);
        }
        else
        {
            if (state.clips.Size() >= state.desc.maxClips)
            {
                Log::Write(LogLevel::Warning, "audio", "mixer: clip limit %u reached", state.desc.maxClips);
                return {};
            }
            index = static_cast<std::uint32_t>(state.clips.Size());
            state.clips.Emplace();
        }
        State::Clip& clip = state.clips[index];
        clip.desc = desc;
        clip.used = true;
        // 자리를 다시 쓰면 옛 클립의 쿨다운 시계를 물려받지 않는다(D-240).
        clip.started = false;
        clip.lastStartFrame = 0;
        return {index, clip.generation};
    }

    void AudioMixer::UnregisterClip(AudioClipHandle handle)
    {
        if (false == IsInitialized() || nullptr == m_state->ResolveClip(handle))
        {
            return;
        }
        State& state = *m_state;
        for (UInt32 index = 0; index < state.voices.Size(); ++index)
        {
            const State::Voice& voice = state.voices[index];
            if (voice.state != State::VoiceState::Free && voice.clip.index == handle.index
                && voice.clip.generation == handle.generation)
            {
                state.ReleaseVoice(index);
            }
        }
        State::Clip& clip = state.clips[handle.index];
        clip.used = false;
        clip.desc = {};
        ++clip.generation;
        if (clip.generation == 0)
        {
            clip.generation = 1;
        }
        state.freeClips.Add(handle.index);
    }

    Bool AudioMixer::IsClipRegistered(AudioClipHandle clip) const
    {
        return IsInitialized() && m_state->ResolveClip(clip) != nullptr;
    }

    double AudioMixer::GetClipDurationSeconds(AudioClipHandle handle) const
    {
        if (false == IsInitialized())
        {
            return 0.0;
        }
        const State::Clip* clip = m_state->ResolveClip(handle);
        if (clip == nullptr || clip->desc.sampleRate == 0)
        {
            return 0.0;
        }
        return static_cast<double>(clip->desc.frameCount) / static_cast<double>(clip->desc.sampleRate);
    }

    AudioBusId AudioMixer::CreateBus(Float volume, AudioBusId parent)
    {
        if (false == IsInitialized())
        {
            return AudioMasterBus;
        }
        State& state = *m_state;
        if (false == state.IsBusValid(parent) || parent == AudioEditorPreviewBus)
        {
            parent = AudioMasterBus;
        }
        for (UInt32 bus = AudioFirstProjectBus; bus < AudioMaxBuses; ++bus)
        {
            if (false == state.buses[bus].used)
            {
                if (false == state.InitBus(state.buses[bus], parent, volume))
                {
                    return AudioMasterBus;
                }
                if (bus + 1 > state.busCount)
                {
                    state.busCount = bus + 1;
                }
                // 솔로 중에 새 버스가 생기면 그 버스도 솔로 규칙을 따라야 한다.
                state.ApplyBusGains();
                return static_cast<AudioBusId>(bus);
            }
        }
        Log::Write(LogLevel::Warning, "audio", "mixer: bus limit %u reached", AudioMaxBuses - AudioFirstProjectBus);
        return AudioMasterBus;
    }

    void AudioMixer::DestroyProjectBuses()
    {
        if (false == IsInitialized())
        {
            return;
        }
        State& state = *m_state;
        for (State::Voice& voice : state.voices)
        {
            if (voice.state != State::VoiceState::Free && voice.bus >= AudioFirstProjectBus)
            {
                state.RouteVoice(voice, AudioMasterBus, voice.filterRouted);
            }
        }
        for (State::Bus& bus : state.buses)
        {
            if (bus.duckTrigger >= AudioFirstProjectBus)
            {
                bus.effects.duckSource.store(nullptr, std::memory_order_release);
                bus.duckTrigger = AudioNoBus;
            }
        }
        for (UInt32 bus = AudioMaxBuses; bus > AudioFirstProjectBus; --bus)
        {
            state.UninitBus(state.buses[bus - 1]);
        }
        state.busCount = AudioFirstProjectBus;
        state.ApplyBusGains();
    }

    AudioBusId AudioMixer::GetBusParent(AudioBusId bus) const
    {
        if (false == IsInitialized() || false == m_state->IsBusValid(bus) || bus < AudioFirstProjectBus)
        {
            return AudioNoBus;
        }
        return m_state->buses[bus].parent;
    }

    UInt32 AudioMixer::GetBusCount() const
    {
        return IsInitialized() ? m_state->busCount : UInt32(0);
    }

    void AudioMixer::SetBusVolume(AudioBusId bus, Float volume, Float fadeSeconds)
    {
        if (false == IsInitialized() || false == m_state->IsBusValid(bus))
        {
            return;
        }
        m_state->buses[bus].volume = Clamp01(volume);
        m_state->ApplyBusGains(bus, std::isfinite(fadeSeconds) ? fadeSeconds : Float(0.0f));
    }

    void AudioMixer::SetBusDucking(AudioBusId bus, AudioBusId trigger, Float amount, Float releaseSeconds)
    {
        if (false == IsInitialized() || false == m_state->IsBusValid(bus))
        {
            return;
        }
        State::Bus& target = m_state->buses[bus];
        const Float safeAmount = Clamp01(amount);
        // 트리거가 이 버스의 소리를 받으면(조상·센드) 제 소리에 눌린다(D-240) - 거절한다.
        if (trigger == AudioNoBus || trigger == bus || false == m_state->IsBusValid(trigger) || safeAmount <= 0.0f
            || m_state->Reaches(bus, trigger))
        {
            target.effects.duckSource.store(nullptr, std::memory_order_release);
            target.effects.duckAmount.store(0.0f, std::memory_order_relaxed);
            target.duckTrigger = AudioNoBus;
            return;
        }
        target.effects.duckAmount.store(safeAmount, std::memory_order_relaxed);
        target.effects.duckRelease.store(Clamped(releaseSeconds, 0.01f, 10.0f), std::memory_order_relaxed);
        target.effects.duckSource.store(&m_state->buses[trigger].effects, std::memory_order_release);
        target.duckTrigger = trigger;
    }

    void AudioMixer::SetBusProcessor(AudioBusId bus, AudioBusProcessCallback callback, void* user)
    {
        if (false == IsInitialized() || false == m_state->IsBusValid(bus))
        {
            return;
        }
        BusEffectNode& node = m_state->buses[bus].effects;
        // 먼저 떼고 기다린 뒤 새 것을 건다 - 옛 처리기가 새 `user` 로 불리는 틈이 없다.
        node.processor.store(nullptr, std::memory_order_seq_cst);
        while (node.inProcessor.load(std::memory_order_seq_cst))
        {
            std::this_thread::yield();
        }
        node.processorUser.store(user, std::memory_order_seq_cst);
        node.processor.store(callback, std::memory_order_seq_cst);
    }

    Float AudioMixer::GetBusGainReduction(AudioBusId bus) const
    {
        if (false == IsInitialized() || false == m_state->IsBusValid(bus))
        {
            return 0.0f;
        }
        return m_state->buses[bus].effects.compReduction.load(std::memory_order_relaxed);
    }

    AudioBusId AudioMixer::GetBusDuckTrigger(AudioBusId bus) const
    {
        return IsInitialized() && m_state->IsBusValid(bus) ? m_state->buses[bus].duckTrigger : AudioNoBus;
    }

    Float AudioMixer::GetBusDuckAmount(AudioBusId bus) const
    {
        if (false == IsInitialized() || false == m_state->IsBusValid(bus) || m_state->buses[bus].duckTrigger == AudioNoBus)
        {
            return 0.0f;
        }
        return m_state->buses[bus].effects.duckAmount.load(std::memory_order_relaxed);
    }

    Float AudioMixer::GetBusVolume(AudioBusId bus) const
    {
        if (false == IsInitialized() || false == m_state->IsBusValid(bus))
        {
            return 0.0f;
        }
        return m_state->buses[bus].volume;
    }

    void AudioMixer::SetBusMuted(AudioBusId bus, Bool muted)
    {
        if (false == IsInitialized() || false == m_state->IsBusValid(bus))
        {
            return;
        }
        m_state->buses[bus].muted = muted;
        m_state->ApplyBusGains();
    }

    Bool AudioMixer::IsBusMuted(AudioBusId bus) const
    {
        return IsInitialized() && m_state->IsBusValid(bus) && m_state->buses[bus].muted;
    }

    void AudioMixer::SetBusEffects(AudioBusId bus, const AudioBusEffects& effects)
    {
        if (false == IsInitialized() || false == m_state->IsBusValid(bus))
        {
            return;
        }
        State::Bus& target = m_state->buses[bus];
        AudioBusEffects safe;
        safe.lowPassHz = std::isfinite(effects.lowPassHz) && effects.lowPassHz > 0.0f ? effects.lowPassHz : Float(0.0f);
        safe.highPassHz = std::isfinite(effects.highPassHz) && effects.highPassHz > 0.0f ? effects.highPassHz : Float(0.0f);
        safe.echoDelay = Clamped(effects.echoDelay, 0.01f, MaxEchoSeconds);
        safe.echoFeedback = Clamped(effects.echoFeedback, 0.0f, 0.95f);
        safe.echoMix = Clamped(effects.echoMix, 0.0f, 1.0f);
        safe.reverbRoom = Clamped(effects.reverbRoom, 0.0f, 1.0f);
        safe.reverbDamping = Clamped(effects.reverbDamping, 0.0f, 1.0f);
        safe.reverbMix = Clamped(effects.reverbMix, 0.0f, 1.0f);
        safe.dry = Clamped(effects.dry, 0.0f, 1.0f);
        safe.eqLowHz = Clamped(effects.eqLowHz, 20.0f, 20000.0f);
        safe.eqLowGain = Clamped(effects.eqLowGain, -24.0f, 24.0f);
        safe.eqMidHz = Clamped(effects.eqMidHz, 20.0f, 20000.0f);
        safe.eqMidGain = Clamped(effects.eqMidGain, -24.0f, 24.0f);
        safe.eqHighHz = Clamped(effects.eqHighHz, 20.0f, 20000.0f);
        safe.eqHighGain = Clamped(effects.eqHighGain, -24.0f, 24.0f);
        safe.distortion = Clamped(effects.distortion, 0.0f, 1.0f);
        safe.distortionMix = Clamped(effects.distortionMix, 0.0f, 1.0f);
        safe.chorusMix = Clamped(effects.chorusMix, 0.0f, 1.0f);
        safe.chorusRate = Clamped(effects.chorusRate, 0.05f, 10.0f);
        safe.chorusDepth = Clamped(effects.chorusDepth, 0.0f, 8.0f);
        safe.pitchShift = Clamped(effects.pitchShift, -12.0f, 12.0f);
        safe.compRatio = Clamped(effects.compRatio, 1.0f, 20.0f);
        safe.compThreshold = Clamped(effects.compThreshold, -60.0f, 0.0f);
        safe.compAttack = Clamped(effects.compAttack, 0.0005f, 0.5f);
        safe.compRelease = Clamped(effects.compRelease, 0.005f, 2.0f);
        safe.compMakeup = Clamped(effects.compMakeup, 0.0f, 24.0f);
        target.settings = safe;
        BusEffectNode& node = target.effects;
        // 버퍼는 처음 켤 때 한 번 잡는다(메인 스레드). 포인터는 다 채운 뒤에 원자로 넘긴다.
        if (safe.echoMix > 0.0f && target.echoStorage.Get() == nullptr)
        {
            target.echoStorage = MakeOwnerPtr<Array<float>>();
            node.echoCapacity = static_cast<std::uint32_t>(MaxEchoSeconds * static_cast<float>(node.sampleRate)) + 1;
            target.echoStorage->Resize(static_cast<std::size_t>(node.echoCapacity) * node.channels);
            for (float& sample : *target.echoStorage)
            {
                sample = 0.0f;
            }
            node.echoBuffer.store(target.echoStorage->Data(), std::memory_order_release);
        }
        if (safe.reverbMix > 0.0f && target.reverbStorage.Get() == nullptr)
        {
            target.reverbStorage = MakeOwnerPtr<Reverb>();
            target.reverbStorage->Prepare(node.sampleRate);
            node.reverb.store(target.reverbStorage.Get(), std::memory_order_release);
        }
        const auto prepareLine = [&node](OwnerPtr<Array<float>>& storage, UInt32& capacity, Float seconds) {
            storage = MakeOwnerPtr<Array<float>>();
            capacity = static_cast<std::uint32_t>(seconds * static_cast<float>(node.sampleRate)) + 2;
            storage->Resize(static_cast<std::size_t>(capacity) * node.channels);
            for (float& sample : *storage)
            {
                sample = 0.0f;
            }
        };
        if (safe.chorusMix > 0.0f && target.chorusStorage.Get() == nullptr)
        {
            prepareLine(target.chorusStorage, node.chorusCapacity, ChorusSeconds);
            node.chorusBuffer.store(target.chorusStorage->Data(), std::memory_order_release);
        }
        if (safe.pitchShift != 0.0f && target.pitchStorage.Get() == nullptr)
        {
            prepareLine(target.pitchStorage, node.pitchCapacity, PitchWindowSeconds * 2.0f);
            node.pitchBuffer.store(target.pitchStorage->Data(), std::memory_order_release);
        }
        node.eqLowHz.store(safe.eqLowHz, std::memory_order_relaxed);
        node.eqLowGain.store(safe.eqLowGain, std::memory_order_relaxed);
        node.eqMidHz.store(safe.eqMidHz, std::memory_order_relaxed);
        node.eqMidGain.store(safe.eqMidGain, std::memory_order_relaxed);
        node.eqHighHz.store(safe.eqHighHz, std::memory_order_relaxed);
        node.eqHighGain.store(safe.eqHighGain, std::memory_order_relaxed);
        node.distortion.store(safe.distortion, std::memory_order_relaxed);
        node.distortionMix.store(safe.distortionMix, std::memory_order_relaxed);
        node.chorusMix.store(safe.chorusMix, std::memory_order_relaxed);
        node.chorusRate.store(safe.chorusRate, std::memory_order_relaxed);
        node.chorusDepth.store(safe.chorusDepth, std::memory_order_relaxed);
        node.pitchShift.store(safe.pitchShift, std::memory_order_relaxed);
        node.compRatio.store(safe.compRatio, std::memory_order_relaxed);
        node.compThreshold.store(safe.compThreshold, std::memory_order_relaxed);
        node.compAttack.store(safe.compAttack, std::memory_order_relaxed);
        node.compRelease.store(safe.compRelease, std::memory_order_relaxed);
        node.compMakeup.store(safe.compMakeup, std::memory_order_relaxed);
        node.lowPass.store(safe.lowPassHz, std::memory_order_relaxed);
        node.highPass.store(safe.highPassHz, std::memory_order_relaxed);
        node.echoDelay.store(safe.echoDelay, std::memory_order_relaxed);
        node.echoFeedback.store(safe.echoFeedback, std::memory_order_relaxed);
        node.echoMix.store(safe.echoMix, std::memory_order_relaxed);
        node.reverbRoom.store(safe.reverbRoom, std::memory_order_relaxed);
        node.reverbDamping.store(safe.reverbDamping, std::memory_order_relaxed);
        node.reverbMix.store(safe.reverbMix, std::memory_order_relaxed);
        node.dry.store(safe.dry, std::memory_order_relaxed);
    }

    AudioBusEffects AudioMixer::GetBusEffects(AudioBusId bus) const
    {
        if (false == IsInitialized() || false == m_state->IsBusValid(bus))
        {
            return {};
        }
        return m_state->buses[bus].settings;
    }

    Bool AudioMixer::SetBusSend(AudioBusId bus, AudioBusId target, Float level)
    {
        if (false == IsInitialized() || false == m_state->IsBusValid(bus) || bus < AudioFirstProjectBus)
        {
            return false;
        }
        State& state = *m_state;
        State::Bus& source = state.buses[bus];
        ma_node* effects = reinterpret_cast<ma_node*>(&source.effects);
        const Float safeLevel = Clamp01(level);
        if (target == AudioNoBus || safeLevel <= 0.0f)
        {
            if (source.sendTarget != AudioNoBus && source.effectsReady)
            {
                ma_node_detach_output_bus(effects, 1);
            }
            source.sendTarget = AudioNoBus;
            source.sendLevel = 0.0f;
            state.ApplyBusGains();
            return true;
        }
        if (false == state.IsBusValid(target) || target == AudioEditorPreviewBus || target == bus
            || false == source.effectsReady)
        {
            return false;
        }
        // 지금의 센드를 빼고 따진다 - 같은 버스로 양만 바꾸는 것은 늘 된다.
        const AudioBusId previous = source.sendTarget;
        source.sendTarget = AudioNoBus;
        if (state.Reaches(target, bus))
        {
            source.sendTarget = previous;
            Log::Write(LogLevel::Warning, "audio", "mixer: a send from bus %u to bus %u would feed back into itself",
                static_cast<unsigned>(bus), static_cast<unsigned>(target));
            return false;
        }
        if (previous != target)
        {
            ma_node_attach_output_bus(effects, 1, reinterpret_cast<ma_node*>(&state.buses[target].group), 0);
        }
        ma_node_set_output_bus_volume(effects, 1, safeLevel);
        source.sendTarget = target;
        source.sendLevel = safeLevel;
        state.ApplyBusGains();
        return true;
    }

    AudioBusId AudioMixer::GetBusSendTarget(AudioBusId bus) const
    {
        return IsInitialized() && m_state->IsBusValid(bus) ? m_state->buses[bus].sendTarget : AudioNoBus;
    }

    Float AudioMixer::GetBusSendLevel(AudioBusId bus) const
    {
        return IsInitialized() && m_state->IsBusValid(bus) ? m_state->buses[bus].sendLevel : Float(0.0f);
    }

    void AudioMixer::SetBusSolo(AudioBusId bus, Bool solo)
    {
        if (false == IsInitialized() || false == m_state->IsBusValid(bus) || bus < AudioFirstProjectBus)
        {
            return;
        }
        m_state->buses[bus].solo = solo;
        m_state->ApplyBusGains();
    }

    Bool AudioMixer::IsBusSolo(AudioBusId bus) const
    {
        return IsInitialized() && m_state->IsBusValid(bus) && m_state->buses[bus].solo;
    }

    Float AudioMixer::GetBusPeak(AudioBusId bus) const
    {
        if (false == IsInitialized() || false == m_state->IsBusValid(bus))
        {
            return 0.0f;
        }
        return m_state->buses[bus].effects.peak.load(std::memory_order_relaxed);
    }

    AudioVoiceHandle AudioMixer::Play(const AudioPlayDesc& desc)
    {
        if (false == IsInitialized())
        {
            return {};
        }
        State& state = *m_state;
        const State::Clip* clip = state.ResolveClip(desc.clip);
        if (clip == nullptr)
        {
            return {};
        }
        const AudioBusId bus = state.IsBusValid(desc.bus) ? desc.bus : AudioMasterBus;
        const Float trim = std::isfinite(clip->desc.gain) && clip->desc.gain > 0.0f ? (clip->desc.gain > 4.0f ? Float(4.0f) : Float(clip->desc.gain)) : Float(1.0f);
        const Float minDistance = SafePositive(desc.minDistance, 1.0f);
        Float maxDistance = SafePositive(desc.maxDistance, 50.0f);
        if (maxDistance < minDistance)
        {
            maxDistance = minDistance;
        }

        // 보이스를 잡기 전에 거른다(D-231). 걸러진 재생은 아무것도 훔치지 않고 쿨다운 시계도 건드리지 않는다.
        if (state.IsInaudibleAtStart(desc, trim, minDistance, maxDistance))
        {
            ++state.voicesCulled;
            return {};
        }
        const UInt64 now = ma_engine_get_time_in_pcm_frames(&state.engine);
        if (clip->desc.cooldownSeconds > 0.0f && std::isfinite(clip->desc.cooldownSeconds) && clip->started)
        {
            const UInt64 cooldown = static_cast<std::uint64_t>(clip->desc.cooldownSeconds
                * static_cast<float>(state.desc.sampleRate));
            if (now - clip->lastStartFrame < cooldown)
            {
                ++state.voicesThrottled;
                return {};
            }
        }
        UInt32 replaced = static_cast<std::uint32_t>(-1);
        if (clip->desc.maxInstances > 0)
        {
            // 줄여 끄는 중인 것은 세지 않는다 - 곧 비고, 세면 연타 때 새것이 계속 버려진다.
            UInt32 instances = 0;
            UInt32 oldest = static_cast<std::uint32_t>(-1);
            for (UInt32 candidate = 0; candidate < state.voices.Size(); ++candidate)
            {
                const State::Voice& other = state.voices[candidate];
                const Bool live = other.state == State::VoiceState::Playing || other.state == State::VoiceState::Paused;
                if (false == live || other.clip.index != desc.clip.index || other.clip.generation != desc.clip.generation)
                {
                    continue;
                }
                ++instances;
                if (other.priority <= desc.priority
                    && (oldest == static_cast<std::uint32_t>(-1) || other.startSerial < state.voices[oldest].startSerial))
                {
                    oldest = candidate;
                }
            }
            if (instances >= clip->desc.maxInstances)
            {
                if (oldest == static_cast<std::uint32_t>(-1))
                {
                    ++state.voicesThrottled;
                    return {};
                }
                replaced = oldest;
            }
        }

        // 거절할 까닭을 모두 먼저 본다(D-240). 훔치거나 줄여 끈 뒤에 거절하면 옛 소리만 죽고 새 소리는 없다.
        if (clip->desc.encoding == AudioClipEncoding::File && (state.desc.openStream == nullptr || false == state.HasIdleStream()))
        {
            if (false == state.warnedStreams)
            {
                state.warnedStreams = true;
                Log::Write(LogLevel::Warning, "audio", state.desc.openStream == nullptr
                    ? "mixer: this host cannot stream from disk - use Streaming or Decompressed"
                    : "mixer: all %u disk streams are busy - a streamed sound was refused", state.desc.maxStreams);
            }
            ++state.voicesRejected;
            return {};
        }
        const UInt32 none = static_cast<std::uint32_t>(-1);
        UInt32 slotVictim = none;
        if (state.freeVoices.IsEmpty())
        {
            slotVictim = state.PickVictim(desc.priority);
            if (slotVictim == none)
            {
                ++state.voicesRejected;
                return {};
            }
        }
        // 섞는 수가 찼다(D-235). 루프는 가상으로 시작하고, 한 번짜리·스트리밍은 가장 약한 루프를 가상으로 돌리거나 실제로 섞는
        // 보이스 가운데서 훔친다.
        Bool startParked = false;
        UInt32 parkVictim = none;
        UInt32 mixVictim = none;
        if (state.VirtualizationEnabled())
        {
            UInt32 mixing = state.CountMixing();
            if (slotVictim != none && State::IsMixing(state.voices[slotVictim]))
            {
                --mixing;
            }
            if (mixing >= state.desc.maxAudibleVoices)
            {
                if (desc.loop && clip->desc.encoding != AudioClipEncoding::File)
                {
                    startParked = true;
                }
                else
                {
                    parkVictim = state.FindWeakestLoop(desc.priority, slotVictim);
                    if (parkVictim == none)
                    {
                        mixVictim = state.PickVictim(desc.priority, true, slotVictim);
                        if (mixVictim == none)
                        {
                            ++state.voicesRejected;
                            return {};
                        }
                    }
                }
            }
        }

        // 이제 실행한다.
        if (replaced != none)
        {
            Stop({replaced, state.voices[replaced].generation}, 0.02f);
            ++state.voicesReplaced;
        }
        if (slotVictim != none && state.voices[slotVictim].state != State::VoiceState::Free)
        {
            state.ReleaseVoice(slotVictim);
            ++state.voicesStolen;
        }
        if (parkVictim != none && state.voices[parkVictim].state == State::VoiceState::Playing)
        {
            state.Park(state.voices[parkVictim], now);
        }
        if (mixVictim != none)
        {
            state.ReleaseVoice(mixVictim);
            ++state.voicesStolen;
        }
        UInt32 index = 0;
        if (state.freeVoices.IsEmpty())
        {
            ++state.voicesRejected;
            return {};
        }
        index = state.freeVoices.Last();
        state.freeVoices.RemoveAt(state.freeVoices.Size() - 1);

        State::Voice& voice = state.voices[index];
        ma_data_source* source = nullptr;
        if (clip->desc.encoding == AudioClipEncoding::Pcm)
        {
            if (ma_audio_buffer_ref_init(ma_format_f32, clip->desc.channels, clip->desc.pcm, clip->desc.frameCount,
                    &voice.buffer) != MA_SUCCESS)
            {
                state.freeVoices.Add(index);
                return {};
            }
            voice.buffer.sampleRate = clip->desc.sampleRate;
            source = &voice.buffer;
        }
        else if (clip->desc.encoding == AudioClipEncoding::File)
        {
            UInt32 slotIndex = State::NoStream;
            for (UInt32 candidate = 0; candidate < state.streams.Size(); ++candidate)
            {
                const StreamSlot& slot = *state.streams[candidate];
                if (slot.baseReady && slot.phase.load(std::memory_order_acquire) == static_cast<std::uint32_t>(StreamPhase::Idle))
                {
                    slotIndex = candidate;
                    break;
                }
            }
            if (slotIndex == State::NoStream || state.desc.openStream == nullptr)
            {
                if (false == state.warnedStreams)
                {
                    state.warnedStreams = true;
                    Log::Write(LogLevel::Warning, "audio", state.desc.openStream == nullptr
                        ? "mixer: this host cannot stream from disk - use Streaming or Decompressed"
                        : "mixer: all %u disk streams are busy - a streamed sound was refused", state.desc.maxStreams);
                }
                ++state.voicesRejected;
                state.freeVoices.Add(index);
                return {};
            }
            StreamSlot& slot = *state.streams[slotIndex];
            // 링은 이 자리를 처음 쓸 때(또는 더 큰 형식을 만날 때) 한 번 잡는다. 그 뒤로는 재사용한다.
            const Float seconds = state.desc.streamBufferSeconds > 0.05f ? state.desc.streamBufferSeconds : Float(0.05f);
            const UInt32 capacity = static_cast<std::uint32_t>(seconds * static_cast<float>(clip->desc.sampleRate)) + 1;
            const std::size_t samples = static_cast<std::size_t>(capacity) * clip->desc.channels;
            if (slot.ring.Get() == nullptr || slot.ring->Size() < samples)
            {
                slot.ring = MakeOwnerPtr<Array<float>>();
                slot.ring->Resize(samples);
            }
            slot.capacity = capacity;
            std::memcpy(slot.path, clip->desc.path, std::strlen(clip->desc.path) + 1);
            slot.channels = clip->desc.channels;
            slot.sampleRate = clip->desc.sampleRate;
            slot.length = clip->desc.frameCount;
            slot.loop.store(desc.loop, std::memory_order_relaxed);
            slot.written.store(0, std::memory_order_relaxed);
            slot.consumed.store(0, std::memory_order_relaxed);
            slot.epoch.store(0, std::memory_order_relaxed);
            slot.epochWritten.store(0, std::memory_order_relaxed);
            slot.epochFrame.store(0, std::memory_order_relaxed);
            slot.seekRequest.store(-1, std::memory_order_relaxed);
            slot.ended.store(false, std::memory_order_relaxed);
            slot.endWritten.store(0, std::memory_order_relaxed);
            slot.cursor.store(0, std::memory_order_relaxed);
            slot.ready.store(false, std::memory_order_relaxed);
            slot.seenEpoch = 0;
            slot.phase.store(static_cast<std::uint32_t>(StreamPhase::Opening), std::memory_order_release);
            state.StartStreamer();
            voice.streamSlot = slotIndex;
            source = &slot.base;
        }
        else
        {
            // 클립의 채널로 푼다 - 모노 에셋(D-231)이면 1 이고, 아니면 등록 때 읽은 파일의 채널이다.
            ma_decoder_config config = ma_decoder_config_init(ma_format_f32, clip->desc.channels <= 8 ? clip->desc.channels : UInt32(0), 0);
            config.allocationCallbacks = state.callbacks;
            if (ma_decoder_init_memory(clip->desc.bytes, clip->desc.byteCount, &config, &voice.decoder) != MA_SUCCESS)
            {
                state.freeVoices.Add(index);
                return {};
            }
            voice.usesDecoder = true;
            source = &voice.decoder;
        }

        ma_sound_config config = ma_sound_config_init_2(&state.engine);
        config.pDataSource = source;
        config.pInitialAttachment = reinterpret_cast<ma_node*>(&state.buses[bus].group);
        config.flags = desc.spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;
        if (ma_sound_init_ex(&state.engine, &config, &voice.sound) != MA_SUCCESS)
        {
            if (voice.usesDecoder)
            {
                ma_decoder_uninit(&voice.decoder);
                voice.usesDecoder = false;
            }
            if (voice.streamSlot != State::NoStream)
            {
                state.streams[voice.streamSlot]->phase.store(static_cast<std::uint32_t>(StreamPhase::Closing),
                    std::memory_order_release);
                voice.streamSlot = State::NoStream;
            }
            state.freeVoices.Add(index);
            return {};
        }

        // 여기서부터 `ma_sound_start` 전까지는 소리가 멈춰 있어 오디오 스레드가 읽지 않는다. 원자가 아닌 값(거리·
        // 감쇠·도플러)은 이 사이에만 쓴다(D-198).
        const Float lowPass = PositiveOrZero(desc.lowPassHz);
        const Float highPass = PositiveOrZero(desc.highPassHz);
        if (lowPass > 0.0f || highPass > 0.0f)
        {
            voice.filter->lowPass.store(lowPass, std::memory_order_relaxed);
            voice.filter->highPass.store(highPass, std::memory_order_relaxed);
            voice.filter->reset.store(true, std::memory_order_release);
            state.RouteVoice(voice, bus, true);
        }
        voice.volume = Clamp01(desc.volume);
        voice.trim = trim;
        voice.looping = desc.loop;
        voice.priority = desc.priority;
        voice.bus = bus;
        voice.clip = desc.clip;
        voice.tag = desc.tag;
        voice.startSerial = ++state.serial;
        voice.pitch = SafePositive(desc.pitch, 1.0f);
        voice.spatial = desc.spatial;
        voice.attenuation = desc.attenuation;
        voice.minDistance = minDistance;
        voice.maxDistance = maxDistance;
        voice.rolloff = SafePositive(desc.rolloff, 1.0f);
        voice.position[0] = desc.position[0];
        voice.position[1] = desc.position[1];
        voice.position[2] = desc.position[2];
        ma_sound_set_volume(&voice.sound, voice.volume * voice.trim);
        ma_sound_set_pitch(&voice.sound, voice.pitch);
        ma_sound_set_looping(&voice.sound, desc.loop ? MA_TRUE : MA_FALSE);
        if (desc.spatial)
        {
            ma_sound_set_attenuation_model(&voice.sound, ToMiniaudio(desc.attenuation));
            ma_sound_set_min_distance(&voice.sound, minDistance);
            ma_sound_set_max_distance(&voice.sound, maxDistance);
            ma_sound_set_rolloff(&voice.sound, SafePositive(desc.rolloff, 1.0f));
            ma_sound_set_doppler_factor(&voice.sound, std::isfinite(desc.dopplerFactor) && desc.dopplerFactor > 0.0f
                ? desc.dopplerFactor : Float(0.0f));
            ma_sound_set_position(&voice.sound, desc.position[0], desc.position[1], desc.position[2]);
        }
        if (desc.fadeInSeconds > 0.0f && std::isfinite(desc.fadeInSeconds))
        {
            ma_sound_set_fade_in_milliseconds(&voice.sound, 0.0f, 1.0f,
                static_cast<ma_uint64>(desc.fadeInSeconds * 1000.0f));
        }
        ma_uint64 delayFrames = 0;
        if (desc.startDelaySeconds > 0.0f && std::isfinite(desc.startDelaySeconds))
        {
            delayFrames = static_cast<ma_uint64>(desc.startDelaySeconds * static_cast<float>(state.desc.sampleRate));
            ma_sound_set_start_time_in_pcm_frames(&voice.sound, ma_engine_get_time_in_pcm_frames(&state.engine) + delayFrames);
        }
        if (startParked)
        {
            // 가상으로 시작한다: 틀지 않고 처음(시작 지연 뒤)부터 센다. 다음 갱신에서 순위에 들면 곧바로 울린다.
            voice.parked = true;
            voice.parkCursor = 0.0;
            voice.parkTime = now + delayFrames;
            voice.state = State::VoiceState::Playing;
            ++state.voicesStarted;
            ++state.voicesVirtualized;
            State::Clip& parkedClip = state.clips[desc.clip.index];
            parkedClip.started = true;
            parkedClip.lastStartFrame = now;
            return {index, voice.generation};
        }
        if (ma_sound_start(&voice.sound) != MA_SUCCESS)
        {
            voice.state = State::VoiceState::Paused;
            state.ReleaseVoice(index);
            return {};
        }
        voice.state = State::VoiceState::Playing;
        ++state.voicesStarted;
        State::Clip& started = state.clips[desc.clip.index];
        started.started = true;
        started.lastStartFrame = now;
        return {index, voice.generation};
    }

    void AudioMixer::Stop(AudioVoiceHandle handle, Float fadeOutSeconds)
    {
        if (false == IsInitialized())
        {
            return;
        }
        State::Voice* voice = m_state->Resolve(handle);
        if (voice == nullptr)
        {
            return;
        }
        if (fadeOutSeconds > 0.0f && std::isfinite(fadeOutSeconds) && voice->state == State::VoiceState::Playing
            && false == voice->parked)
        {
            ma_sound_stop_with_fade_in_milliseconds(&voice->sound, static_cast<ma_uint64>(fadeOutSeconds * 1000.0f));
            voice->state = State::VoiceState::Stopping;
            return;
        }
        m_state->ReleaseVoice(handle.index);
    }

    void AudioMixer::StopAllWithTag(UInt32 tag)
    {
        if (false == IsInitialized())
        {
            return;
        }
        for (UInt32 index = 0; index < m_state->voices.Size(); ++index)
        {
            const State::Voice& voice = m_state->voices[index];
            if (voice.state != State::VoiceState::Free && voice.tag == tag)
            {
                m_state->ReleaseVoice(index);
            }
        }
    }

    void AudioMixer::StopAll()
    {
        if (false == IsInitialized())
        {
            return;
        }
        for (UInt32 index = 0; index < m_state->voices.Size(); ++index)
        {
            m_state->ReleaseVoice(index);
        }
    }

    void AudioMixer::Pause(AudioVoiceHandle handle)
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        // 끝에 닿았는데 아직 거두지 않은 한 번짜리다(D-240). 멈춰 두면 다시 틀 때 처음부터 다시 울린다 - 지금 거둔다.
        if (voice != nullptr && voice->state == State::VoiceState::Playing && false == voice->looping && false == voice->parked
            && ma_sound_at_end(&voice->sound))
        {
            m_state->ReleaseVoice(handle.index);
            return;
        }
        if (voice != nullptr && voice->state == State::VoiceState::Playing)
        {
            if (voice->parked)
            {
                // 가상 보이스는 센 위치를 굳힌다 - 멈춘 동안 흐르지 않는다.
                const UInt64 now = m_state->Now();
                voice->parkCursor = m_state->VirtualCursor(*voice, now);
                voice->parkTime = now > voice->parkTime ? now : voice->parkTime;
            }
            ma_sound_stop(&voice->sound);
            voice->state = State::VoiceState::Paused;
        }
    }

    void AudioMixer::Resume(AudioVoiceHandle handle)
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        if (voice != nullptr && voice->state == State::VoiceState::Paused)
        {
            voice->state = State::VoiceState::Playing;
            if (voice->parked)
            {
                // 가상인 채 다시 센다. 울릴지는 다음 갱신의 순위가 정한다.
                const UInt64 now = m_state->Now();
                voice->parkTime = now > voice->parkTime ? now : voice->parkTime;
                return;
            }
            ma_sound_start(&voice->sound);
        }
    }

    Bool AudioMixer::IsAlive(AudioVoiceHandle handle) const
    {
        return IsInitialized() && m_state->Resolve(handle) != nullptr;
    }

    Bool AudioMixer::IsPaused(AudioVoiceHandle handle) const
    {
        const State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        return voice != nullptr && voice->state == State::VoiceState::Paused;
    }

    double AudioMixer::GetPlaybackSeconds(AudioVoiceHandle handle) const
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        if (voice == nullptr)
        {
            return 0.0;
        }
        if (voice->parked)
        {
            const State::Clip* clip = m_state->ResolveClip(voice->clip);
            return clip != nullptr && clip->desc.sampleRate > 0
                ? m_state->VirtualCursor(*voice, m_state->Now()) / static_cast<double>(clip->desc.sampleRate) : 0.0;
        }
        float seconds = 0.0f;
        if (ma_sound_get_cursor_in_seconds(&voice->sound, &seconds) != MA_SUCCESS)
        {
            return 0.0;
        }
        return seconds;
    }

    void AudioMixer::Seek(AudioVoiceHandle handle, double seconds)
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        if (voice == nullptr || false == std::isfinite(seconds))
        {
            return;
        }
        const State::Clip* clip = m_state->ResolveClip(voice->clip);
        if (clip == nullptr || clip->desc.sampleRate == 0)
        {
            return;
        }
        double frame = seconds < 0.0 ? 0.0 : seconds * static_cast<double>(clip->desc.sampleRate);
        if (clip->desc.frameCount > 0 && frame >= static_cast<double>(clip->desc.frameCount))
        {
            frame = static_cast<double>(clip->desc.frameCount - 1);
        }
        // 데이터 소스의 프레임 단위다(클립 자신의 샘플 레이트). 재생 중에도 목표만 적는다(miniaudio `seekTarget`).
        if (voice->parked)
        {
            voice->parkCursor = frame;
            const UInt64 now = m_state->Now();
            voice->parkTime = now > voice->parkTime ? now : voice->parkTime;
            return;
        }
        ma_sound_seek_to_pcm_frame(&voice->sound, static_cast<ma_uint64>(frame));
    }

    void AudioMixer::SetVolume(AudioVoiceHandle handle, Float volume)
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        if (voice != nullptr)
        {
            voice->volume = Clamp01(volume);
            ma_sound_set_volume(&voice->sound, voice->volume * voice->trim);
        }
    }

    void AudioMixer::SetPitch(AudioVoiceHandle handle, Float pitch)
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        if (voice != nullptr)
        {
            if (voice->parked)
            {
                // 옛 피치로 센 만큼을 굳히고 새 피치로 센다.
                const UInt64 now = m_state->Now();
                voice->parkCursor = m_state->VirtualCursor(*voice, now);
                voice->parkTime = now > voice->parkTime ? now : voice->parkTime;
            }
            voice->pitch = SafePositive(pitch, 1.0f);
            ma_sound_set_pitch(&voice->sound, voice->pitch);
        }
    }

    void AudioMixer::SetLooping(AudioVoiceHandle handle, Bool loop)
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        if (voice != nullptr)
        {
            voice->looping = loop;
            ma_sound_set_looping(&voice->sound, loop ? MA_TRUE : MA_FALSE);
        }
    }

    void AudioMixer::SetPosition(AudioVoiceHandle handle, const float position[3])
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        if (voice != nullptr && position != nullptr)
        {
            voice->position[0] = position[0];
            voice->position[1] = position[1];
            voice->position[2] = position[2];
            ma_sound_set_position(&voice->sound, position[0], position[1], position[2]);
        }
    }

    void AudioMixer::SetVelocity(AudioVoiceHandle handle, const float velocity[3])
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        if (voice != nullptr && velocity != nullptr)
        {
            ma_sound_set_velocity(&voice->sound, velocity[0], velocity[1], velocity[2]);
        }
    }

    void AudioMixer::SetBus(AudioVoiceHandle handle, AudioBusId bus)
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        if (voice == nullptr || false == m_state->IsBusValid(bus) || voice->bus == bus)
        {
            return;
        }
        // 재생 중에도 스레드 안전하다(miniaudio `ma_node_attach_output_bus`, D-198). 보이스를 다시 만들지 않는다.
        m_state->RouteVoice(*voice, bus, voice->filterRouted);
    }

    void AudioMixer::SetVoiceFilter(AudioVoiceHandle handle, Float lowPassHz, Float highPassHz)
    {
        State::Voice* voice = IsInitialized() ? m_state->Resolve(handle) : nullptr;
        if (voice == nullptr)
        {
            return;
        }
        const Float lowPass = PositiveOrZero(lowPassHz);
        const Float highPass = PositiveOrZero(highPassHz);
        const Bool filtered = lowPass > 0.0f || highPass > 0.0f;
        if (filtered && false == voice->filterRouted)
        {
            voice->filter->reset.store(true, std::memory_order_release);
        }
        voice->filter->lowPass.store(lowPass, std::memory_order_relaxed);
        voice->filter->highPass.store(highPass, std::memory_order_relaxed);
        if (filtered != voice->filterRouted)
        {
            m_state->RouteVoice(*voice, voice->bus, filtered);
        }
    }

    void AudioMixer::SetListener(const float position[3], const float forward[3], const float up[3])
    {
        if (false == IsInitialized())
        {
            return;
        }
        ma_engine* engine = &m_state->engine;
        if (position != nullptr)
        {
            ma_engine_listener_set_position(engine, 0, position[0], position[1], position[2]);
        }
        if (forward != nullptr)
        {
            ma_engine_listener_set_direction(engine, 0, forward[0], forward[1], forward[2]);
        }
        if (up != nullptr)
        {
            ma_engine_listener_set_world_up(engine, 0, up[0], up[1], up[2]);
        }
    }

    void AudioMixer::SetListenerVelocity(const float velocity[3])
    {
        if (IsInitialized() && velocity != nullptr)
        {
            ma_engine_listener_set_velocity(&m_state->engine, 0, velocity[0], velocity[1], velocity[2]);
        }
    }

    void AudioMixer::SetMasterVolume(Float volume)
    {
        if (IsInitialized())
        {
            m_state->masterVolume = Clamp01(volume);
            ma_engine_set_volume(&m_state->engine, m_state->masterVolume);
        }
    }

    Float AudioMixer::GetMasterVolume() const
    {
        return IsInitialized() ? m_state->masterVolume : Float(0.0f);
    }

    void AudioMixer::SetOutputGain(Float gain, Float seconds)
    {
        if (false == IsInitialized())
        {
            return;
        }
        State& state = *m_state;
        const Float target = Clamp01(gain);
        const Float frames = std::isfinite(seconds) && seconds > 0.0f
            ? seconds * static_cast<float>(state.desc.sampleRate) : Float(1.0f);
        // 0 에서 1 까지(가장 먼 거리)를 `seconds` 에 걷는 속도다. 지금 값은 오디오 스레드만 알므로 거리로 나누지 않는다.
        state.outputGainRate.store(1.0f / (frames > 1.0f ? frames : Float(1.0f)), std::memory_order_relaxed);
        state.outputGainTarget.store(target, std::memory_order_relaxed);
    }

    void AudioMixer::SetOutputLimiter(Bool enabled, Float ceiling)
    {
        if (IsInitialized())
        {
            m_state->limiterCeiling.store(Clamped(ceiling, 0.1f, 1.0f), std::memory_order_relaxed);
            m_state->limiterEnabled.store(enabled, std::memory_order_relaxed);
        }
    }

    Bool AudioMixer::IsOutputLimiterEnabled() const
    {
        return IsInitialized() && m_state->limiterEnabled.load(std::memory_order_relaxed);
    }

    Float AudioMixer::GetOutputGain() const
    {
        return IsInitialized() ? m_state->outputGainTarget.load(std::memory_order_relaxed) : 0.0f;
    }

    UInt32 AudioMixer::CopyRecentOutput(float* mono, UInt32 count) const
    {
        if (false == IsInitialized() || mono == nullptr)
        {
            return 0;
        }
        const State& state = *m_state;
        if (count > RecentCapacity)
        {
            count = RecentCapacity;
        }
        const UInt32 write = state.recentWrite.load(std::memory_order_acquire);
        for (UInt32 index = 0; index < count; ++index)
        {
            mono[index] = state.recent[(write - count + index) % RecentCapacity];
        }
        return count;
    }

    void AudioMixer::ComputeSpectrum(float* bands, UInt32 bandCount) const
    {
        if (bands == nullptr || bandCount == 0)
        {
            return;
        }
        for (UInt32 band = 0; band < bandCount; ++band)
        {
            bands[band] = 0.0f;
        }
        if (false == IsInitialized())
        {
            return;
        }
        const State& state = *m_state;
        constexpr UInt32 size = State::FftSize;
        CopyRecentOutput(state.fftReal, size);
        // 한 창이다. 크기 A 의 사인파가 A 로 읽히게 창의 평균(0.5)과 FFT 의 N/2 로 나눈다.
        for (UInt32 index = 0; index < size; ++index)
        {
            const Float window = 0.5f - 0.5f * std::cos(Tau * static_cast<float>(index) / static_cast<float>(size - 1));
            state.fftReal[index] *= window;
            state.fftImag[index] = 0.0f;
        }
        Fft(state.fftReal, state.fftImag, size);
        const Float nyquist = static_cast<float>(state.desc.sampleRate) * 0.5f;
        const Float lowest = 30.0f;
        const Float binHz = static_cast<float>(state.desc.sampleRate) / static_cast<float>(size);
        const Float scale = 1.0f / (static_cast<float>(size) * 0.25f);
        for (UInt32 band = 0; band < bandCount; ++band)
        {
            const Float from = lowest * std::pow(nyquist / lowest, static_cast<float>(band) / static_cast<float>(bandCount));
            const Float to = lowest * std::pow(nyquist / lowest, static_cast<float>(band + 1) / static_cast<float>(bandCount));
            UInt32 first = static_cast<std::uint32_t>(from / binHz);
            UInt32 last = static_cast<std::uint32_t>(to / binHz);
            if (last < first)
            {
                last = first;
            }
            if (last >= size / 2)
            {
                last = size / 2 - 1;
            }
            Float magnitude = 0.0f;
            for (UInt32 bin = first; bin <= last; ++bin)
            {
                const Float value = std::sqrt(state.fftReal[bin] * state.fftReal[bin]
                    + state.fftImag[bin] * state.fftImag[bin]) * scale;
                if (value > magnitude)
                {
                    magnitude = value;
                }
            }
            const Float decibels = magnitude > 0.0f ? 20.0f * std::log10(magnitude) : -200.0f;
            bands[band] = Clamp01((decibels + 72.0f) / 72.0f);
        }
    }

    double AudioMixer::GetTimeSeconds() const
    {
        if (false == IsInitialized())
        {
            return 0.0;
        }
        return static_cast<double>(ma_engine_get_time_in_pcm_frames(&m_state->engine))
            / static_cast<double>(m_state->desc.sampleRate);
    }

    Bool AudioMixer::WaitForStreamsIdle(Float timeoutSeconds)
    {
        if (false == IsInitialized())
        {
            return true;
        }
        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(static_cast<long long>((std::isfinite(timeoutSeconds) && timeoutSeconds > 0.0f
                ? timeoutSeconds : Float(0.0f)) * 1000.0f));
        for (;;)
        {
            Bool idle = true;
            for (const OwnerPtr<StreamSlot>& slot : m_state->streams)
            {
                idle = idle && slot->phase.load(std::memory_order_acquire) == static_cast<std::uint32_t>(StreamPhase::Idle);
            }
            if (idle)
            {
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    AudioMixer::Stats AudioMixer::GetStats() const
    {
        Stats stats;
        if (false == IsInitialized())
        {
            return stats;
        }
        const State& state = *m_state;
        stats.activeVoices = state.desc.maxVoices - static_cast<std::uint32_t>(state.freeVoices.Size());
        for (const State::Voice& voice : state.voices)
        {
            stats.virtualVoices += voice.state != State::VoiceState::Free && voice.parked ? 1u : 0u;
        }
        stats.maxVoices = state.desc.maxVoices;
        stats.maxAudibleVoices = state.VirtualizationEnabled() ? state.desc.maxAudibleVoices : state.desc.maxVoices;
        stats.registeredClips = static_cast<std::uint32_t>(state.clips.Size() - state.freeClips.Size());
        stats.voicesStarted = state.voicesStarted;
        stats.voicesStolen = state.voicesStolen;
        stats.voicesRejected = state.voicesRejected;
        stats.voicesCulled = state.voicesCulled;
        stats.voicesThrottled = state.voicesThrottled;
        stats.voicesReplaced = state.voicesReplaced;
        stats.voicesVirtualized = state.voicesVirtualized;
        stats.voicesRealized = state.voicesRealized;
        stats.allocatorGrowths = state.allocator.GetGrowths();
        stats.lastPeak = state.peak.load(std::memory_order_relaxed);
        stats.renderedFrames = state.renderedFrames.load(std::memory_order_relaxed);
        stats.maxStreams = static_cast<std::uint32_t>(state.streams.Size());
        for (const OwnerPtr<StreamSlot>& slot : state.streams)
        {
            if (slot->phase.load(std::memory_order_relaxed) != static_cast<std::uint32_t>(StreamPhase::Idle))
            {
                ++stats.activeStreams;
            }
            stats.streamUnderruns += slot->underruns.load(std::memory_order_relaxed);
        }
        return stats;
    }
}

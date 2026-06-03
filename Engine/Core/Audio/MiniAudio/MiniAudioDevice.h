#pragma once

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  CMiniAudioDevice ─ miniaudio 백엔드
//
//  miniaudio.h 가 Engine/ThirdParty/miniaudio/ 에 추가되어 있을 때만 본격 활성화.
//  Engine.vcxproj 의 PreprocessorDefinitions 에 JBRO_HAS_MINIAUDIO=1 을 추가하면
//  CMiniAudioDevice 가 실제 miniaudio 호출로 작동.  정의되어 있지 않으면 본
//  파일은 빈 클래스로 컴파일되어 무해.  Engine::InitializeAudio 가 그 경우
//  자동으로 CEmptyAudioDevice 로 폴백.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

#include "Core/Audio/IAudioDevice.h"

#if defined(JBRO_HAS_MINIAUDIO) && JBRO_HAS_MINIAUDIO

#include "Core/Audio/IAudioPlayer.h"
#include "Core/Audio/IAudioListener.h"
#include "Core/Audio/IAudioBus.h"
#include "Core/Audio/IAudioEffect.h"

// miniaudio 의 ma_engine / ma_device 등을 직접 헤더에 노출하지 않기 위해
// PIMPL 패턴.  실제 멤버는 cpp 의 익명 namespace 에서 정의된 구조체에 보관.
struct MiniAudioDeviceImpl;

class CMiniAudioDevice final : public IAudioDevice
{
public:
	CMiniAudioDevice();
	~CMiniAudioDevice();

	bool Initialize(const AudioDeviceDesc& desc) override;
	void Finalize() override;
	void Tick(float deltaSeconds) override;

	OwnerPtr<IAudioPlayer>  CreatePlayer (const AudioPlayerDesc& desc) override;
	// 편의 메서드 — 디스크 파일에서 직접 ma_sound 기반 Player 생성.
	// 에디터 미리듣기 처럼 PCM/Streaming 분기 없이 빠르게 한 인스턴스를 띄울 때 사용.
	// bus 로 라우팅 (기본 Master). 버스 미초기화 시 endpoint 직결로 폴백.
	OwnerPtr<IAudioPlayer>  CreatePlayerFromFile(const char* filePathUtf8, EAudioBusKind bus = EAudioBusKind::Master);
	OwnerPtr<IAudioBus>     CreateBus    (EAudioBusKind kind) override;
	SafePtr<IAudioBus>      GetBus       (EAudioBusKind kind) override;
	OwnerPtr<IAudioEffect>  CreateEffect (EAudioEffectKind kind) override;
	SafePtr<IAudioListener> GetPrimaryListener() override;

	double GetGlobalAudioTimeSeconds() const override;
	double GetOutputLatencySeconds()  const override;
	void RegisterPlayerMarker(SafePtr<IAudioPlayer> player,
	                          std::uint64_t frame,
	                          std::function<void()> callback) override;

	void  SetMasterVolume(float v) override;
	float GetMasterVolume() const override;

private:
	OwnerPtr<MiniAudioDeviceImpl> m_impl;
};

#endif // JBRO_HAS_MINIAUDIO

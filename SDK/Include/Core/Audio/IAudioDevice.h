#pragma once

#include "Core/Audio/AudioTypes.h"
#include "Core/Audio/IAudioPlayer.h"   // AudioPlayerDesc
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class IAudioBus;
class IAudioEffect;
class IAudioListener;

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  IAudioDevice ─ 사운드 백엔드 추상화
//
//  RHI 의 IRHIDevice 와 같은 역할.  현재 단일 구현(CMiniAudioDevice)이지만
//  필요 시 다른 백엔드(예: 네이티브 WASAPI, 네이티브 Web Audio API) 로 교체 가능.
//
//  미니오디오 가 자체적으로 거의 모든 플랫폼(Windows/macOS/Linux/Android/iOS/Web)
//  을 지원하므로 1차 구현은 단일 백엔드로 충분하다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
class IAudioDevice : public EnableSafeFromThis<IAudioDevice>
{
public:
	virtual ~IAudioDevice() = default;

public:
	virtual bool Initialize(const AudioDeviceDesc& desc) = 0;
	virtual void Finalize() = 0;

	// 매 프레임 — ended 콜백 처리, listener/player 위치 push, 등.
	virtual void Tick(float deltaSeconds) = 0;

	// ── 생성 ───────────────────────────────────────────────────────────────
	virtual OwnerPtr<IAudioPlayer>  CreatePlayer (const AudioPlayerDesc& desc) = 0;

	// 디바이스 소유 목록 **밖의** 버스를 따로 만든다(Master 하위). 목록에 있는 버스는
	// ConfigureBuses 가 만들고 GetBus 로 얻는다.
	virtual OwnerPtr<IAudioBus>     CreateBus    (const char* name) = 0;

	// 디바이스가 소유하는 믹싱 버스를 이름으로 반환. player 라우팅·카테고리 볼륨의 진입점.
	// 이름이 없거나 비어 있으면 Master 를 준다(목록에 없는 이름은 경고 1회).
	virtual SafePtr<IAudioBus>      GetBus       (const char* name) = 0;

	// 프로젝트가 정의한 버스 목록을 주입한다. "Master" 는 목록에 없어도 항상 만들어지고,
	// 나머지는 전부 Master 의 자식이 된다. 목록에 Master 가 있으면 그 볼륨만 반영한다.
	// 각 버스는 정의된 Volume 으로 시작한다(스크립트가 SetVolume 하면 그쪽이 이긴다).
	//
	// **플레이어가 생기기 전에만 부를 것** — miniaudio 는 살아 있는 sound 의 출력 group 을
	// 바꾸지 못하므로, 이미 붙은 소리가 있는 상태에서 버스를 갈아엎으면 그 소리들이 갈 곳을
	// 잃는다. 호출 지점은 디바이스 초기화 직후 / 프로젝트 로드 시점이다.
	virtual void ConfigureBuses(const std::vector<AudioBusDef>& buses) = 0;

	// 현재 구성된 버스 이름들(정의 순서, Master 가 항상 첫 번째). 저작 UI 표시용.
	virtual std::vector<std::string> GetBusNames() const = 0;
	virtual OwnerPtr<IAudioEffect>  CreateEffect (EAudioEffectKind kind) = 0;

	// 캔버스당 활성 1개 (backend 가 소유). 게임 측 AudioListener 컴포넌트가
	// SetPosition 등으로 매 프레임 갱신.
	virtual SafePtr<IAudioListener> GetPrimaryListener() = 0;

	// ── 정밀 싱크 (리듬게임 등) ────────────────────────────────────────────
	// 오디오 콜백 기준 단조 증가 시계 — 게임 frame rate 와 무관하게 일정.
	virtual double GetGlobalAudioTimeSeconds() const = 0;
	// 디바이스 출력 지연 — 사용자가 실제 듣는 시점 = GlobalAudioTime - OutputLatency.
	virtual double GetOutputLatencySeconds()  const = 0;

	// 재생 중 특정 frame 도달 시 콜백.  콜백은 오디오 콜백 스레드에서 호출될
	// 수 있으므로 호출자가 game-thread queue 로 dispatch 하는 정책 적용 권장.
	virtual void RegisterPlayerMarker(SafePtr<IAudioPlayer> player,
	                                  std::uint64_t frame,
	                                  std::function<void()> callback) = 0;

	// ── 글로벌 볼륨 ────────────────────────────────────────────────────────
	virtual void  SetMasterVolume(float volume) = 0;
	virtual float GetMasterVolume() const = 0;
};

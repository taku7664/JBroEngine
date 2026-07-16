# Codex Audio System TODO

## 목적

오디오를 한 번 초기화하거나 재생한 뒤 종료할 때 발생하는 miniaudio 크래시를 먼저 제거하고,
오디오 자산·재생 인스턴스·DSP·에디터 프리뷰를 수명과 스레드 경계가 명확한 구조로 정리합니다.

이 문서는 공용 `tasks/todo.md`와 분리된 코사원 전용 실행 체크리스트입니다.

## 작업 원칙

- 각 단계는 `구현 -> 단계 전용 검증 -> diff 자체 검토 -> 원자적 커밋` 순서로 완료합니다.
- 한 단계의 검증이 실패하면 다음 단계로 넘어가지 않습니다.
- 기존 작업 트리의 무관한 변경은 스테이징하거나 커밋하지 않습니다.
- Engine 공개 헤더를 바꾸면 SDK 미러와 Windows/Web 빌드 소스 정합성을 같은 단계에서 확인합니다.
- 오디오 콜백에서는 동적 할당, 잠금 대기, 파일 I/O, 엔진 오브젝트 접근을 하지 않습니다.
- 실패를 Stub으로 숨기지 않고 호출자와 로그가 원인을 구분할 수 있게 합니다.
- 크래시 스택이나 재현 결과가 정적 분석과 다르면 즉시 가설을 폐기하고 계획을 갱신합니다.

## 확인된 기준 상태

- 에디터 오디오 프리뷰는 오디오 에셋 진입 시 별도 `CMiniAudioDevice`를 초기화합니다.
- 에디터 전체 종료 경로는 ImWindow의 `Finalize()/OnDestroy()`를 호출하지 않습니다.
- `CMiniAudioDevice` 기본 소멸자는 `Finalize()`를 호출하지 않아 `ma_engine_uninit()`이 누락될 수 있습니다.
- Player/Effect/Custom Bus는 장치 내부 `ma_engine`과 node graph를 빌리지만 장치는 자식 객체를 추적하지 않습니다.
- Freeverb 파라미터는 게임 스레드에서 쓰고 오디오 콜백에서 동기화 없이 읽습니다.
- `CAudioSystem`은 `CAudioAsset` 대신 파일 경로와 `CMiniAudioDevice` downcast를 사용합니다.
- Streaming/Decompressed, DefaultBus, DefaultVolume 등 임포트 설정이 실제 재생 경로에 연결되지 않았습니다.
- non-loop `PlayOnStart` 인스턴스는 종료 후 다음 프레임에 재생성되어 반복 재생될 수 있습니다.

## 완료 기준

- 프리뷰 미사용, 프리뷰 초기화만 수행, 실제 재생, 효과 적용 재생의 모든 종료 경로가 크래시 없이 반복됩니다.
- Device보다 Player/Effect/Bus가 오래 살아도 use-after-free가 발생하지 않습니다.
- 오디오 콜백과 게임 스레드 사이에 C++ 데이터 레이스가 없습니다.
- AudioSystem이 backend 구체 타입을 알지 않고 `AudioPlayerDesc`와 오디오 자산을 통해 재생합니다.
- 임포트 모드와 기본 옵션이 실제 재생 동작에 반영됩니다.
- 동시 재생 상한과 voice 정리 정책이 작동합니다.
- Windows와 Web 빌드 경로가 함께 유지됩니다.

---

## 단계 1 - 종료 크래시 차단과 RAII 복구

상태: 구현 완료 / 실제 오디오 재생 종료 검증 대기

### 구현

- [x] `CMiniAudioDevice` 소멸자가 idempotent `Finalize()`를 호출하도록 변경합니다.
- [x] 중복 `Initialize()`가 기존 엔진을 누수시키지 않도록 재초기화 계약을 정합니다.
- [x] 에디터 종료 시 모든 ImWindow를 명시적으로 `Finalize()`하고 소유 컨테이너를 비운 뒤 ImGui를 종료합니다.
- [x] `EditorAudioPreview::Shutdown()`이 에디터 정상 종료에서 정확히 한 번 호출되는지 보장합니다.
- [x] 잘못된 "Shutdown 미호출도 안전" 주석을 실제 계약에 맞게 정리합니다.

### 검증

- [x] 프리뷰 미사용 종료
- [ ] 오디오 에셋 선택 후 미재생 종료
- [ ] Play -> Stop -> 에디터 종료 20회 반복
- [ ] Play 중 에디터 종료 20회 반복
- [ ] miniaudio device/thread가 종료 후 남지 않는지 디버거 또는 진단 로그로 확인
- [x] `Release_Editor|x64` 빌드
- [x] 관련 수명 테스트 또는 최소 재현 테스트 추가

### 커밋

- [x] `Fix audio preview shutdown lifetime` 형태의 단일 커밋

## 단계 2 - Device와 자식 오디오 객체의 수명 계약

상태: 대기

### 구현

- [ ] Device가 생성한 Player/Effect/Custom Bus를 추적하거나 동일 효력의 backend-state 계약을 도입합니다.
- [ ] Device 종료 시 `Player -> Effect -> child Bus -> standard child Bus -> Master Bus -> Engine` 순으로 안전하게 무효화합니다.
- [ ] 자식 객체 소멸자가 이미 종료된 engine/node graph를 접근하지 않게 합니다.
- [ ] 외부 OwnerPtr가 Device보다 오래 사는 반례를 명시적으로 지원합니다.
- [ ] Debug 빌드에서 live child count와 잘못된 종료 순서를 진단합니다.

### 검증

- [ ] 살아 있는 Player를 둔 채 Device Finalize
- [ ] 살아 있는 Effect/Custom Bus를 둔 채 Device Finalize
- [ ] Device Finalize 후 각 자식 OwnerPtr 파괴
- [ ] Initialize/Finalize 100회 반복
- [ ] ASan 가능 타깃 또는 VS 메모리 진단으로 UAF/이중 해제 확인
- [ ] `Release_Editor|x64`, `Release_Game|x64` 빌드

### 커밋

- [ ] `Enforce audio backend child lifetime` 형태의 단일 커밋

## 단계 3 - AudioSystem 재생 상태와 효과 해제 순서 수정

상태: 대기

### 구현

- [ ] `PlayerInstance::Reset()` 또는 동등한 명시적 해제 경로를 추가합니다.
- [ ] Player가 효과를 분리하고 파괴된 뒤 Effect Owner가 파괴되도록 순서를 보장합니다.
- [ ] non-loop `PlayOnStart`가 한 번 종료된 뒤 자동 재생성되지 않도록 상태를 기록합니다.
- [ ] Disable/Enable, AudioGuid 변경, 컴포넌트 삭제, 시뮬레이션 중지 동작을 정의합니다.
- [ ] 로딩 실패 Stub을 자동 재생 완료로 오인하지 않도록 실패 상태를 분리합니다.

### 검증

- [ ] non-loop PlayOnStart는 정확히 1회 재생
- [ ] Loop는 명시적으로 중지할 때까지 재생
- [ ] Disable/Enable 시 정책대로 재생 상태 전환
- [ ] 효과 0/1/N개 상태에서 Simulation Stop과 Scene Clear 반복
- [ ] AudioPlayer 멀티 컴포넌트가 독립 상태 유지
- [ ] `Release_Editor|x64`, GameScript 빌드

### 커밋

- [ ] `Make audio instance teardown deterministic` 형태의 단일 커밋

## 단계 4 - 자산 기반 Player 생성과 backend 추상화 복구

상태: 대기

### 구현

- [ ] `IAudioDevice::CreatePlayer(const AudioPlayerDesc&)`를 실제 구현합니다.
- [ ] `CAudioSystem`의 `CMiniAudioDevice` downcast와 직접 파일 경로 재생을 제거합니다.
- [ ] Decompressed 자산은 보유 PCM을 사용하고 Streaming 자산은 스트림 데이터 소스를 사용합니다.
- [ ] 패키지 MemoryPayload에서도 Streaming이 가능한 VFS/data-source 계약을 마련합니다.
- [ ] AudioAsset의 DefaultVolume/Loop/Is3D/MinDistance/MaxDistance/DefaultBus 적용 우선순위를 정의합니다.
- [ ] 컴포넌트 override와 자산 default를 구분할 수 있는 데이터 모델을 적용합니다.
- [ ] 임포트 모드 변경 시 PCM/stream 상태가 실제로 재구성되도록 ReloadInto를 수정합니다.

### 검증

- [ ] WAV/MP3/FLAC/OGG 각각 Decompressed와 Streaming 재생
- [ ] 한글 경로와 패키지 MemoryPayload 재생
- [ ] 동일 자산 다중 인스턴스의 데이터 공유 확인
- [ ] DefaultBus 및 자산 기본값 적용
- [ ] Windows 패키지 실제 프로젝트 검증
- [ ] Web 빌드 및 브라우저 재생 검증

### 커밋

- [ ] 자산 Player 구현과 패키지/VFS 변경을 필요 시 2개 원자적 커밋으로 분리

## 단계 5 - DSP 스레드 안전성과 효과 타입 정합성

상태: 대기

### 구현

- [ ] 게임 스레드의 파라미터 변경을 오디오 스레드 안전 snapshot/command queue로 전달합니다.
- [ ] Freeverb의 Wet/Dry/Width/Feedback/Damping 데이터 레이스를 제거합니다.
- [ ] 재생 중 LPF/HPF `reinit`의 안전한 갱신 방식을 적용합니다.
- [ ] DSP 파라미터 범위와 NaN/Inf를 검증합니다.
- [ ] 미지원 Distortion/Compressor/Limiter는 Reverb로 폴백하지 않고 명시적으로 실패시킵니다.
- [ ] 지원할 효과는 실제 구현과 enum/에디터 노출을 동시에 추가합니다.
- [ ] 효과 체인 교체 시 클릭과 끊김을 줄이는 안전한 연결 전환을 적용합니다.

### 검증

- [ ] 재생 중 모든 파라미터를 빠르게 왕복 변경
- [ ] 효과 종류 연속 교체 및 체인 재정렬
- [ ] Thread Sanitizer 가능 환경 또는 별도 스트레스 테스트
- [ ] 1/10/64 voices에 동일 효과 적용 후 안정성 확인
- [ ] Windows/Web DSP 결과 비교

### 커밋

- [ ] thread-safe parameter transport와 효과 정합성을 독립 커밋으로 분리

## 단계 6 - 런타임 성능과 polyphony

상태: 대기

### 구현

- [ ] Player별 마지막 적용 Volume/Pitch/Loop/Spatial 값을 캐시해 변경 시에만 push합니다.
- [ ] Effect AssetRef/generation을 PlayerInstance에 캐시하고 매 프레임 LoadAsset을 제거합니다.
- [ ] 매 프레임 `seen` unordered_set 할당을 frame stamp 또는 직접 인덱스로 제거합니다.
- [ ] `MaxPolyphony`를 적용하고 priority/distance/age 기반 voice stealing 정책을 추가합니다.
- [ ] 짧은 SFX와 긴 BGM에 맞는 preload/stream 정책 및 경고를 추가합니다.
- [ ] 공유 가능한 디코딩 데이터와 bus effect를 활용해 voice별 중복 DSP를 줄입니다.

### 검증

- [ ] 1/32/64/128 voices CPU, 메모리, frame time 기준선 측정
- [ ] 동일 자산 다중 재생 시 디코딩 데이터 중복 여부 측정
- [ ] voice stealing 결과가 결정적인지 확인
- [ ] 프레임 중 동적 할당 횟수 측정
- [ ] 변경 전후 수치 비교 기록

### 커밋

- [ ] 캐시 최적화와 polyphony 정책을 각각 원자적 커밋

## 단계 7 - 에디터 프리뷰 성능과 오류 UX

상태: 대기

### 구현

- [ ] 오디오 에셋 선택 시가 아니라 실제 Play 요청 시 프리뷰 장치를 초기화합니다.
- [ ] 프리뷰에서 자산 PCM과 miniaudio 재디코딩이 중복되지 않게 합니다.
- [ ] 정지 중이거나 재생 위치가 변하지 않으면 FFT를 다시 계산하지 않습니다.
- [ ] 긴 파일 파형 peak를 비동기 생성하고 import 결과로 캐시하는 방안을 적용합니다.
- [ ] 장치 초기화/파일 로드/디코딩/재생 실패를 Inspector에 구체적으로 표시합니다.
- [ ] 프리뷰 device, player, decoder 메모리가 선택 해제 후 기준선으로 돌아오는지 표시하거나 진단합니다.

### 검증

- [ ] 미재생 선택에서 audio device가 생성되지 않음
- [ ] 정지 상태 Inspector CPU가 기준선으로 복귀
- [ ] 긴 음원 선택 시 UI 멈춤 시간 측정
- [ ] 잘못된 파일/누락 파일/미지원 포맷별 오류 메시지 확인
- [ ] 프리뷰 에셋 100개 순회 후 메모리 계단 증가 없음

### 커밋

- [ ] `Defer and cache editor audio preview work` 형태의 단일 커밋

## 단계 8 - 공간음향과 믹서의 현재 공개 계약 완성

상태: 대기

### 구현

- [ ] AudioPlayer Is3D, position, min/max distance를 실제 miniaudio spatialization에 연결합니다.
- [ ] AudioListener position/forward/master volume을 실제 backend에 연결합니다.
- [ ] 다중 Listener를 경고하고 명시적인 priority로 하나를 선택합니다.
- [ ] Project Settings의 Master/Music/SFX/Voice/UI 볼륨을 저장하고 런타임 bus에 적용합니다.
- [ ] 버스 mute/solo와 프로젝트 로드 시 설정 복원을 구현합니다.

### 검증

- [ ] 2D/3D 전환, 거리 감쇠, 비균일 Transform과 listener 이동
- [ ] 다중 Listener 선택 규칙
- [ ] Project Settings 저장/재로드 및 패키지 적용
- [ ] Windows/Web 기능 동등성 확인

### 커밋

- [ ] spatialization과 mixer settings를 각각 원자적 커밋

---

## 추가 기능 제안 백로그

아래 항목은 안정화 단계 완료 후 사용자 우선순위를 확인해 별도 단계로 승격합니다.

### 재생 API와 사용자 편의

- [ ] Script API: Play, Pause, Resume, Stop, Seek, IsPlaying
- [ ] PlayOneShot과 반환형 VoiceHandle
- [ ] FadeIn/FadeOut/CrossFade와 클릭 방지
- [ ] 예약 재생 PlayAt과 샘플 정확도 marker callback
- [ ] BGM gapless loop, loop start/end point
- [ ] Audio Cue/Playlist: 랜덤, 순차, 가중치, 반복 방지

### 믹서와 연출

- [ ] Mixer Window와 bus tree 편집
- [ ] Mixer Snapshot 및 장면 전환 보간
- [ ] side-chain ducking
- [ ] bus별 DSP chain, send/return bus, shared reverb zone
- [ ] mute/solo, peak/RMS meter, clipping 표시

### 공간음향

- [ ] 사용자 정의 attenuation curve
- [ ] stereo pan, doppler, velocity 추적
- [ ] Physics2D raycast 기반 occlusion/obstruction
- [ ] Audio Reverb Zone과 영역 블렌딩
- [ ] Listener priority 및 camera 연동 옵션

### 임포트와 자산

- [ ] mono/stereo 변환 옵션
- [ ] target sample rate, quality/compression 설정
- [ ] loudness 분석 및 normalization
- [ ] silence trim, waveform/seek-table 사전 생성
- [ ] 긴 음원 Streaming 권장과 짧은 음원 Decompressed 권장 경고
- [ ] loop point/marker 메타데이터 편집

### 진단과 플랫폼

- [ ] Audio Profiler: active/virtual voices, decoder memory, DSP CPU, underrun
- [ ] 장치 목록·출력 장치 선택·hot unplug 복구
- [ ] 백그라운드/포커스 상실 시 pause/mute/continue 정책
- [ ] Web autoplay unlock 상태와 사용자 입력 대기 UI
- [ ] 모바일 오디오 interruption과 route change 대응
- [ ] deterministic offline render를 이용한 DSP 골든 테스트

## 전체 검증 매트릭스

- Windows D3D11 Editor: Debug/Release
- Windows Game 패키지: 실제 프로젝트 저장·빌드·실행
- WebGPU Web 빌드: Chromium 계열 브라우저에서 unlock/재생/정지/scene transition
- 빈 오디오 장치 fallback: CI/headless에서 동일 API 무해 동작
- WAV, MP3, FLAC, OGG; 한글·공백 경로; 손상 파일
- 프리뷰/런타임 동시 재생, 시뮬레이션 Stop, 프로젝트 닫기, 에디터 종료

## 작업 기록

각 단계 완료 시 아래 형식으로 이 문서에 기록합니다.

```text
### 단계 N 완료 - YYYY-MM-DD
- 구현:
- 검증:
- 커밋:
- 남은 위험:
```

### 단계 1 구현 완료 - 2026-07-16

- 구현: 오디오 장치 소멸자 RAII, 중복 초기화 실패 계약, ImWindow 역순 종료와 컨테이너 정리, 정상 종료의 프리뷰 Shutdown 연결
- 검증: `Release_Editor|x64` 성공, `BuildTools/Tests/AudioEditorShutdownSmoke.ps1`로 프리뷰 미사용 정상 종료 20/20회 성공
- 커밋: `Fix audio preview shutdown lifetime`
- 남은 위험: Windows UI 캡처가 `SetIsBorderRequired failed (0x80004002)`로 중단되어 오디오 선택, Play/Stop, 재생 중 종료와 device/thread 잔존 검사는 아직 수동 검증이 필요함
- 진행 제한: 작업 원칙에 따라 위 실제 재생 종료 검증 전에는 단계 2로 넘어가지 않음

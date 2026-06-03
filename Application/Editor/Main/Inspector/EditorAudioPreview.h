#pragma once

#include "Engine/Core/Asset/AssetTypes.h"   // AssetGuid
#include "Engine/Core/Audio/AudioTypes.h"   // EAudioEffectKind

#include <map>
#include <string>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  EditorAudioPreview ─ 에디터 전용 사운드 미리듣기 서비스
//
//  Inspector 의 미리보기 영역에서만 사용되는 가벼운 싱글톤.
//  miniaudio 백엔드를 lazy-init 으로 한 번만 띄우고, 한 번에 한 자산만 재생한다.
//  새 파일을 PlayFile 로 요청하면 이전 player 는 즉시 정지·해제된다.
//
//  반드시 게임 런타임의 오디오 디바이스와 분리된 인스턴스 — 게임이 따로 사운드를
//  띄우더라도 충돌 없이 동작.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

namespace EditorAudioPreview
{
    // 디바이스 lazy-init. 이미 초기화되어 있으면 no-op. 실패해도 안전(no-op stub 으로 폴백).
    void EnsureInitialized();

    // 백엔드 종료 — 디바이스/플레이어 해제. 정상 종료 시 호출 권장 (호출 안 해도 누수는 없음).
    void Shutdown();

    // absPathUtf8 파일을 처음부터 재생. 이전 미리듣기는 자동 정지·해제.
    void PlayFile(const char* absPathUtf8, const AssetGuid& guid);

    // 효과(reverb 등)를 적용해 재생 — 효과 에디터 미리듣기용.
    // kind/params 로 효과 노드를 만들어 player 에 부착한다.
    void PlayFileWithEffect(const char* absPathUtf8, const AssetGuid& guid,
                            EAudioEffectKind kind, const std::map<std::string, float>& params);

    // 현재 재생을 정지하고 player 를 해제한다. 안전하게 여러 번 호출 가능.
    void Stop();

    // 현재 EditorAudioPreview 가 추적 중인 자산의 GUID (재생/정지 무관).
    // 자산 선택이 바뀌면 Stop() 으로 같이 해제.
    const AssetGuid& GetCurrentGuid();

    // 현재 player 가 실제로 재생 중인지.
    bool IsPlaying();

    // 진행 표시용 — 현재 위치 / 전체 길이 (초). player 가 없으면 0 반환.
    double GetCurrentPositionSeconds();
    double GetCurrentDurationSeconds();

    // 지정한 초로 재생 위치 이동 (스크럽).
    void SeekSeconds(double seconds);
}

#pragma once

#include "Engine/Core/Asset/AssetTypes.h"   // AssetGuid, AssetMetaData

#include <vector>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  IAssetInspectorPreviewHandler ─ Inspector 의 "맨 위 미리보기" 영역을 그리는
//  자산 타입별 핸들러.  포커스 라이프사이클을 가진다:
//
//    OnEnter  : 이 핸들러가 활성화될 때 1 회 — 자원 셋업 (PCM bind, 캐시 등)
//    OnStay   : 활성 상태인 동안 매 프레임 — 그리기 + 인터랙션 (true 반환 시
//               호출자가 마무리 Separator 를 추가)
//    OnExit   : 이 핸들러가 비활성화될 때 1 회 — 자원 해제 (audio stop, unbind 등)
//
//  Enter/Exit 는 다음 케이스에서 일관되게 호출된다:
//    - 자산 미선택 → 자산 선택            : OnEnter
//    - 자산 A 선택 → 자산 B 선택          : A.OnExit → B.OnEnter
//    - 자산 선택 → 엔티티 선택            : OnExit   (포커스 이동)
//    - 자산 선택 → 선택 해제              : OnExit
//    - Inspector / 에디터 종료            : OnExit (ShutdownAll 경유)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class IAssetInspectorPreviewHandler
{
public:
    virtual ~IAssetInspectorPreviewHandler() = default;
    virtual bool CanPreview(const AssetMetaData& metaData) const = 0;

    virtual void OnEnter(const AssetMetaData& /*metaData*/) {}
    virtual bool OnStay (const AssetMetaData&  metaData) = 0;
    virtual void OnExit () {}
};

namespace AssetInspectorPreview
{
    // 자산 미리보기 영역 호출 — 매칭 handler 의 Enter/Stay 를 자동 관리.
    // 자산이 바뀌면 이전 핸들러 OnExit → 새 핸들러 OnEnter 가 자동으로 일어남.
    bool DrawTopPreview(const AssetMetaData& metaData);

    // Inspector 가 자산을 더 이상 보여주지 않을 때(엔티티 선택/선택 해제 등) 호출.
    // 현재 활성 핸들러가 있다면 OnExit 로 마무리.
    void NotifyInspectionLost();

    // 에디터 종료 시 강제 정리.
    void ShutdownAll();
}

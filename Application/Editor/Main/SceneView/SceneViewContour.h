#pragma once

#include "Engine/Core/Asset/AssetTypes.h"
#include "Engine/GameFramework/ECS/EntityTypes.h"
#include "Utillity/Math/Vector2T.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class CScene;
class IAssetManager;
class IDebugDraw2D;
struct ImDrawList;
struct ImVec2;

// ── CSceneViewContour ─────────────────────────────────────────────────────────
//
// 스프라이트 Alpha 외곽선 폴리곤 캐시 및 렌더링 담당.
//
//  GetOrBuild        : 픽셀 경계 트레이싱으로 로컬 공간 컨투어 폴리곤 빌드 (캐시)
//  DrawOutlinesImGui : ImGui 화면 공간 외곽선 (다중 선택 지원)
//
// 하나의 스프라이트가 여러 개의 분리된 컨투어를 가질 수 있으므로
// 캐시 값은 vector<vector<Vector2>> (컨투어 목록) 입니다.

class CSceneViewContour
{
public:
    void Clear() { m_cache.clear(); }

    // 로컬 스프라이트 공간 컨투어 목록 획득.
    // 각 컨투어: x,y ∈ [-0.5, 0.5], y-up, 원점 = 스프라이트 중앙.
    // 없으면 픽셀 경계 트레이싱 후 캐시에 저장. 실패 시 nullptr.
    const std::vector<std::vector<Vector2>>* GetOrBuild(
        IAssetManager& assetMgr,
        const AssetGuid& spriteGuid,
        std::uint32_t frameIndex);

    // ImGui 외곽선: 선택된 엔티티 목록의 서브트리 외곽선을 화면 공간으로 그리기.
    // 다중 선택을 지원하며, 동일 스프라이트의 중복 렌더링을 방지.
    void DrawOutlinesImGui(
        ImDrawList* dl,
        const CScene& scene,
        IAssetManager* assetMgr,
        const std::vector<EntityId>& selectedEntities,
        const ImVec2& vpMin, const ImVec2& vpSize,
        const Vector2& camPos, float camSize);

private:
    // Key: L"<guid_wstring>:<frameIndex>"
    // Value: 컨투어 폴리곤 목록 (1개의 스프라이트가 여러 독립 영역을 가질 수 있음)
    std::unordered_map<std::wstring, std::vector<std::vector<Vector2>>> m_cache;
};

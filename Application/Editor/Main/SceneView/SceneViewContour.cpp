#include "pch.h"
#include "SceneViewContour.h"

#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/SpriteAsset.h"
#include "Engine/Core/Asset/SpriteAsset.h"
#include "Engine/GameFramework/Object/GameObject.h"
#include "Engine/GameFramework/Component/SpriteRenderer2D.h"
#include "Engine/GameFramework/Component/Transform2D.h"
#include "Engine/GameFramework/Scene/Scene.h"
#include "Engine/GameFramework/Scene/SceneTransformUtils.h"

#include <unordered_set>
#include <cmath>

namespace
{
    // ── 상수 ──────────────────────────────────────────────────────────────────

    constexpr std::uint8_t ALPHA_THRESHOLD   = 0;
    constexpr float        OUTLINE_THICKNESS = 2.0f;

    constexpr ImU32 OUTLINE_COLOR_IMG = IM_COL32(50, 220, 255, 230);

    // ── 좌표 변환 ─────────────────────────────────────────────────────────────

    float GetAspect(const ImVec2& vpSize)
    {
        return vpSize.y > 0.0f ? vpSize.x / vpSize.y : 1.0f;
    }

    ImVec2 WorldToViewport(
        const Vector2& worldPt,
        const ImVec2& vpMin, const ImVec2& vpSize,
        const Vector2& camPos, float camSize)
    {
        const float aspect = GetAspect(vpSize);
        const float ndcX   = (worldPt.x - camPos.x) / (camSize * aspect);
        const float ndcY   = (worldPt.y - camPos.y) / camSize;
        return ImVec2(
            vpMin.x + (ndcX + 1.0f) * 0.5f * vpSize.x,
            vpMin.y + (1.0f - ndcY) * 0.5f * vpSize.y);
    }

    // ── Douglas-Peucker polygon simplification ────────────────────────────────

    float PtLineDist(
        const Vector2& p,
        const Vector2& a,
        const Vector2& b)
    {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-9f)
            return std::sqrt((p.x-a.x)*(p.x-a.x) + (p.y-a.y)*(p.y-a.y));
        return std::fabs((p.x - a.x) * dy - (p.y - a.y) * dx) / len;
    }

    void DPSimplify(
        const std::vector<Vector2>& pts,
        int lo, int hi,
        float eps,
        std::vector<bool>& keep)
    {
        if (hi <= lo + 1) return;
        float maxD = 0.0f;
        int   maxI = lo;
        for (int i = lo + 1; i < hi; ++i)
        {
            const float d = PtLineDist(pts[i], pts[lo], pts[hi]);
            if (d > maxD) { maxD = d; maxI = i; }
        }
        if (maxD > eps)
        {
            keep[maxI] = true;
            DPSimplify(pts, lo, maxI, eps, keep);
            DPSimplify(pts, maxI, hi, eps, keep);
        }
    }

    // 직선 구간의 중복 점만 제거하는 경량 D-P (1픽셀 허용 오차).
    // 모서리·오목 경계는 보존됩니다.
    // 직선 구간의 중간 점만 제거 (콜리니어 압축).
    std::vector<Vector2> SimplifyContour(
        const std::vector<Vector2>& pts,
        float epsLocal)
    {
        const int n = static_cast<int>(pts.size());
        if (n < 3) return pts;

        std::vector<bool> keep(n, false);
        keep[0] = keep[n - 1] = true;
        DPSimplify(pts, 0, n - 1, epsLocal, keep);

        std::vector<Vector2> out;
        out.reserve(n);
        for (int i = 0; i < n; ++i)
            if (keep[i]) out.push_back(pts[i]);
        return out;
    }

    // ── 픽셀 경계 엣지 트레이싱 ──────────────────────────────────────────────
    //
    // 알고리즘:
    //   1. 불투명↔투명 경계에 있는 각 픽셀 면(face)을 방향 엣지로 수집한다.
    //      좌측면: TL→BL  우측면: BR→TR  상단면: TR→TL  하단면: BL→BR
    //   2. 코너 → [다음 코너 목록] 멀티맵을 구축한다.
    //      (단일 맵 사용 시 대각선 픽셀 교차점에서 엣지가 덮어써져 컨투어가 끊김)
    //   3. 엣지 단위 visited 집합으로 각 방향 엣지를 정확히 한 번만 순회한다.
    //   4. 각 컨투어에 D-P 단순화(직선 구간 압축)를 적용한다.
    //
    // 여러 분리 컨투어 지원 (무기, 머리카락 등 독립 영역)
    // 좌표: x,y ∈ [-0.5, 0.5], y-up, 원점 = 스프라이트 중앙

    std::vector<std::vector<Vector2>> TraceAlphaContours(
        const std::vector<std::uint8_t>& pixels,
        std::uint32_t texW,
        std::uint32_t frameX, std::uint32_t frameY,
        std::uint32_t frameW, std::uint32_t frameH)
    {
        if (pixels.empty() || frameW == 0 || frameH == 0 || texW == 0) return {};

        const std::uint32_t texH =
            static_cast<std::uint32_t>(pixels.size() / (static_cast<std::size_t>(texW) * 4));

        auto isOpaqueLocal = [&](int lx, int ly) -> bool
        {
            if (lx < 0 || ly < 0 ||
                static_cast<std::uint32_t>(lx) >= frameW ||
                static_cast<std::uint32_t>(ly) >= frameH)
                return false;
            const std::uint32_t tx = frameX + static_cast<std::uint32_t>(lx);
            const std::uint32_t ty = frameY + static_cast<std::uint32_t>(ly);
            if (tx >= texW || ty >= texH) return false;
            const std::size_t idx =
                (static_cast<std::size_t>(ty) * texW + static_cast<std::size_t>(tx)) * 4 + 3;
            return idx < pixels.size() && pixels[idx] > ALPHA_THRESHOLD;
        };

        const std::uint32_t stride = frameW + 1;
        auto cIdx = [stride](std::uint32_t cx, std::uint32_t cy) -> std::uint32_t
        {
            return cy * stride + cx;
        };

        // ── 경계 엣지 수집 → 멀티맵 (충돌 없이 모든 엣지 보존) ──────────────
        std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> edgesMap;
        edgesMap.reserve(static_cast<std::size_t>(frameW + frameH) * 4);

        std::size_t totalEdges = 0;
        for (std::uint32_t ly = 0; ly < frameH; ++ly)
        {
            for (std::uint32_t lx = 0; lx < frameW; ++lx)
            {
                if (!isOpaqueLocal(static_cast<int>(lx), static_cast<int>(ly))) continue;

                const std::uint32_t TL = cIdx(lx,   ly);
                const std::uint32_t TR = cIdx(lx+1, ly);
                const std::uint32_t BL = cIdx(lx,   ly+1);
                const std::uint32_t BR = cIdx(lx+1, ly+1);

                auto addEdge = [&](std::uint32_t from, std::uint32_t to)
                {
                    edgesMap[from].push_back(to);
                    ++totalEdges;
                };

                if (!isOpaqueLocal(static_cast<int>(lx) - 1, static_cast<int>(ly)))
                    addEdge(TL, BL);
                if (!isOpaqueLocal(static_cast<int>(lx) + 1, static_cast<int>(ly)))
                    addEdge(BR, TR);
                if (!isOpaqueLocal(static_cast<int>(lx), static_cast<int>(ly) - 1))
                    addEdge(TR, TL);
                if (!isOpaqueLocal(static_cast<int>(lx), static_cast<int>(ly) + 1))
                    addEdge(BL, BR);
            }
        }

        if (edgesMap.empty()) return {};

        // ── 엣지 단위 visited (방향 엣지 = from*stride + to 로 인코딩) ─────────
        // 대각선 교차점에서 같은 코너에 2개 엣지가 있을 때 각각 정확히 1회 순회 보장.
        auto mkEdgeKey = [](std::uint64_t from, std::uint64_t to) -> std::uint64_t
        {
            return (from << 32) | to;
        };
        std::unordered_set<std::uint64_t> usedEdges;
        usedEdges.reserve(totalEdges);

        const int maxSteps = static_cast<int>(totalEdges) + 8;

        // D-P 허용 오차: 1픽셀 — 직선 구간의 중간 점만 제거.
        // 모서리·오목 경계는 보존하여 픽셀 경계를 촘촘히 따름.
        const float eps = 1.0f / static_cast<float>(std::max(frameW, frameH));

        std::vector<std::vector<Vector2>> result;

        for (auto& [startCorner, startToList] : edgesMap)
        {
            for (std::uint32_t startNext : startToList)
            {
                if (usedEdges.count(mkEdgeKey(startCorner, startNext))) continue;

                std::vector<Vector2> raw;
                raw.reserve(64);

                std::uint32_t cur      = startCorner;
                std::uint32_t nextStep = startNext;

                for (int k = 0; k < maxSteps; ++k)
                {
                    const auto ek = mkEdgeKey(cur, nextStep);
                    if (usedEdges.count(ek)) break; // 이미 사용된 엣지 (루프 감지)
                    usedEdges.insert(ek);

                    // 코너 인덱스 → 로컬 스프라이트 공간
                    const std::uint32_t cx = cur % stride;
                    const std::uint32_t cy = cur / stride;
                    raw.push_back({
                        static_cast<float>(cx) / static_cast<float>(frameW) - 0.5f,
                        0.5f - static_cast<float>(cy) / static_cast<float>(frameH)
                    });

                    cur = nextStep;
                    if (cur == startCorner) break; // 루프 완성

                    // 다음 미사용 엣지 탐색
                    auto it = edgesMap.find(cur);
                    if (it == edgesMap.end()) break;
                    bool found = false;
                    for (std::uint32_t nxt : it->second)
                    {
                        if (!usedEdges.count(mkEdgeKey(cur, nxt)))
                        {
                            nextStep = nxt;
                            found = true;
                            break;
                        }
                    }
                    if (!found) break;
                }

                if (raw.size() < 3) continue;
                auto simplified = SimplifyContour(raw, eps);
                if (simplified.size() >= 3)
                    result.push_back(std::move(simplified));
            }
        }

        return result;
    }

    // ── SpriteGuid → CSpriteAsset + 프레임 rect 해석 ────────────────────────
    // 통합 후 SpriteGuid 는 CSpriteAsset 만 가리킨다.

    struct ResolvedTex
    {
        const CSpriteAsset* tex = nullptr;
        std::uint32_t fX = 0, fY = 0, fW = 0, fH = 0;
        bool IsValid() const { return tex != nullptr && fW > 0 && fH > 0; }
    };

    ResolvedTex ResolveTexture(
        IAssetManager& mgr,
        const AssetGuid& spriteGuid,
        std::uint32_t frameIndex)
    {
        if (spriteGuid == INVALID_ASSET_GUID) return {};
        AssetRef<IAsset> asset = mgr.FindLoadedAsset(spriteGuid);
        if (!asset || EAssetType::Sprite != asset->GetAssetType()) return {};

        ResolvedTex r;
        r.tex = static_cast<const CSpriteAsset*>(asset.Get());
        if (!r.tex) return {};
        r.fW = r.tex->GetWidth();
        r.fH = r.tex->GetHeight();

        const auto& frames = r.tex->GetFrames();
        if (frameIndex < frames.size())
        {
            const auto& f = frames[frameIndex];
            r.fX = f.X; r.fY = f.Y;
            if (f.Width  > 0) r.fW = f.Width;
            if (f.Height > 0) r.fH = f.Height;
        }

        if (r.tex->GetPixels().empty() || r.fW == 0 || r.fH == 0) return {};
        return r;
    }

} // anonymous namespace

// ── CSceneViewContour implementation ─────────────────────────────────────────

const std::vector<std::vector<Vector2>>* CSceneViewContour::GetOrBuild(
    IAssetManager& assetMgr,
    const AssetGuid& spriteGuid,
    std::uint32_t frameIndex)
{
    if (spriteGuid == INVALID_ASSET_GUID) return nullptr;

    const std::wstring key = spriteGuid.wstring() + L":" + std::to_wstring(frameIndex);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return &it->second;

    const ResolvedTex rt = ResolveTexture(assetMgr, spriteGuid, frameIndex);
    if (!rt.IsValid()) return nullptr;

    auto& entry = m_cache[key];
    entry = TraceAlphaContours(
        rt.tex->GetPixels(), rt.tex->GetWidth(),
        rt.fX, rt.fY, rt.fW, rt.fH);
    return &entry;
}

void CSceneViewContour::DrawOutlinesImGui(
    ImDrawList* dl,
    const CScene& scene,
    IAssetManager* assetMgr,
    const std::vector<ObjectId>& selectedEntities,
    const ImVec2& vpMin, const ImVec2& vpSize,
    const Vector2& camPos, float camSize)
{
    if (!dl || selectedEntities.empty()) return;

    // 다중 선택 시 동일 스프라이트 엔티티에 대한 중복 렌더링 방지.
    // 부모·자식 모두 선택된 경우도 정상 처리됨.
    std::unordered_set<ObjectId> processed;

    auto drawOneSprite = [&](ObjectId entity, const CGameObject& object, const SpriteRenderer2D& sprite)
    {
        if (!processed.insert(entity).second) return; // 이미 처리됨

        const Matrix3x2 spriteMat =
            Matrix3x2::Transform(sprite.Offset, 0.0f, sprite.Size)
            * GetWorldTransform(object);

        const std::vector<std::vector<Vector2>>* contours = nullptr;
        if (assetMgr && sprite.SpriteGuid != INVALID_ASSET_GUID)
            contours = GetOrBuild(*assetMgr, sprite.SpriteGuid, 0);

        if (!contours || contours->empty())
        {
            // OBB fallback
            const Vector2 corners[4] = {
                spriteMat.TransformPoint({-0.5f,  0.5f}),
                spriteMat.TransformPoint({ 0.5f,  0.5f}),
                spriteMat.TransformPoint({ 0.5f, -0.5f}),
                spriteMat.TransformPoint({-0.5f, -0.5f}),
            };
            ImVec2 sc[4];
            for (int i = 0; i < 4; ++i)
                sc[i] = WorldToViewport(corners[i], vpMin, vpSize, camPos, camSize);
            dl->AddPolyline(sc, 4, OUTLINE_COLOR_IMG,
                            ImDrawFlags_Closed, OUTLINE_THICKNESS);
            return;
        }

        for (const auto& contour : *contours)
        {
            if (contour.size() < 3) continue;
            std::vector<ImVec2> screenPts;
            screenPts.reserve(contour.size());
            for (const auto& lp : contour)
                screenPts.push_back(WorldToViewport(
                    spriteMat.TransformPoint(lp),
                    vpMin, vpSize, camPos, camSize));
            dl->AddPolyline(
                screenPts.data(), static_cast<int>(screenPts.size()),
                OUTLINE_COLOR_IMG, ImDrawFlags_Closed, OUTLINE_THICKNESS);
        }
    };

    // 선택 집합에 있는 엔티티 각각의 자체 스프라이트만 렌더링.
    //
    // 루트 모드 클릭: CollectSubtree가 이미 서브트리 전체를 selectedEntities에 담아줌
    //   → 각 엔티티 자신의 스프라이트만 그려도 서브트리 전체가 처리됨.
    //
    // 포커스 모드에서 m_context 자신 클릭: selectedEntities = { A }
    //   → A의 자체 스프라이트만 그림 (자식에 불필요하게 외곽선이 생기지 않음).
    //
    // 포커스 모드에서 자식 클릭: selectedEntities = CollectSubtree(child)
    //   → 자식 + 자식의 모든 자손 각각의 스프라이트를 그림.
    for (ObjectId entity : selectedEntities)
    {
        if (entity == INVALID_OBJECT_ID) continue;

        CGameObject* object = const_cast<CScene&>(scene).FindObjectById(entity);
        if (!object || !object->IsActive) continue;

        const SpriteRenderer2D* sprite = object->GetComponent<SpriteRenderer2D>();
        if (!sprite || !sprite->IsEnabled) continue;

        drawOneSprite(entity, *object, *sprite);
    }
}

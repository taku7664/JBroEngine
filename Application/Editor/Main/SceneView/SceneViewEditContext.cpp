#include "pch.h"
#include "SceneViewEditContext.h"

#include <unordered_set>

#include "Editor/Editor.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/SpriteAsset.h"
#include "Engine/Core/Asset/SpriteAsset.h"
#include "Engine/GameFramework/Component/GameObject.h"
#include "Engine/GameFramework/Component/SpriteRenderer2D.h"
#include "Engine/GameFramework/Component/Transform2D.h"
#include "Engine/GameFramework/Scene/Scene.h"
#include "Engine/GameFramework/Scene/SceneTransformUtils.h"

namespace
{
    // ── 상수 ──────────────────────────────────────────────────────────────────

    constexpr std::uint8_t ALPHA_THRESHOLD = 0;

    // ── 좌표 변환 유틸 ────────────────────────────────────────────────────────

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

    // ── 계층 탐색 헬퍼 ────────────────────────────────────────────────────────

    // 씬 루트까지 올라가서 최상위 조상 반환
    EntityId GetRootAncestor(const CScene& scene, EntityId entity)
    {
        if (entity == INVALID_ENTITY_ID) return INVALID_ENTITY_ID;
        EntityId cur = entity;
        for (;;)
        {
            const EntityId parent = scene.GetParent(cur);
            if (parent == INVALID_ENTITY_ID) return cur;
            cur = parent;
        }
    }

    // context의 직계 자식 중 target을 포함(또는 target 자신)하는 엔티티 반환.
    // target이 context의 자손이 아니면 INVALID_ENTITY_ID.
    EntityId GetDirectChildOfContext(const CScene& scene, EntityId context, EntityId target)
    {
        if (target == INVALID_ENTITY_ID || context == INVALID_ENTITY_ID)
            return INVALID_ENTITY_ID;
        EntityId cur = target;
        for (;;)
        {
            const EntityId parent = scene.GetParent(cur);
            if (parent == context) return cur;
            if (parent == INVALID_ENTITY_ID) return INVALID_ENTITY_ID;
            cur = parent;
        }
    }

    // ── 텍스처 해석 헬퍼 ─────────────────────────────────────────────────────
    //
    // SpriteGuid 는 CSpriteAsset 을 가리킨다 (이전 CTextureAsset 통합됨).
    // CSpriteAsset* + 프레임 UV/픽셀 rect 를 반환.

    struct ResolvedTexture
    {
        const CSpriteAsset* tex = nullptr;
        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f; // AddImageQuad UV
        std::uint32_t fX = 0, fY = 0, fW = 0, fH = 0;       // 픽셀 샘플링 rect
        bool IsValid() const { return tex != nullptr && fW > 0 && fH > 0; }
    };

    ResolvedTexture ResolveAssetTexture(
        IAssetManager& mgr,
        const AssetGuid& spriteGuid,
        std::uint32_t frameIndex = 0)
    {
        if (spriteGuid == INVALID_ASSET_GUID) return {};

        SafePtr<IAsset> assetPtr = mgr.FindLoadedAsset(spriteGuid);
        if (!assetPtr || EAssetType::Sprite != assetPtr->GetAssetType()) return {};

        ResolvedTexture r;
        r.tex = static_cast<const CSpriteAsset*>(assetPtr.TryGet());
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
            const float tw = static_cast<float>(r.tex->GetWidth());
            const float th = static_cast<float>(r.tex->GetHeight());
            if (tw > 0.0f && th > 0.0f)
            {
                r.u0 = static_cast<float>(f.X) / tw;
                r.v0 = static_cast<float>(f.Y) / th;
                r.u1 = r.u0 + static_cast<float>(f.Width)  / tw;
                r.v1 = r.v0 + static_cast<float>(f.Height) / th;
            }
        }
        if (!r.tex || r.tex->GetPixels().empty() || r.fW == 0 || r.fH == 0)
            return {};
        return r;
    }

} // anonymous namespace

// ── CSceneViewEditContext implementation ─────────────────────────────────────

void CSceneViewEditContext::Validate(const CScene& scene)
{
    if (m_context != INVALID_ENTITY_ID && !scene.IsAlive(m_context))
        m_context = INVALID_ENTITY_ID;
}

EntityId CSceneViewEditContext::Pick(
    const CScene& scene,
    const Vector2& worldPt,
    IAssetManager* assetMgr) const
{
    EntityId pickedSprite  = INVALID_ENTITY_ID;
    std::int32_t pickedOrd = std::numeric_limits<std::int32_t>::min();

    scene.ForEach<SpriteRenderer2D, GameObject, Transform2D>(
        [&](EntityId entity, const SpriteRenderer2D& sprite,
            const GameObject& go, const Transform2D&)
        {
            if (!go.IsActive || !sprite.IsEnabled) return;

            // 포커스 모드: m_context 자신 또는 그 자손만 대상
            if (m_context != INVALID_ENTITY_ID)
            {
                if (entity != m_context && !scene.IsDescendantOf(entity, m_context)) return;
            }

            // OBB 히트 테스트
            const Matrix3x2 spriteMat =
                Matrix3x2::Transform(sprite.Offset, 0.0f, sprite.Size)
                * GetWorldTransform(scene, entity);
            Matrix3x2 inv;
            if (!spriteMat.TryInvert(inv)) return;

            const Vector2 local = inv.TransformPoint(worldPt);
            if (local.x < -0.5f || local.x > 0.5f ||
                local.y < -0.5f || local.y > 0.5f) return;

            // Alpha test (에셋 없으면 OBB 히트 그대로 사용)
            if (assetMgr && sprite.SpriteGuid != INVALID_ASSET_GUID)
            {
                const ResolvedTexture rt =
                    ResolveAssetTexture(*assetMgr, sprite.SpriteGuid, 0);
                if (rt.IsValid())
                {
                    const float u = local.x + 0.5f;
                    const float v = 0.5f - local.y;
                    const int px = static_cast<int>(rt.fX) +
                        std::clamp(static_cast<int>(u * static_cast<float>(rt.fW)),
                                   0, static_cast<int>(rt.fW) - 1);
                    const int py = static_cast<int>(rt.fY) +
                        std::clamp(static_cast<int>(v * static_cast<float>(rt.fH)),
                                   0, static_cast<int>(rt.fH) - 1);
                    const std::size_t idx =
                        (static_cast<std::size_t>(py) * rt.tex->GetWidth()
                         + static_cast<std::size_t>(px)) * 4 + 3;
                    if (idx < rt.tex->GetPixels().size() &&
                        rt.tex->GetPixels()[idx] <= ALPHA_THRESHOLD)
                        return; // 완전 투명 → 무시
                }
            }

            if (pickedSprite == INVALID_ENTITY_ID || sprite.SortOrder >= pickedOrd)
            {
                pickedSprite = entity;
                pickedOrd    = sprite.SortOrder;
            }
        });

    if (pickedSprite == INVALID_ENTITY_ID) return INVALID_ENTITY_ID;

    // 컨텍스트 레벨에 맞는 엔티티 반환
    if (m_context == INVALID_ENTITY_ID)
        return GetRootAncestor(scene, pickedSprite);

    // m_context 자신을 클릭한 경우: GetDirectChildOfContext는 자신을 탐색 불가 → 직접 반환
    if (pickedSprite == m_context)
        return m_context;

    return GetDirectChildOfContext(scene, m_context, pickedSprite);
}

std::vector<EntityId> CSceneViewEditContext::PickBox(
    const CScene& scene,
    const Vector2& worldMin,
    const Vector2& worldMax,
    IAssetManager* assetMgr) const
{
    std::unordered_set<EntityId> foundSet;

    // Transform2D + GameObject 전체 순회:
    //   - SpriteRenderer2D 있음 → 불투명 픽셀 tight AABB (없으면 OBB 폴백)
    //   - SpriteRenderer2D 없음 → 1×1 단위 OBB (엔티티 로컬 공간 기준)
    scene.ForEach<Transform2D, GameObject>(
        [&](EntityId entity, const Transform2D&, const GameObject& go)
        {
            if (!go.IsActive) return;

            // 컨텍스트 필터
            if (m_context != INVALID_ENTITY_ID)
            {
                if (entity != m_context && !scene.IsDescendantOf(entity, m_context)) return;
            }

            const Matrix3x2 entityWorldMat = GetWorldTransform(scene, entity);

            // ── 꼭짓점 계산 ──────────────────────────────────────────────────────
            Vector2 corners[4];

            const SpriteRenderer2D* sprite = scene.GetComponent<SpriteRenderer2D>(entity);
            if (sprite && sprite->IsEnabled)
            {
                const Matrix3x2 spriteMat =
                    Matrix3x2::Transform(sprite->Offset, 0.0f, sprite->Size)
                    * entityWorldMat;

                // 픽셀 기반 tight AABB 시도
                bool tightComputed = false;
                if (assetMgr && sprite->SpriteGuid != INVALID_ASSET_GUID)
                {
                    const ResolvedTexture rt =
                        ResolveAssetTexture(*assetMgr, sprite->SpriteGuid, 0);
                    if (rt.IsValid())
                    {
                        int pxMin = static_cast<int>(rt.fW), pxMax = -1;
                        int pyMin = static_cast<int>(rt.fH), pyMax = -1;

                        const auto& pixels  = rt.tex->GetPixels();
                        const int   texW    = static_cast<int>(rt.tex->GetWidth());
                        const int   fW      = static_cast<int>(rt.fW);
                        const int   fH      = static_cast<int>(rt.fH);

                        for (int py = 0; py < fH; ++py)
                        {
                            for (int px = 0; px < fW; ++px)
                            {
                                const std::size_t idx =
                                    (static_cast<std::size_t>(rt.fY + py) * texW
                                     + static_cast<std::size_t>(rt.fX + px)) * 4 + 3;
                                if (idx < pixels.size() && pixels[idx] > ALPHA_THRESHOLD)
                                {
                                    if (px < pxMin) pxMin = px;
                                    if (px > pxMax) pxMax = px;
                                    if (py < pyMin) pyMin = py;
                                    if (py > pyMax) pyMax = py;
                                }
                            }
                        }

                        if (pxMax >= 0)
                        {
                            // 픽셀 인덱스 → local 좌표 변환
                            // u = px / fW, v = py / fH
                            // local.x = u - 0.5, local.y = 0.5 - v
                            const float lxMin = static_cast<float>(pxMin)     / static_cast<float>(fW) - 0.5f;
                            const float lxMax = static_cast<float>(pxMax + 1) / static_cast<float>(fW) - 0.5f;
                            const float lyMin = 0.5f - static_cast<float>(pyMax + 1) / static_cast<float>(fH);
                            const float lyMax = 0.5f - static_cast<float>(pyMin)     / static_cast<float>(fH);

                            corners[0] = spriteMat.TransformPoint({lxMin, lyMax});
                            corners[1] = spriteMat.TransformPoint({lxMax, lyMax});
                            corners[2] = spriteMat.TransformPoint({lxMax, lyMin});
                            corners[3] = spriteMat.TransformPoint({lxMin, lyMin});
                            tightComputed = true;
                        }
                    }
                }

                if (!tightComputed)
                {
                    // 폴백: 전체 OBB 4꼭짓점
                    corners[0] = spriteMat.TransformPoint({-0.5f,  0.5f});
                    corners[1] = spriteMat.TransformPoint({ 0.5f,  0.5f});
                    corners[2] = spriteMat.TransformPoint({ 0.5f, -0.5f});
                    corners[3] = spriteMat.TransformPoint({-0.5f, -0.5f});
                }
            }
            else
            {
                // 스프라이트 없음 → 1×1 단위 OBB (엔티티 로컬 공간)
                corners[0] = entityWorldMat.TransformPoint({-0.5f,  0.5f});
                corners[1] = entityWorldMat.TransformPoint({ 0.5f,  0.5f});
                corners[2] = entityWorldMat.TransformPoint({ 0.5f, -0.5f});
                corners[3] = entityWorldMat.TransformPoint({-0.5f, -0.5f});
            }

            // ── AABB 포함 검사: 4개 꼭짓점 모두 드래그 영역 안 ──────────────
            for (const auto& c : corners)
            {
                if (c.x < worldMin.x || c.x > worldMax.x ||
                    c.y < worldMin.y || c.y > worldMax.y)
                    return;
            }

            // ── 컨텍스트 레벨 엔티티로 매핑 ─────────────────────────────────
            EntityId contextEntity = INVALID_ENTITY_ID;
            if (m_context == INVALID_ENTITY_ID)
                contextEntity = GetRootAncestor(scene, entity);
            else if (entity == m_context)
                contextEntity = entity;
            else
                contextEntity = GetDirectChildOfContext(scene, m_context, entity);

            if (contextEntity != INVALID_ENTITY_ID)
                foundSet.insert(contextEntity);
        });

    return std::vector<EntityId>(foundSet.begin(), foundSet.end());
}

EntityId CSceneViewEditContext::OnDoubleClick(const CScene& /*scene*/, EntityId picked)
{
    if (picked == INVALID_ENTITY_ID) return INVALID_ENTITY_ID;

    // 자식 유무와 무관하게 항상 해당 엔티티 컨텍스트로 진입.
    // 선택 처리(Editor::SelectEntities)는 호출자(SceneViewTool)가 담당.
    m_context = picked;

    return picked; // 호출자가 FocusOnEntity 에 전달
}

EntityId CSceneViewEditContext::OnDoubleClickEmpty(const CScene& scene)
{
    if (m_context == INVALID_ENTITY_ID)
        return INVALID_ENTITY_ID; // 이미 루트, 탈출할 컨텍스트 없음

    const EntityId exited = m_context;             // 방금 탈출한 엔티티
    m_context = scene.GetParent(m_context);        // 한 뎁스 위로 (루트면 INVALID)
    return exited;
}

// DrawOverlay 는 RT 파이프라인(ImEditor)으로 이전됨. 구현 제거.

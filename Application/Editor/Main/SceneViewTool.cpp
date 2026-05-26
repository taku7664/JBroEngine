#include "pch.h"
#include "SceneViewTool.h"

#include "Editor/Editor.h"
#include "Editor/Helper/EditorGuiDrawHelpers.h"
#include "Editor/Main/InspectorTool.h"
#include "Engine/Core/Core.h"
#include <cstring>
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Debug/DebugDraw2D.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/GameFramework/Component/Camera2D.h"
#include "Engine/GameFramework/Component/Physics2DComponents.h"
#include "Engine/GameFramework/Component/SpriteRenderer2D.h"
#include "Engine/GameFramework/Component/Transform2D.h"
#include "Engine/GameFramework/Debug/SceneDebugDrawSystem.h"
#include "Engine/GameFramework/Scene/Scene.h"
#include "Engine/GameFramework/Scene/SceneTransformUtils.h"

namespace
{
    constexpr int   MAX_GRID_LINES      = 300;
    constexpr float AXIS_LINE_THICKNESS = 2.5f;
    constexpr float CAMERA_SMOOTH_SPEED = 10.0f;

    // ── 좌표 변환 유틸 ────────────────────────────────────────────────────────

    float GetAspect(const ImVec2& vpSize)
    {
        return vpSize.y > 0.0f ? vpSize.x / vpSize.y : 1.0f;
    }

    Vector2<float> ViewportToWorld(
        const ImVec2& vpPt,
        const ImVec2& vpMin, const ImVec2& vpSize,
        const Vector2<float>& camPos, float camSize)
    {
        const float ndcX = ((vpPt.x - vpMin.x) / vpSize.x) * 2.0f - 1.0f;
        const float ndcY = 1.0f - ((vpPt.y - vpMin.y) / vpSize.y) * 2.0f;
        const float aspect = GetAspect(vpSize);
        return Vector2<float>(
            ndcX * camSize * aspect + camPos.x,
            ndcY * camSize          + camPos.y);
    }

    // ── 서브트리 엔티티 수집 (DFS) ───────────────────────────────────────────
    //
    // root 엔티티 + 모든 자손을 벡터로 반환. 다중 선택에서 Ctrl+Click 토글,
    // 단일 클릭(부모 = 자식 전부 포함) 등에 사용.

    std::vector<EntityId> CollectSubtree(const CScene& scene, EntityId root)
    {
        if (root == INVALID_ENTITY_ID) return {};
        std::vector<EntityId> result;
        std::vector<EntityId> stack = { root };
        while (!stack.empty())
        {
            const EntityId cur = stack.back();
            stack.pop_back();
            result.push_back(cur);
            EntityId child = scene.GetFirstChild(cur);
            while (child != INVALID_ENTITY_ID)
            {
                stack.push_back(child);
                child = scene.GetNextSibling(child);
            }
        }
        return result;
    }

    // ── 좌표 변환 (World → Viewport) ─────────────────────────────────────────

    ImVec2 WorldToViewport(
        const Vector2<float>& world,
        const ImVec2& vpMin, const ImVec2& vpSize,
        const Vector2<float>& camPos, float camSize)
    {
        const float aspect = GetAspect(vpSize);
        const float ndcX = (world.x - camPos.x) / (camSize * aspect);
        const float ndcY = (world.y - camPos.y) / camSize;
        return ImVec2(
            vpMin.x + (ndcX + 1.0f) * 0.5f * vpSize.x,
            vpMin.y + (1.0f - ndcY) * 0.5f * vpSize.y);
    }

    // ── 그리드 제출 ───────────────────────────────────────────────────────────

    void SubmitGrid(
        IDebugDraw2D& debugDraw,
        float camX, float camY, float camSize, float aspect)
    {
        const float halfW  = camSize * aspect;
        const float worldL = camX - halfW;
        const float worldR = camX + halfW;
        const float worldB = camY - camSize;
        const float worldT = camY + camSize;

        const float rawStep   = (2.0f * camSize) / 6.0f;
        const float magnitude = std::powf(10.0f,
            std::floorf(std::log10f(std::max(rawStep, 1e-6f))));
        const float normalized = rawStep / magnitude;
        float step;
        if      (normalized < 1.5f) step = 1.0f  * magnitude;
        else if (normalized < 3.5f) step = 2.0f  * magnitude;
        else if (normalized < 7.5f) step = 5.0f  * magnitude;
        else                        step = 10.0f * magnitude;

        constexpr DebugColor gridCol  = DebugColorRGBA( 80,  80,  90, 128);
        constexpr DebugColor axisYCol = DebugColorRGBA( 50, 200,  80, 128); // Y-axis: 수직선 (x=0) → 초록
        constexpr DebugColor axisXCol = DebugColorRGBA(220,  60,  60, 128); // X-axis: 수평선 (y=0) → 빨강

        const int xStart = static_cast<int>(std::ceilf(worldL / step));
        const int xEnd   = static_cast<int>(std::floorf(worldR / step));
        for (int k = xStart; k <= xEnd && (k - xStart) < MAX_GRID_LINES; ++k)
        {
            const float wx    = static_cast<float>(k) * step;
            const bool  isAxis = (k == 0);
            debugDraw.DrawLine(
                { wx, worldB }, { wx, worldT },
                isAxis ? axisYCol : gridCol,
                isAxis ? AXIS_LINE_THICKNESS : 1.0f);
        }

        const int yStart = static_cast<int>(std::ceilf(worldB / step));
        const int yEnd   = static_cast<int>(std::floorf(worldT / step));
        for (int k = yStart; k <= yEnd && (k - yStart) < MAX_GRID_LINES; ++k)
        {
            const float wy    = static_cast<float>(k) * step;
            const bool  isAxis = (k == 0);
            debugDraw.DrawLine(
                { worldL, wy }, { worldR, wy },
                isAxis ? axisXCol : gridCol,
                isAxis ? AXIS_LINE_THICKNESS : 1.0f);
        }
    }

    // ── 그리드 눈금 레이블 ────────────────────────────────────────────────────
    //
    // SubmitGrid와 동일한 step 계산으로 각 그리드 선에 좌표값 레이블을 표시.
    //   showPixel = false: 월드 유닛값 (예: "1", "2.5")
    //   showPixel = true : 픽셀값 (예: "100px", "-50px")
    // 레이블이 겹치지 않도록 직전 레이블과 최소 거리 이하면 건너뜀.

    void DrawGridLabels(
        ImDrawList* dl,
        const ImVec2& vpMin, const ImVec2& vpSize,
        float camX, float camY, float camSize,
        float ppu, bool showPixel)
    {
        if (!dl) return;

        const float aspect = GetAspect(vpSize);
        const float halfW  = camSize * aspect;
        const float worldL = camX - halfW;
        const float worldR = camX + halfW;
        const float worldB = camY - camSize;
        const float worldT = camY + camSize;

        constexpr ImU32  labelCol   = IM_COL32(185, 195, 210, 210);
        constexpr float  STRIP_SIZE = 16.0f;  // 레이블 위치 계산용 여백
        constexpr float  MIN_DIST_X = 36.0f;
        constexpr float  MIN_DIST_Y = 14.0f;

        // 레이블 전용 소형 폰트 (기본 크기의 80%)
        ImFont*     labelFont     = ImGui::GetFont();
        const float labelFontSize = ImGui::GetFontSize() * 0.80f;
        auto calcTS = [&](const char* text) -> ImVec2
        {
            return labelFont->CalcTextSizeA(labelFontSize, FLT_MAX, 0.0f, text);
        };

        // 레이블 배경 띠 없음 (제거됨)

        const float labelYPos = vpMin.y + vpSize.y - STRIP_SIZE + (STRIP_SIZE - labelFontSize) * 0.5f;
        const float labelXPos = vpMin.x + 3.0f;

        const Vector2<float> camPosV{ camX, camY };

        // step 계산 (SubmitGrid와 동일)
        const float rawStep   = (2.0f * camSize) / 6.0f;
        const float magnitude = std::powf(10.0f,
            std::floorf(std::log10f(std::max(rawStep, 1e-6f))));
        const float normalized = rawStep / magnitude;
        float step;
        if      (normalized < 1.5f) step = 1.0f  * magnitude;
        else if (normalized < 3.5f) step = 2.0f  * magnitude;
        else if (normalized < 7.5f) step = 5.0f  * magnitude;
        else                        step = 10.0f * magnitude;

        // ── X축 레이블 (각 수직 그리드 선 하단, 텍스트 중앙 정렬) ──────────
        const int xStart = static_cast<int>(std::ceilf (worldL / step));
        const int xEnd   = static_cast<int>(std::floorf(worldR / step));
        float prevScrXEnd = -1e9f;
        for (int k = xStart; k <= xEnd && (k - xStart) < MAX_GRID_LINES; ++k)
        {
            const float wx  = static_cast<float>(k) * step;
            const ImVec2 sp = WorldToViewport({ wx, camY }, vpMin, vpSize, camPosV, camSize);

            char buf[32];
            if (k == 0)
                std::snprintf(buf, sizeof(buf), "0");
            else if (showPixel)
                std::snprintf(buf, sizeof(buf), "%.4g", wx * ppu);
            else
                std::snprintf(buf, sizeof(buf), "%.4g", wx);

            const ImVec2 ts       = calcTS(buf);
            const float  textLeft = sp.x - ts.x * 0.5f;
            if (textLeft < prevScrXEnd + 2.0f) continue;
            prevScrXEnd = textLeft + ts.x;

            dl->AddText(labelFont, labelFontSize,
                        ImVec2(textLeft, labelYPos), labelCol, buf);
        }

        // ── Y축 레이블 (각 수평 그리드 선 좌측) ─────────────────────────────
        const int yStart = static_cast<int>(std::ceilf (worldB / step));
        const int yEnd   = static_cast<int>(std::floorf(worldT / step));
        float prevScrY = -1e9f;
        for (int k = yStart; k <= yEnd && (k - yStart) < MAX_GRID_LINES; ++k)
        {
            const float wy  = static_cast<float>(k) * step;
            const ImVec2 sp = WorldToViewport({ camX, wy }, vpMin, vpSize, camPosV, camSize);

            if (std::abs(sp.y - prevScrY) < MIN_DIST_Y) continue;
            prevScrY = sp.y;

            char buf[32];
            if (k == 0)
                std::snprintf(buf, sizeof(buf), "0");
            else if (showPixel)
                std::snprintf(buf, sizeof(buf), "%.4g", wy * ppu);
            else
                std::snprintf(buf, sizeof(buf), "%.4g", wy);

            const ImVec2 ts = calcTS(buf);
            dl->AddText(labelFont, labelFontSize,
                        ImVec2(labelXPos, sp.y - ts.y * 0.5f), labelCol, buf);
        }
    }

} // anonymous namespace

// ── CSceneViewTool ─────────────────────────────────────────────────────────────

void CSceneViewTool::SetEditorCamera(float x, float y, float size)
{
    m_targetCameraPos  = Vector2<float>(x, y);
    m_cameraPos        = Vector2<float>(x, y);
    m_targetCameraSize = (size > 0.0f) ? size : 5.0f;
    m_cameraSize       = m_targetCameraSize;
}

void CSceneViewTool::FocusOnEntity(EntityId entity, const CScene& scene)
{
    if (INVALID_ENTITY_ID == entity) return;

    const Matrix3x2      worldTransform = GetWorldTransform(scene, entity);
    const Vector2<float> worldPos       = worldTransform.TransformPoint(Vector2<float>(0.0f, 0.0f));

    const float scaleX = std::sqrt(
        worldTransform.M11 * worldTransform.M11 + worldTransform.M12 * worldTransform.M12);
    const float scaleY = std::sqrt(
        worldTransform.M21 * worldTransform.M21 + worldTransform.M22 * worldTransform.M22);

    float halfExtent = 0.5f;
    const SpriteRenderer2D* sprite = scene.GetComponent<SpriteRenderer2D>(entity);
    if (sprite && sprite->IsEnabled)
    {
        const float wx = sprite->Size.x * scaleX;
        const float wy = sprite->Size.y * scaleY;
        halfExtent = std::max(wx, wy) * 0.5f;
    }
    else
    {
        halfExtent = std::max(scaleX, scaleY) * 0.5f;
    }

    constexpr float FOCUS_PADDING = 2.5f;
    const float newSize = std::clamp(halfExtent * FOCUS_PADDING, 0.5f, 1000.0f);

    m_targetCameraPos  = worldPos;
    m_targetCameraSize = newSize;
}

void CSceneViewTool::SetFocusContext(EntityId entity, const CScene& scene)
{
    if (INVALID_ENTITY_ID == entity) return;

    // 편집 컨텍스트를 entity로 전환 (씬뷰 더블클릭과 동일한 경로)
    m_editCtx.OnDoubleClick(scene, entity);

    // 카메라도 해당 엔티티 위치로 이동
    FocusOnEntity(entity, scene);
}

void CSceneViewTool::ClearEditContext()
{
    m_editCtx.Clear();
}

void CSceneViewTool::OnCreate()
{
    SetTitle("SceneView");
}

void CSceneViewTool::OnDestroy()
{
}

void CSceneViewTool::OnUpdate()
{
}

void CSceneViewTool::OnRenderStay()
{
    ImVec2 vpSize = ImGui::GetContentRegionAvail();
    vpSize.x = std::max(vpSize.x, 1.0f);
    vpSize.y = std::max(vpSize.y, 1.0f);

    // ── 카메라 보간 ───────────────────────────────────────────────────────────
    {
        const float dt    = std::clamp(ImGui::GetIO().DeltaTime, 0.001f, 0.1f);
        const float alpha = 1.0f - std::expf(-CAMERA_SMOOTH_SPEED * dt);
        m_cameraPos.x  += (m_targetCameraPos.x  - m_cameraPos.x)  * alpha;
        m_cameraPos.y  += (m_targetCameraPos.y  - m_cameraPos.y)  * alpha;
        m_cameraSize   += (m_targetCameraSize   - m_cameraSize)   * alpha;
    }

    // ── 카메라 → ImEditor 전달 및 RT 요청 ────────────────────────────────────
    if (Editor::ImEditor)
    {
        Editor::ImEditor->SetSceneViewCamera(m_cameraPos.x, m_cameraPos.y, m_cameraSize);
        Editor::ImEditor->RequestSceneViewRenderTarget(
            static_cast<std::uint32_t>(vpSize.x),
            static_cast<std::uint32_t>(vpSize.y));
    }

    // ── AssetManager / PPU 획득 (picking, contour, overlay, 레이블 공통) ───────
    IAssetManager* assetMgr = nullptr;
    float          ppu      = 100.0f;
    if (Editor::ImEditor)
    {
        SafePtr<CProjectManager> pm = Editor::ImEditor->GetProjectManager();
        if (pm && pm->IsProjectLoaded())
        {
            assetMgr = pm->GetAssetManager().TryGet();
            ppu      = pm->GetPixelsPerUnit();
        }
    }

    // ── DebugDraw 제출 (RT에 렌더됨) ─────────────────────────────────────────
    if (Core::DebugDraw2D.IsValid())
    {
        const float aspect = GetAspect(vpSize);
        SubmitGrid(*Core::DebugDraw2D, m_cameraPos.x, m_cameraPos.y, m_cameraSize, aspect);

        if (Core::SceneManager)
        {
            SafePtr<CScene> scene = Core::SceneManager->GetActiveScene();
            if (scene)
            {
                // 씬 기본 디버그 (선택 엔티티 OBB 등)
                float resW = 0.0f, resH = 0.0f;
                if (Editor::ImEditor)
                {
                    SafePtr<CProjectManager> pm = Editor::ImEditor->GetProjectManager();
                    if (pm && pm->IsProjectLoaded())
                    {
                        resW = static_cast<float>(pm->GetResolutionWidth());
                        resH = static_cast<float>(pm->GetResolutionHeight());
                    }
                    else
                    {
                        const std::uint32_t gvW = Editor::ImEditor->GetGameViewWidth();
                        const std::uint32_t gvH = Editor::ImEditor->GetGameViewHeight();
                        if (gvW > 0 && gvH > 0)
                        {
                            resW = static_cast<float>(gvW);
                            resH = static_cast<float>(gvH);
                        }
                    }
                }
                const char* activeCompType =
                    Editor::Inspector ? Editor::Inspector->GetActiveComponentTypeName() : nullptr;
                SceneDebugDraw::Submit(*scene, *Core::DebugDraw2D,
                                       Editor::GetSelectedEntity(), resW, resH, activeCompType);
            }
        }
    }

    // ── ImGui 레이어 시작 ─────────────────────────────────────────────────────
    const ImVec2 vpMin = ImGui::GetCursorScreenPos();
    ImDrawList* dl     = ImGui::GetWindowDrawList();

    ImGui::InvisibleButton("##SceneViewInput", vpSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    // Layer 1: 배경
    dl->AddRectFilled(vpMin, vpMin + vpSize, IM_COL32(26, 28, 32, 255));

    // Layer 2: 씬 RT (그리드 + 스프라이트 + GPU 외곽선 포함)
    void* texID = Editor::ImEditor ? Editor::ImEditor->GetSceneViewTextureID() : nullptr;
    if (texID)
    {
        dl->AddImage(reinterpret_cast<ImTextureID>(texID),
                     vpMin, vpMin + vpSize,
                     ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
    }

    // Layer 2.5: 그리드 눈금 레이블 (Unit/Pixel 모드)
    DrawGridLabels(dl, vpMin, vpSize,
                   m_cameraPos.x, m_cameraPos.y, m_cameraSize,
                   ppu, m_showPixelGrid);

    // Layer 2.6: Unit/Pixel 토글 버튼 (우상단 오버레이, draw-list 방식)
    constexpr float BTN_W  = 90.0f;
    constexpr float BTN_H  = 22.0f;
    constexpr float MARGIN = 10.0f;
    const ImVec2 btnMin(vpMin.x + vpSize.x - BTN_W - MARGIN, vpMin.y + MARGIN);
    const ImVec2 btnMax(btnMin.x + BTN_W, btnMin.y + BTN_H);

    const bool toggleHovered = ImGui::IsMouseHoveringRect(btnMin, btnMax);
    if (toggleHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        m_showPixelGrid = !m_showPixelGrid;

    dl->AddRectFilled(btnMin, btnMax,
        toggleHovered ? IM_COL32(60, 72, 88, 230) : IM_COL32(28, 33, 42, 175), 4.0f);
    dl->AddRect(btnMin, btnMax, IM_COL32(88, 100, 120, 200), 4.0f);
    {
        const char* btnLabel = m_showPixelGrid
            ? Utillity::U8(u8"단위: Pixel")
            : Utillity::U8(u8"단위: Unit");
        const ImVec2 ts = ImGui::CalcTextSize(btnLabel);
        dl->AddText(
            ImVec2(btnMin.x + (BTN_W - ts.x) * 0.5f,
                   btnMin.y + (BTN_H - ts.y) * 0.5f),
            IM_COL32(200, 210, 225, 255), btnLabel);
    }

    // Layer 2.7: 선택 오브젝트 피벗 기즈모 (원 두 개 + 십자선) ─────────────────
    {
        dl->PushClipRect(vpMin, vpMin + vpSize, true);

        if (Core::SceneManager)
        {
            SafePtr<CScene> gizmoScene = Core::SceneManager->GetActiveScene();
            if (gizmoScene)
            {
                constexpr float OUTER_R    = 5.0f;
                constexpr float INNER_R    = 3.0f;
                constexpr float CROSS_HALF = OUTER_R + 2.5f; // 십자선 끝 (원 바깥 2.5px)
                constexpr float LINE_W     = 1.0f;
                constexpr ImU32 SHADOW     = IM_COL32(  0,   0,   0, 120);
                constexpr ImU32 WHITE      = IM_COL32(255, 255, 255, 225);
                constexpr ImU32 RED        = IM_COL32(255,  60,  60, 235);

                for (EntityId e : Editor::GetSelectedEntities())
                {
                    if (!gizmoScene->IsAlive(e)) continue;

                    const Matrix3x2      worldTf  = GetWorldTransform(*gizmoScene, e);
                    const Vector2<float> worldPos = worldTf.TransformPoint({0.0f, 0.0f});
                    // 픽셀 스냅: D3D11 래스터라이저의 픽셀 중심은 N+0.5f 이므로
                    // floor 후 +0.5f 를 더해 항상 픽셀 중심에 정렬한다.
                    const ImVec2 raw = WorldToViewport(worldPos, vpMin, vpSize, m_cameraPos, m_cameraSize);
                    const ImVec2 sp(std::floor(raw.x) + 0.5f, std::floor(raw.y) + 0.5f);

                    // ── 그림자 (1px 오프셋) ──────────────────────────────────
                    const ImVec2 sh(sp.x + 1.0f, sp.y + 1.0f);
                    dl->AddCircle(sh, OUTER_R, SHADOW, 24, LINE_W + 0.5f);
                    dl->AddCircle(sh, INNER_R, SHADOW, 12, LINE_W + 0.5f);
                    dl->AddLine({ sh.x - CROSS_HALF, sh.y }, { sh.x - INNER_R,    sh.y }, SHADOW, LINE_W + 0.5f);
                    dl->AddLine({ sh.x + INNER_R,    sh.y }, { sh.x + CROSS_HALF, sh.y }, SHADOW, LINE_W + 0.5f);
                    dl->AddLine({ sh.x, sh.y - CROSS_HALF }, { sh.x, sh.y - INNER_R    }, SHADOW, LINE_W + 0.5f);
                    dl->AddLine({ sh.x, sh.y + INNER_R    }, { sh.x, sh.y + CROSS_HALF }, SHADOW, LINE_W + 0.5f);

                    // ── 원 두 개(빨간색) + 십자선(흰색) ────────────────────────
                    dl->AddCircle(sp, OUTER_R, RED,   24, LINE_W);
                    dl->AddCircle(sp, INNER_R, RED,   12, LINE_W);
                    dl->AddLine({ sp.x - CROSS_HALF, sp.y }, { sp.x - INNER_R,    sp.y }, WHITE, LINE_W);
                    dl->AddLine({ sp.x + INNER_R,    sp.y }, { sp.x + CROSS_HALF, sp.y }, WHITE, LINE_W);
                    dl->AddLine({ sp.x, sp.y - CROSS_HALF }, { sp.x, sp.y - INNER_R    }, WHITE, LINE_W);
                    dl->AddLine({ sp.x, sp.y + INNER_R    }, { sp.x, sp.y + CROSS_HALF }, WHITE, LINE_W);
                }
            }
        }

        dl->PopClipRect();
    }

    // Layer 2.8: 콜라이더 꼭짓점 핸들 (Inspector 콜라이더 탭 활성 시) ──────────
    {
        const char* inspTypeCol = Editor::Inspector
            ? Editor::Inspector->GetActiveComponentTypeName() : nullptr;
        const bool polyTabActive   = inspTypeCol && std::strcmp(inspTypeCol, "PolygonCollider2D") == 0;
        const bool circleTabActive = inspTypeCol && std::strcmp(inspTypeCol, "CircleCollider2D")  == 0;

        if ((polyTabActive || circleTabActive) && Core::SceneManager)
        {
            SafePtr<CScene> colScene = Core::SceneManager->GetActiveScene();
            const EntityId  colEnt   = Editor::GetSelectedEntity();
            if (colScene && colEnt != INVALID_ENTITY_ID && colScene->IsAlive(colEnt))
            {
                constexpr float HANDLE_R    = 3.0f;
                constexpr ImU32 HANDLE_COL  = IM_COL32( 80, 180, 255, 235);
                constexpr ImU32 HANDLE_SHAD = IM_COL32(  0,   0,   0, 120);

                const Matrix3x2 wt = GetWorldTransform(*colScene, colEnt);

                dl->PushClipRect(vpMin, vpMin + vpSize, true);

                auto toScreen = [&](const Vector2<float>& wp) -> ImVec2
                {
                    const ImVec2 raw = WorldToViewport(wp, vpMin, vpSize, m_cameraPos, m_cameraSize);
                    return ImVec2(std::floor(raw.x) + 0.5f, std::floor(raw.y) + 0.5f);
                };

                auto drawHandle = [&](const Vector2<float>& worldPt)
                {
                    const ImVec2 hp = toScreen(worldPt);
                    dl->AddCircleFilled(ImVec2(hp.x + 1.0f, hp.y + 1.0f), HANDLE_R, HANDLE_SHAD);
                    dl->AddCircleFilled(hp, HANDLE_R, HANDLE_COL);
                };

                if (polyTabActive)
                {
                    const PolygonCollider2D* poly = colScene->GetComponent<PolygonCollider2D>(colEnt);
                    if (poly && poly->IsEnabled)
                    {
                        std::vector<Vector2<float>> localPts;
                        poly->BuildLocalPoints(localPts);

                        // ── 꼭짓점 핸들 ───────────────────────────────────────
                        for (const auto& lp : localPts)
                            drawHandle(wt.TransformPoint(lp));
                    }
                }

                if (circleTabActive)
                {
                    const CircleCollider2D* circle = colScene->GetComponent<CircleCollider2D>(colEnt);
                    if (circle && circle->IsEnabled)
                    {
                        const Vector2<float> wCenter = wt.TransformPoint(circle->LocalCenter);
                        const float sx = std::sqrt(wt.M11 * wt.M11 + wt.M12 * wt.M12);
                        const float sy = std::sqrt(wt.M21 * wt.M21 + wt.M22 * wt.M22);
                        const float wr = circle->Radius * std::max(sx, sy);

                        // ── 꼭짓점 핸들 (중심 + 4방향 엣지) ─────────────────
                        drawHandle(wCenter);
                        drawHandle({ wCenter.x + wr, wCenter.y });
                        drawHandle({ wCenter.x - wr, wCenter.y });
                        drawHandle({ wCenter.x,      wCenter.y + wr });
                        drawHandle({ wCenter.x,      wCenter.y - wr });
                    }
                }

                dl->PopClipRect();
            }
        }
    }

    // ── 입력 처리 ────────────────────────────────────────────────────────────
    const bool isHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool isActive  = ImGui::IsItemActive();

    // 우클릭 드래그 → 팬
    if (isActive && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f))
    {
        const ImVec2 delta  = ImGui::GetIO().MouseDelta;
        const float  aspect = GetAspect(vpSize);
        m_targetCameraPos.x -= delta.x / vpSize.x * 2.0f * m_targetCameraSize * aspect;
        m_targetCameraPos.y += delta.y / vpSize.y * 2.0f * m_targetCameraSize;
        m_rightDragging     = true;   // 팬 시작 → 컨텍스트 메뉴 취소
        m_rightClickPending = false;
    }

    // 스크롤 → 줌 (커서 위치 고정)
    if (isHovered)
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            const ImVec2 mousePos = ImGui::GetIO().MousePos;
            const Vector2<float> worldBefore =
                ViewportToWorld(mousePos, vpMin, vpSize,
                                m_targetCameraPos, m_targetCameraSize);
            m_targetCameraSize *= std::powf(0.85f, wheel);
            m_targetCameraSize  = std::clamp(m_targetCameraSize, 0.01f, 2000.0f);
            const Vector2<float> worldAfter =
                ViewportToWorld(mousePos, vpMin, vpSize,
                                m_targetCameraPos, m_targetCameraSize);
            m_targetCameraPos.x += worldBefore.x - worldAfter.x;
            m_targetCameraPos.y += worldBefore.y - worldAfter.y;
        }
    }

    // ── 좌클릭 상태 머신 (클릭 vs 드래그 박스 선택) ─────────────────────────
    //
    // 상태 전이:
    //   mouse-down  → m_clickPending = true (즉시 선택하지 않고 대기)
    //   mouse-drag  → m_isDraggingLeft = true, m_clickPending = false
    //   mouse-up (drag) → 박스 선택 실행
    //   mouse-up (click) → 단일/더블 클릭 처리
    //
    // 토글 버튼 영역은 씬 입력에서 제외.

    if (!toggleHovered)
    {
        // 마우스 누름: 클릭 인텐트 기록
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            m_clickPending       = true;
            m_clickPendingDouble = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            m_dragStartScrn      = ImGui::GetIO().MousePos;
            m_isDraggingLeft     = false;
        }

        // 우클릭 누름: 컨텍스트 메뉴 인텐트 기록
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            m_rightClickPending = true;
            m_rightDragging     = false;
        }

        // 드래그 감지 (4px 이상 이동 시 클릭 → 드래그 전환)
        if (m_clickPending && isActive &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f))
        {
            m_isDraggingLeft = true;
            m_clickPending   = false;
        }

        // 드래그 중: 현재 위치 업데이트
        if (m_isDraggingLeft && isActive)
            m_dragCurrentScrn = ImGui::GetIO().MousePos;

        // 마우스 놓음
        if (ImGui::IsItemDeactivated())
        {
            if (m_isDraggingLeft)
            {
                // ── 드래그 박스 선택 ────────────────────────────────────────
                m_dragCurrentScrn = ImGui::GetIO().MousePos;

                const ImVec2 rMinS(
                    std::min(m_dragStartScrn.x, m_dragCurrentScrn.x),
                    std::min(m_dragStartScrn.y, m_dragCurrentScrn.y));
                const ImVec2 rMaxS(
                    std::max(m_dragStartScrn.x, m_dragCurrentScrn.x),
                    std::max(m_dragStartScrn.y, m_dragCurrentScrn.y));

                // 화면 → 월드 변환 (Y축 반전 주의: screen top = world high Y)
                const Vector2<float> boxWorldMin =
                    ViewportToWorld(ImVec2(rMinS.x, rMaxS.y),
                                    vpMin, vpSize, m_cameraPos, m_cameraSize);
                const Vector2<float> boxWorldMax =
                    ViewportToWorld(ImVec2(rMaxS.x, rMinS.y),
                                    vpMin, vpSize, m_cameraPos, m_cameraSize);

                if (Core::SceneManager)
                {
                    SafePtr<CScene> scene = Core::SceneManager->GetActiveScene();
                    if (scene)
                    {
                        m_editCtx.Validate(*scene);
                        const std::vector<EntityId> picked =
                            m_editCtx.PickBox(*scene, boxWorldMin, boxWorldMax, assetMgr);

                        std::vector<EntityId> toSelect;
                        toSelect.reserve(picked.size() * 4);
                        for (EntityId e : picked)
                        {
                            const bool isSelfInFocus =
                                m_editCtx.IsActive() && (e == m_editCtx.GetContext());
                            if (isSelfInFocus)
                                toSelect.push_back(e);
                            else
                            {
                                auto subtree = CollectSubtree(*scene, e);
                                toSelect.insert(toSelect.end(), subtree.begin(), subtree.end());
                            }
                        }
                        Editor::SelectEntities(toSelect);
                    }
                }

                m_isDraggingLeft = false;
            }
            else if (m_clickPending)
            {
                // ── 단일/더블 클릭 선택 ─────────────────────────────────────
                m_clickPending = false;

                if (Core::SceneManager)
                {
                    SafePtr<CScene> scene = Core::SceneManager->GetActiveScene();
                    if (scene)
                    {
                        m_editCtx.Validate(*scene);

                        const Vector2<float> worldPt =
                            ViewportToWorld(ImGui::GetIO().MousePos,
                                            vpMin, vpSize, m_cameraPos, m_cameraSize);

                        if (m_clickPendingDouble)
                        {
                            const EntityId picked = m_editCtx.Pick(*scene, worldPt, assetMgr);
                            if (picked != INVALID_ENTITY_ID)
                            {
                                const bool wasSelfInFocus =
                                    m_editCtx.IsActive() && (picked == m_editCtx.GetContext());
                                const EntityId toFocus = m_editCtx.OnDoubleClick(*scene, picked);
                                if (wasSelfInFocus)
                                    Editor::SelectEntities({ picked });
                                else
                                    Editor::SelectEntities(CollectSubtree(*scene, picked));
                                if (toFocus != INVALID_ENTITY_ID)
                                    FocusOnEntity(toFocus, *scene);
                            }
                            else
                            {
                                // 빈 공간 더블클릭: 한 뎁스 탈출 + 탈출한 엔티티 선택 & 포커싱
                                const EntityId exited = m_editCtx.OnDoubleClickEmpty(*scene);
                                if (exited != INVALID_ENTITY_ID)
                                {
                                    Editor::SelectEntities(CollectSubtree(*scene, exited));
                                    FocusOnEntity(exited, *scene);
                                }
                                else
                                {
                                    Editor::ClearSelection();
                                }
                            }
                        }
                        else
                        {
                            // ── 단일 클릭: 선택 + 컨텍스트 메뉴 ───────────────
                            const EntityId picked = m_editCtx.Pick(*scene, worldPt, assetMgr);
                            if (picked != INVALID_ENTITY_ID)
                            {
                                const bool isSelfInFocus =
                                    m_editCtx.IsActive() && (picked == m_editCtx.GetContext());

                                if (ImGui::GetIO().KeyCtrl)
                                {
                                    std::vector<EntityId> targets =
                                        isSelfInFocus ? std::vector<EntityId>{ picked }
                                                      : CollectSubtree(*scene, picked);
                                    if (Editor::IsSelected(picked))
                                        for (EntityId e : targets) Editor::RemoveFromSelection(e);
                                    else
                                        for (EntityId e : targets) Editor::AddToSelection(e);
                                }
                                else
                                {
                                    if (isSelfInFocus)
                                        Editor::SelectEntities({ picked });
                                    else
                                        Editor::SelectEntities(CollectSubtree(*scene, picked));
                                }
                            }
                            else
                            {
                                Editor::ClearSelection();
                            }
                        }
                    }
                }
            }

            // ── 우클릭 단일 클릭: 피킹 + 컨텍스트 메뉴 ──────────────────────
            if (m_rightClickPending && !m_rightDragging)
            {
                m_rightClickPending = false;
                if (Core::SceneManager)
                {
                    SafePtr<CScene> scene = Core::SceneManager->GetActiveScene();
                    if (scene)
                    {
                        m_editCtx.Validate(*scene);
                        const Vector2<float> worldPt =
                            ViewportToWorld(ImGui::GetIO().MousePos,
                                            vpMin, vpSize, m_cameraPos, m_cameraSize);
                        const EntityId picked = m_editCtx.Pick(*scene, worldPt, assetMgr);
                        if (picked != INVALID_ENTITY_ID)
                        {
                            // 오브젝트 우클릭 → 선택 후 컨텍스트 메뉴
                            const bool isSelfInFocus =
                                m_editCtx.IsActive() && (picked == m_editCtx.GetContext());
                            if (isSelfInFocus)
                                Editor::SelectEntities({ picked });
                            else
                                Editor::SelectEntities(CollectSubtree(*scene, picked));
                            m_contextMenuEntity = picked;
                            m_contextMenuParent = picked;
                        }
                        else
                        {
                            // 빈 공간 우클릭: 포커스 중이면 포커스 엔티티를 부모로
                            m_contextMenuEntity = INVALID_ENTITY_ID;
                            m_contextMenuParent = m_editCtx.IsActive()
                                ? m_editCtx.GetContext()
                                : INVALID_ENTITY_ID;
                        }
                        ImGui::OpenPopup("##SVCtxMenu");
                    }
                }
            }
            m_rightClickPending = false;
            m_rightDragging     = false;
        }
    }
    else
    {
        // 토글 버튼 위에 있을 때 클릭 인텐트 취소
        m_clickPending      = false;
        m_isDraggingLeft    = false;
        m_rightClickPending = false;
        m_rightDragging     = false;
    }

    // ── 씬뷰 컨텍스트 메뉴 ───────────────────────────────────────────────────
    //
    // 우클릭(드래그 아님) → OpenPopup("##SVCtxMenu") → 이곳에서 렌더
    //   빈 공간 클릭 : "Add Object" (포커스 중이면 포커스 엔티티를 부모로)
    //   오브젝트 클릭: "Add Child Object" + Separator + "Add Component >"
    if (ImGui::BeginPopup("##SVCtxMenu"))
    {
        SafePtr<CScene> popupScene =
            Core::SceneManager ? Core::SceneManager->GetActiveScene() : SafePtr<CScene>();
        if (popupScene)
        {
            const bool entityValid =
                m_contextMenuEntity != INVALID_ENTITY_ID &&
                popupScene->IsAlive(m_contextMenuEntity);

            if (entityValid)
            {
                // 오브젝트 우클릭: "Add Child Object" + "Add Component ▶"
                EditorGuiDrawHelpers::DrawAddObjectMenu(*popupScene, m_contextMenuEntity);
                ImGui::Separator();
                EditorGuiDrawHelpers::DrawAddComponentMenu(*popupScene, m_contextMenuEntity);
            }
            else
            {
                // 빈 공간 우클릭: m_contextMenuParent가 포커스 엔티티면 "Add Child Object"
                EditorGuiDrawHelpers::DrawAddObjectMenu(*popupScene, m_contextMenuParent);
            }
        }
        ImGui::EndPopup();
    }

    // ── Layer 3 / 3.5: 포커스 오버레이 + 선택 아웃라인 ──────────────────────
    // RT 파이프라인으로 이전됨 (ImEditor::OnRender에서 GPU 셰이더로 처리).
    // 여기서는 ImEditor에 상태만 전달.
    {
        SafePtr<CScene> scene;
        if (Core::SceneManager) scene = Core::SceneManager->GetActiveScene();

        // 포커스 컨텍스트
        if (m_editCtx.IsActive() && scene)
        {
            m_editCtx.Validate(*scene);
            if (m_editCtx.IsActive())
            {
                auto contextEntities = CollectSubtree(*scene, m_editCtx.GetContext());
                if (Editor::ImEditor)
                    Editor::ImEditor->SetSceneViewFocusContext(std::move(contextEntities));
            }
            else
            {
                if (Editor::ImEditor) Editor::ImEditor->ClearSceneViewFocusContext();
            }
        }
        else
        {
            if (Editor::ImEditor) Editor::ImEditor->ClearSceneViewFocusContext();
        }

        // 선택 아웃라인
        const auto& sel = Editor::GetSelectedEntities();
        if (!sel.empty() && scene)
        {
            std::vector<EntityId> alive;
            alive.reserve(sel.size());
            for (EntityId e : sel)
                if (scene->IsAlive(e)) alive.push_back(e);
            if (Editor::ImEditor)
                Editor::ImEditor->SetSceneViewSelection(std::move(alive));
        }
        else
        {
            if (Editor::ImEditor) Editor::ImEditor->ClearSceneViewSelection();
        }
    }

    // ── Layer 3.8: 드래그 박스 선택 시각화 ───────────────────────────────────
    if (m_isDraggingLeft)
    {
        const ImVec2 rMin(
            std::min(m_dragStartScrn.x, m_dragCurrentScrn.x),
            std::min(m_dragStartScrn.y, m_dragCurrentScrn.y));
        const ImVec2 rMax(
            std::max(m_dragStartScrn.x, m_dragCurrentScrn.x),
            std::max(m_dragStartScrn.y, m_dragCurrentScrn.y));
        dl->PushClipRect(vpMin, vpMin + vpSize, true);
        dl->AddRectFilled(rMin, rMax, IM_COL32(50, 160, 220, 20));
        dl->AddRect(rMin, rMax, IM_COL32(50, 160, 220, 200), 0.0f, 0, 1.5f);
        dl->PopClipRect();
    }

    // ── Layer 4: 텍스트 오버레이 ─────────────────────────────────────────────
    const bool hasScene =
        Core::SceneManager.IsValid() && Core::SceneManager->GetActiveScene().IsValid();
    const ImVec2 textPos = vpMin + ImVec2(12.0f, 10.0f);
    dl->AddText(textPos, IM_COL32(210, 216, 224, 255),
                hasScene ? "Active Scene" : "No Active Scene");

    const EntityId selectedEntity = Editor::GetSelectedEntity();
    char selText[96] = {};
    if (selectedEntity == INVALID_ENTITY_ID)
        std::snprintf(selText, sizeof(selText), "Selected: None");
    else
        std::snprintf(selText, sizeof(selText), "Selected: %llu",
                      static_cast<unsigned long long>(selectedEntity));
    dl->AddText(textPos + ImVec2(0.0f, 20.0f), IM_COL32(150, 158, 170, 255), selText);

    char camText[128] = {};
    std::snprintf(camText, sizeof(camText), "Cam (%.2f, %.2f) | Size %.2f",
                  m_cameraPos.x, m_cameraPos.y, m_cameraSize);
    dl->AddText(textPos + ImVec2(0.0f, 40.0f), IM_COL32(130, 140, 155, 200), camText);

    if (m_editCtx.IsActive())
    {
        char ctxText[64] = {};
        std::snprintf(ctxText, sizeof(ctxText),
                      "Focus: %llu  [Dbl-click empty to exit]",
                      static_cast<unsigned long long>(m_editCtx.GetContext()));
        dl->AddText(textPos + ImVec2(0.0f, 60.0f),
                    IM_COL32(255, 220, 50, 200), ctxText);
    }

    // ── Camera2D 뷰포트 인디케이터 ─────────────────────────────────────────────
    // Inspector에서 Camera2D 탭이 활성화된 경우에만 표시
    {
        const char* inspTypeCam = Editor::Inspector
            ? Editor::Inspector->GetActiveComponentTypeName() : nullptr;
        const bool cameraTabActive =
            inspTypeCam && std::strcmp(inspTypeCam, "Camera2D") == 0;

    if (cameraTabActive && selectedEntity != INVALID_ENTITY_ID && Core::SceneManager)
    {
        SafePtr<CScene> sceneForVP = Core::SceneManager->GetActiveScene();
        if (sceneForVP)
        {
            const Camera2D* cam = sceneForVP->GetComponent<Camera2D>(selectedEntity);
            if (cam)
            {
                float resW = 1920.0f, resH = 1080.0f;
                if (Editor::ImEditor)
                {
                    SafePtr<CProjectManager> pm = Editor::ImEditor->GetProjectManager();
                    if (pm && pm->IsProjectLoaded())
                    {
                        resW = static_cast<float>(pm->GetResolutionWidth());
                        resH = static_cast<float>(pm->GetResolutionHeight());
                    }
                }

                const Vector2<float> posPixel  = cam->Position.Resolve(resW, resH);
                      Vector2<float> sizePixel = cam->Size.Resolve(resW, resH);
                if (sizePixel.x < 1.0f) sizePixel.x = resW;
                if (sizePixel.y < 1.0f) sizePixel.y = resH;

                const float normX = posPixel.x  / resW;
                const float normY = posPixel.y  / resH;
                const float normW = sizePixel.x / resW;
                const float normH = sizePixel.y / resH;

                const float indW   = 160.0f;
                const float indH   = indW * (resH / resW);
                const float margin = 12.0f;

                const ImVec2 indMin(
                    vpMin.x + vpSize.x - indW - margin,
                    vpMin.y + vpSize.y - indH - margin);
                const ImVec2 indMax(indMin.x + indW, indMin.y + indH);

                dl->PushClipRect(vpMin, vpMin + vpSize, true);
                dl->AddRectFilled(indMin, indMax, IM_COL32(15, 18, 22, 210));
                dl->AddRect(indMin, indMax, IM_COL32(70, 80, 95, 180), 0.0f, 0, 1.0f);

                const ImVec2 vpRectMin(indMin.x + normX * indW, indMin.y + normY * indH);
                const ImVec2 vpRectMax(
                    vpRectMin.x + normW * indW, vpRectMin.y + normH * indH);
                dl->AddRectFilled(vpRectMin, vpRectMax, IM_COL32(50, 200, 80, 45));
                dl->AddRect(vpRectMin, vpRectMax,
                            IM_COL32(80, 230, 110, 230), 0.0f, 0, 1.5f);
                dl->AddText(ImVec2(indMin.x, indMin.y - 18.0f),
                            IM_COL32(80, 230, 110, 220),
                            Utillity::U8(u8"뷰포트 (선택된 카메라)"));
                dl->PopClipRect();
            }
        }
    }
    } // cameraTabActive block
}

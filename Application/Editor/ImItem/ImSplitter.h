#pragma once

#include "ThirdParty/imgui/imgui.h"   // ImVec2

class ImVerticalSplitter
{
public:
    ImVerticalSplitter();

    // Configure thickness of the draggable splitter bar
    ImVerticalSplitter& SetThickness(float t);

    // Minimum pixel size for either side
    ImVerticalSplitter& SetMinPixels(float px);

    // Draw and handle the vertical splitter using absolute coordinates.
    // `position` is the pixel offset from `regionMin.x` where the splitter should be placed.
    // `regionMin` is the top-left screen-space coordinate of the region containing the splitter.
    // `regionSize` is the size of that region in pixels.
    // Returns true while the splitter is actively being dragged (position changed).
    bool operator()(const char* id, float& position, ImVec2 regionMin, ImVec2 regionSize);

private:
    float m_thickness = 1.0f;
    float m_minPixels = 32.0f;
};

class ImHorizontalSplitter
{
public:
    ImHorizontalSplitter();

    ImHorizontalSplitter& SetThickness(float t);
    ImHorizontalSplitter& SetMinPixels(float px);

    // `position` is the pixel offset from `regionMin.y` where the horizontal splitter should be placed.
    bool operator()(const char* id, float& position, ImVec2 regionMin, ImVec2 regionSize);

private:
    float m_thickness = 1.0f;
    float m_minPixels = 32.0f;
};

// Simple free function that forwards to ImSplitter (global, pixel-based)
bool VerticalSplitter(const char* id, float& position, ImVec2 regionMin, ImVec2 regionSize, float thickness = 1.0f);
bool HorizontalSplitter(const char* id, float& position, ImVec2 regionMin, ImVec2 regionSize, float thickness = 1.0f);

namespace ImGui
{
    namespace Utillity
    {
        // Legacy API (ratio-based) kept for compatibility with existing call sites.
        bool VerticalSplitter(const char* id, float& ratio, ImVec2 availSpace,
            const float minRatio = 0.15f, const float maxRatio = 0.8f,
            float thickness = 1.0f);
    }
}

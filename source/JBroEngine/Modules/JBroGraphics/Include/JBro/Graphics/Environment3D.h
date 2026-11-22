#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

#include <cstdint>

namespace JBro
{
    // 뷰의 환경광 출처다(D-447). 캔버스의 `EnvironmentAmbientSource` 와 같은 차례다.
    enum class EnvironmentAmbient : std::uint8_t
    {
        Color,
        Gradient,
        Sky
    };

    // **뷰 하나의 환경**(D-447, lighting3d-plan §6). `CameraParams::environment` 로 뷰마다 온다. 표준 머티리얼(`Standard`)이 b4·t6·s1 로 읽는다.
    // 기본값은 빛을 더하지 않는다(검은 `Color`, 하늘 없음) - 환경이 생기기 전과 같은 그림이다.
    struct Environment3D
    {
        EnvironmentAmbient ambient = EnvironmentAmbient::Color;
        // 세기를 곱한 선형 색이다. `Color` 는 `ambientColor`, `Gradient` 는 위·옆·아래 셋이다. 반사도 이 색을 비춘다.
        Float ambientColor[3] = {0.0f, 0.0f, 0.0f};
        Float skyColor[3] = {0.0f, 0.0f, 0.0f};
        Float equatorColor[3] = {0.0f, 0.0f, 0.0f};
        Float groundColor[3] = {0.0f, 0.0f, 0.0f};
        // **하늘 맵**(`Renderer::RegisterEnvironmentMap`). 비면 `Sky` 는 빛이 없고 하늘도 그리지 않는다.
        AssetHandle map;
        // 하늘 맵을 방향마다 적분한 확산광(조도 / π)의 SH 9 계수다(`ProjectEnvironmentSh`). 하늘의 세기와 환경광 세기를 이미 곱했다.
        Float sh[9][3] = {};
        // 하늘 맵 그림에 곱하는 밝기다(배경과 반사).
        Float skyIntensity = 1.0f;
        // 하늘을 +Y 축으로 돌린 각(라디안)이다.
        Float skyRotation = 0.0f;
        // 반사에 곱한다.
        Float reflectionIntensity = 1.0f;
        // 이 뷰는 하늘 배경만 그린다 - 브리지가 레이어 뷰 앞에 연다. 하늘 맵이 없으면 아무것도 그리지 않는다.
        Bool drawSky = false;
    };
}

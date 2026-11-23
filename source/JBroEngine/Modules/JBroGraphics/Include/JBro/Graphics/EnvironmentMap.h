#pragma once

#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

#include <cstddef>

namespace JBro
{
    // **하늘 맵을 굽는다**(D-447, lighting3d-plan §6). 렌더러를 모르는 CPU 셈이다 - 시험이 장치 없이 본다.
    //
    // 그림은 등장방형이다: u 는 +Y 둘레를 한 바퀴(-Z 가 0.5, +X 가 0.75), v 는 바로 위(0)에서 바로 아래(1)까지다. `BuiltinEnvironment.hlsli` 와 같다.
    // 밉 0 은 256 x 128 로 맞춘 그림(거울)이고, 그 아래 단계는 거칠기 단계 / (단계 수 - 1) 의 GGX 로 미리 거른 그림이다(Karis 2013 의 미리 거르기,
    // 원본은 거칠기에 맞는 밉에서 읽는다 - 적은 표본으로 얼룩이 없다). 확산광은 SH 9 계수다(Ramamoorthi·Hanrahan 2001).

    inline constexpr UInt32 EnvironmentMapWidth = 256;
    inline constexpr UInt32 EnvironmentMapHeight = 128;
    inline constexpr UInt32 EnvironmentMapLevels = 6;

    struct EnvironmentMapLevel
    {
        UInt32 width = 0;
        UInt32 height = 0;
        // `RGBA16Float` 텍셀이다(알파 1).
        Array<std::byte> rgba16f;
    };

    struct EnvironmentMapBuild
    {
        Array<EnvironmentMapLevel> levels;
        // 조도 / π 의 SH 9 계수다(rgb). 셰이더가 법선의 기저와 곱해 더하면 흰 램버트 면의 밝기다.
        Float sh[9][3] = {};
    };

    // 등장방형 방향과 텍셀 자리를 오간다. `u`·`v` 는 0..1 이다.
    void EnvironmentDirection(Float u, Float v, Float& x, Float& y, Float& z);
    void EnvironmentUv(Float x, Float y, Float z, Float& u, Float& v);

    // 굽기를 도중에 멈출지 묻는다(D-449). 굽기는 워커에서 도므로 하늘이 다시 바뀌거나 캔버스가 내려가면 남은 줄을 버리고 빨리 돌아와야 한다.
    // `requested` 가 비면 끝까지 굽는다.
    struct EnvironmentMapCancel
    {
        Bool (*requested)(const void* context) = nullptr;
        const void* context = nullptr;
    };

    // `rgb` 는 `width * height * 3` 의 선형 빛이다(행은 위에서 아래). 크기가 0 이거나 값이 모자라면 거짓이다. 멈추라고 하면 거짓이다.
    // 할당이 있고 디버그 빌드에서 몇 초 걸린다 - 프레임 경로가 아니라 워커에서 부른다(`EnvironmentLibrary`).
    Bool BuildEnvironmentMap(const Float* rgb, UInt32 width, UInt32 height, EnvironmentMapBuild& result, const EnvironmentMapCancel& cancel = {});

    // 조도 / π 의 SH 9 계수만 낸다. `BuildEnvironmentMap` 도 이것을 쓴다(밉 0 에서).
    void ProjectEnvironmentSh(const Float* rgb, UInt32 width, UInt32 height, Float (&sh)[9][3]);

    // SH 9 계수를 방향 하나에서 푼다(셰이더의 `EnvironmentDiffuse` 와 같은 식, 0 아래는 0).
    void EvaluateEnvironmentSh(const Float (&sh)[9][3], Float x, Float y, Float z, Float (&rgb)[3]);
}

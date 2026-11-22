#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Canvas/Environment.h>
#include <JBro/Graphics/Environment3D.h>
#include <JBro/Graphics/EnvironmentMap.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    class AssetSystem;
    class Renderer;

    // **캔버스 환경을 렌더러의 환경으로 푼다**(D-447, lighting3d-plan §6). 색은 세기를 곱해 옮기고, 하늘 맵(`EnvironmentMap` 에셋)은 SH 와 미리 거른 밉으로
    // 한 번 구워 렌더러에 올린다(`BuildEnvironmentMap`·`Renderer::RegisterEnvironmentMap`). 같은 하늘·같은 자료 세대면 다시 굽지 않는다.
    // 렌더러 프레임 밖에서만 부른다(굽기와 올리기가 있다).
    class EnvironmentLibrary
    {
    public:
        void Initialize(Renderer* renderer, const AssetSystem* assets);
        void Shutdown();

        // 캔버스 환경 하나를 푼다. 하늘이 없거나 풀리지 않으면 `map` 이 비고 `Sky` 환경광은 빛이 없다.
        void Resolve(const CanvasEnvironment& environment, Environment3D& result);

        // 지금 올려 둔 하늘 맵이다(시험).
        AssetHandle GetUploadedMap() const
        {
            return m_uploaded;
        }
        // 하늘을 구운 횟수다(시험 - 같은 하늘을 다시 굽지 않는지 본다).
        UInt32 GetBuildCount() const
        {
            return m_buildCount;
        }

    private:
        void Release();

        Renderer* m_renderer = nullptr;
        const AssetSystem* m_assets = nullptr;
        // 구운 하늘의 출처(에셋 핸들과 자료 세대)와 그 결과다.
        AssetHandle m_source;
        UInt32 m_sourceGeneration = 0;
        AssetHandle m_uploaded;
        Float m_sh[9][3] = {};
        UInt32 m_buildCount = 0;
    };
}

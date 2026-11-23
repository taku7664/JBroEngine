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
    class TaskManager;
    class EnvironmentBakeTask;
    struct EnvironmentMapData;

    // **캔버스 환경을 렌더러의 환경으로 푼다**(D-447, lighting3d-plan §6). 색은 세기를 곱해 옮기고, 하늘 맵(`EnvironmentMap` 에셋)은 SH 와 미리 거른 밉으로
    // 구워 렌더러에 올린다(`BuildEnvironmentMap`·`Renderer::RegisterEnvironmentMap`). 같은 하늘·같은 자료 세대면 다시 굽지 않는다.
    //
    // **굽기는 워커에서 돈다**(D-449). 디버그에서 2~4 초라 프레임에서 구우면 에디터가 그만큼 멈췄다. 하늘이 바뀌면 원본을 떠서 태스크 관리자에 넘기고,
    // 다 구운 것은 그 뒤의 `Resolve` 가 올린다. 그동안은 앞 하늘을 그대로 그리고(처음이면 하늘 없이), 하늘을 비우면 바로 내린다.
    // 굽는 도중 하늘이 또 바뀌면 앞 굽기는 멈추라고 하고 결과를 버린다. 태스크 관리자가 없으면(시험·워커 없는 빌드) 그 자리에서 굽는다.
    // 렌더러 프레임 밖에서만 부른다(올리기가 있다).
    class EnvironmentLibrary
    {
    public:
        void Initialize(Renderer* renderer, const AssetSystem* assets, TaskManager* tasks = nullptr);
        // 도는 굽기에 멈추라고 하고 끝나기를 기다린 뒤 올린 하늘을 내린다. 굽기는 줄마다 멈춤을 보므로 오래 서지 않는다.
        void Shutdown();

        // 캔버스 환경 하나를 푼다. 하늘이 없거나 아직 굽고 있거나 풀리지 않으면 `map` 이 비고 `Sky` 환경광은 빛이 없다.
        void Resolve(const CanvasEnvironment& environment, Environment3D& result);

        // 지금 올려 둔 하늘 맵이다(시험).
        AssetHandle GetUploadedMap() const
        {
            return m_uploaded;
        }
        // 하늘을 다 구운 횟수다(시험 - 같은 하늘을 다시 굽지 않는지 본다). 멈춘 굽기는 세지 않는다.
        UInt32 GetBuildCount() const
        {
            return m_buildCount;
        }
        // 워커에서 도는 굽기가 있거나 다 구운 것이 아직 올라가지 않았다(시험·에디터 - 하늘이 설 때까지 프레임을 돌린다).
        Bool IsBaking() const
        {
            return m_bakesInFlight > 0 || m_bakedReady;
        }

    private:
        friend class EnvironmentBakeTask;

        void Release();
        void StartBake(AssetHandle sky, const EnvironmentMapData& data);
        // 굽기 하나가 끝났다(메인 스레드). 지금 바라는 굽기(번호)의 것이면 남겨 두고 다음 `Resolve` 가 올린다.
        void FinishBake(UInt32 serial, Bool fromWorker, Bool completed, Bool built, EnvironmentMapBuild&& build);
        void CancelBakes();

        Renderer* m_renderer = nullptr;
        const AssetSystem* m_assets = nullptr;
        TaskManager* m_tasks = nullptr;
        // 올린 하늘의 출처(에셋 핸들과 자료 세대)와 그 결과다.
        AssetHandle m_source;
        UInt32 m_sourceGeneration = 0;
        AssetHandle m_uploaded;
        Float m_sh[9][3] = {};
        UInt32 m_buildCount = 0;
        // 굽고 있거나 다 구운 하늘이다. 다른 하늘의 결과는 버린다.
        AssetHandle m_wanted;
        UInt32 m_wantedGeneration = 0;
        Bool m_wantedActive = false;
        // 굽기마다 하나씩 오르는 번호다. 지금 바라는 굽기는 마지막 번호다.
        UInt32 m_bakeSerial = 0;
        // 그 굽기의 묶음 번호(`TaskGroupId`)다. 0 은 없음이다 - 이 헤더를 쓰는 곳이 태스크 모듈을 몰라도 되게 수로 든다.
        UInt64 m_wantedGroup = 0;
        // 아직 끝나지 않은 굽기 수다(멈추라고 한 것도 센다). 내려갈 때 0 이 될 때까지 기다린다.
        UInt32 m_bakesInFlight = 0;
        Bool m_bakedReady = false;
        Bool m_bakedOk = false;
        EnvironmentMapBuild m_baked;
        // 굽지 못한 하늘이다. 같은 하늘·같은 세대를 프레임마다 다시 굽지 않는다.
        AssetHandle m_failed;
        UInt32 m_failedGeneration = 0;
    };
}

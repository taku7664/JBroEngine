#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Task/TaskGroup.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/String.h>

#include <cstdint>

// 에셋을 워커에서 디코드해 싣는 로드 묶음이다(D-233). 캔버스를 열 때 그 캔버스가 쓰는 에셋을 여기로 보내면, 파일 읽기와
// 디코드는 워커가 하고 풀에 넣는 것은 메인 스레드의 콜백이 한다(`Log`·`SafePtr` 는 메인 전용). 끝나면 부르는 쪽이 바인딩한다 -
// 그때 `Load` 는 이미 실린 것을 찾아 참조만 올린다.
//
// 에디터와 게임이 함께 쓰려고 호스트에 둔다. 호스트는 이미 `JBroTask`·`JBroAsset` 을 둘 다 본다.
namespace JBro
{
    class AssetSystem;
    class IAssetSource;
    class TaskManager;

    // 로드 묶음의 결과다. **묶음이 끝날 때까지 부르는 쪽이 살려 둔다** - 먼저 내려야 하면 묶음을 `RequestCancel` 하고 `Wait` 한다.
    struct AssetLoadResult
    {
        // 워커로 실은 에셋마다 참조 하나다. 바인딩한 뒤 `AssetSystem::ReleaseAll` 로 놓는다 - 그 전에 놓으면
        // `CollectUnused` 가 바인딩 전에 내릴 수 있다.
        Array<AssetHandle> held;
        std::uint32_t failed = 0;
        // 처음 실패한 것의 사유다(어느 파일을 왜 못 읽었는지). 알림 한 줄에 쓴다.
        String firstFailure;
        // 묶음의 `OnFinished` 가 불렸다. 취소로 끝나도 참이다.
        bool finished = false;
        bool canceled = false;
    };

    // `ids` 가운데 워커로 갈 수 있는 것(텍스처·오디오, 스프라이트는 주인 텍스처)을 태스크 하나씩으로 묶어 병렬로 제출한다.
    // 이미 실린 것과 이 길로 가지 않는 것은 건너뛴다 - 바인딩이 동기로 싣는다. 보낼 것이 없어도 빈 묶음을 제출해 다음 `Update`
    // 에서 끝난다. 묶음 이름은 로컬라이징 키 `groupNameKey`, 태스크 이름은 에셋 파일 이름이다.
    // 초기화되지 않은 관리자면 `InvalidTaskGroupId` 이고 `result.finished` 가 곧바로 참이다.
    // 파일은 에셋 시스템의 소스(`AssetSystem::GetSource`)로 읽는다. 워커에서 읽을 수 없는 소스(패키지)면 보낼 것이 없다(D-233).
    TaskGroupId SubmitAssetLoad(TaskManager& tasks, AssetSystem& assets,
        ArrayView<const AssetId> ids, const char* groupNameKey, AssetLoadResult& result);
}

#pragma once

#include <JBro/Asset/ModelDecoder.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math3D.h>
#include <JBro/Types/Matrix4x4.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    // **뼈대의 자세를 셈한다**(D-457, skeletal-plan §3.3). 캔버스도 렌더러도 모르는 순수 셈이라 시험이 그대로 본다.
    // 배열은 부르는 쪽이 뼈 수만큼 잡아 두고 넘긴다 - 이 함수들은 프레임 경로에서 돌므로 할당하지 않는다(`BindClipTracks`·`BuildMaskWeights` 만 묶을 때 한 번 잡는다).

    // 뼈 하나의 자세(부모 뼈 기준)다.
    struct BonePose
    {
        Vector3 position;
        Quaternion rotation;
        Vector3 scale{1.0f, 1.0f, 1.0f};
    };

    // 쉬는 자세(파일의 노드 자세)로 채운다. `pose` 는 뼈 수만큼이어야 한다.
    void MakeRestPose(const ModelSkeleton& skeleton, Array<BonePose>& pose);

    // 클립의 트랙마다 뼈 번호를 이름으로 찾는다(D-451 - 같은 이름의 뼈대면 다른 모델의 클립도 쓴다). 없는 뼈의 트랙은 -1 이다. 묶을 때 한 번 부른다.
    void BindClipTracks(const ModelSkeleton& skeleton, const ModelAnimation& clip, Array<Int32>& trackBones);

    // 재생 시간을 클립 안으로 맞춘다. 반복이면 감고, 아니면 끝에서 멈춘다. 길이가 0 이면 0 이다.
    Float WrapClipTime(Float time, Float duration, Bool loop);

    // 클립을 `time` 에서 뽑아 트랙이 닿는 뼈만 덮어쓴다(닿지 않는 뼈는 그대로). 위치·크기는 선형, 회전은 짧은 쪽으로 정규화 선형 보간이다. STEP 은 앞 키다.
    void SampleClip(const ModelAnimation& clip, const Array<Int32>& trackBones, Float time, Array<BonePose>& pose);

    // **덮어쓰는 레이어**: `base` 를 `layer` 쪽으로 (가중치 x 뼈의 마스크) 만큼 옮긴다. `boneWeights` 가 비면 모든 뼈가 1 이다.
    void BlendPose(Array<BonePose>& base, const Array<BonePose>& layer, Float weight, const Array<Float>& boneWeights);
    // **더하는 레이어**: `layer` 가 `reference` 에서 벗어난 만큼을 `base` 에 (가중치 x 마스크) 만큼 더한다(위치는 차, 회전은 상대 회전, 크기는 비).
    void AddPose(Array<BonePose>& base, const Array<BonePose>& layer, const Array<BonePose>& reference, Float weight, const Array<Float>& boneWeights);

    // **뼈 마스크**(Unreal 의 Branch Filter). `rootBone` 과 그 자식 모두가 1 이고 나머지는 0 이다. `blendDepth` 가 n 이면 뿌리부터 n 뼈에 걸쳐 1/(n+1) 씩 올라
    // 그 아래는 1 이다(0 이면 뿌리부터 1). 뿌리가 없으면(-1) 모두 1 이다. 묶을 때 한 번 부른다.
    void BuildMaskWeights(const ModelSkeleton& skeleton, Int32 rootBone, UInt32 blendDepth, Array<Float>& weights);

    // 뼈마다 월드(모델 뿌리 기준) 행렬이다. 뿌리 뼈 앞에 뼈대의 `rootParent` 를 곱한다. 부모가 늘 앞에 있다고 본다(디코더가 그렇게 늘어놓는다).
    void ComputeBoneMatrices(const ModelSkeleton& skeleton, const Array<BonePose>& pose, Array<Matrix4x4>& model);
    // 스키닝 행렬 = 뼈의 모델 행렬 x 역바인드. 정점 셰이더가 이것으로 메시 좌표의 정점을 옮긴다.
    void ComputeSkinMatrices(const ModelSkeleton& skeleton, const Array<Matrix4x4>& model, Array<Matrix4x4>& skin);

    // 뼈 이름으로 번호를 찾는다. 없으면 -1. 묶을 때 쓴다(프레임 경로에서 문자열을 견주지 않는다).
    Int32 FindBone(const ModelSkeleton& skeleton, std::string_view name);
}

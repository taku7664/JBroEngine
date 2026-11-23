#include <JBro/Framework3DSystem/Animation/AnimationPose.h>

#include <JBro/Framework3DSystem/Math3DMatrix.h>

#include <cmath>

namespace JBro
{
    namespace
    {
        Float QuaternionDot(const Quaternion& left, const Quaternion& right)
        {
            return left.x * right.x + left.y * right.y + left.z * right.z + left.w * right.w;
        }

        // 짧은 쪽으로 도는 정규화 선형 보간이다. 키 사이가 좁은 애니메이션에서 slerp 와 눈으로 가르기 어렵고 삼각함수가 없다.
        Quaternion Nlerp(const Quaternion& from, const Quaternion& to, Float t)
        {
            const Float sign = QuaternionDot(from, to) < 0.0f ? Float(-1.0f) : Float(1.0f);
            const Float keep = 1.0f - t;
            const Float take = t * sign;
            return Normalize(Quaternion{from.x * keep + to.x * take, from.y * keep + to.y * take, from.z * keep + to.z * take,
                from.w * keep + to.w * take});
        }

        Vector3 Lerp(const Vector3& from, const Vector3& to, Float t)
        {
            return Add(from, Scale(Subtract(to, from), t));
        }

        // 키 사이에서 앞 키와 비율을 찾는다. 키가 하나면 그 키다.
        void FindKey(const Array<Float>& times, Float time, std::size_t& key, Float& t)
        {
            const std::size_t count = times.Size();
            key = 0;
            t = 0.0f;
            if (count < 2 || time <= times[0])
            {
                return;
            }
            if (time >= times[count - 1])
            {
                key = count - 1;
                return;
            }
            // 두 갈래로 찾는다 - 키가 많은 클립도 프레임마다 짧다.
            std::size_t low = 0;
            std::size_t high = count - 1;
            while (high - low > 1)
            {
                const std::size_t middle = (low + high) / 2;
                if (times[middle] <= time)
                {
                    low = middle;
                }
                else
                {
                    high = middle;
                }
            }
            key = low;
            const Float span = times[high] - times[low];
            t = span > 0.0f ? Float((time - times[low]) / span) : Float(0.0f);
        }

        Float MaskOf(const Array<Float>& boneWeights, std::size_t bone)
        {
            return boneWeights.IsEmpty() ? Float(1.0f) : (bone < boneWeights.Size() ? boneWeights[bone] : Float(0.0f));
        }
    }

    void MakeRestPose(const ModelSkeleton& skeleton, Array<BonePose>& pose)
    {
        const std::size_t count = skeleton.bones.Size() < pose.Size() ? skeleton.bones.Size() : pose.Size();
        for (std::size_t bone = 0; bone < count; ++bone)
        {
            pose[bone].position = skeleton.bones[bone].position;
            pose[bone].rotation = skeleton.bones[bone].rotation;
            pose[bone].scale = skeleton.bones[bone].scale;
        }
    }

    Int32 FindBone(const ModelSkeleton& skeleton, std::string_view name)
    {
        for (std::size_t bone = 0; bone < skeleton.bones.Size(); ++bone)
        {
            if (skeleton.bones[bone].name == name)
            {
                return static_cast<Int32>(bone);
            }
        }
        return -1;
    }

    void BindClipTracks(const ModelSkeleton& skeleton, const ModelAnimation& clip, Array<Int32>& trackBones)
    {
        trackBones.Resize(clip.tracks.Size());
        for (std::size_t track = 0; track < clip.tracks.Size(); ++track)
        {
            trackBones[track] = FindBone(skeleton, clip.tracks[track].bone);
        }
    }

    Float WrapClipTime(Float time, Float duration, Bool loop)
    {
        if (false == (duration > 0.0f) || false == std::isfinite(time.Get()))
        {
            return 0.0f;
        }
        if (loop)
        {
            const Float wrapped = std::fmod(time.Get(), duration.Get());
            return wrapped < 0.0f ? Float(wrapped + duration) : wrapped;
        }
        return time < 0.0f ? Float(0.0f) : (time > duration ? duration : time);
    }

    void SampleClip(const ModelAnimation& clip, const Array<Int32>& trackBones, Float time, Array<BonePose>& pose)
    {
        const std::size_t tracks = clip.tracks.Size() < trackBones.Size() ? clip.tracks.Size() : trackBones.Size();
        for (std::size_t index = 0; index < tracks; ++index)
        {
            const Int32 bone = trackBones[index];
            const ModelAnimationTrack& track = clip.tracks[index];
            if (bone < 0 || static_cast<std::size_t>(bone.Get()) >= pose.Size() || track.times.IsEmpty())
            {
                continue;
            }
            std::size_t key = 0;
            Float t = 0.0f;
            FindKey(track.times, time, key, t);
            if (track.interpolation == ModelInterpolation::Step)
            {
                t = 0.0f;
            }
            const std::size_t next = key + 1 < track.times.Size() ? key + 1 : key;
            BonePose& target = pose[static_cast<std::size_t>(bone.Get())];
            if (track.path == ModelTrackPath::Rotation)
            {
                if (track.values.Size() < (next + 1) * 4)
                {
                    continue;
                }
                const Quaternion from{track.values[key * 4 + 0], track.values[key * 4 + 1], track.values[key * 4 + 2], track.values[key * 4 + 3]};
                const Quaternion to{track.values[next * 4 + 0], track.values[next * 4 + 1], track.values[next * 4 + 2], track.values[next * 4 + 3]};
                target.rotation = Nlerp(from, to, t);
                continue;
            }
            if (track.values.Size() < (next + 1) * 3)
            {
                continue;
            }
            const Vector3 from{track.values[key * 3 + 0], track.values[key * 3 + 1], track.values[key * 3 + 2]};
            const Vector3 to{track.values[next * 3 + 0], track.values[next * 3 + 1], track.values[next * 3 + 2]};
            if (track.path == ModelTrackPath::Position)
            {
                target.position = Lerp(from, to, t);
            }
            else
            {
                target.scale = Lerp(from, to, t);
            }
        }
    }

    void BlendPose(Array<BonePose>& base, const Array<BonePose>& layer, Float weight, const Array<Float>& boneWeights)
    {
        const std::size_t count = base.Size() < layer.Size() ? base.Size() : layer.Size();
        for (std::size_t bone = 0; bone < count; ++bone)
        {
            const Float t = weight * MaskOf(boneWeights, bone);
            if (t <= 0.0f)
            {
                continue;
            }
            if (t >= 1.0f)
            {
                base[bone] = layer[bone];
                continue;
            }
            base[bone].position = Lerp(base[bone].position, layer[bone].position, t);
            base[bone].rotation = Nlerp(base[bone].rotation, layer[bone].rotation, t);
            base[bone].scale = Lerp(base[bone].scale, layer[bone].scale, t);
        }
    }

    void AddPose(Array<BonePose>& base, const Array<BonePose>& layer, const Array<BonePose>& reference, Float weight, const Array<Float>& boneWeights)
    {
        std::size_t count = base.Size() < layer.Size() ? base.Size() : layer.Size();
        count = count < reference.Size() ? count : reference.Size();
        for (std::size_t bone = 0; bone < count; ++bone)
        {
            const Float t = weight * MaskOf(boneWeights, bone);
            if (t <= 0.0f)
            {
                continue;
            }
            const Vector3 moved = Subtract(layer[bone].position, reference[bone].position);
            base[bone].position = Add(base[bone].position, Scale(moved, t));
            // 상대 회전 = 기준의 역 x 레이어. 가중치만큼 단위 회전에서 그쪽으로 옮겨 뒤에 곱한다.
            const Quaternion relative = Multiply(Conjugate(reference[bone].rotation), layer[bone].rotation);
            base[bone].rotation = Normalize(Multiply(base[bone].rotation, Nlerp(Quaternion{}, relative, t)));
            const Vector3 ratio{reference[bone].scale.x != 0.0f ? Float(layer[bone].scale.x / reference[bone].scale.x) : Float(1.0f),
                reference[bone].scale.y != 0.0f ? Float(layer[bone].scale.y / reference[bone].scale.y) : Float(1.0f),
                reference[bone].scale.z != 0.0f ? Float(layer[bone].scale.z / reference[bone].scale.z) : Float(1.0f)};
            base[bone].scale = Multiply(base[bone].scale, Lerp(Vector3{1.0f, 1.0f, 1.0f}, ratio, t));
        }
    }

    void BuildMaskWeights(const ModelSkeleton& skeleton, Int32 rootBone, UInt32 blendDepth, Array<Float>& weights)
    {
        const std::size_t count = skeleton.bones.Size();
        weights.Resize(count);
        if (rootBone < 0 || static_cast<std::size_t>(rootBone.Get()) >= count)
        {
            for (std::size_t bone = 0; bone < count; ++bone)
            {
                weights[bone] = 1.0f;
            }
            return;
        }
        // 뿌리에서의 깊이(뿌리 0)다. 뿌리 아래가 아니면 -1. 부모가 늘 앞에 있으므로 한 번 훑으면 된다.
        Array<Int32> depth;
        depth.Resize(count);
        for (std::size_t bone = 0; bone < count; ++bone)
        {
            const Int32 parent = skeleton.bones[bone].parent;
            if (static_cast<Int32>(bone) == rootBone)
            {
                depth[bone] = 0;
            }
            else if (parent >= 0 && static_cast<std::size_t>(parent.Get()) < bone && depth[static_cast<std::size_t>(parent.Get())] >= 0)
            {
                depth[bone] = depth[static_cast<std::size_t>(parent.Get())] + 1;
            }
            else
            {
                depth[bone] = -1;
            }
        }
        for (std::size_t bone = 0; bone < count; ++bone)
        {
            if (depth[bone] < 0)
            {
                weights[bone] = 0.0f;
                continue;
            }
            // 깊이 n 이면 뿌리가 1/(n+1), 그 아래로 한 칸마다 1/(n+1) 씩 올라 n 칸 아래부터 1 이다.
            const Float step = Float(1.0f) / static_cast<JBro::Float>(blendDepth.Get() + 1);
            const Float value = step * static_cast<JBro::Float>(depth[bone].Get() + 1);
            weights[bone] = value > 1.0f ? Float(1.0f) : value;
        }
    }

    void ComputeBoneMatrices(const ModelSkeleton& skeleton, const Array<BonePose>& pose, Array<Matrix4x4>& model)
    {
        std::size_t count = skeleton.bones.Size() < pose.Size() ? skeleton.bones.Size() : pose.Size();
        count = count < model.Size() ? count : model.Size();
        for (std::size_t bone = 0; bone < count; ++bone)
        {
            const Matrix4x4 local = MakeTransformMatrix3D(pose[bone].position, pose[bone].rotation, pose[bone].scale);
            const Int32 parent = skeleton.bones[bone].parent;
            const Matrix4x4& above = parent >= 0 && static_cast<std::size_t>(parent.Get()) < bone ? model[static_cast<std::size_t>(parent.Get())] : skeleton.rootParent;
            model[bone] = MultiplyMatrix4x4(above, local);
        }
    }

    void ComputeSkinMatrices(const ModelSkeleton& skeleton, const Array<Matrix4x4>& model, Array<Matrix4x4>& skin)
    {
        std::size_t count = skeleton.bones.Size() < model.Size() ? skeleton.bones.Size() : model.Size();
        count = count < skin.Size() ? count : skin.Size();
        for (std::size_t bone = 0; bone < count; ++bone)
        {
            skin[bone] = MultiplyMatrix4x4(model[bone], skeleton.bones[bone].inverseBind);
        }
    }
}

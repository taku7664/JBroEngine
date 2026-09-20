#pragma once

#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Math2D.h>
#include <JBro/NetworkSystem/CanvasPoolAdapter.h>

#include <type_traits>

namespace JBro
{
    // `Transform2D` 가 와이어에 실리는 모양. 저작 값 셋이다. 월드 캐시는 받는 쪽의 `Transform2DSystem` 이 다시 계산한다.
    struct Transform2DWire
    {
        Vec2 position;
        float rotation = 0.0f;
        Vec2 scale{ 1.0f, 1.0f };
    };

    static_assert(std::is_trivially_copyable_v<Transform2DWire>);
    static_assert(sizeof(Transform2DWire) == 20);

    struct Transform2DReplicationTraits
    {
        static void Pack(const Component::Transform2D& transform, Transform2DWire& wire)
        {
            wire.position = transform.position;
            wire.rotation = transform.rotation;
            wire.scale = transform.scale;
        }

        // `from` 이 있으면 두 스냅숏 사이를 `alpha` 로 보간한다(network-plan §2.6). 없으면 그대로 둔다.
        static void Unpack(const Transform2DWire* from, const Transform2DWire& to, float alpha, Component::Transform2D& transform)
        {
            if (nullptr == from || alpha >= 1.0f)
            {
                transform.position = to.position;
                transform.rotation = to.rotation;
                transform.scale = to.scale;
                return;
            }
            const float t = alpha < 0.0f ? 0.0f : alpha;
            transform.position = { from->position.x + (to.position.x - from->position.x) * t,
                from->position.y + (to.position.y - from->position.y) * t };
            transform.rotation = from->rotation + (to.rotation - from->rotation) * t;
            transform.scale = { from->scale.x + (to.scale.x - from->scale.x) * t, from->scale.y + (to.scale.y - from->scale.y) * t };
        }
    };

    using Transform2DReplicatedPool = CanvasPoolAdapter<Component::Transform2D, Transform2DWire, Transform2DReplicationTraits>;
}

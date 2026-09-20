#pragma once

#include <JBro/Network/Replication/ReplicationTypes.h>

#include <cstdint>

// 복제 핵심이 엔진을 보는 유일한 창이다. 핵심은 타입도 차원도 모른다 - 오브젝트 식별자와 고정 크기 바이트 블록만 다룬다.
// 부착 단계의 어댑터가 `Canvas` 의 컴포넌트 풀을 이 모양으로 감싼다. 독립 단계에서는 테스트의 가짜 풀이 그 자리에 선다.
namespace JBro::Network
{
    class IReplicatedPoolVisitor
    {
    public:
        virtual ~IReplicatedPoolVisitor() = default;
        // 살아 있는 원소 하나. `bytes` 는 `ElementBytes()` 만큼이고 이 호출 동안만 유효하다.
        virtual void Element(InstanceId object, const std::uint8_t* bytes) = 0;
    };

    // 복제 대상으로 등록한 컴포넌트 타입 하나의 풀.
    class IReplicatedPool
    {
    public:
        virtual ~IReplicatedPool() = default;

        // 원소 하나가 와이어에 실리는 바이트 수. 고정이다.
        virtual std::uint32_t ElementBytes() const = 0;
        // 서버. 살아 있는 원소를 전부 방문한다. 순서는 상관없다.
        virtual void Visit(IReplicatedPoolVisitor& visitor) = 0;
        // 클라이언트. `object` 의 값을 `to` 로 둔다. `from` 이 있으면 그 사이를 `alpha` 로 보간할 수 있다(타입을 아는 어댑터만).
        // 오브젝트에 이 컴포넌트가 없으면 붙일지 말지는 어댑터가 정한다. 거짓이면 적용하지 못했다.
        virtual bool Apply(InstanceId object, const std::uint8_t* from, const std::uint8_t* to, float alpha) = 0;
        // 클라이언트. 서버에서 이 컴포넌트가 떨어졌다.
        virtual void Detach(InstanceId object) = 0;
    };

    // 오브젝트의 생성과 소멸을 실제로 하는 쪽. 서버에서는 "이 오브젝트를 어떻게 만들면 되는가" 를, 클라이언트에서는 만들고 없애는 일을.
    class IReplicationHost
    {
    public:
        virtual ~IReplicationHost() = default;

        // 서버. 복제 대상이 아니면 거짓이다 - 그 오브젝트는 와이어에 오르지 않는다.
        virtual bool DescribeObject(InstanceId object, SpawnDesc& outDesc) = 0;
        // 클라이언트. 만든 오브젝트의 지역 식별자. 못 만들면 `InvalidInstanceId`.
        virtual InstanceId SpawnObject(NetworkObjectId id, const SpawnDesc& desc) = 0;
        virtual void DespawnObject(InstanceId object) = 0;
    };
}

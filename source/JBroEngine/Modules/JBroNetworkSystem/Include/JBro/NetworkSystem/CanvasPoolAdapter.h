#pragma once

#include <JBro/Canvas/Canvas.h>
#include <JBro/Network/Replication/IReplicatedPool.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/Ref.h>

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace JBro
{
    // 캔버스의 컴포넌트 풀 하나를 복제 풀로 감싼다(network-plan §2.6 (A)). 핵심은 타입을 모르므로, 타입을 아는 것은
    // `Traits` 하나뿐이다:
    //   static void Pack(const Component&, Wire&);
    //   static void Unpack(const Wire* from, const Wire& to, float alpha, Component&);   // from 이 있으면 보간할 수 있다
    // `Wire` 는 POD 이고 와이어에 그대로 실린다. 컴포넌트의 월드 캐시·vtable·SafePtr 는 실리지 않는다 - 그래서 어댑터가 필요하다.
    // 차원 모듈이 자기 타입의 Traits 를 두고 이 템플릿을 별칭으로 쓴다.
    template <typename Component, typename Wire, typename Traits>
    class CanvasPoolAdapter final : public Network::IReplicatedPool
    {
        static_assert(std::is_trivially_copyable_v<Wire>, "the wire form must be POD");

    public:
        explicit CanvasPoolAdapter(Canvas& canvas)
            : m_canvas(canvas)
        {
        }

        std::uint32_t ElementBytes() const override
        {
            return static_cast<std::uint32_t>(sizeof(Wire));
        }

        void Visit(Network::IReplicatedPoolVisitor& visitor) override
        {
            m_canvas.ForEach<Component>([&visitor](Component& component)
            {
                Wire wire;
                Traits::Pack(component, wire);
                visitor.Element(component.GetOwner().GetInstanceId(), reinterpret_cast<const std::uint8_t*>(&wire));
            });
        }

        bool Apply(InstanceId object, const std::uint8_t* from, const std::uint8_t* to, float alpha) override
        {
            GameObject* owner = Resolve(object);
            if (nullptr == owner)
            {
                return false;
            }
            Component* component = m_canvas.FindComponentRaw<Component>(owner);
            if (nullptr == component)
            {
                component = m_canvas.AttachComponent<Component>(owner);
                if (nullptr == component)
                {
                    return false;
                }
            }
            Wire toWire;
            std::memcpy(&toWire, to, sizeof(Wire));
            if (nullptr == from)
            {
                Traits::Unpack(nullptr, toWire, alpha, *component);
                return true;
            }
            Wire fromWire;
            std::memcpy(&fromWire, from, sizeof(Wire));
            Traits::Unpack(&fromWire, toWire, alpha, *component);
            return true;
        }

        void Detach(InstanceId object) override
        {
            GameObject* owner = Resolve(object);
            if (nullptr == owner)
            {
                return;
            }
            Component* component = m_canvas.FindComponentRaw<Component>(owner);
            if (nullptr != component)
            {
                m_canvas.DetachComponent<Component>(owner, component);
            }
        }

    private:
        static GameObject* Resolve(InstanceId object)
        {
            if (InvalidInstanceId == object)
            {
                return nullptr;
            }
            const Internal::ResolvedInstance resolved =
                Internal::ResolveInstanceById(object, InvalidInstanceId, RefCategory::Object);
            return static_cast<GameObject*>(resolved.Pointer);
        }

        Canvas& m_canvas;
    };
}

// 단계 3에서 바꾼 코어 모델을 적대적으로 찌른다.
//
// 여기 있는 것은 "잘 도는지" 보는 스모크가 아니라, 각 변경이 틀렸을 때 정확히 무엇이 깨지는지를
// 노리는 경우들이다. 풀의 블록 재활용·주소 탐색, 순회 중 파괴, 상속 활성 전파, 레이어 재색인,
// 타입 캐시가 대상이다.

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Core/ObjectPool.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Internal/InstanceRegistry.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/NameTable.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    class Probe final : public JBro::EnableSafeFromThis<Probe>
    {
    public:
        explicit Probe(int value) : Value(value) {}
        int Value = 0;
    };

    // 청크가 여러 개가 되도록 키운 뒤, 슬롯을 뒤섞어 파괴·재생성한다.
    // 주소 이분 탐색이 틀리면 Destroy 가 false 를 내거나 엉뚱한 슬롯을 지운다.
    void TestPoolAddressLookupAcrossManyChunks()
    {
        JBro::TObjectPool<Probe, 8> pool(JBro::CreateDefaultAllocator());

        JBro::Array<Probe*> live;
        for (int value = 0; value < 500; ++value)
        {
            Probe* created = pool.Create(value);
            Check(created != nullptr, "pool must create across many chunks");
            live.Add(created);
        }
        Check(pool.GetLiveCount() == 500, "every created object must be live");

        // 값과 주소가 짝지어 살아 있는지 먼저 확인한다.
        for (std::size_t index = 0; index < live.Size(); ++index)
        {
            Check(live[index]->Value == static_cast<int>(index),
                "pooled objects must keep their own storage");
        }

        // 홀수만 지운다. free-list 가 섞이고 주소 순서와 슬롯 순서가 어긋난다.
        for (std::size_t index = 1; index < live.Size(); index += 2)
        {
            Check(pool.Destroy(live[index]),
                "destroying a scattered subset must locate every slot");
        }
        Check(pool.GetLiveCount() == 250, "half of the objects must remain");

        // 남은 것이 훼손되지 않았는지.
        for (std::size_t index = 0; index < live.Size(); index += 2)
        {
            Check(live[index]->Value == static_cast<int>(index),
                "surviving objects must be untouched by neighbouring destroys");
        }

        // 같은 포인터를 다시 지우면 실패해야 한다.
        Check(false == pool.Destroy(live[1]),
            "destroying an already freed object must fail, not corrupt the pool");

        // 풀 밖의 주소는 절대 슬롯으로 오인되면 안 된다.
        Probe outside(-1);
        Check(false == pool.Destroy(&outside),
            "an address outside the pool must never resolve to a slot");
        Check(false == pool.Destroy(nullptr), "a null pointer must be rejected");

        // 빈 자리를 다시 채운다.
        for (int value = 1000; value < 1250; ++value)
        {
            Check(pool.Create(value) != nullptr, "freed slots must be reusable");
        }
        Check(pool.GetLiveCount() == 500, "refilling must restore the live count");
    }

    // 참조가 남은 채로 파괴된 블록은 재활용 대기열에 들어가면 안 된다.
    // 들어가면 살아 있는 SafePtr 가 다음 객체를 가리키게 된다 — 조용한 오염이다.
    void TestControlBlockWithLiveReferenceIsNotRecycled()
    {
        JBro::TObjectPool<Probe, 4> pool(JBro::CreateDefaultAllocator());
        Probe* first = pool.Create(1);
        Check(first != nullptr, "fixture object must be created");

        JBro::SafePtr<Probe> outstanding = first->SafeFromThis();
        Check(outstanding.IsValid(), "a live reference must observe its object");

        Check(pool.Destroy(first), "destroying must succeed while a reference is held");
        Check(false == outstanding.IsValid(),
            "a reference to a destroyed object must expire");

        // 참조를 든 채로 새 객체를 만든다. 블록이 잘못 재활용됐다면 만료됐던 참조가
        // 새 객체를 가리키며 되살아난다.
        Probe* second = pool.Create(2);
        Check(second != nullptr, "the pool must keep serving after the destroy");
        Check(false == outstanding.IsValid(),
            "an expired reference must never revive onto a later object");
        Check(outstanding.TryGet() == nullptr,
            "an expired reference must not hand out the recycled slot");

        outstanding.Reset();
        Check(pool.Destroy(second), "the later object must still be destroyable");
    }

    // 부모와 자식이 함께 큐에 들어갔을 때, 둘 중 어느 쪽이 먼저 flush 되든 안전해야 한다.
    //
    // 큐는 LIFO 다. 순회 순서는 Transform 풀의 슬롯 순서, 즉 **컴포넌트 부착 순서**이고
    // 오브젝트 생성 순서가 아니다. 그래서 부착 순서를 뒤집어야 flush 순서가 뒤집힌다.
    // 부모를 나중에 부착하면 부모가 나중에 큐에 들어가고 먼저 flush 되어,
    // 큐에 남은 자식 항목이 이미 만료된 경로를 밟는다. 한쪽만 돌리면 그 경로는 실행되지 않는다.
    void RunDeferredDestroyWithQueuedPair(bool attachParentLast, const char* label)
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* parent = canvas.CreateObject("parent");
        JBro::GameObject* child = canvas.CreateObject("child");
        JBro::GameObject* bystander = canvas.CreateObject("bystander");
        child->SetParent(parent);

        JBro::GameObject* const attachOrder[3] = {
            attachParentLast ? child : parent,
            attachParentLast ? parent : child,
            bystander};
        for (JBro::GameObject* object : attachOrder)
        {
            Check(canvas.AttachComponent<JBro::Component::Transform2D>(object) != nullptr,
                "stress fixture must attach a transform to each object");
        }
        Check(canvas.GetObjectCount() == 3, label);

        std::size_t visited = 0;
        canvas.ForEach<JBro::Component::Transform2D>(
            [&](JBro::Component::Transform2D& transform)
        {
            ++visited;
            JBro::GameObject* owner = JBro::Internal::CanvasAccess::GetOwner(transform);
            if (owner == bystander)
            {
                return;
            }
            Check(canvas.DestroyObject(owner), "queuing a destroy must be accepted");
        });

        Check(visited == 3, "every transform must be visited despite the destroy requests");
        Check(canvas.GetObjectCount() == 3, "nothing may die before the safe point");
        Check(canvas.GetPendingDestroyCount() == 2, "both requests must be queued");

        canvas.FlushPendingDestroy();
        Check(canvas.GetObjectCount() == 1, "the parent and its child must both be gone");
        Check(canvas.GetPendingDestroyCount() == 0, "the queue must drain completely");

        Check(bystander->IsActiveInHierarchy(), "the untouched object must survive intact");
        Check(canvas.CreateObject("after flush") != nullptr,
            "the canvas must keep working after a flush");
    }

    void TestDeferredDestroyWithParentAndChildQueued()
    {
        // 자식이 먼저 flush 되는 경우.
        RunDeferredDestroyWithQueuedPair(false, "child-first fixture must hold three objects");
        // 부모가 먼저 flush 되어, 큐에 남은 자식 항목이 이미 만료된 경우.
        RunDeferredDestroyWithQueuedPair(true, "parent-first fixture must hold three objects");
    }

    // 순회 중 컴포넌트 파괴도 같은 계약을 따라야 한다.
    void TestDeferredComponentDestroyDuringIteration()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* object = canvas.CreateObject("multi");
        auto* first = canvas.AttachComponent<JBro::Component::Collider2D>(object);
        auto* second = canvas.AttachComponent<JBro::Component::Collider2D>(object);
        Check(first != nullptr && second != nullptr && first != second,
            "an object must hold two colliders of the same type");

        std::size_t visited = 0;
        canvas.ForEach<JBro::Component::Collider2D>([&](JBro::Component::Collider2D& collider)
        {
            ++visited;
            Check(canvas.DetachComponent(object, &collider),
                "detaching during iteration must be accepted");
        });
        Check(visited == 2, "both colliders must be visited");
        Check(object->GetComponents().Size() == 2,
            "components may not leave their owner before the safe point");

        canvas.FlushPendingDestroy();
        Check(object->GetComponents().Size() == 0,
            "flushing must detach every queued component");
    }

    // 타입 캐시가 비어 있거나 잘못 채워지면 조회가 조용히 실패한다.
    void TestNameTableHoldsTheTextThatTagsDropped()
    {
        JBro::NameTable& names = JBro::NameTable::Get();
        const std::size_t before = names.GetCount();

        const JBro::NameId first = names.Intern("player");
        const JBro::NameId again = names.Intern("player");
        Check(first == again, "the same text must always intern to the same id");
        Check(first == JBro::MakeNameId("player"),
            "an id must be derivable from the text without touching the table");
        Check(first != JBro::InvalidNameId, "a real name must not intern to the invalid id");
        Check(names.Intern(nullptr) == JBro::InvalidNameId, "a null name must intern to the invalid id");
        Check(names.Intern("") == JBro::InvalidNameId, "an empty name must intern to the invalid id");

        const char* resolved = names.Resolve(first);
        Check(resolved != nullptr && std::strcmp(resolved, "player") == 0,
            "the table must give the text back");
        const char* unknown = names.Resolve(JBro::MakeNameId("never interned at all"));
        Check(unknown != nullptr && unknown[0] == '\0', "an unknown id must resolve to an empty string");
        Check(names.GetCount() == before + 1, "interning the same text twice must store it once");
        Check(names.GetCollisionCount() == 0, "no two test names may fold onto one id");

        // 태그는 이제 정수다. 문자열 API 는 표를 거쳐 그대로 동작해야 한다.
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* object = canvas.CreateObject("enemy spawner");
        Check(object != nullptr, "the tagged object must be created");
        Check(std::strcmp(object->GetTag(), "enemy spawner") == 0,
            "an object must give back the name it was created with");
        Check(object->GetTagId() == JBro::MakeNameId("enemy spawner"),
            "a tag must compare as an integer without visiting the table");

        object->SetTag("player");
        Check(object->GetTagId() == first, "setting a known tag must reuse its id");
        object->SetTagId(JBro::InvalidNameId);
        Check(object->GetTag()[0] == '\0', "clearing a tag must resolve to an empty string");
    }

    void TestCachedTypeIdMatchesTheVirtualAnswer()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* object = canvas.CreateObject("typed");
        auto* transform = canvas.AttachComponent<JBro::Component::Transform2D>(object);
        auto* sprite = canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(object);
        auto* body = canvas.AttachComponent<JBro::Component::Rigidbody2D>(object);
        Check(transform && sprite && body, "typed fixture must attach three kinds");

        for (const JBro::ComponentSlot& slot : object->GetComponents())
        {
            JBro::ComponentBase* component = slot.reference.TryGet();
            Check(component != nullptr, "every attached component must be reachable");
            Check(component->GetCachedTypeId() == component->GetTypeId(),
                "the cached type must agree with the virtual answer");
            Check(component->GetCachedTypeId() != JBro::InvalidComponentTypeId,
                "the cache must be filled at attach time");
            // 슬롯의 사본이 컴포넌트 자신의 답과 어긋나면 타입 조회가 조용히 빗나간다.
            Check(slot.typeId == component->GetCachedTypeId(),
                "the slot must carry the same type id the component reports");
        }

        // 같은 타입이 여럿일 때 조회가 첫 번째를 준다(D-31).
        auto* secondSprite = canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(object);
        Check(secondSprite != nullptr, "a second sprite of the same type must attach");
        Check(canvas.FindComponentRaw<JBro::Component::SpriteRenderer2D>(object) == sprite,
            "the raw lookup must return the first match, by cached type");
    }

    // 레이어를 지우면 그 레이어의 오브젝트가 대체 레이어로 옮겨 가고, 순서가 다시 촘촘해져야 한다.
    void TestLayerDestroyReassignsObjectsAndReindexes()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::Layer& first = *canvas.GetLayerAt(0);
        JBro::Layer& second = canvas.CreateLayer("second");
        JBro::Layer& third = canvas.CreateLayer("third");

        JBro::GameObject* onSecond = canvas.CreateObject("on second");
        Check(canvas.SetObjectLayer(onSecond, second.GetId()), "object must take a layer");
        Check(onSecond->GetLayer() == &second, "object must point at its layer");

        const JBro::LayerId secondId = second.GetId();
        Check(canvas.DestroyLayer(secondId), "destroying a populated layer must succeed");
        Check(onSecond->GetLayer() != nullptr && onSecond->GetLayer() != &second,
            "an orphaned object must be moved to a surviving layer");
        Check(first.GetOrder() == 0 && third.GetOrder() == 1,
            "the order must close up after a destroy");
        Check(canvas.FindLayer(secondId) == nullptr,
            "a destroyed layer must not be findable by its identifier");

        // 마지막 레이어는 지울 수 없다. 오브젝트가 갈 곳이 없어진다.
        Check(canvas.DestroyLayer(third.GetId()), "a non-final layer must be destroyable");
        Check(false == canvas.DestroyLayer(first.GetId()),
            "the final layer must refuse to be destroyed");
    }

    // 파괴된 오브젝트의 슬롯이 재사용되어도, 예전 핸들이 새 오브젝트로 부활하면 안 된다.
    void TestHandleDoesNotReviveOnARecycledSlot()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* first = canvas.CreateObject("first");
        const JBro::GameObjectHandle stale = first->GetScriptHandle();
        Check(stale.IsValid(), "a fresh handle must resolve");

        Check(canvas.DestroyObject(first), "the object must be destroyable");
        Check(false == stale.IsValid(), "a handle to a destroyed object must not resolve");

        // 같은 슬롯이 거의 확실히 재사용된다.
        JBro::GameObject* second = canvas.CreateObject("second");
        Check(second != nullptr, "the canvas must keep creating");
        Check(false == stale.IsValid(),
            "a stale handle must not resolve onto whatever took the slot");
        Check(stale.GetInstanceId() != second->GetInstanceId(),
            "identifiers must never be handed out twice");
    }
}

int RunCoreModelStressTests()
{
    TestPoolAddressLookupAcrossManyChunks();
    TestControlBlockWithLiveReferenceIsNotRecycled();
    TestDeferredDestroyWithParentAndChildQueued();
    TestDeferredComponentDestroyDuringIteration();
    TestNameTableHoldsTheTextThatTagsDropped();
    TestCachedTypeIdMatchesTheVirtualAnswer();
    TestLayerDestroyReassignsObjectsAndReindexes();
    TestHandleDoesNotReviveOnARecycledSlot();
    std::cout << "Core model stress tests passed.\n";
    return 0;
}

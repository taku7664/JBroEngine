// SafePtr / OwnerPtr / TObjectPool 수명 회귀 테스트.
//
// Docs/Internal 의 SafePtr 검수 보고 §7 "필요한 회귀 테스트" 목록을 코드로 옮긴 것이다.
// 특히 다음 두 가지를 지킨다.
//   · 다중상속 변환에서 보정 포인터(m_ptr)가 보존되는가 — 변환 생성/변환 대입 양쪽.
//   · 풀이 ControlBlock 을 재활용해도 만료 SafePtr 가 새 객체로 되살아나지 않는가.
//     (참조가 남은 블록을 캐시에 담으면 정확히 이 사고가 난다.)

#include "GameFramework/Object/ObjectPool.h"
#include "Utillity/Pointer/SafePtr.h"

#include <iostream>

namespace
{
	// 다중상속 — BaseB 서브오브젝트는 오프셋이 0 이 아니라 보정 여부를 눈으로 확인할 수 있다.
	struct BaseA
	{
		virtual ~BaseA() = default;
		int AValue = 11;
	};

	struct BaseB
	{
		virtual ~BaseB() = default;
		int BValue = 22;
	};

	struct Derived : BaseA, BaseB
	{
		int DValue = 33;
	};

	// 풀에 들어가는 타입은 EnableSafeFromThis 를 상속해야 한다(풀의 static_assert 계약).
	struct PooledProbe : EnableSafeFromThis<PooledProbe>
	{
		explicit PooledProbe(int value)
			: Value(value)
		{
			++AliveCount;
		}

		~PooledProbe()
		{
			--AliveCount;
		}

		int Value = 0;
		inline static int AliveCount = 0;
	};

	// ── 1. OwnerPtr 파괴 후 모든 SafePtr 가 만료 판정 ────────────────────────────
	bool TestOwnerDestroyExpiresSafePtr()
	{
		SafePtr<BaseA> weak;
		{
			OwnerPtr<BaseA> owner = MakeOwnerPtr<BaseA>();
			weak = owner.GetSafePtr();
			if (false == weak.IsValid() || 11 != weak->AValue)
			{
				return false;
			}
		}

		// 소유자가 사라졌다 — 만료로 보여야 하고 nullptr 과 같아야 한다.
		if (weak.IsValid() || nullptr != weak.TryGet() || false == (weak == nullptr))
		{
			return false;
		}
		return true;
	}

	// ── 2. 복사 / 이동 / 자기 대입에서 참조 수가 유지되는가 ──────────────────────
	bool TestCopyMoveSelfAssign()
	{
		OwnerPtr<BaseA> owner = MakeOwnerPtr<BaseA>();
		SafePtr<BaseA> first = owner.GetSafePtr();

		{
			SafePtr<BaseA> copy = first;
			SafePtr<BaseA> moved = std::move(copy);
			if (copy.IsValid() || false == moved.IsValid())
			{
				return false;   // 이동 후 원본은 비고, 대상은 살아야 한다.
			}
		}

		// 자기 대입이 참조를 떨어뜨려 조기 해제하면 안 된다.
		SafePtr<BaseA>& alias = first;
		first = alias;
		if (false == first.IsValid() || 11 != first->AValue)
		{
			return false;
		}

		// 자기 이동 대입도 마찬가지(this == &rhs 가드).
		first = std::move(alias);
		if (false == first.IsValid() || 11 != first->AValue)
		{
			return false;
		}
		return true;
	}

	// ── 3. 다중상속 변환에서 보정 포인터가 보존되는가 ────────────────────────────
	bool TestMultipleInheritanceOffset()
	{
		OwnerPtr<Derived> owner = MakeOwnerPtr<Derived>();
		SafePtr<Derived> derived = owner.GetSafePtr();

		// (a) 변환 **생성** — BaseB 는 오프셋이 붙어야 한다.
		SafePtr<BaseB> viaConstruct = derived;
		if (22 != viaConstruct->BValue || 33 != derived->DValue)
		{
			return false;
		}
		if (static_cast<const void*>(viaConstruct.TryGet()) == static_cast<const void*>(derived.TryGet()))
		{
			return false;   // 오프셋이 안 붙었다 = 보정 실패.
		}

		// (b) 변환 **대입** — 같은 ControlBlock 이라 참조 증감은 생략되지만,
		//     접근 포인터는 새 정적 타입에 맞게 갱신되어야 한다.
		SafePtr<BaseB> viaAssign;
		viaAssign = derived;                 // 빈 상태 → 다른 블록 → 전체 경로
		SafePtr<Derived> sameObject = owner.GetSafePtr();
		viaAssign = sameObject;              // 같은 블록 → 조기 반환 경로
		if (22 != viaAssign->BValue)
		{
			return false;
		}
		if (viaAssign.TryGet() != viaConstruct.TryGet())
		{
			return false;   // 두 경로가 같은 주소를 내야 한다.
		}

		// (c) 두 베이스는 서로 다른 주소지만 소유권 identity 는 같다(== 는 ControlBlock 비교).
		SafePtr<BaseA> asA = derived;
		if (static_cast<const void*>(asA.TryGet()) == static_cast<const void*>(viaConstruct.TryGet()))
		{
			return false;
		}
		if (false == (asA == viaConstruct))
		{
			return false;
		}
		return true;
	}

	// ── 4. SafePtr ↔ OwnerPtr 직접 비교(임시 없는 identity 비교) ─────────────────
	bool TestOwnerComparison()
	{
		OwnerPtr<BaseA> owner = MakeOwnerPtr<BaseA>();
		OwnerPtr<BaseA> other = MakeOwnerPtr<BaseA>();
		SafePtr<BaseA> safe = owner.GetSafePtr();

		if (false == (safe == owner) || (safe != owner))
		{
			return false;
		}
		if ((safe == other) || false == (safe != other))
		{
			return false;
		}
		// GetSafePtr() 경유 비교와 결과가 같아야 한다(의미 동일, 임시만 없앤 것).
		if ((safe == owner) != (safe == owner.GetSafePtr()))
		{
			return false;
		}
		return true;
	}

	// ── 5. 풀 슬롯 재사용 후 옛 SafePtr 가 새 객체를 가리키지 않는가 ─────────────
	//     ControlBlock 재활용을 잘못 구현하면(참조 남은 블록을 캐시에 담으면) 여기서 깨진다.
	bool TestPoolSlotReuseKeepsStaleSafePtrExpired()
	{
		TObjectPool<PooledProbe> pool;

		PooledProbe* first = pool.Allocate(100);
		if (nullptr == first)
		{
			return false;
		}
		SafePtr<PooledProbe> stale = first->SafeFromThis();
		if (false == stale.IsValid() || 100 != stale->Value)
		{
			return false;
		}

		// 참조가 살아 있는 채로 파괴 — 이 블록은 풀을 떠나야 한다(재활용 금지).
		pool.Free(first);
		if (stale.IsValid())
		{
			return false;
		}

		// 같은 슬롯이 재사용되더라도 옛 SafePtr 는 만료 상태를 유지해야 한다.
		PooledProbe* second = pool.Allocate(200);
		if (nullptr == second || 200 != second->Value)
		{
			return false;
		}
		if (stale.IsValid() || nullptr != stale.TryGet())
		{
			return false;   // 캐시에 잘못 담긴 블록이 되살아난 경우 여기서 잡힌다.
		}
		// 새 객체의 SafePtr 는 옛 것과 소유권 identity 가 달라야 한다.
		SafePtr<PooledProbe> fresh = second->SafeFromThis();
		if (false == fresh.IsValid() || fresh == stale)
		{
			return false;
		}

		pool.Free(second);
		return true;
	}

	// ── 6. 참조 없이 파괴된 블록은 재활용되는가(할당 절약이 실제로 도는지) ───────
	bool TestControlBlockRecycled()
	{
		TObjectPool<PooledProbe> pool;

		PooledProbe* object = pool.Allocate(1);
		if (nullptr == object || 0 != pool.GetCachedControlBlockCount())
		{
			return false;
		}

		// SafePtr 를 남기지 않고 파괴 → 블록이 캐시로 들어가야 한다.
		pool.Free(object);
		if (1 != pool.GetCachedControlBlockCount())
		{
			return false;
		}

		// 다음 Allocate 는 캐시를 소비해야 한다(= 새 힙 할당 없음).
		PooledProbe* reused = pool.Allocate(2);
		if (nullptr == reused || 0 != pool.GetCachedControlBlockCount())
		{
			return false;
		}
		// 되살린 블록으로도 SafePtr 가 정상 동작해야 한다.
		SafePtr<PooledProbe> safe = reused->SafeFromThis();
		if (false == safe.IsValid() || 2 != safe->Value)
		{
			return false;
		}

		// 참조가 남은 채 파괴하면 캐시에 담기지 않아야 한다(5번의 반대 방향 확인).
		pool.Free(reused);
		if (0 != pool.GetCachedControlBlockCount() || safe.IsValid())
		{
			return false;
		}
		return true;
	}

	// ── 7. 풀 Clear() 후 남은 SafePtr 가 안전하게 만료되는가 + 객체 누수 없는가 ──
	bool TestPoolClearExpiresSafePtr()
	{
		PooledProbe::AliveCount = 0;

		SafePtr<PooledProbe> survivor;
		{
			TObjectPool<PooledProbe> pool;
			for (int i = 0; i < 40; ++i)   // 청크(32) 를 넘겨 다중 청크 경로도 태운다.
			{
				PooledProbe* object = pool.Allocate(i);
				if (nullptr == object)
				{
					return false;
				}
				if (7 == i)
				{
					survivor = object->SafeFromThis();
				}
			}
			if (40 != PooledProbe::AliveCount || false == survivor.IsValid())
			{
				return false;
			}

			pool.Clear();
			if (0 != PooledProbe::AliveCount || survivor.IsValid())
			{
				return false;   // Clear 는 전부 파괴하고 남은 SafePtr 를 만료시켜야 한다.
			}

			// Clear 후에도 풀은 재사용 가능해야 한다.
			if (nullptr == pool.Allocate(999) || 1 != PooledProbe::AliveCount)
			{
				return false;
			}
		}

		// 풀 파괴 후 — 남은 객체도 정리되고 만료 SafePtr 는 여전히 안전하다.
		if (0 != PooledProbe::AliveCount || survivor.IsValid())
		{
			return false;
		}
		return true;
	}
}

int main()
{
	if (false == TestOwnerDestroyExpiresSafePtr())
	{
		std::cerr << "OwnerPtr destroy / SafePtr expiry test failed.\n";
		return 1;
	}

	if (false == TestCopyMoveSelfAssign())
	{
		std::cerr << "SafePtr copy/move/self-assign test failed.\n";
		return 2;
	}

	if (false == TestMultipleInheritanceOffset())
	{
		std::cerr << "Multiple-inheritance pointer adjustment test failed.\n";
		return 3;
	}

	if (false == TestOwnerComparison())
	{
		std::cerr << "SafePtr/OwnerPtr identity comparison test failed.\n";
		return 4;
	}

	if (false == TestPoolSlotReuseKeepsStaleSafePtrExpired())
	{
		std::cerr << "Object pool slot reuse / stale SafePtr test failed.\n";
		return 5;
	}

	if (false == TestControlBlockRecycled())
	{
		std::cerr << "Object pool control-block recycling test failed.\n";
		return 6;
	}

	if (false == TestPoolClearExpiresSafePtr())
	{
		std::cerr << "Object pool Clear / SafePtr expiry test failed.\n";
		return 7;
	}

	std::cout << "SafePtr lifetime smoke test passed.\n";
	return 0;
}

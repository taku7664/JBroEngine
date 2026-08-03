#pragma once

#include <type_traits>

// ─────────────────────────────────────────────────────────────────────────────
//  SafePtr / OwnerPtr — 소유(OwnerPtr) 와 비소유 안전참조(SafePtr) 한 쌍.
//
//  ■ 스레드 계약: **메인 스레드 전용이다.**
//    ControlBlock 의 `SafeCount`/`Alive` 는 의도적으로 비원자(non-atomic)다. SafePtr 는
//    렌더 수집·오브젝트 순회 같은 프레임 핫패스에서 초당 수만 번 복사되므로, 원자
//    read-modify-write 비용과 cache-line contention 을 지불할 자리가 아니다.
//
//    따라서 다음을 금지한다.
//      · 워커 스레드에서 SafePtr/OwnerPtr 을 **생성·복사·파괴**하는 것
//        (SafeFromThis(), GetSafePtr(), 값 전달, 람다 캡처 복사·소멸 전부 포함)
//      · SafePtr 을 캡처한 람다를 워커 스레드에서 실행/파괴하는 것
//        (std::function 이 소멸하며 캡처된 SafePtr 의 ReleaseRef 가 워커에서 돈다)
//
//    워커 스레드로 넘길 데이터는 **값·raw 포인터·std::shared_ptr** 중 하나를 쓰고,
//    대상 객체의 수명은 태스크를 스폰한 쪽이 "태스크가 끝나기 전엔 파괴하지 않는다" 로
//    보장한다(TryGet() 은 검사 직후 파괴되는 TOCTOU 를 막지 못하므로 애초에 보호가 아니다).
//    참고 선례: CInputSystem 의 진동 타이머 태스크는 shared_ptr + POD 만 캡처한다.
//
//  ■ 비교 의미: operator== 는 **소유권 identity(ControlBlock)** 비교다. 주소 비교가 아니다.
//      · 같은 객체의 서로 다른 베이스 서브오브젝트 SafePtr 는 서로 같다(== true).
//      · 만료된 서로 다른 SafePtr 는 각각 nullptr 과 같지만, 서로는 같지 않을 수 있다.
//    주소 동일성이 필요하면 TryGet() 결과를 직접 비교한다.
// ─────────────────────────────────────────────────────────────────────────────

template<typename T>
class SafePtr;

template<typename T>
class OwnerPtr;

template<typename T>
class EnableSafeFromThis;

namespace SafePtrDetail
{
	// 제어 블록은 삭제 전용 포인터(void*)와 삭제자만 들고 있다. 접근용 포인터는 각
	// SafePtr/OwnerPtr 이 자기 정적 타입에 맞게 보정한 값을 따로 보관한다(shared_ptr 의
	// aliasing 모델). 이렇게 해야 다중상속에서 2차 베이스로 캐스트해도 올바른 주소가 나온다
	// (void* → static_cast 만 쓰면 오프셋 보정이 사라져 오주소가 된다).
	struct ControlBlock
	{
		void* Ptr;
		bool Alive;
		std::size_t SafeCount;
		void(*Deleter)(void*);

		ControlBlock(void* ptr, void(*deleter)(void*))
			: Ptr(ptr), Alive(ptr != nullptr), SafeCount(0), Deleter(deleter)
		{
		}
	};

	template<typename T>
	inline void DeletePtr(void* ptr)
	{
		delete static_cast<T*>(ptr);
	}

	template<typename TObject>
	inline void BindSafeFromThisControlBlock(TObject* ptr, ControlBlock* block)
	{
		if constexpr (requires(TObject* obj, ControlBlock* cb) { obj->__BindSafeControlBlock(cb); })
		{
			ptr->__BindSafeControlBlock(block);
		}
	}
}

template<typename T>
class EnableSafeFromThis
{
	template<typename>
	friend class OwnerPtr;
	template<typename TObject>
	friend void SafePtrDetail::BindSafeFromThisControlBlock(TObject* ptr, SafePtrDetail::ControlBlock* block);

public:
	SafePtr<T> SafeFromThis()
	{
		if (nullptr == m_controlBlock)
		{
			return nullptr;
		}

		// static_cast<T*>(this) — T 는 EnableSafeFromThis<T> 를 상속(CRTP)하므로 정식 다운캐스트.
		return SafePtr<T>(m_controlBlock, static_cast<T*>(this));
	}

	SafePtr<const T> SafeFromThis() const
	{
		if (nullptr == m_controlBlock)
		{
			return nullptr;
		}

		return SafePtr<const T>(m_controlBlock, static_cast<const T*>(this));
	}

private:
	void __BindSafeControlBlock(SafePtrDetail::ControlBlock* block)
	{
		m_controlBlock = block;
	}

private:
	SafePtrDetail::ControlBlock* m_controlBlock = nullptr;
};

template<typename T>
class OwnerPtr final
{
	template<typename>
	friend class OwnerPtr;
	template<typename>
	friend class SafePtr;

public:
	OwnerPtr() : m_controlBlock(nullptr), m_ptr(nullptr) {}
	OwnerPtr(std::nullptr_t) : m_controlBlock(nullptr), m_ptr(nullptr) {}
	explicit OwnerPtr(T* ptr)
		: m_controlBlock(ptr ? new SafePtrDetail::ControlBlock(ptr, &SafePtrDetail::DeletePtr<T>) : nullptr)
		, m_ptr(ptr)
	{
		if (ptr && m_controlBlock)
		{
			SafePtrDetail::BindSafeFromThisControlBlock(ptr, m_controlBlock);
		}
	}

	OwnerPtr(const OwnerPtr&) = delete;
	OwnerPtr& operator=(const OwnerPtr&) = delete;

	OwnerPtr(OwnerPtr&& rhs) noexcept
		: m_controlBlock(rhs.m_controlBlock)
		, m_ptr(rhs.m_ptr)
	{
		rhs.m_controlBlock = nullptr;
		rhs.m_ptr = nullptr;
	}
	OwnerPtr& operator=(OwnerPtr&& rhs) noexcept
	{
		if (this == &rhs)
			return *this;

		DestroyOwned();
		m_controlBlock = rhs.m_controlBlock;
		m_ptr = rhs.m_ptr;
		rhs.m_controlBlock = nullptr;
		rhs.m_ptr = nullptr;
		return *this;
	}

	template<typename U>
	requires std::is_convertible_v<U*, T*>
	OwnerPtr(OwnerPtr<U>&& rhs) noexcept
		: m_controlBlock(rhs.m_controlBlock)
		, m_ptr(rhs.m_ptr)   // U* → T* 정식 변환(오프셋 보정 포함)
	{
		rhs.m_controlBlock = nullptr;
		rhs.m_ptr = nullptr;
	}

	template<typename U>
	requires std::is_convertible_v<U*, T*>
	OwnerPtr& operator=(OwnerPtr<U>&& rhs) noexcept
	{
		DestroyOwned();
		m_controlBlock = rhs.m_controlBlock;
		m_ptr = rhs.m_ptr;
		rhs.m_controlBlock = nullptr;
		rhs.m_ptr = nullptr;
		return *this;
	}

	~OwnerPtr()
	{
		DestroyOwned();
	}

	void Reset()
	{
		DestroyOwned();
	}
	void Reset(T* ptr)
	{
		DestroyOwned();
		if (ptr)
		{
			m_controlBlock = new SafePtrDetail::ControlBlock(ptr, &SafePtrDetail::DeletePtr<T>);
			m_ptr = ptr;
			SafePtrDetail::BindSafeFromThisControlBlock(ptr, m_controlBlock);
		}
	}

	T* Get() const
	{
		return (m_controlBlock && m_controlBlock->Alive) ? m_ptr : nullptr;
	}

	T& operator*() const { return *Get(); }
	T* operator->() const { return Get(); }
	explicit operator bool() const { return Get() != nullptr; }

	SafePtr<T> GetSafePtr() const
	{
		return SafePtr<T>(m_controlBlock, m_ptr);
	}
	operator SafePtr<T>() const { return GetSafePtr(); }

private:
	void DestroyOwned()
	{
		if (!m_controlBlock)
		{
			m_ptr = nullptr;
			return;
		}

		if (m_controlBlock->Alive && m_controlBlock->Ptr)
		{
			m_controlBlock->Deleter(m_controlBlock->Ptr);
			m_controlBlock->Ptr = nullptr;
			m_controlBlock->Alive = false;
		}

		if (m_controlBlock->SafeCount == 0)
		{
			delete m_controlBlock;
		}

		m_controlBlock = nullptr;
		m_ptr = nullptr;
	}

private:
	SafePtrDetail::ControlBlock* m_controlBlock;
	T*                          m_ptr;   // 접근용 보정 포인터(삭제는 ControlBlock::Ptr 담당).
};

template<typename T>
class SafePtr final
{
	template<typename>
	friend class SafePtr;
	template<typename>
	friend class OwnerPtr;
	template<typename>
	friend class EnableSafeFromThis;

	template<typename To, typename From>
	friend SafePtr<To> StaticSafePtrCast(const SafePtr<From>& ptr);

	template<typename To, typename From>
	friend SafePtr<To> DynamicSafePtrCast(const SafePtr<From>& ptr);

public:
	SafePtr() : m_controlBlock(nullptr), m_ptr(nullptr) {}
	SafePtr(std::nullptr_t) : m_controlBlock(nullptr), m_ptr(nullptr) {}

	SafePtr(const SafePtr& rhs)
		: m_controlBlock(rhs.m_controlBlock)
		, m_ptr(rhs.m_ptr)
	{
		AddRef();
	}
	SafePtr& operator=(const SafePtr& rhs)
	{
		if (this == &rhs)
			return *this;

		ReleaseRef();
		m_controlBlock = rhs.m_controlBlock;
		m_ptr = rhs.m_ptr;
		AddRef();
		return *this;
	}

	template<typename U>
	requires std::is_convertible_v<U*, T*>
	SafePtr(const SafePtr<U>& rhs)
		: m_controlBlock(rhs.m_controlBlock)
		, m_ptr(rhs.m_ptr)   // U* → T* 정식 변환(오프셋 보정 포함)
	{
		AddRef();
	}
	template<typename U>
	requires std::is_convertible_v<U*, T*>
	SafePtr& operator=(const SafePtr<U>& rhs)
	{
		if (m_controlBlock == rhs.m_controlBlock)
		{
			// 참조 수는 그대로 두되(같은 블록이라 증감이 상쇄된다) **접근 포인터는 갱신한다**.
			// ControlBlock 이 같아도 정적 타입이 다르면 보정 오프셋이 달라질 수 있다
			// (다중상속의 2차 베이스). 옛 코드는 여기서 즉시 return 해 m_ptr 을 옛 값으로
			// 남겼다 — 현재 생성 경로에서는 두 값이 항상 일치해 재현되지 않지만,
			// "같은 블록 = 같은 주소" 라는 암묵 가정에 기대는 자리라 명시적으로 대입한다.
			m_ptr = rhs.m_ptr;
			return *this;
		}

		ReleaseRef();
		m_controlBlock = rhs.m_controlBlock;
		m_ptr = rhs.m_ptr;
		AddRef();
		return *this;
	}

	SafePtr(SafePtr&& rhs) noexcept
		: m_controlBlock(rhs.m_controlBlock)
		, m_ptr(rhs.m_ptr)
	{
		rhs.m_controlBlock = nullptr;
		rhs.m_ptr = nullptr;
	}
	SafePtr& operator=(SafePtr&& rhs) noexcept
	{
		if (this == &rhs)
			return *this;

		ReleaseRef();
		m_controlBlock = rhs.m_controlBlock;
		m_ptr = rhs.m_ptr;
		rhs.m_controlBlock = nullptr;
		rhs.m_ptr = nullptr;
		return *this;
	}

	~SafePtr()
	{
		ReleaseRef();
	}

	void Reset()
	{
		ReleaseRef();
		m_controlBlock = nullptr;
		m_ptr = nullptr;
	}

	bool IsValid() const
	{
		return m_controlBlock != nullptr && m_controlBlock->Alive;
	}

	T* TryGet() const
	{
		return IsValid() ? m_ptr : nullptr;
	}

	T& operator*() const { return *TryGet(); }
	T* operator->() const { return TryGet(); }
	explicit operator bool() const { return IsValid(); }

	template<typename U>
	bool operator==(const SafePtr<U>& rhs) const
	{
		return m_controlBlock == rhs.m_controlBlock;
	}

	template<typename U>
	bool operator!=(const SafePtr<U>& rhs) const
	{
		return false == (*this == rhs);
	}

	// OwnerPtr 와의 소유권 identity 비교 — `safe == owner.GetSafePtr()` 는 임시 SafePtr 를
	// 만들어 AddRef/ReleaseRef 를 왕복한다. 렌더 핫패스의 "이게 그 기본 메시/파이프라인인가"
	// 판정은 드로우마다 돌기 때문에, ControlBlock 만 비교하는 경로를 따로 둔다.
	// (C++20 은 역방향 후보를 자동 생성하므로 `owner == safe` 도 이 연산자로 접힌다.)
	template<typename U>
	bool operator==(const OwnerPtr<U>& rhs) const
	{
		return m_controlBlock == rhs.m_controlBlock;
	}

	template<typename U>
	bool operator!=(const OwnerPtr<U>& rhs) const
	{
		return false == (*this == rhs);
	}

	bool operator==(std::nullptr_t) const
	{
		return false == IsValid();
	}

	bool operator!=(std::nullptr_t) const
	{
		return IsValid();
	}

private:
	SafePtr(SafePtrDetail::ControlBlock* block, T* ptr)
		: m_controlBlock(block)
		, m_ptr(ptr)
	{
		AddRef();
	}

	void AddRef()
	{
		if (m_controlBlock)
			++m_controlBlock->SafeCount;
	}

	void ReleaseRef()
	{
		if (!m_controlBlock)
			return;

		--m_controlBlock->SafeCount;

		if (m_controlBlock->SafeCount == 0 && !m_controlBlock->Alive)
		{
			delete m_controlBlock;
		}

		m_controlBlock = nullptr;
		m_ptr = nullptr;
	}

private:
	SafePtrDetail::ControlBlock* m_controlBlock;
	T*                          m_ptr;   // 접근용 보정 포인터(수명은 ControlBlock 이 관리).
};

template<typename T>
bool operator==(std::nullptr_t, const SafePtr<T>& ptr)
{
	return ptr == nullptr;
}

template<typename T>
bool operator!=(std::nullptr_t, const SafePtr<T>& ptr)
{
	return ptr != nullptr;
}

template<typename T, typename... Args>
OwnerPtr<T> MakeOwnerPtr(Args&&... args)
{
	return OwnerPtr<T>(new T(std::forward<Args>(args)...));
}

template<typename To, typename From>
SafePtr<To> StaticSafePtrCast(const SafePtr<From>& ptr)
{
	if (!ptr.IsValid())
	{
		return nullptr;
	}

	// 보정된 접근 포인터로 정식 static_cast(void* 경유가 아니므로 오프셋 정확).
	return SafePtr<To>(ptr.m_controlBlock, static_cast<To*>(ptr.m_ptr));
}

template<typename To, typename From>
SafePtr<To> DynamicSafePtrCast(const SafePtr<From>& ptr)
{
	if (!ptr.IsValid())
	{
		return nullptr;
	}

	To* casted = dynamic_cast<To*>(ptr.m_ptr);
	if (nullptr == casted)
	{
		return nullptr;
	}

	return SafePtr<To>(ptr.m_controlBlock, casted);
}

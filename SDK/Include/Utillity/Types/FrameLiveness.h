#pragma once

#include <cstdint>

// 프레임 단위 캐시의 생존 표시 / 청소.
//
// "이번 프레임에 봤다"를 별도 집합(seen)에 모으면 프레임마다 힙 할당이 생긴다. 원소 하나당
// 노드 하나이고, 프레임 끝에 전부 해제된다. 캐시가 이미 그 키를 들고 있는데도 그렇다.
//
// 대신 캐시 항목이 스탬프를 하나 들고, 볼 때마다 현재 스탬프로 덮어쓴다. 프레임 끝에
// 스탬프가 뒤처진 항목만 지우면 결과가 같고 할당이 0 이다.
//
// 스탬프는 시스템마다 자기 것을 쓴다(OnUpdate 진입에서 증가). 시스템끼리 스탬프를 비교하지
// 않으므로 전역 프레임 번호에 의존할 필요가 없다.
//
// 사용 예)
//   const std::uint64_t stamp = ++m_frameStamp;
//   ... 살아 있는 항목마다 entry.LastSeenFrame = stamp;
//   RemoveStaleEntries(m_cache, stamp, [](const Cached& v) { return v.LastSeenFrame; });

template<typename Map, typename StampOf>
void RemoveStaleEntries(Map& map, std::uint64_t liveStamp, StampOf&& stampOf)
{
	for (auto it = map.begin(); it != map.end(); )
	{
		if (liveStamp != stampOf(it->second))
		{
			it = map.erase(it);
		}
		else
		{
			++it;
		}
	}
}

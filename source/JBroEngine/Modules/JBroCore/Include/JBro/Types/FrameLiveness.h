#pragma once

#include <cstdint>

// 프레임 단위 캐시의 생존 표시와 청소다.
//
// **왜 스탬프인가**: "이번 프레임에 봤다" 를 따로 모으면 원소마다 자리가 하나씩 더 생기고
// 프레임 끝에 전부 버려진다. 캐시가 이미 그 열쇠를 들고 있는데도 그렇다. 대신 항목이 스탬프를
// 하나 들고 볼 때마다 이번 프레임의 값으로 덮으면, 프레임 끝에 스탬프가 뒤처진 것만 지워도
// 결과가 같고 **프레임마다 새로 잡는 자리가 없다**(§7, 매 프레임 경로의 힙 할당 금지).
//
// **스탬프는 시스템마다 자기 것을 쓴다.** 시스템끼리 스탬프를 견주지 않으므로 전역 프레임
// 번호가 필요 없고, `OnUpdate` 들머리에서 자기 것을 하나 올리면 된다.
//
// **도는 중에 지우지 않는다.** `Table` 은 원소를 지우면 자리를 다시 놓으므로, 도는 중에
// 지우면 아직 보지 않은 항목을 건너뛸 수 있다. 그래서 지울 열쇠를 먼저 모으고 그 뒤에 지운다.
// 모으는 그릇은 부르는 쪽이 들고 있다가 다시 쓴다(`Clear` 는 용량을 남긴다).
//
// 쓰는 모양)
//     ++m_frame;
//     ... 살아 있는 항목마다 entry.lastSeenFrame = m_frame;
//     RemoveStaleEntries(m_entries, m_frame, m_scratchUnseen,
//         [](const Entry& entry) { return entry.lastSeenFrame; });
namespace JBro
{
    template<typename Table, typename KeyScratch, typename StampOf>
    void RemoveStaleEntries(Table& table, std::uint64_t liveStamp, KeyScratch& scratchKeys, StampOf&& stampOf)
    {
        scratchKeys.Clear();
        for (auto it = table.begin(); it != table.end(); ++it)
        {
            if (stampOf(it->MappedValue) != liveStamp)
            {
                scratchKeys.Add(it->KeyValue);
            }
        }
        for (const auto& key : scratchKeys)
        {
            table.Remove(key);
        }
    }
}

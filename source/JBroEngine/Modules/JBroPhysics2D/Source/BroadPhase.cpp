#include <JBro/Physics2D/BroadPhase.h>

#include <algorithm>
#include <cmath>

namespace JBro::Physics2D
{
    void SweepAndPrune::FindPairs(ArrayView<const Rect> boxes, Array<ProxyPair>& pairs)
    {
        pairs.Clear();
        m_sorted.Clear();
        m_active.Clear();

        for (std::uint32_t i = 0; i < boxes.Size(); ++i)
        {
            const Rect& box = boxes[i];
            // NaN 이 섞이면 정렬 비교가 엄격한 약순서를 잃어 std::sort 가 범위를 벗어날 수 있다.
            if (false == (std::isfinite(box.min.x) && std::isfinite(box.max.x)
                && std::isfinite(box.min.y) && std::isfinite(box.max.y)))
            {
                continue;
            }
            m_sorted.Add({ box, i });
        }

        std::sort(m_sorted.begin(), m_sorted.end(), [](const Proxy& left, const Proxy& right)
        {
            if (left.box.min.x != right.box.min.x)
            {
                return left.box.min.x < right.box.min.x;
            }
            return left.index < right.index;
        });

        for (const Proxy& current : m_sorted)
        {
            // x 로 이미 지나간 것을 걷어 낸다. 뒤에 올 것은 모두 min.x 가 current 이상이다.
            std::size_t kept = 0;
            for (std::size_t read = 0; read < m_active.Size(); ++read)
            {
                if (m_active[read].box.max.x >= current.box.min.x)
                {
                    m_active[kept] = m_active[read];
                    ++kept;
                }
            }
            m_active.Resize(kept);

            for (const Proxy& other : m_active)
            {
                // y 도 담기 전에 본다. x 만 보면 같은 열에 선 것들의 조합이 모두 쌓인다(기존 엔진 실측).
                if (current.box.min.y > other.box.max.y || current.box.max.y < other.box.min.y)
                {
                    continue;
                }
                const std::uint32_t low = std::min(current.index, other.index);
                const std::uint32_t high = std::max(current.index, other.index);
                pairs.Add({ low, high });
            }
            m_active.Add(current);
        }

        std::sort(pairs.begin(), pairs.end(), [](const ProxyPair& left, const ProxyPair& right)
        {
            if (left.first != right.first)
            {
                return left.first < right.first;
            }
            return left.second < right.second;
        });
    }
}

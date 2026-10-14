#pragma once

#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/String.h>
#include <JBro/Types/UInt.h>

#include <string_view>

// 노드 그래프의 도메인이다(D-323, node-graph-plan §2.1). 셰이더 그래프·수식 샘플처럼 쓰임마다 하나이고,
// 어떤 핀 타입이 있고 무엇이 무엇으로 넓혀지며 어떤 노드 종류가 있는지를 든다.
//
// 그래프 모델과 화면은 도메인을 이 표로만 안다 - 도메인을 더할 때 모델·위젯·패널을 고치지 않는다.
// 노드 종류 이름과 핀 이름은 번역하지 않는다(컴포넌트 이름·필드 이름과 같은 규칙, ProjectRule §11.2).
namespace JBro::NodeGraph
{
    // 핀 값이다. 핀 타입이 앞에서부터 몇 칸을 쓰는지 안다(`PinTypeInfo::components`).
    struct PinValue
    {
        Float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};

        Bool operator==(const PinValue& other) const noexcept
        {
            for (std::size_t index = 0; index < 4; ++index)
            {
                if (values[index].Get() != other.values[index].Get())
                {
                    return false;
                }
            }
            return true;
        }
    };

    // 도메인 안의 핀 타입 번호다. 등록한 차례다.
    using PinType = UInt32;

    struct PinTypeInfo
    {
        String name;
        Color color;
        // 값의 앞 몇 칸을 쓰나(1~4).
        UInt32 components = 1;
    };

    struct PinInfo
    {
        String name;
        PinType type = 0;
        PinValue defaultValue;
    };

    struct NodeTypeInfo
    {
        String name;
        // 노드 더하기 메뉴의 묶음이다. 비면 맨 위에 둔다.
        String category;
        Array<PinInfo> inputs;
        Array<PinInfo> outputs;
        // 그래프에 하나만 둘 수 있다(결과 노드).
        Bool unique = false;
    };

    inline constexpr std::size_t InvalidPinIndex = static_cast<std::size_t>(-1);

    class Domain final
    {
    public:
        explicit Domain(String name);

        const String& GetName() const noexcept { return m_name; }

        PinType AddPinType(PinTypeInfo info);
        // `from` 출력을 `to` 입력에 이을 수 있게 한다. 같은 타입끼리는 늘 된다.
        void AddConversion(PinType from, PinType to);
        // 같은 이름이 이미 있으면 더하지 않고 거짓이다.
        Bool AddNodeType(NodeTypeInfo info);

        ArrayView<const PinTypeInfo> GetPinTypes() const noexcept { return m_pinTypes.View(); }
        ArrayView<const NodeTypeInfo> GetNodeTypes() const noexcept { return m_nodeTypes.View(); }

        const PinTypeInfo* FindPinType(PinType type) const noexcept;
        const NodeTypeInfo* FindNodeType(std::string_view name) const noexcept;
        Bool CanConvert(PinType from, PinType to) const noexcept;

        static std::size_t FindInput(const NodeTypeInfo& type, std::string_view pin) noexcept;
        static std::size_t FindOutput(const NodeTypeInfo& type, std::string_view pin) noexcept;

    private:
        struct Conversion
        {
            PinType from = 0;
            PinType to = 0;
        };

        String m_name;
        Array<PinTypeInfo> m_pinTypes;
        Array<Conversion> m_conversions;
        Array<NodeTypeInfo> m_nodeTypes;
    };

    // 이름으로 도메인을 찾는 표다. 에디터가 시작할 때 채우고, 그 뒤로 찾은 주소는 표가 사는 동안 그대로다.
    class DomainRegistry final
    {
    public:
        // 같은 이름이 이미 있으면 더하지 않고 거짓이다.
        Bool Add(OwnerPtr<Domain> domain);
        const Domain* Find(std::string_view name) const noexcept;
        std::size_t GetCount() const noexcept { return m_domains.Size(); }
        const Domain& GetAt(std::size_t index) const noexcept { return *m_domains[index]; }

    private:
        Array<OwnerPtr<Domain>> m_domains;
    };
}

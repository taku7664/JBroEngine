#pragma once

#include <JBro/NodeGraph/Domain.h>
#include <JBro/NodeGraph/Graph.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/SafePtr.h>

// 수식 샘플 도메인 `Math` 다(D-308, node-graph-plan §2.4). 에디터에서 노드 그래프를 열어 다듬기 위한 것이고,
// "그래프를 결과물로 바꾸는 컴파일러" 자리를 `EvaluateMath` 가 시험한다.
//
// 핀 타입은 `Float`·`Vector2`·`Vector3`·`Vector4` 이고 좁은 것은 넓은 것으로 넓혀진다(`Float` 는 모든 칸에 같은 값,
// 벡터는 남는 칸이 0). 넓은 것을 좁은 것에 잇는 것은 거절한다. 사칙 연산·`Sin`·`Lerp` 는 `Vector4` 로 칸마다 셈한다.
namespace JBro::NodeGraph
{
    inline constexpr const char* MathDomainName = "Math";

    OwnerPtr<Domain> MakeMathDomain();

    // `Output` 노드의 `Color` 입력 값을 낸다. 결과 노드가 없으면 거짓이다.
    Bool EvaluateMath(const Graph& graph, const Domain& domain, PinValue& result);
    // 그 노드의 그 출력 값을 낸다. 노드나 출력이 없으면 거짓이다.
    Bool EvaluateMathOutput(const Graph& graph, const Domain& domain, NodeId node, std::size_t output, PinValue& result);
}

#pragma once

#include <JBro/NodeGraph/Domain.h>
#include <JBro/NodeGraph/Graph.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/String.h>

#include <cstddef>
#include <string_view>

// 그래프를 YAML 로 쓰고 읽는다(D-308, node-graph-plan §2.2). `.jgraph` 본문이고, 되돌리기 커맨드가 떠 두는 글자다.
//
//   Domain: Math
//   NextId: 4
//   Nodes:
//     - Id: 1
//       Type: Float
//       X: 0
//       Y: 0
//       Inputs:
//         - Pin: Value
//           Value: [ 2 ]        (핀 타입이 쓰는 칸만큼)
//   Links:
//     - Id: 3
//       FromNode: 1
//       FromPin: Result
//       ToNode: 2
//       ToPin: A
//
// 핀은 이름으로 적는다 - 도메인에서 핀 차례가 바뀌어도 잇기가 남는다.
namespace JBro::NodeGraph
{
    String WriteGraph(const Graph& graph, const Domain* domain);

    // 첫 키 `Domain` 만 읽는다. 어느 도메인으로 읽을지 고르는 데 쓴다. 없으면 거짓이다.
    Bool ReadGraphDomain(std::string_view text, String& domain);

    // YAML 이 깨졌으면 거짓이고 `graph` 는 비어 있다. 그 밖의 문제(모르는 노드 종류·모르는 핀·거절된 링크)는 `problems` 에
    // 한 줄씩 적고 읽기를 이어 간다 - 모르는 종류의 노드는 그대로 남고, 닿을 수 없는 링크만 빠진다.
    Bool ReadGraph(std::string_view text, const Domain* domain, Graph& graph, Array<String>& problems);
}

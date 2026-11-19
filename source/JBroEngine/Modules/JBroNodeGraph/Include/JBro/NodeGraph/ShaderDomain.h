#pragma once

#include <JBro/NodeGraph/Domain.h>
#include <JBro/NodeGraph/Graph.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/String.h>

// **셰이더 그래프 도메인**(D-369). 그래프는 머티리얼 셰이더(`.hlsl`)를 만드는 원본이다 - 저장하면 에디터가 그래프 옆에 `.hlsl` 을 쓰고, 머티리얼·패키지·렌더러는 그
// 파일만 본다. 만든 글자는 세 자리(`PostProcess`·`Sprite`·`Mesh`)에서 모두 구워지고, 자리에 없는 입력(후처리의 법선 따위)은 정해진 값이다.
//
// 핀 타입은 `Float`·`Vector2`·`Vector3`·`Vector4` 이고 서로 이을 수 있다: 넓힐 때 `Float` 는 모든 칸에 같은 값, 벡터의 남는 칸은 0 이고, 좁힐 때는 앞 칸만 쓴다.
// 사칙 연산·`Lerp`·`Sin` 따위는 `Vector4` 로 칸마다 셈한다. `FloatParameter`·`ColorParameter`·`TextureParameter` 는 머티리얼 인스펙터에 나오는 파라미터이고
// 이름은 노드의 `label` 이다.
namespace JBro::NodeGraph
{
    inline constexpr const char* ShaderDomainName = "Shader";

    OwnerPtr<Domain> MakeShaderDomain();

    // 그래프를 머티리얼 셰이더의 HLSL 글자로 바꾼다. `Output` 노드가 없거나, 파라미터 이름이 겹치거나, 텍스처가 상한(6)을 넘으면 거짓이고 까닭은 `error` 다.
    Bool GenerateShaderHlsl(const Graph& graph, const Domain& domain, String& hlsl, String& error);
}

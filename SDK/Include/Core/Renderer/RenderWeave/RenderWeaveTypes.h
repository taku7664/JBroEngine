#pragma once

#include "Core/RHI/RHIGraphicsTypes.h"   // ERHITextureFormat
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class IRHICommandContext;
class IRHITexture;
class RWGraph;

// ─────────────────────────────────────────────────────────────────────────────
//  RenderWeave — 이 엔진 고유의 2D 경량 렌더그래프.
//
//  패스를 데이터로 선언하면(reads/write + 실행 람다) 그래프가 배선·RT 대여·컬링을
//  담당한다. 라이팅/그림자/포스트가 모두 "패스 등록"이라는 단일 메커니즘으로 확장된다.
//
//  ⚠ 전부 호스트 전용이다. 실행 람다(std::function)는 DLL 경계를 넘지 않는다
//     — 렌더러(호스트)만 그래프를 구성/실행한다.
// ─────────────────────────────────────────────────────────────────────────────

// 그래프 내 텍스처를 가리키는 불투명 핸들(등록 순서 인덱스). 음수 = 무효.
using RWTextureHandle = int;
inline constexpr RWTextureHandle RW_INVALID_TEXTURE = -1;

// transient RT 요청 사양. RT 풀이 같은 desc 를 재사용한다.
struct RWTextureDesc
{
	std::uint32_t     Width  = 0;
	std::uint32_t     Height = 0;
	ERHITextureFormat Format = ERHITextureFormat::RGBA8;

	bool operator==(const RWTextureDesc& rhs) const
	{
		return Width == rhs.Width && Height == rhs.Height && Format == rhs.Format;
	}
};

// 한 렌더 패스. reads = 입력 텍스처들, Write = 출력 텍스처(하나). Execute 가 실제
// 커맨드를 낸다 — 람다 안에서 graph.Resolve(handle) 로 실제 RT 를 얻어 렌더패스를 구성한다.
struct RWPassDesc
{
	std::string                                          Name;
	std::vector<RWTextureHandle>                         Reads;
	RWTextureHandle                                      Write = RW_INVALID_TEXTURE;
	std::function<void(IRHICommandContext&, RWGraph&)>   Execute;
};

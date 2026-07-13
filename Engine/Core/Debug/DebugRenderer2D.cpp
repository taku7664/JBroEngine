#include "pch.h"
#include "DebugRenderer2D.h"

#include "Core/Debug/DebugDraw2D.h"
#include "Core/RHI/IRHIBuffer.h"
#include "Core/RHI/IRHICommandContext.h"
#include "Core/RHI/IRHIDevice.h"
#include "Core/RHI/IRHIGraphicsPipeline.h"
#include "Core/RHI/IRHIProgram.h"
#include "Core/RHI/RHIGraphicsTypes.h"

#include <cmath>
#include <vector>

namespace
{
	// ── Per-vertex layout ──────────────────────────────────────────────────────────
	struct DebugVertex2D_
	{
		float Position[2]; // world-space X, Y
		float Color[4];    // R, G, B, A in [0, 1]
	};

	// ── Per-draw constant buffer ───────────────────────────────────────────────────
	struct DebugViewConstants2D
	{
		float ViewScale[4];
	};

	// ── Colour helpers ─────────────────────────────────────────────────────────────
	inline void UnpackColor(DebugColor c, float out[4])
	{
		out[0] = static_cast<float>( c        & 0xFF) / 255.0f; // R
		out[1] = static_cast<float>((c >>  8) & 0xFF) / 255.0f; // G
		out[2] = static_cast<float>((c >> 16) & 0xFF) / 255.0f; // B
		out[3] = static_cast<float>((c >> 24) & 0xFF) / 255.0f; // A
	}

	// ── 두꺼운 선분 → 2삼각형 쿼드 (TriangleList) ────────────────────────────
	//
	// D3D11 LineList 는 픽셀 두께를 지원하지 않는다(항상 1px 고정).
	// 선분을 수직 방향으로 halfThicknessWorld 만큼 확장한 사각형(2 삼각형)으로
	// 삼각화하면 임의 두께 렌더가 가능하다.
	//
	// halfThicknessWorld = (pixelThickness / 2) * (camSize * 2 / viewHeight)
	//                    = pixelThickness * camSize / viewHeight
	//
	// X·Y 방향의 pixelsPerWorldUnit 이 동일하므로(직교 카메라의 스케일 동등성)
	// 월드 공간 수직 벡터가 화면에서도 수직이 보장된다.

	void AppendLine(std::vector<DebugVertex2D_>& out, const DebugLine2D& line, float halfThicknessWorld)
	{
		DebugVertex2D_ v;
		UnpackColor(line.Color, v.Color);

		const float dx  = line.B.x - line.A.x;
		const float dy  = line.B.y - line.A.y;
		const float len = std::sqrtf(dx * dx + dy * dy);
		if (len < 1e-7f) return; // 영길이 선분 무시

		// 수직 단위벡터 × 반두께
		const float px = (-dy / len) * halfThicknessWorld;
		const float py = ( dx / len) * halfThicknessWorld;

		// 쿼드 4 꼭짓점
		const float ax0 = line.A.x + px,  ay0 = line.A.y + py;
		const float ax1 = line.A.x - px,  ay1 = line.A.y - py;
		const float bx0 = line.B.x + px,  by0 = line.B.y + py;
		const float bx1 = line.B.x - px,  by1 = line.B.y - py;

		// 삼각형 1: A0 → B0 → A1  (CW in NDC = 앞면)
		v.Position[0] = ax0; v.Position[1] = ay0; out.push_back(v);
		v.Position[0] = bx0; v.Position[1] = by0; out.push_back(v);
		v.Position[0] = ax1; v.Position[1] = ay1; out.push_back(v);
		// 삼각형 2: A1 → B0 → B1  (CW in NDC = 앞면)
		v.Position[0] = ax1; v.Position[1] = ay1; out.push_back(v);
		v.Position[0] = bx0; v.Position[1] = by0; out.push_back(v);
		v.Position[0] = bx1; v.Position[1] = by1; out.push_back(v);
	}

	void AppendCircle(std::vector<DebugVertex2D_>& out, const DebugCircle2D& circle, float halfThicknessWorld)
	{
		static constexpr float TWO_PI = 6.28318530718f;
		const int segs = (circle.Segments > 2) ? circle.Segments : 3;

		DebugLine2D seg;
		seg.Color     = circle.Color;
		seg.Thickness = circle.Thickness;

		for (int k = 0; k < segs; ++k)
		{
			const float a0 = static_cast<float>(k    ) / static_cast<float>(segs) * TWO_PI;
			const float a1 = static_cast<float>(k + 1) / static_cast<float>(segs) * TWO_PI;
			seg.A = { circle.Center.x + std::cosf(a0) * circle.Radius,
			          circle.Center.y + std::sinf(a0) * circle.Radius };
			seg.B = { circle.Center.x + std::cosf(a1) * circle.Radius,
			          circle.Center.y + std::sinf(a1) * circle.Radius };
			AppendLine(out, seg, halfThicknessWorld);
		}
	}

	// ── 공통 GPU 디스패치 (자유 함수, DebugVertex2D_ 가시성 때문에 네임스페이스 안) ──
	void DispatchDebugVertices(
		SafePtr<IRHICommandContext> commandContext,
		SafePtr<IRHIDevice>            rhiDevice,
		SafePtr<IRHIGraphicsPipeline>  pipeline,
		const std::vector<DebugVertex2D_>& vertices,
		float camX, float camY, float camSize,
		int viewWidth, int viewHeight)
	{
		if (vertices.empty()) return;
		if (!rhiDevice.IsValid() || !pipeline || !commandContext.IsValid()) return;

		RHIBufferDesc vbDesc;
		vbDesc.SizeInBytes = vertices.size() * sizeof(DebugVertex2D_);
		vbDesc.Usage       = ERHIBufferUsage::Default;
		vbDesc.BindFlags   = static_cast<RHIBindFlags>(ERHIBindFlag::VertexBuffer);
		OwnerPtr<IRHIBuffer> vertexBuffer = rhiDevice->CreateBuffer(vbDesc, vertices.data());
		if (!vertexBuffer) return;

		const float width  = static_cast<float>(viewWidth);
		const float height = static_cast<float>(viewHeight);
		const float aspect = (width > 0.0f && height > 0.0f) ? width / height : 1.0f;
		const float size   = (camSize > 0.0f) ? camSize : 1.0f;

		DebugViewConstants2D constants;
		constants.ViewScale[0] =  1.0f / (size * aspect);
		constants.ViewScale[1] =  1.0f / size;
		constants.ViewScale[2] = -camX * constants.ViewScale[0];
		constants.ViewScale[3] = -camY * constants.ViewScale[1];

		RHIBufferDesc cbDesc;
		cbDesc.SizeInBytes = sizeof(DebugViewConstants2D);
		cbDesc.Usage       = ERHIBufferUsage::Default;
		cbDesc.BindFlags   = static_cast<RHIBindFlags>(ERHIBindFlag::ConstantBuffer);
		OwnerPtr<IRHIBuffer> constantBuffer = rhiDevice->CreateBuffer(cbDesc, &constants);
		if (!constantBuffer) return;

		commandContext->SetGraphicsPipeline(pipeline);
		commandContext->SetVertexBuffer(0, vertexBuffer.GetSafePtr(), sizeof(DebugVertex2D_), 0);
		commandContext->SetConstantBuffer(ERHIProgramStage::Vertex, 0, constantBuffer.GetSafePtr());
		commandContext->Draw(static_cast<std::uint32_t>(vertices.size()), 0);
	}

	// ── Shaders ───────────────────────────────────────────────────────────────────

	const char* DEBUG_SHADER_HLSL = R"(
cbuffer DebugViewConstants : register(b0)
{
	float4 gViewScale;
};

struct VSIn
{
	float2 Position : POSITION;
	float4 Color    : COLOR0;
};

struct VSOut
{
	float4 Position : SV_POSITION;
	float4 Color    : COLOR0;
};

VSOut VSMain(VSIn input)
{
	VSOut output;
	output.Position = float4(
		input.Position.x * gViewScale.x + gViewScale.z,
		input.Position.y * gViewScale.y + gViewScale.w,
		0.0f, 1.0f);
	output.Color = input.Color;
	return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
	return input.Color;
}
)";

	const char* DEBUG_SHADER_WGSL = R"(
struct DebugViewConstants
{
	ViewScale : vec4<f32>,
};

@group(0) @binding(0)
var<uniform> gConstants : DebugViewConstants;

struct VSIn
{
	@location(0) Position : vec2<f32>,
	@location(1) Color    : vec4<f32>,
};

struct VSOut
{
	@builtin(position) Position : vec4<f32>,
	@location(0) Color          : vec4<f32>,
};

@vertex
fn VSMain(input : VSIn) -> VSOut
{
	var output : VSOut;
	output.Position = vec4<f32>(
		input.Position.x * gConstants.ViewScale.x + gConstants.ViewScale.z,
		input.Position.y * gConstants.ViewScale.y + gConstants.ViewScale.w,
		0.0, 1.0);
	output.Color = input.Color;
	return output;
}

@fragment
fn PSMain(input : VSOut) -> @location(0) vec4<f32>
{
	return input.Color;
}
)";
} // namespace

bool CDebugRenderer2D::Initialize(SafePtr<IRHIDevice> rhiDevice)
{
	if (m_isInitialized) return true;

	m_rhiDevice = rhiDevice;
	if (false == m_rhiDevice.IsValid()) return false;

	if (false == CreatePipeline())
	{
		if (ERHIApi::D3D11 == m_rhiDevice->GetApi())
		{
			Finalize();
			return false;
		}
		m_isInitialized = true;
		return true;
	}

	m_isInitialized = true;
	return true;
}

void CDebugRenderer2D::Finalize()
{
	m_pipeline.Reset();
	m_pixelProgram.Reset();
	m_vertexProgram.Reset();
	m_rhiDevice.Reset();
	m_isInitialized = false;
}

// ── 전체 렌더 (기존 Render 호환) ──────────────────────────────────────────────

void CDebugRenderer2D::Render(
	SafePtr<IRHICommandContext> commandContext,
	const IDebugDraw2D& debugDraw,
	float camX, float camY, float camSize,
	int viewWidth, int viewHeight)
{
	const float baseHalf = (viewHeight > 0 && camSize > 0.0f)
		? camSize / static_cast<float>(viewHeight) : 0.5f;

	std::vector<DebugVertex2D_> vertices;
	const auto& lines   = debugDraw.GetLines();
	const auto& circles = debugDraw.GetCircles();
	vertices.reserve(lines.size() * 6 + circles.size() * 48 * 6);
	for (const auto& line   : lines)   AppendLine(vertices, line, line.Thickness * baseHalf);
	for (const auto& circle : circles) AppendCircle(vertices, circle, circle.Thickness * baseHalf);
	DispatchDebugVertices(commandContext, m_rhiDevice, m_pipeline.GetSafePtr(), vertices, camX, camY, camSize, viewWidth, viewHeight);
}

// ── 전역(그리드) 전용 렌더 ────────────────────────────────────────────────────

void CDebugRenderer2D::RenderGlobal(
	SafePtr<IRHICommandContext> commandContext,
	const IDebugDraw2D& debugDraw,
	float camX, float camY, float camSize,
	int viewWidth, int viewHeight)
{
	const float baseHalf = (viewHeight > 0 && camSize > 0.0f)
		? camSize / static_cast<float>(viewHeight) : 0.5f;

	std::vector<DebugVertex2D_> vertices;
	for (const auto& line   : debugDraw.GetLines())
		if (line.Entity == INVALID_DEBUG_OBJECT_ID)   AppendLine(vertices, line, line.Thickness * baseHalf);
	for (const auto& circle : debugDraw.GetCircles())
		if (circle.Entity == INVALID_DEBUG_OBJECT_ID) AppendCircle(vertices, circle, circle.Thickness * baseHalf);
	DispatchDebugVertices(commandContext, m_rhiDevice, m_pipeline.GetSafePtr(), vertices, camX, camY, camSize, viewWidth, viewHeight);
}

// ── 엔티티 태깅 도형 렌더 (콜라이더) ─────────────────────────────────────────

void CDebugRenderer2D::RenderEntities(
	SafePtr<IRHICommandContext> commandContext,
	const IDebugDraw2D& debugDraw,
	float camX, float camY, float camSize,
	int viewWidth, int viewHeight,
	const std::unordered_set<DebugObjectId>* filter)
{
	const float baseHalf = (viewHeight > 0 && camSize > 0.0f)
		? camSize / static_cast<float>(viewHeight) : 0.5f;

	std::vector<DebugVertex2D_> vertices;
	for (const auto& line : debugDraw.GetLines())
	{
		if (line.Entity == INVALID_DEBUG_OBJECT_ID) continue; // 그리드 등 전역 → 제외
		if (filter && !filter->empty() && filter->find(line.Entity) == filter->end()) continue;
		AppendLine(vertices, line, line.Thickness * baseHalf);
	}
	for (const auto& circle : debugDraw.GetCircles())
	{
		if (circle.Entity == INVALID_DEBUG_OBJECT_ID) continue;
		if (filter && !filter->empty() && filter->find(circle.Entity) == filter->end()) continue;
		AppendCircle(vertices, circle, circle.Thickness * baseHalf);
	}
	DispatchDebugVertices(commandContext, m_rhiDevice, m_pipeline.GetSafePtr(), vertices, camX, camY, camSize, viewWidth, viewHeight);
}

bool CDebugRenderer2D::CreatePipeline()
{
	const ERHIApi api = m_rhiDevice->GetApi();
	const ERHIProgramLanguage lang =
		(ERHIApi::WebGPU == api) ? ERHIProgramLanguage::WGSL : ERHIProgramLanguage::HLSL;
	const char* shaderSrc =
		(ERHIApi::WebGPU == api) ? DEBUG_SHADER_WGSL : DEBUG_SHADER_HLSL;

	RHIProgramDesc vsProg;
	vsProg.Stage      = ERHIProgramStage::Vertex;
	vsProg.Language   = lang;
	vsProg.EntryPoint = "VSMain";
	vsProg.Source     = shaderSrc;
	m_vertexProgram   = m_rhiDevice->CreateProgram(vsProg);

	RHIProgramDesc psProg;
	psProg.Stage      = ERHIProgramStage::Pixel;
	psProg.Language   = lang;
	psProg.EntryPoint = "PSMain";
	psProg.Source     = shaderSrc;
	m_pixelProgram    = m_rhiDevice->CreateProgram(psProg);

	if (!m_vertexProgram || !m_pixelProgram) return false;

	RHIVertexElementDesc elements[2];
	elements[0].SemanticName  = "POSITION";
	elements[0].SemanticIndex = 0;
	elements[0].Format        = ERHIVertexFormat::Float2;
	elements[0].Offset        = offsetof(DebugVertex2D_, Position);

	elements[1].SemanticName  = "COLOR";
	elements[1].SemanticIndex = 0;
	elements[1].Format        = ERHIVertexFormat::Float4;
	elements[1].Offset        = offsetof(DebugVertex2D_, Color);

	RHIGraphicsPipelineDesc pipelineDesc;
	pipelineDesc.VertexProgram      = m_vertexProgram.GetSafePtr();
	pipelineDesc.PixelProgram       = m_pixelProgram.GetSafePtr();
	pipelineDesc.VertexElements     = elements;
	pipelineDesc.VertexElementCount = 2;
	pipelineDesc.PrimitiveTopology  = ERHIPrimitiveTopology::TriangleList;
	m_pipeline = m_rhiDevice->CreateGraphicsPipeline(pipelineDesc);
	return static_cast<bool>(m_pipeline);
}

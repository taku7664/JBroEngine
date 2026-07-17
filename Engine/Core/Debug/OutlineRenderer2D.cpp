#include "pch.h"
#include "OutlineRenderer2D.h"

#include "Core/Renderer/Forward2DRenderer.h"
#include "Core/Renderer/IRenderScene.h"
#include "Core/RHI/IRHIBuffer.h"
#include "Core/RHI/IRHICommandContext.h"
#include "Core/RHI/IRHIDevice.h"
#include "Core/RHI/IRHIGraphicsPipeline.h"
#include "Core/RHI/IRHIProgram.h"
#include "Core/RHI/IRHISampler.h"
#include "Core/RHI/IRHITexture.h"
#include "Core/RHI/RHICommandTypes.h"
#include "Core/RHI/RHIGraphicsTypes.h"

namespace
{
	// ── Shared constant buffer layout ──────────────────────────────────────────
	struct OutlineConstants
	{
		float OutlineColor[4]; // gOutlineColor: RGBA (0~1)
		float TexelSize[2];    // gTexelSize: (1/W, 1/H)
		float OutlineWidth;    // gOutlineWidth: 팽창 반경 (픽셀)
		float Pad;
	};

	// ── Separable Outline Shaders ───────────────────────────────────────────────
	//
	// Pass 1 (PSHDilate):  maskRT → hDilatedRT, 수평 max-alpha 팽창
	// Pass 2 (PSVDilate):  hDilatedRT → vDilatedRT, 수직 max-alpha 팽창
	// Pass 3 (PSComposite): maskRT + vDilatedRT → 캔버스 RT, 엣지 픽셀 합성
	//
	// 분리 팽창(Separable Dilation)은 체비쇼프(정사각형) 거리 기반으로
	// 아웃라인을 생성합니다. r=2px 수준에서 원형과 시각적 차이가 거의 없습니다.
	// 샘플 수: 구버전 O(r²) → 신버전 2×O(r).

	const char* OUTLINE_SHADER_HLSL = R"(
cbuffer OutlineConstants : register(b0)
{
	float4 gOutlineColor;
	float2 gTexelSize;
	float  gOutlineWidth;
	float  gPad;
};

Texture2D    gMaskTex    : register(t0);   // 원본 스프라이트 마스크
Texture2D    gDilatedTex : register(t1);   // H팽창 결과 (V패스) 또는 최종팽창 (합성패스)
SamplerState gSampler    : register(s0);

struct VSOut
{
	float4 Position : SV_POSITION;
	float2 UV       : TEXCOORD0;
};

// InputLayout 없이 SV_VertexID 로 풀스크린 삼각형 생성
VSOut VSMain(uint vid : SV_VertexID)
{
	float2 positions[3] =
	{
		float2(-1.0f, -1.0f),
		float2(-1.0f,  3.0f),
		float2( 3.0f, -1.0f)
	};
	// D3D11 UV: (0,0) = 좌상단, (1,1) = 우하단
	float2 uvs[3] =
	{
		float2(0.0f,  1.0f),
		float2(0.0f, -1.0f),
		float2(2.0f,  1.0f)
	};
	VSOut o;
	o.Position = float4(positions[vid], 0.0f, 1.0f);
	o.UV       = uvs[vid];
	return o;
}

// ── Pass 1: 수평 max-alpha 팽창 ─────────────────────────────────────────────
// t0 = gMaskTex (원본 마스크) 를 읽어 수평 방향으로 팽창
float4 PSHDilate(VSOut input) : SV_TARGET
{
	float2 uv = input.UV;
	if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
		return float4(0.0f, 0.0f, 0.0f, 0.0f);

	int r = (int)ceil(gOutlineWidth);
	float maxA = 0.0f;
	[loop]
	for (int dx = -r; dx <= r; ++dx)
	{
		float2 s = uv + float2((float)dx * gTexelSize.x, 0.0f);
		maxA = max(maxA, gMaskTex.SampleLevel(gSampler, s, 0).a);
	}
	return float4(0.0f, 0.0f, 0.0f, maxA);
}

// ── Pass 2: 수직 max-alpha 팽창 ─────────────────────────────────────────────
// t1 = gDilatedTex (H팽창 결과) 를 읽어 수직 방향으로 팽창
float4 PSVDilate(VSOut input) : SV_TARGET
{
	float2 uv = input.UV;
	if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
		return float4(0.0f, 0.0f, 0.0f, 0.0f);

	int r = (int)ceil(gOutlineWidth);
	float maxA = 0.0f;
	[loop]
	for (int dy = -r; dy <= r; ++dy)
	{
		float2 s = uv + float2(0.0f, (float)dy * gTexelSize.y);
		maxA = max(maxA, gDilatedTex.SampleLevel(gSampler, s, 0).a);
	}
	return float4(0.0f, 0.0f, 0.0f, maxA);
}

// ── Pass 3: 아웃라인 합성 ────────────────────────────────────────────────────
// t0 = gMaskTex (원본 마스크, 스프라이트 내부 판별)
// t1 = gDilatedTex (최종 팽창 결과, 아웃라인 영역 판별)
float4 PSComposite(VSOut input) : SV_TARGET
{
	float2 uv = input.UV;
	if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
		discard;

	// 스프라이트 내부 픽셀은 아웃라인 불필요
	if (gMaskTex.SampleLevel(gSampler, uv, 0).a > 0.5f)
		discard;

	// 팽창 영역 밖은 아웃라인 픽셀이 아님
	if (gDilatedTex.SampleLevel(gSampler, uv, 0).a < 0.5f)
		discard;

	return gOutlineColor;
}
)";
} // namespace

// ── COutlineRenderer2D ────────────────────────────────────────────────────────

bool COutlineRenderer2D::Initialize(SafePtr<IRHIDevice> rhiDevice)
{
	if (m_isInitialized) return true;

	m_rhiDevice = rhiDevice;
	if (!m_rhiDevice.IsValid()) return false;

	// D3D11 이외 플랫폼은 HLSL 셰이더 불가 → 초기화 성공으로 처리 (아웃라인 없음)
	if (ERHIApi::D3D11 != m_rhiDevice->GetApi())
	{
		m_isInitialized = true;
		return true;
	}

	if (!CreatePipelines())
	{
		Finalize();
		return false;
	}

	// 영구 상수 버퍼: 더미 데이터로 초기 생성, 매 프레임 UpdateBuffer 로 갱신
	OutlineConstants dummy = {};
	RHIBufferDesc cbDesc;
	cbDesc.SizeInBytes = sizeof(OutlineConstants);
	cbDesc.Usage       = ERHIBufferUsage::Default;   // D3D11_USAGE_DEFAULT → UpdateSubresource
	cbDesc.BindFlags   = static_cast<RHIBindFlags>(ERHIBindFlag::ConstantBuffer);
	m_constantBuffer   = m_rhiDevice->CreateBuffer(cbDesc, &dummy);
	if (!m_constantBuffer)
	{
		Finalize();
		return false;
	}

	// Point sampler (마스크 픽셀 경계를 흐리지 않게)
	RHISamplerDesc samplerDesc;
	samplerDesc.Filter   = ERHIFilterMode::Point;
	samplerDesc.AddressU = ERHIAddressMode::Clamp;
	samplerDesc.AddressV = ERHIAddressMode::Clamp;
	m_pointSampler = m_rhiDevice->CreateSampler(samplerDesc);

	m_isInitialized = true;
	return true;
}

void COutlineRenderer2D::Finalize()
{
	m_maskRT.Reset();
	m_hDilatedRT.Reset();
	m_vDilatedRT.Reset();
	m_hDilationPipeline.Reset();
	m_vDilationPipeline.Reset();
	m_compositePipeline.Reset();
	m_hDilationPS.Reset();
	m_vDilationPS.Reset();
	m_compositePS.Reset();
	m_vsProgram.Reset();
	m_constantBuffer.Reset();
	m_pointSampler.Reset();
	m_rhiDevice.Reset();
	m_isInitialized = false;
}

// ── Step 1: 마스크 렌더 + H/V 팽창 패스 ────────────────────────────────────────

void COutlineRenderer2D::RenderMask(
	SafePtr<IRHICommandContext> ctx,
	CForward2DRenderer& spriteRenderer,
	IRenderScene& scene,
	const std::unordered_set<DebugObjectId>& entities,
	float camX, float camY, float camSize,
	int viewW, int viewH,
	float outlineWidthPx)
{
	if (!m_isInitialized || !ctx.IsValid()) return;
	if (entities.empty()) return;

	EnsureRTs(static_cast<uint32_t>(viewW), static_cast<uint32_t>(viewH));
	if (!m_maskRT || !m_hDilatedRT || !m_vDilatedRT) return;

	// ── CB 갱신: 팽창 패스에서 사용할 texelSize / outlineWidth ──────────────────
	{
		OutlineConstants cb = {};
		cb.TexelSize[0]  = viewW > 0 ? 1.0f / static_cast<float>(viewW) : 0.0f;
		cb.TexelSize[1]  = viewH > 0 ? 1.0f / static_cast<float>(viewH) : 0.0f;
		cb.OutlineWidth  = outlineWidthPx;
		ctx->UpdateBuffer(m_constantBuffer.GetSafePtr(), &cb, sizeof(cb));
	}

	// ── 마스크 패스: 선택 스프라이트 렌더 → m_maskRT ──────────────────────────
	{
		RenderPassDesc maskDesc;
		maskDesc.ColorAttachment.Target     = m_maskRT.GetSafePtr();
		maskDesc.ColorAttachment.LoadOp     = ERHILoadOp::Clear;
		maskDesc.ColorAttachment.StoreOp    = ERHIStoreOp::Store;
		maskDesc.ColorAttachment.ClearColor = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
		ctx->BeginRenderPass(maskDesc);

		spriteRenderer.SetRenderTargetSize(RenderSurfaceSize{ viewW, viewH });
		spriteRenderer.SetViewCamera(camX, camY, camSize);
		spriteRenderer.RenderFiltered(scene, entities);

		ctx->EndRenderPass();
	}

	// ── H-팽창 패스: m_maskRT → m_hDilatedRT (수평 max-alpha) ─────────────────
	{
		RenderPassDesc hDesc;
		hDesc.ColorAttachment.Target     = m_hDilatedRT.GetSafePtr();
		hDesc.ColorAttachment.LoadOp     = ERHILoadOp::Clear;
		hDesc.ColorAttachment.StoreOp    = ERHIStoreOp::Store;
		hDesc.ColorAttachment.ClearColor = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
		ctx->BeginRenderPass(hDesc);

		ctx->SetGraphicsPipeline(m_hDilationPipeline.GetSafePtr());
		ctx->SetConstantBuffer(ERHIProgramStage::Pixel, 0, m_constantBuffer.GetSafePtr());
		ctx->SetTexture(ERHIProgramStage::Pixel, 0, m_maskRT.GetSafePtr());
		ctx->SetSampler(ERHIProgramStage::Pixel, 0, m_pointSampler.GetSafePtr());
		ctx->Draw(3, 0);

		ctx->EndRenderPass();
	}

	// ── V-팽창 패스: m_hDilatedRT → m_vDilatedRT (수직 max-alpha) ─────────────
	{
		RenderPassDesc vDesc;
		vDesc.ColorAttachment.Target     = m_vDilatedRT.GetSafePtr();
		vDesc.ColorAttachment.LoadOp     = ERHILoadOp::Clear;
		vDesc.ColorAttachment.StoreOp    = ERHIStoreOp::Store;
		vDesc.ColorAttachment.ClearColor = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
		ctx->BeginRenderPass(vDesc);

		ctx->SetGraphicsPipeline(m_vDilationPipeline.GetSafePtr());
		ctx->SetConstantBuffer(ERHIProgramStage::Pixel, 0, m_constantBuffer.GetSafePtr());
		ctx->SetTexture(ERHIProgramStage::Pixel, 1, m_hDilatedRT.GetSafePtr());  // t1: H팽창 결과
		ctx->SetSampler(ERHIProgramStage::Pixel, 0, m_pointSampler.GetSafePtr());
		ctx->Draw(3, 0);

		ctx->EndRenderPass();
	}
}

// ── Step 2: 아웃라인 합성 (현재 RenderPass 안) ────────────────────────────────

void COutlineRenderer2D::RenderOutline(
	SafePtr<IRHICommandContext> ctx,
	float r, float g, float b, float a,
	float outlineWidthPx,
	int viewW, int viewH)
{
	if (!m_isInitialized || !m_compositePipeline || !m_maskRT || !m_vDilatedRT || !ctx.IsValid()) return;
	if (!m_pointSampler || !m_constantBuffer) return;

	// ── CB 갱신: 합성 패스용 색상 + texelSize ────────────────────────────────
	OutlineConstants cb;
	cb.OutlineColor[0] = r;
	cb.OutlineColor[1] = g;
	cb.OutlineColor[2] = b;
	cb.OutlineColor[3] = a;
	cb.TexelSize[0]    = viewW > 0 ? 1.0f / static_cast<float>(viewW) : 0.0f;
	cb.TexelSize[1]    = viewH > 0 ? 1.0f / static_cast<float>(viewH) : 0.0f;
	cb.OutlineWidth    = outlineWidthPx;
	cb.Pad             = 0.0f;
	ctx->UpdateBuffer(m_constantBuffer.GetSafePtr(), &cb, sizeof(cb));

	// ── 풀스크린 삼각형 드로우 ──────────────────────────────────────────────
	ctx->SetGraphicsPipeline(m_compositePipeline.GetSafePtr());
	ctx->SetConstantBuffer(ERHIProgramStage::Pixel, 0, m_constantBuffer.GetSafePtr());
	ctx->SetTexture(ERHIProgramStage::Pixel, 0, m_maskRT.GetSafePtr());       // t0: 원본 마스크
	ctx->SetTexture(ERHIProgramStage::Pixel, 1, m_vDilatedRT.GetSafePtr());   // t1: 최종 팽창
	ctx->SetSampler(ERHIProgramStage::Pixel, 0, m_pointSampler.GetSafePtr());
	ctx->Draw(3, 0);  // SV_VertexID 기반 풀스크린 삼각형
}

// ── 내부 헬퍼 ─────────────────────────────────────────────────────────────────

void COutlineRenderer2D::EnsureRTs(uint32_t w, uint32_t h)
{
	// 이미 같은 해상도라면 재생성 불필요
	if (m_maskRT && m_maskWidth == w && m_maskHeight == h) return;

	m_maskRT.Reset();
	m_hDilatedRT.Reset();
	m_vDilatedRT.Reset();
	m_maskWidth  = w;
	m_maskHeight = h;

	if (w == 0 || h == 0) return;

	RHITexture2DDesc desc;
	desc.Width     = w;
	desc.Height    = h;
	desc.Format    = ERHITextureFormat::RGBA8;
	desc.BindFlags = static_cast<RHITextureBindFlags>(ERHITextureBindFlag::ShaderResource) |
	                 static_cast<RHITextureBindFlags>(ERHITextureBindFlag::RenderTarget);

	m_maskRT     = m_rhiDevice->CreateTexture2D(desc, nullptr);
	m_hDilatedRT = m_rhiDevice->CreateTexture2D(desc, nullptr);
	m_vDilatedRT = m_rhiDevice->CreateTexture2D(desc, nullptr);
}

bool COutlineRenderer2D::CreatePipelines()
{
	// ── 공유 Vertex Shader (SV_VertexID 풀스크린 삼각형) ──────────────────────
	RHIProgramDesc vsProg;
	vsProg.Stage      = ERHIProgramStage::Vertex;
	vsProg.Language   = ERHIProgramLanguage::HLSL;
	vsProg.EntryPoint = "VSMain";
	vsProg.Source     = OUTLINE_SHADER_HLSL;
	m_vsProgram = m_rhiDevice->CreateProgram(vsProg);
	if (!m_vsProgram) return false;

	// ── PSHDilate ─────────────────────────────────────────────────────────────
	RHIProgramDesc hPS;
	hPS.Stage      = ERHIProgramStage::Pixel;
	hPS.Language   = ERHIProgramLanguage::HLSL;
	hPS.EntryPoint = "PSHDilate";
	hPS.Source     = OUTLINE_SHADER_HLSL;
	m_hDilationPS  = m_rhiDevice->CreateProgram(hPS);
	if (!m_hDilationPS) return false;

	// ── PSVDilate ─────────────────────────────────────────────────────────────
	RHIProgramDesc vPS;
	vPS.Stage      = ERHIProgramStage::Pixel;
	vPS.Language   = ERHIProgramLanguage::HLSL;
	vPS.EntryPoint = "PSVDilate";
	vPS.Source     = OUTLINE_SHADER_HLSL;
	m_vDilationPS  = m_rhiDevice->CreateProgram(vPS);
	if (!m_vDilationPS) return false;

	// ── PSComposite ───────────────────────────────────────────────────────────
	RHIProgramDesc compPS;
	compPS.Stage      = ERHIProgramStage::Pixel;
	compPS.Language   = ERHIProgramLanguage::HLSL;
	compPS.EntryPoint = "PSComposite";
	compPS.Source     = OUTLINE_SHADER_HLSL;
	m_compositePS     = m_rhiDevice->CreateProgram(compPS);
	if (!m_compositePS) return false;

	// ── H-팽창 파이프라인 (Opaque: 팽창 alpha를 그대로 RT에 기록) ─────────────
	RHIGraphicsPipelineDesc hPipeDesc;
	hPipeDesc.VertexProgram      = m_vsProgram.GetSafePtr();
	hPipeDesc.PixelProgram       = m_hDilationPS.GetSafePtr();
	hPipeDesc.VertexElements     = nullptr;
	hPipeDesc.VertexElementCount = 0;
	hPipeDesc.PrimitiveTopology  = ERHIPrimitiveTopology::TriangleList;
	hPipeDesc.BlendMode          = ERHIBlendMode::Opaque;
	m_hDilationPipeline = m_rhiDevice->CreateGraphicsPipeline(hPipeDesc);
	if (!m_hDilationPipeline) return false;

	// ── V-팽창 파이프라인 (Opaque) ────────────────────────────────────────────
	RHIGraphicsPipelineDesc vPipeDesc;
	vPipeDesc.VertexProgram      = m_vsProgram.GetSafePtr();
	vPipeDesc.PixelProgram       = m_vDilationPS.GetSafePtr();
	vPipeDesc.VertexElements     = nullptr;
	vPipeDesc.VertexElementCount = 0;
	vPipeDesc.PrimitiveTopology  = ERHIPrimitiveTopology::TriangleList;
	vPipeDesc.BlendMode          = ERHIBlendMode::Opaque;
	m_vDilationPipeline = m_rhiDevice->CreateGraphicsPipeline(vPipeDesc);
	if (!m_vDilationPipeline) return false;

	// ── 합성 파이프라인 (AlphaBlend: 캔버스 RT 위에 아웃라인 덧그리기) ─────────────
	RHIGraphicsPipelineDesc compPipeDesc;
	compPipeDesc.VertexProgram      = m_vsProgram.GetSafePtr();
	compPipeDesc.PixelProgram       = m_compositePS.GetSafePtr();
	compPipeDesc.VertexElements     = nullptr;
	compPipeDesc.VertexElementCount = 0;
	compPipeDesc.PrimitiveTopology  = ERHIPrimitiveTopology::TriangleList;
	compPipeDesc.BlendMode          = ERHIBlendMode::AlphaBlend;
	m_compositePipeline = m_rhiDevice->CreateGraphicsPipeline(compPipeDesc);
	if (!m_compositePipeline) return false;

	return true;
}

#include "pch.h"
#include "RenderWeaveGraph.h"

#include "Core/Logging/LoggerInternal.h"
#include "Core/RHI/IRHIDevice.h"
#include "Core/RHI/IRHITexture.h"
#include "Core/RHI/RHIGraphicsTypes.h"

#include <format>

// ── RWTexturePool ──────────────────────────────────────────────────────────────

void RWTexturePool::SetDevice(SafePtr<IRHIDevice> device)
{
	m_device = device;
}

void RWTexturePool::BeginFrame()
{
	// 지난 프레임 사용 여부로 유휴 카운트를 갱신하고, 오래 미사용인 엔트리를 해제한다.
	// InUse 는 직전 프레임의 Acquire 가 세팅한 값(아직 리셋 전) — 이걸로 사용 여부를 판정한 뒤
	// 이번 프레임용으로 false 로 되돌린다. 리사이즈/라이트 수 변동으로 desc 가 바뀌면 옛 RT 가
	// 계속 쌓이던 문제를 kMaxIdleFrames 경과 후 해제로 막는다.
	constexpr std::uint32_t kMaxIdleFrames = 60;
	for (std::size_t i = 0; i < m_entries.size();)
	{
		Entry& entry = m_entries[i];
		entry.IdleFrames = entry.InUse ? 0u : (entry.IdleFrames + 1u);
		if (false == entry.InUse && entry.IdleFrames >= kMaxIdleFrames)
		{
			m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(i));
		}
		else
		{
			entry.InUse = false;
			++i;
		}
	}
}

SafePtr<IRHITexture> RWTexturePool::Acquire(const RWTextureDesc& desc)
{
	if (false == m_device.IsValid() || 0 == desc.Width || 0 == desc.Height)
	{
		return nullptr;
	}

	// 재사용 — 같은 desc 의 미사용 엔트리.
	for (Entry& entry : m_entries)
	{
		if (false == entry.InUse && entry.Desc == desc && entry.Texture)
		{
			entry.InUse = true;
			return entry.Texture.GetSafePtr();
		}
	}

	// 신규 생성 — RenderTarget + ShaderResource.
	RHITexture2DDesc textureDesc;
	textureDesc.Width     = desc.Width;
	textureDesc.Height    = desc.Height;
	textureDesc.Format    = desc.Format;
	textureDesc.BindFlags = static_cast<RHITextureBindFlags>(ERHITextureBindFlag::RenderTarget)
	                      | static_cast<RHITextureBindFlags>(ERHITextureBindFlag::ShaderResource);

	Entry entry;
	entry.Desc    = desc;
	entry.Texture = m_device->CreateTexture2D(textureDesc, nullptr);
	entry.InUse   = true;
	if (false == static_cast<bool>(entry.Texture))
	{
		return nullptr;
	}

	SafePtr<IRHITexture> result = entry.Texture.GetSafePtr();
	m_entries.push_back(std::move(entry));
	return result;
}

void RWTexturePool::Clear()
{
	m_entries.clear();
}

// ── RWGraph ────────────────────────────────────────────────────────────────────

RWGraph::RWGraph(RWTexturePool& pool)
	: m_pool(pool)
{
}

RWTextureHandle RWGraph::CreateTexture(const RWTextureDesc& desc)
{
	TextureNode node;
	node.Desc       = desc;
	node.IsImported = false;
	m_textures.push_back(node);
	return static_cast<RWTextureHandle>(m_textures.size()) - 1;
}

RWTextureHandle RWGraph::ImportTexture(SafePtr<IRHITexture> external)
{
	TextureNode node;
	node.IsImported = true;
	node.External   = external;
	node.Resolved   = external;   // 외부는 이미 해결됨.
	m_textures.push_back(node);
	return static_cast<RWTextureHandle>(m_textures.size()) - 1;
}

void RWGraph::AddPass(RWPassDesc pass)
{
	m_passes.push_back(std::move(pass));
}

SafePtr<IRHITexture> RWGraph::Resolve(RWTextureHandle handle)
{
	if (handle < 0 || handle >= static_cast<RWTextureHandle>(m_textures.size()))
	{
		return nullptr;
	}
	TextureNode& node = m_textures[static_cast<std::size_t>(handle)];
	if (node.Resolved.IsValid())
	{
		return node.Resolved;
	}
	// transient — 이 시점에 풀에서 대여.
	node.Resolved = m_pool.Acquire(node.Desc);
	return node.Resolved;
}

void RWGraph::Compile(RWTextureHandle finalOutput)
{
	m_passAlive.assign(m_passes.size(), false);
	if (m_passes.empty())
	{
		m_compiled = true;
		return;
	}

	// 텍스처 → 그것을 쓰는 패스 인덱스(마지막 producer). 역방향 도달 분석에 사용.
	auto producerOf = [&](RWTextureHandle tex) -> int
	{
		for (int i = static_cast<int>(m_passes.size()) - 1; i >= 0; --i)
		{
			if (m_passes[static_cast<std::size_t>(i)].Write == tex)
			{
				return i;
			}
		}
		return -1;
	};

	// finalOutput 의 producer 부터 reads 를 따라 역방향으로 살린다.
	std::vector<int> worklist;
	const int finalProducer = producerOf(finalOutput);
	if (finalProducer >= 0)
	{
		worklist.push_back(finalProducer);
		m_passAlive[static_cast<std::size_t>(finalProducer)] = true;
	}

	while (false == worklist.empty())
	{
		const int passIndex = worklist.back();
		worklist.pop_back();
		for (const RWTextureHandle readTex : m_passes[static_cast<std::size_t>(passIndex)].Reads)
		{
			const int producer = producerOf(readTex);
			if (producer >= 0 && false == m_passAlive[static_cast<std::size_t>(producer)])
			{
				m_passAlive[static_cast<std::size_t>(producer)] = true;
				worklist.push_back(producer);
			}
		}
	}

	m_compiled = true;
}

void RWGraph::Execute(IRHICommandContext& commandContext)
{
	if (false == m_compiled)
	{
		// Compile 없이 실행하면 전부 실행(컬링 없음).
		m_passAlive.assign(m_passes.size(), true);
	}

	for (std::size_t i = 0; i < m_passes.size(); ++i)
	{
		if (false == m_passAlive[i])
		{
			continue;
		}
		RWPassDesc& pass = m_passes[i];
		// 출력 RT 를 먼저 확보(transient 면 여기서 대여).
		Resolve(pass.Write);
		if (pass.Execute)
		{
			pass.Execute(commandContext, *this);
		}
	}
}

void RWGraph::DebugDump() const
{
	CSystemLog::Info(std::format("[RenderWeave] passes={}, textures={}", m_passes.size(), m_textures.size()));
	for (std::size_t i = 0; i < m_passes.size(); ++i)
	{
		const bool alive = i < m_passAlive.size() ? m_passAlive[i] : true;
		CSystemLog::Info(std::format("[RenderWeave]  pass[{}] '{}' {} reads={} write={}",
			i,
			m_passes[i].Name,
			alive ? "ALIVE" : "culled",
			m_passes[i].Reads.size(),
			m_passes[i].Write));
	}
}

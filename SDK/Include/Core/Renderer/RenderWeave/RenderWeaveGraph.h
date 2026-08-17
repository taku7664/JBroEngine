#pragma once

#include "Core/Renderer/RenderWeave/RenderWeaveTypes.h"
#include "Utillity/Pointer/SafePtr.h"   // SafePtr + OwnerPtr

#include <cstdint>
#include <vector>

class IRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
//  RWTexturePool — transient RT 대여 풀.
//
//  같은 desc(크기·포맷)의 RT 를 재사용한다. 프레임 시작 시 BeginFrame() 으로 대여
//  상태를 리셋하고, 그래프 실행 중 Acquire 로 빌려 쓴다. 실제 GPU 텍스처는 풀이 소유
//  (OwnerPtr)하며 수명 내내 유지된다(리사이즈/포맷 변경 시 새 엔트리).
// ─────────────────────────────────────────────────────────────────────────────
class RWTexturePool
{
public:
	void SetDevice(SafePtr<IRHIDevice> device);
	// 프레임 시작 — 모든 엔트리를 미사용으로 표시(다음 Acquire 가 재사용).
	void BeginFrame();
	// desc 에 맞는 RT 를 대여. 재사용 가능한 미사용 엔트리가 있으면 그것을, 없으면 신규 생성.
	SafePtr<IRHITexture> Acquire(const RWTextureDesc& desc);
	// 프레임 중간 반납 — 이 RT 를 더 읽지 않게 된 시점에 부르면 같은 프레임의 다음 Acquire 가
	// 재사용한다. 부르지 않아도 다음 BeginFrame 에 회수되므로 정확성에는 영향이 없다.
	//
	// 왜 필요한가: 레이어 컴포짓은 "스크래치에 그리고 → 대상에 합성" 을 레이어마다 반복한다.
	// 반납이 없으면 레이어 수 × 뷰포트 수만큼의 뷰포트 크기 RT 가 프레임 내내 동시 상주한다
	// (에디터는 전 레이어를 RT 경유로 강제하므로 1920×1080 RGBA8 × 20장 = 160MB 급).
	// 실제로 동시에 필요한 것은 1장이다.
	//
	// ⚠ 즉시 실행 컨텍스트 전제 — 반납 시점에 앞선 드로우가 이미 GPU 에 제출되어 있어야 한다.
	//    지연 커맨드리스트를 도입하면 반납을 프레임그래프의 last-use 분석으로 옮겨야 한다.
	void Release(const SafePtr<IRHITexture>& texture);
	// 모든 RT 해제(셧다운).
	void Clear();

private:
	struct Entry
	{
		RWTextureDesc         Desc;
		OwnerPtr<IRHITexture> Texture;
		bool                  InUse = false;
		// 이번 프레임에 한 번이라도 대여됐는가. IdleFrames 판정은 **이 값**으로 한다 —
		// InUse 로 판정하면 Release 로 반납한 RT 가 "안 쓴 RT" 로 오인되어 계속 쓰는데도
		// kMaxIdleFrames 후 해제·재생성을 반복한다.
		bool                  UsedThisFrame = false;
		// 연속 미사용 프레임 수. 임계 초과 시 BeginFrame 이 해제(리사이즈/라이트 수 변동으로
		// 더 이상 안 쓰는 desc 의 RT 가 영구 누적되는 것을 막는다).
		std::uint32_t         IdleFrames = 0;
	};
	std::vector<Entry>  m_entries;
	SafePtr<IRHIDevice> m_device;
};

// ─────────────────────────────────────────────────────────────────────────────
//  RWGraph — 카메라 1대 프레임의 렌더 계획.
//
//  사용:  CreateTexture/ImportTexture 로 텍스처 핸들 확보 → AddPass 로 패스 등록 →
//         Compile(finalOutput) 로 미기여 패스 컬링 → Execute 로 실행.
//  실행 람다 안에서는 Resolve(handle) 로 실제 RT 를 얻는다.
// ─────────────────────────────────────────────────────────────────────────────
class RWGraph
{
public:
	explicit RWGraph(RWTexturePool& pool);

	// 그래프가 관리하는 transient 텍스처(풀에서 대여). 핸들 반환.
	RWTextureHandle CreateTexture(const RWTextureDesc& desc);
	// 외부 소유 텍스처(스왑체인/게임 RT 등)를 그래프에 들여온다. 핸들 반환.
	RWTextureHandle ImportTexture(SafePtr<IRHITexture> external);

	void AddPass(RWPassDesc pass);

	// 실행 람다 안에서 핸들 → 실제 RT. 미해결 transient 는 이 시점에 풀에서 대여.
	SafePtr<IRHITexture> Resolve(RWTextureHandle handle);

	// finalOutput 에 기여하지 않는 패스를 컬링한다(역방향 도달 분석).
	void Compile(RWTextureHandle finalOutput);
	// 살아남은 패스를 등록 순서로 실행.
	void Execute(IRHICommandContext& commandContext);
	// 패스 목록/컬링/텍스처 현황을 로그로 덤프(그래프 디버깅).
	void DebugDump() const;

private:
	struct TextureNode
	{
		RWTextureDesc        Desc;
		bool                 IsImported = false;
		SafePtr<IRHITexture> External;    // IsImported 일 때 유효.
		SafePtr<IRHITexture> Resolved;    // 대여/해결된 실제 RT(캐시).
	};

	RWTexturePool&            m_pool;
	std::vector<TextureNode>  m_textures;
	std::vector<RWPassDesc>   m_passes;
	std::vector<bool>         m_passAlive;
	bool                      m_compiled = false;
};

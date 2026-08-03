#pragma once

#include "Utillity/Pointer/SafePtr.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  ISaveStorage — 게임이 쓰기 가능한 유일한 저장소.
//
//  왜 CFileSystem 이 아닌가:
//    `CFileSystem` 은 **에셋 루트**에 뿌리내린 읽기 전용 창구다. 패키지 빌드에서 에셋은
//    팩 파일이고 설치 경로는 쓰기 권한이 없을 수 있다. 웹에서는 아예 `--preload-file` 로
//    구운 MEMFS 라 써 봐야 새로고침하면 사라진다. 세이브는 **다른 뿌리**가 필요하다.
//
//  왜 인터페이스인가:
//    구현이 플랫폼 코드(Win32 경로 / Emscripten IDBFS)에 닿는다. 게임 스크립트 DLL 은
//    그 obj 를 링크하면 안 되므로, 스크립트는 vtable 을 통해서만 부른다.
//    IPrefabSpawner/IAssetManager 와 같은 방식이다.
//
//  슬롯 이름(slot):
//    파일 하나를 가리키는 납작한 이름이다. 디렉터리 구분자(`/` `\`), `..`, 드라이브 표기
//    (`:`)가 들어 있으면 **거부한다** — 스크립트가 저장소 밖으로 나가 남의 파일을 덮어쓰는
//    길을 열어 주지 않기 위해서다. 확장자는 자유롭게 붙여도 된다(예: "slot0.yaml").
//
//  Flush 계약:
//    · 네이티브 — 쓰기가 이미 디스크에 닿았으므로 사실상 확인 절차다.
//    · 웹 — 쓰기는 브라우저 메모리 FS 에만 반영된다. **Flush 를 불러야 IndexedDB 로 넘어가
//      다음 방문에 남는다.** 세이브 직후 부르지 않으면 탭을 닫는 순간 사라진다.
//    호출 비용이 낮지 않으므로(웹은 IndexedDB 트랜잭션) 매 프레임이 아니라 저장 시점에 부른다.
// ─────────────────────────────────────────────────────────────────────────────
class ISaveStorage : public EnableSafeFromThis<ISaveStorage>
{
public:
	virtual ~ISaveStorage() = default;

	// 저장소가 준비됐는가(디렉터리 확보 + 웹이면 IndexedDB 적재 완료).
	// false 면 아래 호출은 전부 실패한다.
	virtual bool IsReady() const = 0;

	virtual bool WriteBytes(const char* slot, const void* data, std::size_t size) = 0;
	virtual bool ReadBytes(const char* slot, std::vector<std::uint8_t>& outData) const = 0;
	virtual bool Exists(const char* slot) const = 0;
	virtual bool Remove(const char* slot) = 0;

	// 쓴 내용을 영속 저장소에 밀어 넣는다. 위 Flush 계약 참조.
	virtual bool Flush() = 0;

	// ── 텍스트 편의 ──────────────────────────────────────────────────────────
	// 가상이 아니다 — 바이트 경로 하나만 구현하면 되게. 인라인이라 DLL 에서도 안전하다
	// (경계를 넘는 건 위의 가상 호출뿐이다).
	bool WriteText(const char* slot, std::string_view text)
	{
		return WriteBytes(slot, text.data(), text.size());
	}

	bool ReadText(const char* slot, std::string& outText) const
	{
		std::vector<std::uint8_t> bytes;
		if (false == ReadBytes(slot, bytes))
		{
			outText.clear();
			return false;
		}
		outText.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		return true;
	}
};

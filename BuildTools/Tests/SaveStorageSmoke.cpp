// ─────────────────────────────────────────────────────────────────────────────
//  SaveStorageSmoke — CSaveStorage 회귀 테스트.
//
//  세이브는 "동작하는 줄 알았는데 안 남아 있었다" 가 가장 비싼 종류의 버그다(유저의
//  진행이 날아간다). 그래서 왕복뿐 아니라 **거부돼야 하는 것이 거부되는지**까지 본다.
//
//  종료 코드가 곧 실패한 항목 번호다(0 = 전부 통과).
//    1 Initialize/IsReady        2 텍스트 왕복       3 바이너리 왕복(NUL 포함)
//    4 Exists/Remove             5 없는 슬롯 읽기    6 슬롯 이름 거부(경로 탈출)
//    7 덮어쓰기(잘린 꼬리 없음)  8 Flush
//
//  Engine.lib 를 링크한다 — CSaveStorage 는 헤더 온리가 아니다.
// ─────────────────────────────────────────────────────────────────────────────
#include "Core/Save/SaveStorage.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <system_error>
#include <vector>

namespace
{
	int Fail(int code, const char* message)
	{
		std::printf("SaveStorage smoke FAILED (%d): %s\n", code, message);
		return code;
	}

	// 이 테스트가 만든 디렉터리를 지워 다음 실행이 이전 잔여물에 기대지 않게 한다.
	void RemoveStorageTree(const File::Path& root)
	{
		std::error_code error;
		File::fs::remove_all(root, error);
	}
}

int main()
{
	// 실제 제품 세이브를 건드리지 않도록 전용 이름을 쓴다.
	CSaveStorage storage;
	if (false == storage.Initialize("JBroEngine-SaveStorageSmoke"))
	{
		return Fail(1, "Initialize returned false.");
	}
	if (false == storage.IsReady())
	{
		return Fail(1, "IsReady is false after a successful Initialize.");
	}

	// 이전 실행이 남긴 파일이 있으면 결과가 오염된다. 지우고 다시 연다.
	const File::Path root = storage.GetRootPath();
	storage.Finalize();
	RemoveStorageTree(root);
	if (false == storage.Initialize("JBroEngine-SaveStorageSmoke"))
	{
		return Fail(1, "Initialize returned false on the second call.");
	}

	// ── 2) 텍스트 왕복 ────────────────────────────────────────────────────────
	const std::string written = "slot: 1\nplayer: \xed\x95\x9c\xea\xb8\x80\n";   // UTF-8 한글 포함
	if (false == storage.WriteText("slot0.yaml", written))
	{
		return Fail(2, "WriteText returned false.");
	}
	std::string readBack;
	if (false == storage.ReadText("slot0.yaml", readBack))
	{
		return Fail(2, "ReadText returned false.");
	}
	if (readBack != written)
	{
		return Fail(2, "ReadText returned different content than WriteText wrote.");
	}

	// ── 3) 바이너리 왕복 — 중간 NUL 이 문자열로 취급돼 잘리지 않아야 한다 ──────
	const std::vector<std::uint8_t> binary = { 0x00, 0x01, 0xFF, 0x00, 0x7F, 0x80 };
	if (false == storage.WriteBytes("blob.bin", binary.data(), binary.size()))
	{
		return Fail(3, "WriteBytes returned false.");
	}
	std::vector<std::uint8_t> binaryBack;
	if (false == storage.ReadBytes("blob.bin", binaryBack))
	{
		return Fail(3, "ReadBytes returned false.");
	}
	if (binaryBack != binary)
	{
		return Fail(3, "ReadBytes returned different bytes (embedded NUL truncation?).");
	}

	// ── 4) Exists / Remove ────────────────────────────────────────────────────
	if (false == storage.Exists("slot0.yaml"))
	{
		return Fail(4, "Exists is false for a slot that was just written.");
	}
	if (false == storage.Remove("slot0.yaml"))
	{
		return Fail(4, "Remove returned false.");
	}
	if (storage.Exists("slot0.yaml"))
	{
		return Fail(4, "Exists is still true after Remove.");
	}

	// ── 5) 없는 슬롯 읽기 — 실패를 돌려주되 출력은 비워야 한다 ─────────────────
	std::vector<std::uint8_t> missing = { 0xAA };   // 호출 전 값이 남지 않는지 보려고 채워 둔다.
	if (storage.ReadBytes("no-such-slot", missing))
	{
		return Fail(5, "ReadBytes succeeded for a slot that does not exist.");
	}
	if (false == missing.empty())
	{
		return Fail(5, "ReadBytes left stale data in the out parameter on failure.");
	}

	// ── 6) 슬롯 이름 거부 — 저장소 밖으로 나가는 이름은 전부 막혀야 한다 ───────
	// 여기가 뚫리면 스크립트가 사용자의 아무 파일이나 덮어쓸 수 있다.
	const char* rejected[] = { "", "../escape.txt", "sub/slot.txt", "sub\\slot.txt",
	                           "C:absolute.txt", "..", "a/../../b.txt" };
	for (const char* slot : rejected)
	{
		if (storage.WriteText(slot, "should not be written"))
		{
			std::printf("  slot that should have been rejected: '%s'\n", slot);
			return Fail(6, "WriteText accepted a slot name that escapes the storage root.");
		}
	}

	// ── 7) 덮어쓰기 — 짧은 내용으로 덮으면 이전 꼬리가 남으면 안 된다 ──────────
	if (false == storage.WriteText("overwrite.txt", "0123456789ABCDEF"))
	{
		return Fail(7, "WriteText (long) returned false.");
	}
	if (false == storage.WriteText("overwrite.txt", "short"))
	{
		return Fail(7, "WriteText (short) returned false.");
	}
	std::string overwritten;
	if (false == storage.ReadText("overwrite.txt", overwritten))
	{
		return Fail(7, "ReadText after overwrite returned false.");
	}
	if (overwritten != "short")
	{
		std::printf("  got: '%s'\n", overwritten.c_str());
		return Fail(7, "Overwrite left the tail of the previous content.");
	}

	// ── 8) Flush ──────────────────────────────────────────────────────────────
	if (false == storage.Flush())
	{
		return Fail(8, "Flush returned false.");
	}

	storage.Finalize();
	RemoveStorageTree(root);

	std::printf("Save storage smoke test passed.\n");
	return 0;
}

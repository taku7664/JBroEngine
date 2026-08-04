#include "pch.h"
#include "SaveStorage.h"

#include "Core/FileSystem/UserDataPath.h"
#include "Core/Logging/LoggerInternal.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>

#if JBRO_PLATFORM_WEB
#include <emscripten.h>
#endif

namespace
{
#if JBRO_PLATFORM_WEB
	// IDBFS 를 `/UserData` 에 마운트하고 IndexedDB 에 있던 내용을 메모리 FS 로 끌어온다.
	// ASYNCIFY 가 켜져 있어(-sASYNCIFY=1) C++ 쪽에서는 동기 호출처럼 보인다 —
	// 그래서 네이티브와 웹이 같은 API 계약을 갖는다.
	EM_ASYNC_JS(int, JBroMountUserData, (), {
		try
		{
			if (!FS.analyzePath('/UserData').exists)
			{
				FS.mkdir('/UserData');
				FS.mount(IDBFS, {}, '/UserData');
			}
			await new Promise((resolve, reject) => {
				// populate=true — IndexedDB → 메모리 FS. 부팅 시 한 번만 한다.
				FS.syncfs(true, (err) => err ? reject(err) : resolve());
			});
			return 1;
		}
		catch (e)
		{
			console.error('[SaveStorage] mount failed:', e);
			return 0;
		}
	});

	// 메모리 FS 의 변경을 IndexedDB 로 밀어 넣는다. 이걸 안 부르면 탭을 닫는 순간 사라진다.
	EM_ASYNC_JS(int, JBroSyncUserData, (), {
		try
		{
			await new Promise((resolve, reject) => {
				FS.syncfs(false, (err) => err ? reject(err) : resolve());
			});
			return 1;
		}
		catch (e)
		{
			console.error('[SaveStorage] sync failed:', e);
			return 0;
		}
	});
#endif

}

bool CSaveStorage::Initialize(const char* productName)
{
	m_isReady = false;
	m_rootPath = File::Path();

#if JBRO_PLATFORM_WEB
	if (0 == JBroMountUserData())
	{
		CSystemLog::Error("Save storage init failed (IDBFS mount).");
		return false;
	}
#endif

	// 뿌리 결정은 UserData 공용 헬퍼가 한다 — 로그도 같은 뿌리를 쓴다.
	const File::Path userRoot = UserData::ResolveRoot(productName);
	if (userRoot.empty())
	{
		CSystemLog::Error("Save storage init failed (no writable user directory for this platform).");
		return false;
	}
	File::Path root = userRoot / File::FString("Saves");

	std::error_code error;
	File::fs::create_directories(root, error);
	// create_directories 는 "이미 있음"을 실패로 보고하지 않지만(error 가 비어 있다),
	// 권한 문제 등 진짜 실패와 구분하려면 존재 여부를 따로 확인해야 한다.
	if (false == File::fs::exists(root, error))
	{
		CSystemLog::Error("Save storage init failed (cannot create directory): " + root.string());
		return false;
	}

	m_rootPath = std::move(root);
	m_isReady = true;
	CSystemLog::Info("Save storage ready: " + m_rootPath.string());
	return true;
}

void CSaveStorage::Finalize()
{
	// 종료 시 한 번 밀어 준다 — 게임이 Flush 를 깜빡해도 정상 종료라면 살아남게.
	// (웹에서 탭을 강제로 닫는 경우까지는 보장하지 못한다.)
	if (m_isReady)
	{
		Flush();
	}
	m_isReady = false;
	m_rootPath = File::Path();
}

bool CSaveStorage::IsReady() const
{
	return m_isReady;
}

File::Path CSaveStorage::ResolveSlotPath(const char* slot) const
{
	if (false == m_isReady || nullptr == slot || '\0' == slot[0])
	{
		return File::Path();
	}

	// 저장소 밖으로 나가는 이름을 막는다. 스크립트가 넘기는 값이므로 신뢰하지 않는다.
	for (const char* c = slot; '\0' != *c; ++c)
	{
		if ('/' == *c || '\\' == *c || ':' == *c)
		{
			return File::Path();
		}
	}
	if (nullptr != std::strstr(slot, ".."))
	{
		return File::Path();
	}

	return m_rootPath / File::FString(slot);
}

bool CSaveStorage::WriteBytes(const char* slot, const void* data, std::size_t size)
{
	const File::Path path = ResolveSlotPath(slot);
	if (path.empty())
	{
		CSystemLog::Error(std::string("Save write failed (invalid slot name): ")
			+ (slot ? slot : "<null>"));
		return false;
	}
	if (nullptr == data && size > 0)
	{
		return false;
	}

	std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
	if (false == file.is_open())
	{
		CSystemLog::Error("Save write failed (cannot open): " + path.string());
		return false;
	}
	if (size > 0)
	{
		file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
	}
	// 스트림 상태를 확인하고 돌려준다 — 디스크가 가득 찬 경우를 성공으로 보고하지 않기 위해서.
	file.close();
	if (false == file.good())
	{
		CSystemLog::Error("Save write failed (stream error): " + path.string());
		return false;
	}
	return true;
}

bool CSaveStorage::ReadBytes(const char* slot, std::vector<std::uint8_t>& outData) const
{
	outData.clear();

	const File::Path path = ResolveSlotPath(slot);
	if (path.empty())
	{
		return false;
	}

	std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);
	if (false == file.is_open())
	{
		return false;   // 없는 슬롯은 흔한 정상 경로다(첫 실행) — 로그를 남기지 않는다.
	}

	const std::streampos size = file.tellg();
	if (size < 0)
	{
		return false;
	}
	file.seekg(0, std::ios::beg);

	outData.resize(static_cast<std::size_t>(size));
	if (size > 0)
	{
		file.read(reinterpret_cast<char*>(outData.data()), size);
		if (file.gcount() != size)
		{
			outData.clear();
			return false;
		}
	}
	return true;
}

bool CSaveStorage::Exists(const char* slot) const
{
	const File::Path path = ResolveSlotPath(slot);
	if (path.empty())
	{
		return false;
	}
	std::error_code error;
	return File::fs::exists(path, error);
}

bool CSaveStorage::Remove(const char* slot)
{
	const File::Path path = ResolveSlotPath(slot);
	if (path.empty())
	{
		return false;
	}
	std::error_code error;
	// remove 는 "없었다"를 false + error 없음으로 돌려준다. 지우려던 게 없는 것도
	// 호출자 입장에서는 목적 달성이므로 error 만 실패로 본다.
	File::fs::remove(path, error);
	return false == static_cast<bool>(error);
}

bool CSaveStorage::Flush()
{
	if (false == m_isReady)
	{
		return false;
	}
#if JBRO_PLATFORM_WEB
	return 0 != JBroSyncUserData();
#else
	// 네이티브는 ofstream 을 닫는 시점에 이미 OS 로 넘어갔다. 여기서 fsync 까지 하지 않는 건
	// 세이브 한 번에 디스크 동기화 지연을 물리는 것이 득보다 실이 크기 때문이다.
	return true;
#endif
}

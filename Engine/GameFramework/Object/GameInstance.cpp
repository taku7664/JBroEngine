#include "pch.h"
#include "GameFramework/Object/GameInstance.h"

#include "Utillity/File/GuidConvert.h"

// 여기(헤더가 아니라 .cpp)에 두는 이유:
//
// 변환은 File::Guid(= fs::path) 를 문자열로 펴는 일이라 <filesystem> 코드가 딸려 온다.
// GameInstance.h 는 오브젝트/컴포넌트/스크립트의 공통 베이스라 사실상 모든 TU 가 포함하는데,
// 그 자리에 인라인으로 두면 같은 변환이 전 TU 에 인스턴스화된다. 실제로 게임 스크립트 DLL 의
// Release(LTCG) 링크가 이 인라인 때문에 백엔드 ICE(C1001, link!InvokeCompilerPass)로 죽었다.
//
// 호출은 생성/로드 시점뿐이라 함수 호출 한 번이 붙는 것은 문제가 되지 않는다.
void GameInstance::AssignInstanceGuid(const File::Guid& instanceGuid)
{
	m_instanceGuid    = instanceGuid;
	m_instanceGuid128 = ToGuid128(instanceGuid);
}

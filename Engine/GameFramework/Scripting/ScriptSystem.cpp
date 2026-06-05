#include "pch.h"
#include "ScriptSystem.h"

#include "Core/ScriptCore.h"
#include "Core/Logging/LoggerInternal.h"
#include "GameFramework/Component/ScriptComponent.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameFramework/Scene/Scene.h"

#include <cstring>

namespace
{
	// ── ApplyPendingFields ────────────────────────────────────────────────────
	// ScriptComponent::PendingFields 를 인스턴스에 적용한다.
	// 이름으로 프로퍼티를 찾아 raw bytes 를 memcpy 하므로,
	// DLL 교체로 오프셋이 바뀌어도 안전하게 복원된다.
	// clearAfter=false 면 적용 후 PendingFields 를 보존한다. 에디트타임 미리보기
	// 인스턴스가 PendingFields 를 소비해버리면, Play 진입 시 새로 만들어지는 인스턴스가
	// 복원할 값을 잃기 때문(= Ref/Asset 등이 Play 에서 비어버리는 버그). 에디트타임에는
	// 보존하고, 실제 Play 의 지연 생성 경로에서만 소비(clear)한다.
	void ApplyPendingFields(ScriptComponent& script, const ScriptTypeInfo& typeInfo, bool clearAfter = true)
	{
		if (script.PendingFields.empty() || nullptr == script.Instance)
		{
			return;
		}

		for (const ScriptPendingField& pending : script.PendingFields)
		{
			for (const ReflectPropertyInfo& prop : typeInfo.Properties)
			{
				if (nullptr == prop.Name || pending.Name != prop.Name)
				{
					continue;
				}
				// Text 로 보존되는 타입들(raw Data 미사용): AssetGuid(File::Guid), Ref(RefBase POD 버퍼), String.
				const bool assetGuidPair =
					(EReflectPropertyType::AssetGuid == pending.Type && EReflectPropertyType::AssetGuid == prop.Type);
				const bool refPair =
					(EReflectPropertyType::Ref       == pending.Type && EReflectPropertyType::Ref       == prop.Type);
				const bool stringPair =
					(EReflectPropertyType::String    == pending.Type && EReflectPropertyType::String    == prop.Type);

				if (pending.Data.size() != prop.Size)
				{
					// 타입/크기가 바뀐 경우 무시 (기본값 유지). 단 Text 기반은 예외.
					if (false == assetGuidPair && false == refPair && false == stringPair)
					{
						break;
					}
				}

				void* field = CReflectionRegistry::GetPropertyAddress(script.Instance, prop);
				if (nullptr == field)
				{
					break;
				}
				if (assetGuidPair)
				{
					*static_cast<File::Guid*>(field) = File::Guid(pending.Text);
				}
				else if (refPair)
				{
					// Ref 는 POD 버퍼 — 호스트가 써도 게임 DLL 이 동일 바이트로 읽는다.
					static_cast<RefBase*>(field)->SetGuidText(pending.Text.c_str());
				}
				else if (stringPair)
				{
					*static_cast<std::string*>(field) = pending.Text;
				}
				else
				{
					std::memcpy(field, pending.Data.data(), prop.Size);
				}
				break;
			}
		}

		if (clearAfter)
		{
			script.PendingFields.clear();
		}
	}
}

void CScriptSystem::EnsureEditTimeInstance(ScriptComponent& script)
{
	if (nullptr != script.Instance)
	{
		return;
	}
	if (INVALID_TYPE_ID == script.ScriptTypeId || false == Script.Reflection.IsValid())
	{
		return;
	}

	const ScriptTypeInfo* typeInfo = Script.Reflection->FindScript(script.ScriptTypeId);
	if (nullptr == typeInfo)
	{
		return;   // DLL 미로드/등록 누락 — 인스펙터는 "대기 중" 으로 둔다.
	}

	ScriptInstanceHandle handle = Script.Reflection->CreateScriptInstance(script.ScriptTypeId);
	if (nullptr == handle.Instance)
	{
		return;
	}

	script.SetInstance(std::move(handle));

	// 저장돼 있던 값(PendingFields)을 인스턴스에 복원. Bind/Start 는 하지 않는다.
	// clearAfter=false: 에디트타임 미리보기는 PendingFields 를 소비하지 않는다.
	// (Play 진입 시 새 인스턴스가 같은 값을 복원할 수 있어야 함.)
	if (false == script.PendingFields.empty())
	{
		ApplyPendingFields(script, *typeInfo, /*clearAfter*/ false);
	}
}

// ── OnUpdate ──────────────────────────────────────────────────────────────────
// 2-pass 구조:
//   Pass 1: 인스턴스 지연 생성 + Bind + Start(OnStart). 새로 시작한 스크립트는
//           이번 프레임에 OnUpdate 까지 받지 않고 다음 프레임부터 받는다.
//           (모든 스크립트의 OnStart 가 끝난 다음에 OnUpdate 가 도는 시맨틱.)
//   Pass 2: IsStarted 인 스크립트만 OnUpdate.
//
// 진단 로그:
//   - ScriptTypeId 가 유효하지만 CreateScriptInstance 가 실패한 경우
//     (entity, typeId) 당 1회만 경고. DLL 로드 실패/등록 누락의 가장 흔한 증상.
void CScriptSystem::OnUpdate(CScene& scene)
{
	// ── Pass 1: 인스턴스 보장 + Bind + Start ──────────────────────────────────
	scene.ForEach<ScriptComponent>(
		[&](ScriptComponent& script)
		{
			if (false == script.IsEnabled)
			{
				return;
			}

			if (INVALID_TYPE_ID == script.ScriptTypeId)
			{
				return;
			}

			CGameObject* owner = script.GetOwner();
			if (nullptr == owner)
			{
				return;
			}

			// 인스턴스 지연 생성
			if (nullptr == script.Instance)
			{
				if (false == Script.Reflection.IsValid())
				{
					return;
				}

				ScriptInstanceHandle handle = Script.Reflection->CreateScriptInstance(script.ScriptTypeId);
				if (nullptr == handle.Instance)
				{
					const ObjectTypeKey key{ owner, script.ScriptTypeId };
					if (m_warnedFailedCreate.insert(key).second)
					{
						const ScriptTypeInfo* info = Script.Reflection->FindScript(script.ScriptTypeId);
						const char* name = (info && info->Type.Name) ? info->Type.Name : "<unknown>";
						CSystemLog::Warning(std::format(
							"ScriptSystem: CreateScriptInstance failed (object={}, type={}, typeId={}). "
							"DLL not loaded, type not registered, or allocator failure.",
							owner->GetName(),
							name,
							static_cast<unsigned long long>(script.ScriptTypeId)));
					}
					return;
				}

				script.SetInstance(std::move(handle));

				// PendingFields 적용: OnStart 호출 전에 값을 복원한다.
				if (false == script.PendingFields.empty())
				{
					const ScriptTypeInfo* typeInfo = Script.Reflection->FindScript(script.ScriptTypeId);
					if (typeInfo)
					{
						ApplyPendingFields(script, *typeInfo);
					}
				}
			}

			if (nullptr == script.Instance)
			{
				return;
			}

			if (false == script.Instance->IsBound())
			{
				script.Instance->Bind(scene, *owner);
			}

			// OnStart 만 호출 (OnUpdate 는 Pass 2 에서)
			if (false == script.Instance->IsStarted())
			{
				script.Instance->Start();
			}
		});

	// ── Pass 2: 이미 시작된 스크립트만 OnUpdate ───────────────────────────────
	scene.ForEach<ScriptComponent>(
		[](ScriptComponent& script)
		{
			if (false == script.IsEnabled || nullptr == script.Instance)
			{
				return;
			}

			if (false == script.Instance->IsStarted())
			{
				return; // 이번 프레임에 막 Start 된 경우 다음 프레임부터 OnUpdate
			}

			// GameScript::Update 는 미시작 시 Start 를 호출하지만
			// 위에서 IsStarted 를 보장했으므로 OnUpdate 만 돈다.
			script.Instance->Update();
		});
}

void CScriptSystem::OnFixedUpdate(CScene& scene)
{
	// OnStart() 가 완료된 스크립트만 FixedUpdate 를 받는다.
	// 아직 시작되지 않은 스크립트는 OnUpdate 에서 따라잡는다.
	scene.ForEach<ScriptComponent>(
		[](ScriptComponent& script)
		{
			if (false == script.IsEnabled || nullptr == script.Instance)
			{
				return;
			}

			if (script.Instance->IsStarted())
			{
				script.Instance->FixedUpdate();
			}
		});
}

#include <JBro/Framework2D/Scripting/ScriptModule.h>

#include <JBro/Framework2D/Internal/ScriptModuleContext.h>
#include <JBro/Framework2D/Internal/SystemContext.h>
#include <JBro/Framework2D/ServiceContext.h>
#include <JBro/AudioTypes/Internal/ScriptModuleContext.h>
#include <JBro/InputTypes/Internal/ScriptModuleContext.h>
#include <JBro/LocalizationTypes/Internal/ScriptModuleContext.h>
#include <JBro/Network/Internal/ScriptModuleContext.h>
#include <JBro/SaveTypes/Internal/ScriptModuleContext.h>
#include <JBro/Internal/InstanceRegistry.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/ScriptModule.h>
#include <JBro/Runtime/ScriptRegistry.h>
#include <JBro/Runtime/ServiceContext.h>
#include <JBro/Runtime/SystemContext.h>
#include <JBro/Runtime/TextStore.h>
#include <JBro/Types/NameTable.h>

namespace JBro::Internal
{
    namespace
    {
        // **바인딩 목록은 여기 한 곳이다**(cpp-script-plan §3.2). 사용자가 손으로 적던 때는 시험 DLL 조차 호스트가 넘기는 오디오·네트워크를
        // 묶지 않았다. 차원과 무관한 서비스의 줄은 `JBroFramework3D` 의 같은 파일과 같아야 한다 - 서비스를 더하면 두 곳을 함께 고친다.
        void BindHostServices(const ScriptModuleLoadContext& context)
        {
            // 호스트가 낸 것만 묶는다. 호스트가 그 서비스를 두지 않으면(네트워크를 끈 설정 등) 이 DLL 의 빈 사본이 남는다.
            if (const InputServiceContext* found = FindInputServiceContext(context))
            {
                BindInputServiceContext(*found);
            }
            if (const InputSystemContext* found = FindInputSystemContext(context))
            {
                BindInputSystemContext(*found);
            }
            if (const SaveServiceContext* found = FindSaveServiceContext(context))
            {
                BindSaveServiceContext(*found);
            }
            if (const SaveSystemContext* found = FindSaveSystemContext(context))
            {
                BindSaveSystemContext(*found);
            }
            if (const LocalizationServiceContext* found = FindLocalizationServiceContext(context))
            {
                BindLocalizationServiceContext(*found);
            }
            if (const LocalizationSystemContext* found = FindLocalizationSystemContext(context))
            {
                BindLocalizationSystemContext(*found);
            }
            if (const AudioServiceContext* found = FindAudioServiceContext(context))
            {
                BindAudioServiceContext(*found);
            }
            if (const AudioSystemContext* found = FindAudioSystemContext(context))
            {
                BindAudioSystemContext(*found);
            }
            if (const NetworkServiceContext* found = FindNetworkServiceContext(context))
            {
                BindNetworkServiceContext(*found);
            }
            if (const NetworkSystemContext* found = FindNetworkSystemContext(context))
            {
                BindNetworkSystemContext(*found);
            }
        }

        void UnbindEverything() noexcept
        {
            BindFramework2DServiceContext({});
            BindFramework2DSystemContext({});
            BindInputServiceContext({});
            BindInputSystemContext({});
            BindSaveServiceContext({});
            BindSaveSystemContext({});
            BindLocalizationServiceContext({});
            BindLocalizationSystemContext({});
            BindAudioServiceContext({});
            BindAudioSystemContext({});
            BindNetworkServiceContext({});
            BindNetworkSystemContext({});
            InstanceRegistry::Bind(nullptr);
            ScriptRegistry::Bind(nullptr);
            PropertyRegistry::BindScript(nullptr);
            NameTable::Bind(nullptr);
            TextStore::Bind(nullptr);
            BindSystemContext({});
            BindServiceContext({});
        }

        bool LoadModule(const ScriptModuleLoadContext* context) noexcept
        {
            if (context == nullptr || false == ValidateScriptModuleLoadContext(*context))
            {
                return false;
            }
            const Framework2DServiceContext* frameworkServices = FindFramework2DServiceContext(*context);
            const Framework2DSystemContext* frameworkSystems = FindFramework2DSystemContext(*context);
            if (frameworkServices == nullptr || frameworkSystems == nullptr)
            {
                return false;
            }
            // 실패하면 호스트가 `Unload` 를 되돌리기 훅으로 부르고 등록한 표를 비운다(ProjectRule §6.2). 여기서 풀지 않는다.
            try
            {
                if (false == BindScriptModuleContexts(*context))
                {
                    return false;
                }
                BindFramework2DServiceContext(*frameworkServices);
                BindFramework2DSystemContext(*frameworkSystems);
                BindHostServices(*context);
                // 호스트 표를 모두 묶은 **뒤에** 등록한다. 이름과 필드 표가 호스트 쪽에 들어가야 한다.
                return RegisterPendingScriptTypes();
            }
            catch (...)
            {
                // 예외는 DLL 경계를 넘지 않는다(ProjectRule §6.2).
                return false;
            }
        }

        void UnloadModule() noexcept
        {
            UnbindEverything();
        }

        constexpr ScriptContextRequirement RequiredContexts[] =
        {
            Framework2DServiceContextRequirement,
            Framework2DSystemContextRequirement
        };

        constexpr ScriptModuleApi ModuleApi =
        {
            ScriptModuleAbiVersion,
            sizeof(ScriptModuleApi),
            RequiredContexts,
            static_cast<std::uint32_t>(sizeof(RequiredContexts) / sizeof(RequiredContexts[0])),
            0,
            &LoadModule,
            &UnloadModule
        };
    }

    const ScriptModuleApi* GetScriptModuleApi2D(std::uint32_t hostAbiVersion, std::uint32_t hostApiSize) noexcept
    {
        if (hostAbiVersion != ScriptModuleAbiVersion || hostApiSize != sizeof(ScriptModuleApi))
        {
            return nullptr;
        }
        return &ModuleApi;
    }
}

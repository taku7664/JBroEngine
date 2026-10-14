#include "CatalogBuilder.h"
namespace JBro::ScriptApiCatalog2D::Internal
{
void AddServices(ApiCatalog& catalog)
{
    {
    auto& type = AddType(catalog, "TimeService", "JBro::Service::TimeService", ApiTypeKind::Service);
    {
        auto member = DeclareFunction<static_cast<Float (Service::TimeService::*)() const>(&Service::TimeService::DeltaTime)>("DeltaTime", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Service::TimeService::*)() const>(&Service::TimeService::UnscaledDeltaTime)>("UnscaledDeltaTime", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Service::TimeService::*)() const>(&Service::TimeService::FixedDeltaTime)>("FixedDeltaTime", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<double (Service::TimeService::*)() const>(&Service::TimeService::Time)>("Time", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<double (Service::TimeService::*)() const>(&Service::TimeService::UnscaledTime)>("UnscaledTime", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<double (Service::TimeService::*)() const>(&Service::TimeService::FixedTime)>("FixedTime", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<UInt64 (Service::TimeService::*)() const>(&Service::TimeService::FrameCount)>("FrameCount", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Service::TimeService::*)() const>(&Service::TimeService::TimeScale)>("TimeScale", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::TimeService::*)(Float) const>(&Service::TimeService::SetTimeScale)>("SetTimeScale", Parameters(In("scale")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::TimeService::*)() const>(&Service::TimeService::IsInFixedStep)>("IsInFixedStep", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Service::TimeService::*)() const>(&Service::TimeService::FixedStepAlpha)>("FixedStepAlpha", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::TimeService::*)() const>(&Service::TimeService::IsPaused)>("IsPaused", Parameters());
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "RandomService", "JBro::Service::RandomService", ApiTypeKind::Service);
    {
        auto member = DeclareFunction<static_cast<JBro::UInt32 (Service::RandomService::*)() const>(&Service::RandomService::UInt32)>("UInt32", Parameters());
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Service::RandomService::*)() const>(&Service::RandomService::Value)>("Value", Parameters());
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Int32 (Service::RandomService::*)(Int32,Int32) const>(&Service::RandomService::Range)>("Range", Parameters(In("min"), In("max")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Service::RandomService::*)(Float,Float) const>(&Service::RandomService::Range)>("Range", Parameters(In("min"), In("max")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::RandomService::*)(Float) const>(&Service::RandomService::Chance)>("Chance", Parameters(In("probability")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<UInt64 (Service::RandomService::*)() const>(&Service::RandomService::GetSeed)>("GetSeed", Parameters());
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::RandomService::*)(UInt64) const>(&Service::RandomService::SetSeed)>("SetSeed", Parameters(In("seed")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<RandomState (Service::RandomService::*)() const>(&Service::RandomService::GetState)>("GetState", Parameters());
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::RandomService::*)(const RandomState&) const>(&Service::RandomService::SetState)>("SetState", Parameters(In("state")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<RandomStream (Service::RandomService::*)() const>(&Service::RandomService::MakeStream)>("MakeStream", Parameters());
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "ObjectsService", "JBro::Service::ObjectService", ApiTypeKind::Service);
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Service::ObjectService::*)(const char*,Handle::GameObject,Handle::Layer) const>(&Service::ObjectService::Create)>("Create", Parameters(In("name"), In("parent", "null"), In("layer", "null")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Service::ObjectService::*)(const char*) const>(&Service::ObjectService::Find)>("Find", Parameters(In("name")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Service::ObjectService::*)(NameId) const>(&Service::ObjectService::Find)>("Find", Parameters(In("name")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::ObjectService::*)(const char*,Array<Handle::GameObject>&) const>(&Service::ObjectService::FindAll)>("FindAll", Parameters(In("name"), Out("results")));
        member.Type = TypeName<Array<Handle::GameObject>>();
        member.Projection = ApiProjectionKind::CollectionReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = false;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Service::ObjectService::*)(const char*) const>(&Service::ObjectService::FindWithTag)>("FindWithTag", Parameters(In("tag")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::ObjectService::*)(const char*,Array<Handle::GameObject>&) const>(&Service::ObjectService::FindAllWithTag)>("FindAllWithTag", Parameters(In("tag"), Out("results")));
        member.Type = TypeName<Array<Handle::GameObject>>();
        member.Projection = ApiProjectionKind::CollectionReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = false;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::Layer (Service::ObjectService::*)(const char*) const>(&Service::ObjectService::GetLayer)>("GetLayer", Parameters(In("name")));
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "AudioService", "JBro::Service::AudioService", ApiTypeKind::Service);
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)(AssetHandle,Float,Float) const>(&Service::AudioService::PlayOneShot)>("PlayOneShot", Parameters(In("clip"), In("volume", "1.0f"), In("pitch", "1.0f")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)(AssetHandle,const char*,Float,Float) const>(&Service::AudioService::PlayOneShot)>("PlayOneShot", Parameters(In("clip"), In("bus"), In("volume", "1.0f"), In("pitch", "1.0f")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)(AssetHandle,AudioBusName,Float,Float) const>(&Service::AudioService::PlayOneShot)>("PlayOneShot", Parameters(In("clip"), In("bus"), In("volume", "1.0f"), In("pitch", "1.0f")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)(AssetHandle,Float,Float,Float,Float) const>(&Service::AudioService::PlayOneShotAt)>("PlayOneShotAt", Parameters(In("clip"), In("x"), In("y"), In("z", "0.0f"), In("volume", "1.0f")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)(AssetHandle,AudioBusName,Float,Float,Float,Float) const>(&Service::AudioService::PlayOneShotAt)>("PlayOneShotAt", Parameters(In("clip"), In("bus"), In("x"), In("y"), In("z"), In("volume")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)(Handle::AudioSource) const>(&Service::AudioService::Play)>("Play", Parameters(In("source")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)(Handle::AudioSource,Float) const>(&Service::AudioService::Stop)>("Stop", Parameters(In("source"), In("fadeOutSeconds", "0.0f")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)(Handle::AudioSource) const>(&Service::AudioService::Pause)>("Pause", Parameters(In("source")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)(Handle::AudioSource) const>(&Service::AudioService::Resume)>("Resume", Parameters(In("source")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::AudioService::*)(Handle::AudioSource) const>(&Service::AudioService::IsPlaying)>("IsPlaying", Parameters(In("source")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<double (Service::AudioService::*)(Handle::AudioSource) const>(&Service::AudioService::GetTime)>("GetTime", Parameters(In("source")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)(const char*,Float) const>(&Service::AudioService::SetBusVolume)>("SetBusVolume", Parameters(In("bus"), In("volume")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)(AudioBusName,Float) const>(&Service::AudioService::SetBusVolume)>("SetBusVolume", Parameters(In("bus"), In("volume")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Service::AudioService::*)(const char*) const>(&Service::AudioService::GetBusVolume)>("GetBusVolume", Parameters(In("bus")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Service::AudioService::*)(AudioBusName) const>(&Service::AudioService::GetBusVolume)>("GetBusVolume", Parameters(In("bus")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)(const char*,Bool) const>(&Service::AudioService::SetBusMuted)>("SetBusMuted", Parameters(In("bus"), In("muted")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)(AudioBusName,Bool) const>(&Service::AudioService::SetBusMuted)>("SetBusMuted", Parameters(In("bus"), In("muted")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::AudioService::*)(AudioBusName) const>(&Service::AudioService::IsBusMuted)>("IsBusMuted", Parameters(In("bus")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)(AudioBusName,const AudioBusEffects&) const>(&Service::AudioService::SetBusEffects)>("SetBusEffects", Parameters(In("bus"), In("effects")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<AudioBusEffects (Service::AudioService::*)(AudioBusName) const>(&Service::AudioService::GetBusEffects)>("GetBusEffects", Parameters(In("bus")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)(const char*,Float) const>(&Service::AudioService::SetBusLowPass)>("SetBusLowPass", Parameters(In("bus"), In("cutoffHz")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)(const char*,Float,Float) const>(&Service::AudioService::FadeBusVolume)>("FadeBusVolume", Parameters(In("bus"), In("volume"), In("seconds")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)(AudioBusName,Float,Float) const>(&Service::AudioService::FadeBusVolume)>("FadeBusVolume", Parameters(In("bus"), In("volume"), In("seconds")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)() const>(&Service::AudioService::StopAll)>("StopAll", Parameters());
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<UInt32 (Service::AudioService::*)() const>(&Service::AudioService::GetOutputDeviceCount)>("GetOutputDeviceCount", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<const char* (Service::AudioService::*)(UInt32) const>(&Service::AudioService::GetOutputDeviceName)>("GetOutputDeviceName", Parameters(In("index")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<const char* (Service::AudioService::*)() const>(&Service::AudioService::GetOutputDevice)>("GetOutputDevice", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::AudioService::*)(const char*) const>(&Service::AudioService::SetOutputDevice)>("SetOutputDevice", Parameters(In("name")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::AudioService::*)() const>(&Service::AudioService::IsWaitingForUserGesture)>("IsWaitingForUserGesture", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)(Bool) const>(&Service::AudioService::SetMuteWhenUnfocused)>("SetMuteWhenUnfocused", Parameters(In("mute")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::AudioService::*)() const>(&Service::AudioService::IsMuteWhenUnfocused)>("IsMuteWhenUnfocused", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::AudioService::*)(Float) const>(&Service::AudioService::SetMasterVolume)>("SetMasterVolume", Parameters(In("volume")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Service::AudioService::*)() const>(&Service::AudioService::GetMasterVolume)>("GetMasterVolume", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<UInt32 (Service::AudioService::*)() const>(&Service::AudioService::GetBusCount)>("GetBusCount", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<const char* (Service::AudioService::*)(UInt32) const>(&Service::AudioService::GetBusName)>("GetBusName", Parameters(In("index")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<double (Service::AudioService::*)() const>(&Service::AudioService::GetAudioTime)>("GetAudioTime", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Service::AudioService::*)() const>(&Service::AudioService::GetOutputLatency)>("GetOutputLatency", Parameters());
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "InputService", "JBro::Service::InputService", ApiTypeKind::Service);
    {
        auto member = DeclareFunction<static_cast<const InputView& (Service::InputService::*)() const>(&Service::InputService::GetView)>("GetView", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<const KeyboardState& (Service::InputService::*)() const>(&Service::InputService::Keyboard)>("Keyboard", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<const MouseState& (Service::InputService::*)() const>(&Service::InputService::Mouse)>("Mouse", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<const GamepadState& (Service::InputService::*)(UInt32) const>(&Service::InputService::Gamepad)>("Gamepad", Parameters(In("slot")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::InputService::*)(UInt32,Float,Float,Float) const>(&Service::InputService::SetGamepadVibration)>("SetGamepadVibration", Parameters(In("slot"), In("low"), In("high"), In("seconds", "0.0f")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::InputService::*)(UInt32) const>(&Service::InputService::StopGamepadVibration)>("StopGamepadVibration", Parameters(In("slot")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::InputService::*)(Float,Float) const>(&Service::InputService::SetGamepadDeadzones)>("SetGamepadDeadzones", Parameters(In("stick"), In("trigger")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<const TouchState& (Service::InputService::*)() const>(&Service::InputService::Touch)>("Touch", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::InputService::*)(UInt32,Float,Float,TouchPhase) const>(&Service::InputService::InjectTouch)>("InjectTouch", Parameters(In("id"), In("x"), In("y"), In("phase")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::InputService::*)(NameId) const>(&Service::InputService::EnableActionSet)>("EnableActionSet", Parameters(In("set")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::InputService::*)(NameId) const>(&Service::InputService::DisableActionSet)>("DisableActionSet", Parameters(In("set")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::InputService::*)(NameId) const>(&Service::InputService::IsActionSetEnabled)>("IsActionSetEnabled", Parameters(In("set")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<UInt32 (Service::InputService::*)(InputActionId) const>(&Service::InputService::GetActionBindingCount)>("GetActionBindingCount", Parameters(In("action")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::InputService::*)(InputActionId,UInt32,InputBinding&) const>(&Service::InputService::GetActionBinding)>("GetActionBinding", Parameters(In("action"), In("index"), Out("out")));
        member.Type = TypeName<InputBinding>() + "?";
        member.Projection = ApiProjectionKind::NullableReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::InputService::*)(InputActionId,UInt32,const InputBinding&) const>(&Service::InputService::SetActionBinding)>("SetActionBinding", Parameters(In("action"), In("index"), In("binding")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::InputService::*)(InputActionId,UInt32) const>(&Service::InputService::RemoveActionBinding)>("RemoveActionBinding", Parameters(In("action"), In("index")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::InputService::*)(InputActionId) const>(&Service::InputService::ResetActionBindings)>("ResetActionBindings", Parameters(In("action")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::InputService::*)() const>(&Service::InputService::ResetAllActionBindings)>("ResetAllActionBindings", Parameters());
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<InputCaptureResult (Service::InputService::*)(InputBinding&) const>(&Service::InputService::CaptureBinding)>("CaptureBinding", Parameters(Out("out")));
        member.Type = "InputCapture";
        member.Projection = ApiProjectionKind::ValueReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = false;
        member.NativeReturnField = "result";
        member.Description = "Preserves native capture status and output binding in separate result fields.";
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::InputService::*)(String&) const>(&Service::InputService::WriteBindingOverrides)>("WriteBindingOverrides", Parameters(Out("out")));
        member.Type = TypeName<String>() + "?";
        member.Projection = ApiProjectionKind::NullableReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = true;
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::InputService::*)(const String&) const>(&Service::InputService::ReadBindingOverrides)>("ReadBindingOverrides", Parameters(In("text")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "SaveService", "JBro::Service::SaveService", ApiTypeKind::Service);
    {
        auto member = DeclareFunction<static_cast<Bool (Service::SaveService::*)() const>(&Service::SaveService::IsReady)>("IsReady", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::SaveService::*)(const char*,const void*,std::size_t) const>(&Service::SaveService::WriteBytes)>("WriteBytes", Parameters(In("slot"), In("data"), Count("size")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::SaveService::*)(const char*,const String&) const>(&Service::SaveService::WriteText)>("WriteText", Parameters(In("slot"), In("text")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::SaveService::*)(const char*,Array<std::byte>&) const>(&Service::SaveService::ReadBytes)>("ReadBytes", Parameters(In("slot"), Out("out")));
        member.Type = TypeName<Array<std::byte>>() + "?";
        member.Projection = ApiProjectionKind::NullableReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::SaveService::*)(const char*,String&) const>(&Service::SaveService::ReadText)>("ReadText", Parameters(In("slot"), Out("out")));
        member.Type = TypeName<String>() + "?";
        member.Projection = ApiProjectionKind::NullableReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::SaveService::*)(const char*) const>(&Service::SaveService::Exists)>("Exists", Parameters(In("slot")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::SaveService::*)(const char*) const>(&Service::SaveService::Remove)>("Remove", Parameters(In("slot")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::SaveService::*)() const>(&Service::SaveService::Flush)>("Flush", Parameters());
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "LocalizationService", "JBro::Service::LocalizationService", ApiTypeKind::Service);
    {
        auto member = DeclareFunction<static_cast<Bool (Service::LocalizationService::*)() const>(&Service::LocalizationService::IsReady)>("IsReady", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::LocalizationService::*)(const char*) const>(&Service::LocalizationService::SetLocale)>("SetLocale", Parameters(In("locale")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<String (Service::LocalizationService::*)() const>(&Service::LocalizationService::GetLocale)>("GetLocale", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<String (Service::LocalizationService::*)(const char*) const>(&Service::LocalizationService::GetText)>("GetText", Parameters(In("key")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::LocalizationService::*)(const char*,String&) const>(&Service::LocalizationService::TryGetText)>("TryGetText", Parameters(In("key"), Out("out")));
        member.Type = TypeName<String>() + "?";
        member.Projection = ApiProjectionKind::NullableReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<UInt32 (Service::LocalizationService::*)() const>(&Service::LocalizationService::GetRevision)>("GetRevision", Parameters());
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "NetworkService", "JBro::Service::NetworkService", ApiTypeKind::Service);
    {
        auto member = DeclareFunction<static_cast<Bool (Service::NetworkService::*)(Network::ConnectionId,Network::MessageId,const void*,UInt32,Network::NetChannel) const>(&Service::NetworkService::Send)>("Send", Parameters(In("connection"), In("messageId"), In("data"), Count("size"), In("channel", "NetChannel::ReliableOrdered")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::NetworkService::*)(Network::MessageId,const void*,UInt32,Network::NetChannel) const>(&Service::NetworkService::Broadcast)>("Broadcast", Parameters(In("messageId"), In("data"), Count("size"), In("channel", "NetChannel::ReliableOrdered")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<UInt32 (Service::NetworkService::*)(Network::MessageView*,UInt32) const>(&Service::NetworkService::TakeMessages)>("TakeMessages", Parameters(Out("messages"), Count("capacity")));
        member.Type = TypeName<Array<Network::MessageView>>();
        member.Projection = ApiProjectionKind::CollectionReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = false;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Network::NetworkObjectId (Service::NetworkService::*)(InstanceId) const>(&Service::NetworkService::FindNetworkId)>("FindNetworkId", Parameters(In("object")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<InstanceId (Service::NetworkService::*)(Network::NetworkObjectId) const>(&Service::NetworkService::FindLocalObject)>("FindLocalObject", Parameters(In("id")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::NetworkService::*)(InstanceId) const>(&Service::NetworkService::HasAuthority)>("HasAuthority", Parameters(In("object")));
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "NetworkSessionService", "JBro::Service::NetworkSessionService", ApiTypeKind::Service);
    {
        auto member = DeclareFunction<static_cast<Bool (Service::NetworkSessionService::*)(std::uint16_t) const>(&Service::NetworkSessionService::StartServer)>("StartServer", Parameters(In("port")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::NetworkSessionService::*)(const char*,std::uint16_t) const>(&Service::NetworkSessionService::Connect)>("Connect", Parameters(In("host"), In("port")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::NetworkSessionService::*)() const>(&Service::NetworkSessionService::Disconnect)>("Disconnect", Parameters());
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Network::NetworkRole (Service::NetworkSessionService::*)() const>(&Service::NetworkSessionService::GetRole)>("GetRole", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::NetworkSessionService::*)() const>(&Service::NetworkSessionService::IsConnected)>("IsConnected", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<UInt32 (Service::NetworkSessionService::*)() const>(&Service::NetworkSessionService::GetConnectionCount)>("GetConnectionCount", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Network::ConnectionId (Service::NetworkSessionService::*)(UInt32) const>(&Service::NetworkSessionService::GetConnectionAt)>("GetConnectionAt", Parameters(In("index")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<double (Service::NetworkSessionService::*)(Network::ConnectionId) const>(&Service::NetworkSessionService::GetRoundTripMilliseconds)>("GetRoundTripMilliseconds", Parameters(In("connection")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<double (Service::NetworkSessionService::*)(Network::ConnectionId) const>(&Service::NetworkSessionService::GetUdpLossRate)>("GetUdpLossRate", Parameters(In("connection")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<UInt32 (Service::NetworkSessionService::*)(Network::NetworkEvent*,UInt32) const>(&Service::NetworkSessionService::TakeEvents)>("TakeEvents", Parameters(Out("events"), Count("capacity")));
        member.Type = TypeName<Array<Network::NetworkEvent>>();
        member.Projection = ApiProjectionKind::CollectionReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = false;
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "Physics2DService", "JBro::Service::Physics2DService", ApiTypeKind::Service);
    {
        auto member = DeclareFunction<static_cast<Bool (Service::Physics2DService::*)(Vector2,Vector2,Float,RaycastHit2D&,UInt32) const>(&Service::Physics2DService::Raycast)>("Raycast", Parameters(In("origin"), In("direction"), In("distance"), Out("hit"), In("layerMask", "AllPhysicsLayers")));
        member.Type = TypeName<RaycastHit2D>() + "?";
        member.Projection = ApiProjectionKind::NullableReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::Physics2DService::*)(Vector2,Vector2,Float,Array<RaycastHit2D>&,UInt32) const>(&Service::Physics2DService::RaycastAll)>("RaycastAll", Parameters(In("origin"), In("direction"), In("distance"), Out("hits"), In("layerMask", "AllPhysicsLayers")));
        member.Type = TypeName<Array<RaycastHit2D>>();
        member.Projection = ApiProjectionKind::CollectionReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = false;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::Physics2DService::*)(const Rect&,Array<Handle::GameObject>&,UInt32) const>(&Service::Physics2DService::OverlapBox)>("OverlapBox", Parameters(In("area"), Out("results"), In("layerMask", "AllPhysicsLayers")));
        member.Type = TypeName<Array<Handle::GameObject>>();
        member.Projection = ApiProjectionKind::CollectionReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = false;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Service::Physics2DService::*)(Vector2,UInt32) const>(&Service::Physics2DService::OverlapPoint)>("OverlapPoint", Parameters(In("point"), In("layerMask", "AllPhysicsLayers")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::Physics2DService::*)(Vector2,Float,Array<Handle::GameObject>&,UInt32) const>(&Service::Physics2DService::OverlapCircle)>("OverlapCircle", Parameters(In("center"), In("radius"), Out("results"), In("layerMask", "AllPhysicsLayers")));
        member.Type = TypeName<Array<Handle::GameObject>>();
        member.Projection = ApiProjectionKind::CollectionReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = false;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::Physics2DService::*)(Vector2,Float,Vector2,Float,RaycastHit2D&,UInt32) const>(&Service::Physics2DService::CircleCast)>("CircleCast", Parameters(In("origin"), In("radius"), In("direction"), In("distance"), Out("hit"), In("layerMask", "AllPhysicsLayers")));
        member.Type = TypeName<RaycastHit2D>() + "?";
        member.Projection = ApiProjectionKind::NullableReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::Physics2DService::*)(Vector2,Vector2,Float,Vector2,Float,RaycastHit2D&,UInt32) const>(&Service::Physics2DService::BoxCast)>("BoxCast", Parameters(In("center"), In("halfExtents"), In("angle"), In("direction"), In("distance"), Out("hit"), In("layerMask", "AllPhysicsLayers")));
        member.Type = TypeName<RaycastHit2D>() + "?";
        member.Projection = ApiProjectionKind::NullableReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = true;
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "Text2DService", "JBro::Service::Text2DService", ApiTypeKind::Service);
    {
        auto member = DeclareFunction<static_cast<Bool (Service::TextServiceBase<Handle::Text2D, Service::Text2DService>::*)(Handle::Text2D,const char*,UInt32) const>(&Service::TextServiceBase<Handle::Text2D, Service::Text2DService>::SetText)>("SetText", Parameters(In("text"), In("utf8"), In("length")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::TextServiceBase<Handle::Text2D, Service::Text2DService>::*)(Handle::Text2D,const char*) const>(&Service::TextServiceBase<Handle::Text2D, Service::Text2DService>::SetText)>("SetText", Parameters(In("text"), In("utf8")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::TextServiceBase<Handle::Text2D, Service::Text2DService>::*)(Handle::Text2D,const char*) const>(&Service::TextServiceBase<Handle::Text2D, Service::Text2DService>::SetTextKey)>("SetTextKey", Parameters(In("text"), In("key")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<UInt32 (Service::TextServiceBase<Handle::Text2D, Service::Text2DService>::*)(Handle::Text2D) const>(&Service::TextServiceBase<Handle::Text2D, Service::Text2DService>::GetTextLength)>("GetTextLength", Parameters(In("text")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<UInt32 (Service::TextServiceBase<Handle::Text2D, Service::Text2DService>::*)(Handle::Text2D,char*,UInt32) const>(&Service::TextServiceBase<Handle::Text2D, Service::Text2DService>::CopyText)>("CopyText", Parameters(In("text"), Out("buffer"), Count("capacity")));
        member.Type = TypeName<Array<char>>();
        member.Projection = ApiProjectionKind::CollectionReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = false;
        type.Members.Add(std::move(member));
    }
    type.Members.Last().Type = "String";
    type.Members.Last().Projection = ApiProjectionKind::StringReturn;
    }
    {
    auto& type = AddType(catalog, "Screen2DService", "JBro::Service::Screen2DService", ApiTypeKind::Service);
    {
        auto member = DeclareFunction<static_cast<Bool (Service::Screen2DService::*)(Vector2,Handle::GameObject,Vector2&) const>(&Service::Screen2DService::ScreenToLayer)>("ScreenToLayer", Parameters(In("pixel"), In("object"), Out("point")));
        member.Type = TypeName<Vector2>() + "?";
        member.Projection = ApiProjectionKind::NullableReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::Screen2DService::*)(Vector2,Handle::GameObject,Vector2&) const>(&Service::Screen2DService::LayerToScreen)>("LayerToScreen", Parameters(In("point"), In("object"), Out("pixel")));
        member.Type = TypeName<Vector2>() + "?";
        member.Projection = ApiProjectionKind::NullableReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Service::Screen2DService::*)() const>(&Service::Screen2DService::IsPointerOverButton)>("IsPointerOverButton", Parameters());
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "DebugDraw2DService", "JBro::Service::DebugDraw2DService", ApiTypeKind::Service);
    {
        auto member = DeclareFunction<static_cast<void (Service::DebugDraw2DService::*)(Vector2,Vector2,const Color&,Float,Float) const>(&Service::DebugDraw2DService::Line)>("Line", Parameters(In("from"), In("to"), In("color", "Color(1,1,1,1)"), In("duration", "0.0f"), In("thickness", "1.0f")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::DebugDraw2DService::*)(Vector2,Vector2,const Color&,Float,Float) const>(&Service::DebugDraw2DService::Ray)>("Ray", Parameters(In("origin"), In("direction"), In("color", "Color(1,1,1,1)"), In("duration", "0.0f"), In("thickness", "1.0f")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::DebugDraw2DService::*)(Vector2,Vector2,const Color&,Float,Float) const>(&Service::DebugDraw2DService::Arrow)>("Arrow", Parameters(In("from"), In("to"), In("color", "Color(1,1,1,1)"), In("duration", "0.0f"), In("thickness", "1.0f")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::DebugDraw2DService::*)(Vector2,Vector2,Float,const Color&,Float,Float) const>(&Service::DebugDraw2DService::Rect)>("Rect", Parameters(In("center"), In("size"), In("angle", "0.0f"), In("color", "Color(1,1,1,1)"), In("duration", "0.0f"), In("thickness", "1.0f")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::DebugDraw2DService::*)(Vector2,Float,const Color&,Float,Float) const>(&Service::DebugDraw2DService::Circle)>("Circle", Parameters(In("center"), In("radius"), In("color", "Color(1,1,1,1)"), In("duration", "0.0f"), In("thickness", "1.0f")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::DebugDraw2DService::*)(const Vector2*,UInt32,Bool,const Color&,Float,Float) const>(&Service::DebugDraw2DService::Polygon)>("Polygon", Parameters(In("points"), Count("count"), In("closed", "true"), In("color", "Color(1,1,1,1)"), In("duration", "0.0f"), In("thickness", "1.0f")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Service::DebugDraw2DService::*)(Vector2,Float,const Color&,Float,Float) const>(&Service::DebugDraw2DService::Cross)>("Cross", Parameters(In("at"), In("size"), In("color", "Color(1,1,1,1)"), In("duration", "0.0f"), In("thickness", "1.0f")));
        type.Members.Add(std::move(member));
    }
    }
    catalog.Globals.Add({"Time", "TimeService", "GetServiceContext().Time"});
    catalog.Globals.Add({"Random", "RandomService", "GetServiceContext().Random"});
    catalog.Globals.Add({"Objects", "ObjectsService", "GetServiceContext().Objects"});
    catalog.Globals.Add({"Audio", "AudioService", "GetAudioServices().Audio"});
    catalog.Globals.Add({"Input", "InputService", "GetInputServices().Input"});
    catalog.Globals.Add({"Save", "SaveService", "GetSaveServices().Save"});
    catalog.Globals.Add({"Localization", "LocalizationService", "GetLocalizationServices().Localization"});
    catalog.Globals.Add({"Network", "NetworkService", "GetNetworkServices().Network"});
    catalog.Globals.Add({"Session", "NetworkSessionService", "GetNetworkServices().Session"});
    catalog.Globals.Add({"Physics", "Physics2DService", "GetFramework2DServices().Physics2D"});
    catalog.Globals.Add({"Text", "Text2DService", "GetFramework2DServices().Text2D"});
    catalog.Globals.Add({"Screen", "Screen2DService", "GetFramework2DServices().Screen2D"});
    catalog.Globals.Add({"DebugDraw", "DebugDraw2DService", "GetFramework2DServices().DebugDraw"});
}
}

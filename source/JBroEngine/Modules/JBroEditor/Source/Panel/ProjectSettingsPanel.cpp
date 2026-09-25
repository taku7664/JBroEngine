#include "ProjectSettingsPanel.h"

#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Core/Log.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/Editor/Widget/PathField.h>
#include <JBro/Editor/Widget/Scalar.h>
#include <JBro/Editor/Widget/TextField.h>

#include <imgui.h>

#include <cstdio>

namespace JBro
{
    namespace
    {
        // 해상도의 한계다. 0 은 그릴 화면이 없다는 뜻이고, 위쪽은 사람이 실수로
        // 자릿수를 하나 더 치는 것을 막는 값이다.
        constexpr int MinResolution = 16;
        constexpr int MaxResolution = 16384;
    }

    const char* ProjectSettingsPanel::GetTitle() const
    {
        return "ProjectSettings";
    }

    const char* ProjectSettingsPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::PanelProjectSettings, "Project Settings");
    }

    bool ProjectSettingsPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        // 늘 보는 창이 아니다. 창 메뉴에서 열어 본다.
        SetOpen(false);
        return true;
    }

    void ProjectSettingsPanel::Reload()
    {
        m_draft = m_editor->GetProjectFile();
        Widget::JoinLines(m_draft.assetIgnorePatterns, m_ignorePatterns);
        m_loadedPath = m_editor->GetProjectFilePath();
        m_loaded = true;
        m_message.clear();
        m_messageIsError = false;
        m_audioDevicesListed = false;
    }

    void ProjectSettingsPanel::DrawPathValue(const char* id, String& value, const char* filterName,
        const char* filterPattern, bool assetRelative)
    {
        const Widget::PathFieldResult result = Widget::PathField(id, value).Draw();
        if (false == result.browse)
        {
            return;
        }
        PathBrowseRequest request;
        request.folder = filterPattern == nullptr;
        request.filterName = filterName != nullptr ? filterName : "";
        request.filterPattern = filterPattern != nullptr ? filterPattern : "";
        request.relative = true;
        if (assetRelative)
        {
            request.baseFolder = m_editor->GetAssetRoot();
        }
        // 편집본의 그 칸에 넣는다. 패널은 에디터와 수명을 같이하고, 편집본은 다시 읽어도 자리가 그대로다.
        request.deliver = [](void* user, const String& path) { *static_cast<String*>(user) = path; };
        request.user = &value;
        m_editor->RequestBrowsePath(request);
    }

    void ProjectSettingsPanel::OnDraw()
    {
        if (m_editor == nullptr)
        {
            return;
        }
        const String& path = m_editor->GetProjectFilePath();
        if (path.empty())
        {
            // 파일로 열지 않은 프로젝트다(테스트의 것이 그렇다). 고칠 파일이 없다.
            Widget::HintTextF("%s",
                Loc::TextOr(LocKeys::ProjectSettingsNoFile, "this project has no file to edit"));
            m_loaded = false;
            return;
        }
        // 프로젝트가 바뀌었으면 다시 읽는다. 앞 프로젝트의 값을 다음 파일에 쓰면 안 된다.
        if (false == m_loaded || m_loadedPath != path)
        {
            Reload();
        }

        Widget::SectionHeader(
            Loc::TextOr(LocKeys::ProjectSettingsGeneral, "General")).Draw();
        {
            Widget::FormLayout layout("##general");
            // 엔진 판과 차원은 **보여만 준다.** 판은 런처가 어느 설치를 띄울지 고르는 값이고,
            // 차원은 열려 있는 프로젝트에서 바꾸면 지금 선 프레임워크와 어긋난다.
            layout.Row(
                [] { Widget::Text("EngineVersion"); },
                [&]
                {
                    Widget::DisableScope disabled(true);
                    Widget::Text(m_draft.engineVersion.c_str());
                });
            layout.Row(
                [] { Widget::Text("Framework"); },
                [&]
                {
                    Widget::DisableScope disabled(true);
                    Widget::Text(
                        m_draft.framework == FrameworkKind::Framework3D ? "3D" : "2D");
                });
            layout.Row(
                [] { Widget::Text("ResolutionWidth"); },
                [&]
                {
                    int value = static_cast<int>(m_draft.resolutionWidth);
                    if (Widget::DragInt("##width").Range(MinResolution, MaxResolution).Draw(value))
                    {
                        m_draft.resolutionWidth = static_cast<std::uint32_t>(value);
                    }
                });
            layout.Row(
                [] { Widget::Text("ResolutionHeight"); },
                [&]
                {
                    int value = static_cast<int>(m_draft.resolutionHeight);
                    if (Widget::DragInt("##height").Range(MinResolution, MaxResolution).Draw(value))
                    {
                        m_draft.resolutionHeight = static_cast<std::uint32_t>(value);
                    }
                });
            layout.Row(
                [] { Widget::Text("TextureFilter"); },
                [&]
                {
                    // 둘뿐이다(`Default` 는 텍스처의 임포트 옵션에만 있다, D-117).
                    bool linear = m_draft.textureFilter == TextureFilter::Linear;
                    if (Widget::Checkbox("##filter", linear))
                    {
                        m_draft.textureFilter =
                            linear ? TextureFilter::Linear : TextureFilter::Nearest;
                    }
                    Widget::HoveredTooltip(Loc::TextOr(
                        LocKeys::ProjectSettingsLinearFilter,
                        "linear sampling; off is nearest, which is the pixel-art default"));
                });
            layout.Row(
                [] { Widget::Text("DebugModeEnabled"); },
                [&] { Widget::Checkbox("##debug", m_draft.debugModeEnabled); });
        }

        // **에디터 언어**(D-146). 기존 엔진도 설정 창에서 골랐고, 고른 값은 프로젝트에 남는다.
        // 저장 단추를 기다리지 않고 **고르는 즉시 바뀐다** - 글자가 바뀌는 것을 눈으로 보고
        // 고르는 일이라, 저장한 뒤에야 바뀌면 무엇을 고른 것인지 알 수 없다.
        {
            const Array<String> locales = m_editor->GetAvailableLocales();
            Widget::FormLayout layout("##localization");
            layout.Row(
                Widget::FieldLabel(Loc::TextOr(LocKeys::ProjectSettingsLanguage, "Language")),
                [&]() {
                    if (locales.IsEmpty())
                    {
                        Widget::HintText(Loc::TextOr(LocKeys::ProjectSettingsNoLanguages,
                            "no language files were found"));
                        return;
                    }
                    // 고르는 목록은 공용 콤보다(§11.1). 패널이 `BeginCombo` 로 직접 그리지 않는다.
                    Array<const char*> names;
                    int current = -1;
                    for (std::size_t index = 0; index < locales.Size(); ++index)
                    {
                        names.Add(locales[index].c_str());
                        if (locales[index] == m_editor->GetEditorLocale())
                        {
                            current = static_cast<int>(index);
                        }
                    }
                    if (Widget::FilterCombo("##language",
                            ArrayView<const char* const>(names.Data(), names.Size()), current)
                            .ShowFilter(false)
                            .Draw()
                        && current >= 0)
                    {
                        m_editor->SetEditorLocale(locales[static_cast<std::size_t>(current)].c_str());
                        m_draft.editorLocale = m_editor->GetEditorLocale();
                    }
                });
        }

        Widget::SectionHeader(
            Loc::TextOr(LocKeys::ProjectSettingsPaths, "Paths")).SpacingBefore().Draw();
        {
            Widget::FormLayout layout("##paths");
            const char* canvasFilter = Loc::TextOr(LocKeys::DialogCanvasFilter, "JBro canvas file");
            layout.Row(
                [] { Widget::Text("AssetDirectory"); },
                [&] { DrawPathValue("##assets", m_draft.assetDirectory); });
            layout.Row(
                [] { Widget::Text("ScriptSourceDirectory"); },
                [&] { DrawPathValue("##scriptSource", m_draft.scriptSourceDirectory); });
            layout.Row(
                [] { Widget::Text("ScriptOutputLibraryPath"); },
                [&] { DrawPathValue("##scriptOut", m_draft.scriptOutputLibraryPath, "DLL", "*.dll"); });
            layout.Row(
                [] { Widget::Text("LastOpenedCanvasPath"); },
                [&] { DrawPathValue("##lastCanvas", m_draft.lastOpenedCanvasPath, canvasFilter, "*.jcanvas", true); });
            // **스캔과 파일 감시가 건너뛸 이름들**(D-189, 기존 프로젝트 설정의 에셋 감시 칸).
            // 프로젝트 파일에는 있는데 고칠 길이 없어, 손으로 파일을 열어야 했다.
            layout.Row(
                [] { Widget::Text("AssetIgnorePatterns"); },
                [&] {
                    if (Widget::NameListEdit("##ignore", m_ignorePatterns))
                    {
                        Widget::SplitLines(m_ignorePatterns, m_draft.assetIgnorePatterns);
                    }
                    Widget::HoveredTooltip(Loc::TextOr(LocKeys::ProjectSettingsIgnorePatterns,
                        "one pattern a line; the scan and the watcher skip what matches"));
                });
        }

        // **오디오 버스**(D-197, 기존 설정 창의 오디오 갈래). 이름·시작 음량·이펙트 사슬(D-202)이다. Master 는 늘 있으므로
        // 목록에 없다. 버스마다 제 표를 연다 - 접는 이펙트 마디가 표 사이에 서야 해서다.
        Widget::SectionHeader(
            Loc::TextOr(LocKeys::ProjectSettingsAudio, "Audio")).SpacingBefore().Draw();
        // 출력 장치와 포커스 정책(D-203). 장치 목록은 창을 열 때 한 번 읽는다.
        if (false == m_audioDevicesListed)
        {
            const std::uint32_t found = m_editor->EnumerateAudioOutputs(m_audioDevices, MaxAudioDevices);
            m_audioDeviceCount = found < MaxAudioDevices ? found : MaxAudioDevices;
            m_audioDevicesListed = true;
        }
        {
            Widget::FormLayout device("##audioDevice");
            device.Row([] { Widget::Text("AudioOutputDevice"); },
                [&] {
                    m_busChoices.Clear();
                    m_busChoices.Add(Loc::TextOr(LocKeys::ProjectSettingsAudioDefaultDevice, "System default"));
                    int current = m_draft.audioOutputDevice.empty() ? 0 : -1;
                    for (std::uint32_t index = 0; index < m_audioDeviceCount; ++index)
                    {
                        m_busChoices.Add(m_audioDevices[index].name);
                        if (m_draft.audioOutputDevice == m_audioDevices[index].name)
                        {
                            current = static_cast<int>(index) + 1;
                        }
                    }
                    // 이 기계에 없는 이름이 적혀 있으면 그 이름을 그대로 보인다 - 지우지 않는다.
                    if (Widget::FilterCombo("##device", {m_busChoices.Data(), m_busChoices.Size()}, current)
                            .EmptyText(m_draft.audioOutputDevice.c_str())
                            .Draw())
                    {
                        m_draft.audioOutputDevice = current <= 0 ? String() : String(m_busChoices[static_cast<std::size_t>(current)]);
                    }
                    Widget::HoveredTooltip(Loc::TextOr(LocKeys::ProjectSettingsAudioDeviceHelp,
                        "On a computer without this device the system default is used"));
                    ImGui::SameLine();
                    if (Widget::Button(Loc::TextOr(LocKeys::ProjectSettingsAudioRefreshDevices, "Refresh")))
                    {
                        m_audioDevicesListed = false;
                    }
                });
            device.Row([] { Widget::Text("AudioMuteWhenUnfocused"); },
                [&] {
                    Widget::Checkbox("##muteUnfocused", m_draft.audioMuteWhenUnfocused);
                    Widget::HoveredTooltip(Loc::TextOr(LocKeys::ProjectSettingsAudioMuteHelp,
                        "Mutes the sound while the window is not focused"));
                });
        }
        Widget::HintText(Loc::TextOr(LocKeys::ProjectSettingsAudioBusesHelp,
            "Master is always there and every bus plays under it."));
        {
            std::size_t removeAt = static_cast<std::size_t>(-1);
            if (m_draft.audioBuses.IsEmpty())
            {
                Widget::HintText(Loc::TextOr(LocKeys::ProjectSettingsAudioNoBuses, "Only Master - every sound plays there"));
            }
            for (std::size_t index = 0; index < m_draft.audioBuses.Size(); ++index)
            {
                ProjectAudioBus& bus = m_draft.audioBuses[index];
                ImGui::PushID(static_cast<int>(index));
                bool duplicate = bus.name == "Master";
                for (std::size_t other = 0; other < index; ++other)
                {
                    duplicate = duplicate || m_draft.audioBuses[other].name == bus.name;
                }
                {
                    Widget::FormLayout layout("##bus");
                    layout.Row(
                        [&] {
                            Widget::TextField("##name", bus.name).Draw();
                            if (duplicate)
                            {
                                Widget::HoveredTooltip(Loc::TextOr(LocKeys::ProjectSettingsAudioDuplicate,
                                    "This name is already used"));
                            }
                        },
                        [&] {
                            Widget::SliderFloat("##volume", bus.volume, 0.0f, 1.0f);
                            ImGui::SameLine();
                            if (Widget::ActionButton(Loc::TextOr(LocKeys::ProjectSettingsAudioRemoveBus, "Remove"),
                                    Widget::Severity::Error))
                            {
                                removeAt = index;
                            }
                        });
                }
                // 값 하나가 한 줄이다. 켜는 칸(0 이면 꺼짐)을 먼저 두고 세부를 뒤에 둔다.
                if (Widget::FoldNode(Loc::TextOr(LocKeys::ProjectSettingsAudioEffects, "Effects")))
                {
                    // 표는 마디를 닫기 전에 끝나야 한다 - 그래서 제 괄호 안에 둔다.
                    {
                        Widget::FormLayout effects("##effects");
                        const auto slider = [&](const char* name, const char* id, float& value, float low, float high,
                                                bool turnsOff) {
                            effects.Row([name] { Widget::Text(name); },
                                [&value, id, low, high, turnsOff] {
                                    Widget::SliderFloat(id, value, low, high);
                                    if (turnsOff)
                                    {
                                        Widget::HoveredTooltip(
                                            Loc::TextOr(LocKeys::ProjectSettingsAudioEffectOff, "0 turns it off"));
                                    }
                                });
                        };
                        AudioBusEffects& chain = bus.effects;
                        slider("LowPass", "##lowPass", chain.lowPassHz, 0.0f, 20000.0f, true);
                        slider("HighPass", "##highPass", chain.highPassHz, 0.0f, 5000.0f, true);
                        slider("EchoMix", "##echoMix", chain.echoMix, 0.0f, 1.0f, true);
                        slider("EchoDelay", "##echoDelay", chain.echoDelay, 0.01f, 2.0f, false);
                        slider("EchoFeedback", "##echoFeedback", chain.echoFeedback, 0.0f, 0.95f, false);
                        slider("ReverbMix", "##reverbMix", chain.reverbMix, 0.0f, 1.0f, true);
                        slider("ReverbRoom", "##reverbRoom", chain.reverbRoom, 0.0f, 1.0f, false);
                        slider("ReverbDamping", "##reverbDamping", chain.reverbDamping, 0.0f, 1.0f, false);
                        slider("Dry", "##dry", chain.dry, 0.0f, 1.0f, false);
                    }
                    Widget::TreePop();
                }
                // 부모와 센드(D-203). 부모는 위에 있는 버스만 고른다 - 파일의 차례가 곧 만드는 차례라서다.
                if (Widget::FoldNode(Loc::TextOr(LocKeys::ProjectSettingsAudioRouting, "Routing")))
                {
                    // 표는 마디를 닫기 전에 끝나야 한다 - 그래서 제 괄호 안에 둔다.
                    {
                        Widget::FormLayout routing("##routing");
                        routing.Row([] { Widget::Text("Parent"); },
                            [&] {
                                m_busChoices.Clear();
                                m_busChoices.Add(AudioMasterBusName);
                                int current = 0;
                                for (std::size_t other = 0; other < index; ++other)
                                {
                                    m_busChoices.Add(m_draft.audioBuses[other].name.c_str());
                                    if (m_draft.audioBuses[other].name == bus.parent)
                                    {
                                        current = static_cast<int>(other) + 1;
                                    }
                                }
                                if (Widget::FilterCombo("##parent", {m_busChoices.Data(), m_busChoices.Size()}, current)
                                        .ShowFilter(false)
                                        .Draw())
                                {
                                    bus.parent = current <= 0 ? String() : m_draft.audioBuses[current - 1].name;
                                }
                                Widget::HoveredTooltip(Loc::TextOr(LocKeys::ProjectSettingsAudioParentHelp,
                                    "Only buses above this one can be its parent"));
                            });
                        routing.Row([] { Widget::Text("Send"); },
                            [&] {
                                m_busChoices.Clear();
                                m_busChoices.Add(Loc::TextOr(LocKeys::ProjectSettingsAudioNoSend, "None"));
                                int current = 0;
                                for (std::size_t other = 0; other < m_draft.audioBuses.Size(); ++other)
                                {
                                    if (other == index)
                                    {
                                        continue;
                                    }
                                    m_busChoices.Add(m_draft.audioBuses[other].name.c_str());
                                    if (m_draft.audioBuses[other].name == bus.send)
                                    {
                                        current = static_cast<int>(m_busChoices.Size()) - 1;
                                    }
                                }
                                if (Widget::FilterCombo("##send", {m_busChoices.Data(), m_busChoices.Size()}, current)
                                        .ShowFilter(false)
                                        .Draw())
                                {
                                    bus.send = current <= 0 ? String() : String(m_busChoices[static_cast<std::size_t>(current)]);
                                    if (current > 0 && bus.sendLevel <= 0.0f)
                                    {
                                        bus.sendLevel = 0.5f;
                                    }
                                }
                                Widget::HoveredTooltip(Loc::TextOr(LocKeys::ProjectSettingsAudioSendHelp,
                                    "Also sends this bus to another bus, e.g. a shared reverb bus with Dry 0"));
                            });
                        routing.Row([] { Widget::Text("SendLevel"); },
                            [&] { Widget::SliderFloat("##sendLevel", bus.sendLevel, 0.0f, 1.0f); });
                    }
                    Widget::TreePop();
                }
                ImGui::PopID();
            }
            if (removeAt < m_draft.audioBuses.Size())
            {
                m_draft.audioBuses.RemoveAt(removeAt);
            }
        }
        if (Widget::Button(Loc::TextOr(LocKeys::ProjectSettingsAudioAddBus, "Add Bus")))
        {
            // 겹치지 않는 이름으로 시작한다. 바로 고쳐 쓰면 된다.
            String name;
            for (int suffix = 1;; ++suffix)
            {
                char text[24] = {};
                std::snprintf(text, sizeof(text), "Bus %d", suffix);
                name = text;
                bool taken = false;
                for (std::size_t index = 0; index < m_draft.audioBuses.Size(); ++index)
                {
                    taken = taken || m_draft.audioBuses[index].name == name;
                }
                if (false == taken)
                {
                    break;
                }
            }
            m_draft.audioBuses.Add(ProjectAudioBus{name, 1.0f});
        }

        Widget::SectionHeader(
            Loc::TextOr(LocKeys::ProjectSettingsBuild, "Build")).SpacingBefore().Draw();
        {
            Widget::FormLayout layout("##build");
            layout.Row(
                [] { Widget::Text("ProductName"); },
                [&] { Widget::TextField("##product", m_draft.build.productName).Draw(); });
            layout.Row(
                [] { Widget::Text("OutputDirectory"); },
                [&] { DrawPathValue("##output", m_draft.build.outputDirectory); });
            layout.Row(
                [] { Widget::Text("StartupCanvas"); },
                [&] {
                    DrawPathValue("##startup", m_draft.build.startupCanvas,
                        Loc::TextOr(LocKeys::DialogCanvasFilter, "JBro canvas file"), "*.jcanvas", true);
                });
            layout.Row(
                [] { Widget::Text("EnableWindows"); },
                [&] { Widget::Checkbox("##windows", m_draft.build.enableWindows); });
            layout.Row(
                [] { Widget::Text("EnableWeb"); },
                [&] { Widget::Checkbox("##web", m_draft.build.enableWeb); });
            layout.Row(
                [] { Widget::Text("EnableAndroid"); },
                [&] { Widget::Checkbox("##android", m_draft.build.enableAndroid); });
            layout.Row(
                [] { Widget::Text("EnableIOS"); },
                [&] { Widget::Checkbox("##ios", m_draft.build.enableIOS); });
        }

        ImGui::Spacing();
        if (Widget::ActionButton(Loc::TextOr(LocKeys::ProjectSettingsSave, "Save"),
                Widget::Severity::Success))
        {
            // **여기서야 파일에 간다.** 고치는 동안 파일을 건드리면 되돌릴 방법이 없다.
            ProjectFileError error;
            if (m_editor->SaveProjectSettings(m_draft, error))
            {
                m_message = Loc::TextOr(LocKeys::ProjectSettingsSaved, "saved");
                m_messageIsError = false;
                Log::Write(LogLevel::Info, "project", "settings saved: %s", path.c_str());
            }
            else
            {
                m_message = error.message;
                m_messageIsError = true;
                Log::Write(LogLevel::Error, "project",
                    "the settings could not be saved: %s", error.message.c_str());
            }
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (Widget::ActionButton(Loc::TextOr(LocKeys::ProjectSettingsRevert, "Revert"),
                Widget::Severity::Warning))
        {
            Reload();
        }
        if (false == m_message.empty())
        {
            Widget::ValidationMessage(
                m_messageIsError ? Widget::Severity::Error : Widget::Severity::Info,
                m_message.c_str()).Draw();
        }
    }
}

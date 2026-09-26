#include "ProjectSettingsPanel.h"

#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Editor/Widget/AssetField.h>
#include <JBro/Editor/Widget/List.h>
#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Core/Log.h>
#include <JBro/InputTypes/InputState.h>
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
        // 물리 워커 수의 상한이다. 커널의 `Physics2D::MaxWorkerCount` 와 같다(D-223).
        constexpr int MaxPhysicsWorkers = 16;
    }

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

    void ProjectSettingsPanel::RefreshFontChoices()
    {
        const AssetRegistry& registry = m_editor->GetAssetRegistry();
        if (m_fontChoicesBuilt && m_fontChoicesRevision == registry.GetRevision())
        {
            return;
        }
        m_fontChoicesBuilt = true;
        m_fontChoicesRevision = registry.GetRevision();
        m_fontNames.Clear();
        m_fontIds.Clear();
        for (std::size_t index = 0; index < registry.GetCount(); ++index)
        {
            const AssetRecord& record = registry.GetRecord(index);
            if (record.type == AssetType::Font)
            {
                m_fontNames.Add(record.relativePath);
                m_fontIds.Add(record.id);
            }
        }
        // 이름을 다 모은 뒤에 가리킨다. 모으는 중에 배열이 자라면 앞의 포인터가 무효가 된다.
        m_fontNamePointers.Clear();
        for (std::size_t index = 0; index < m_fontNames.Size(); ++index)
        {
            m_fontNamePointers.Add(m_fontNames[index].c_str());
        }
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

    namespace
    {
        // 입력 설정의 고르기 목록이다(D-214). 파일에 적히는 글자와 같다.
        constexpr const char* InputActionTypeChoices[] = {"Bool", "Float", "Vector2"};
        constexpr const char* InputBindingSourceChoices[] = {"Key", "MouseButton", "GamepadButton", "GamepadAxis", "GamepadStick"};
        constexpr const char* InputCompositeChoices[] = {"None", "Up", "Down", "Left", "Right"};
        constexpr const char* InputStickChoices[] = {"Left", "Right"};
        constexpr const char* DefaultInputLayers[] = {"Modal", "UI", "Game", "World", "Debug"};
        // 세트 칸이 비었을 때 흐리게 보이는 이름이다. 세트 이름은 데이터라 번역하지 않는다.
        constexpr const char* DefaultInputActionSetName = "Default";

        bool IsGamepadSource(InputBindingSource source)
        {
            return source == InputBindingSource::GamepadButton || source == InputBindingSource::GamepadAxis
                || source == InputBindingSource::GamepadStick;
        }
    }

    void ProjectSettingsPanel::FillInputCodeChoices(InputBindingSource source)
    {
        m_inputCodeChoices.Clear();
        switch (source)
        {
        case InputBindingSource::Key:
            // `Unknown` 은 고를 것이 아니다.
            for (std::size_t index = 1; index < KeyCount; ++index)
            {
                m_inputCodeChoices.Add(GetKeyName(static_cast<Key>(index)));
            }
            break;
        case InputBindingSource::MouseButton:
            for (std::size_t index = 0; index < MouseButtonCount; ++index)
            {
                m_inputCodeChoices.Add(GetMouseButtonName(static_cast<MouseButton>(index)));
            }
            break;
        case InputBindingSource::GamepadButton:
            for (std::size_t index = 0; index < GamepadButtonCount; ++index)
            {
                m_inputCodeChoices.Add(GetGamepadButtonName(static_cast<GamepadButton>(index)));
            }
            break;
        case InputBindingSource::GamepadAxis:
            for (std::size_t index = 0; index < GamepadAxisCount; ++index)
            {
                m_inputCodeChoices.Add(GetGamepadAxisName(static_cast<GamepadAxis>(index)));
            }
            break;
        case InputBindingSource::GamepadStick:
            for (const char* stick : InputStickChoices)
            {
                m_inputCodeChoices.Add(stick);
            }
            break;
        }
    }

    void ProjectSettingsPanel::DrawGameLanguages()
    {
        // **게임 언어**(D-226). 문자열 표의 로케일 목록과 기본·폴백이다. 에디터 언어(`EditorLocale`)와는 다른 값이다 - 위의 언어는
        // 에디터 화면의 것이고 이것은 게임이 보일 글자의 것이다.
        Widget::SectionHeader(
            Loc::TextOr(LocKeys::ProjectSettingsGameLanguages, "Game Languages")).SpacingBefore().Draw();
        Widget::HintText(Loc::TextOr(LocKeys::ProjectSettingsGameLanguagesHelp,
            "The languages of the string tables (.jstrings). The game starts in DefaultLocale; a key missing from the current language's tables is looked up in FallbackLocale's."));
        // 순서가 기본값(첫 언어)을 정하므로 폰트와 같은 목록 위젯이다. 새 줄은 빈 이름으로 시작한다.
        Widget::List("##locales", m_draft.locales,
            [&](String& locale, int) { Widget::TextField("##locale", locale).Hint("ko-KR").Draw(); },
            String(), Widget::ListFlagsShowIndex);
        // 기본·폴백은 목록에서 고른다. 목록에 없는 이름이 적혀 있으면(손으로 고친 파일) 비어 보이고 그대로 남는다.
        constexpr std::size_t MaxLocales = 32;
        const char* names[MaxLocales] = {};
        const std::size_t count = std::min(m_draft.locales.Size(), MaxLocales);
        for (std::size_t index = 0; index < count; ++index)
        {
            names[index] = m_draft.locales[index].c_str();
        }
        const auto pick = [&](const char* id, String& value) {
            int current = -1;
            for (std::size_t index = 0; index < count; ++index)
            {
                if (m_draft.locales[index] == value)
                {
                    current = static_cast<int>(index);
                }
            }
            if (Widget::FilterCombo(id, ArrayView<const char* const>(names, count), current).ShowFilter(false).Draw()
                && current >= 0)
            {
                value = m_draft.locales[static_cast<std::size_t>(current)];
            }
        };
        Widget::FormLayout layout("##gameLanguages");
        layout.Row([] { Widget::Text("DefaultLocale"); }, [&] { pick("##default", m_draft.defaultLocale); });
        layout.Row([] { Widget::Text("FallbackLocale"); }, [&] { pick("##fallback", m_draft.fallbackLocale); });
    }

    void ProjectSettingsPanel::DrawInputSettings()
    {
        Widget::SectionHeader(
            Loc::TextOr(LocKeys::ProjectSettingsInput, "Input")).SpacingBefore().Draw();

        // ── 레이어 순서 ──
        Widget::HintText(Loc::TextOr(LocKeys::ProjectSettingsInputLayersHelp,
            "Layers higher in the list get input first. When one blocks, the layers below get nothing"));
        if (m_draft.inputLayers.IsEmpty())
        {
            Widget::HintText(Loc::TextOr(LocKeys::ProjectSettingsInputDefaultLayers,
                "Using the default order (Modal, UI, Game, World, Debug)"));
        }
        {
            std::size_t removeAt = static_cast<std::size_t>(-1);
            std::size_t moveUp = static_cast<std::size_t>(-1);
            for (std::size_t index = 0; index < m_draft.inputLayers.Size(); ++index)
            {
                String& layer = m_draft.inputLayers[index];
                ImGui::PushID(static_cast<int>(index));
                bool duplicate = false;
                for (std::size_t other = 0; other < index; ++other)
                {
                    duplicate = duplicate || m_draft.inputLayers[other] == layer;
                }
                // 표는 ID 를 되돌리기 전에 닫혀야 한다 - 그래서 제 괄호 안에 둔다.
                {
                    Widget::FormLayout layout("##layer");
                    // 라벨 열은 순서 번호다. 이름은 값 열에 둔다 - 라벨 열은 좁아서 이름이 보이지 않는다.
                    char order[16] = {};
                    std::snprintf(order, sizeof(order), "%zu", index + 1);
                    layout.Row(
                        [&] { Widget::Text(order); },
                        [&] {
                            Widget::TextField("##name", layer).Width(ImGui::GetContentRegionAvail().x * 0.5f).Draw();
                            if (duplicate)
                            {
                                Widget::HoveredTooltip(Loc::TextOr(LocKeys::ProjectSettingsInputDuplicate,
                                    "This name is already used"));
                            }
                            ImGui::SameLine();
                            {
                                Widget::DisableScope first(index == 0);
                                if (Widget::Button(Loc::TextOr(LocKeys::ProjectSettingsInputMoveUp, "Move Up")))
                                {
                                    moveUp = index;
                                }
                            }
                            ImGui::SameLine();
                            if (Widget::ActionButton(Loc::TextOr(LocKeys::ProjectSettingsInputRemove, "Remove"),
                                    Widget::Severity::Error))
                            {
                                removeAt = index;
                            }
                        });
                }
                ImGui::PopID();
            }
            if (moveUp < m_draft.inputLayers.Size() && moveUp > 0)
            {
                String above = m_draft.inputLayers[moveUp - 1];
                m_draft.inputLayers[moveUp - 1] = m_draft.inputLayers[moveUp];
                m_draft.inputLayers[moveUp] = above;
            }
            if (removeAt < m_draft.inputLayers.Size())
            {
                m_draft.inputLayers.RemoveAt(removeAt);
            }
        }
        if (Widget::Button(Loc::TextOr(LocKeys::ProjectSettingsInputAddLayer, "Add Layer")))
        {
            // 기본 순서를 쓰고 있었으면 그것을 먼저 옮겨 적는다. 새 레이어만 남기면 기본 레이어가 모두 맨 아래로 간다.
            if (m_draft.inputLayers.IsEmpty())
            {
                for (const char* layer : DefaultInputLayers)
                {
                    m_draft.inputLayers.Add(String(layer));
                }
            }
            String name;
            for (int suffix = 1;; ++suffix)
            {
                char text[24] = {};
                std::snprintf(text, sizeof(text), "Layer %d", suffix);
                name = text;
                bool taken = false;
                for (const String& layer : m_draft.inputLayers)
                {
                    taken = taken || layer == name;
                }
                if (false == taken)
                {
                    break;
                }
            }
            m_draft.inputLayers.Add(name);
        }

        // ── 액션 ──
        Widget::HintText(Loc::TextOr(LocKeys::ProjectSettingsInputActionsHelp,
            "Scripts read input by action name instead of by key. Each action can bind several keys, buttons and sticks"));
        if (m_draft.inputActions.IsEmpty())
        {
            Widget::HintText(Loc::TextOr(LocKeys::ProjectSettingsInputNoActions, "No actions"));
        }
        bool tooMany = m_draft.inputActions.Size() > MaxInputActions;
        std::size_t removeAction = static_cast<std::size_t>(-1);
        for (std::size_t index = 0; index < m_draft.inputActions.Size(); ++index)
        {
            ProjectInputAction& action = m_draft.inputActions[index];
            tooMany = tooMany || action.bindings.Size() > MaxInputBindingsPerAction;
            ImGui::PushID(static_cast<int>(index));
            bool duplicate = false;
            for (std::size_t other = 0; other < index; ++other)
            {
                duplicate = duplicate || m_draft.inputActions[other].name == action.name;
            }
            // 마디 제목은 액션 이름이다. `###` 뒤는 자리 번호라 이름을 고쳐도 마디가 닫히지 않는다.
            char title[96] = {};
            std::snprintf(title, sizeof(title), "%s###action", action.name.empty() ? "?" : action.name.c_str());
            if (Widget::FoldNode(title))
            {
                {
                    Widget::FormLayout layout("##action");
                    layout.Row([] { Widget::Text("Name"); },
                        [&] {
                            Widget::TextField("##name", action.name).Draw();
                            if (duplicate)
                            {
                                Widget::HoveredTooltip(Loc::TextOr(LocKeys::ProjectSettingsInputDuplicate,
                                    "This name is already used"));
                            }
                        });
                    layout.Row([] { Widget::Text("Type"); },
                        [&] {
                            int current = static_cast<int>(action.type);
                            if (Widget::FilterCombo("##type", InputActionTypeChoices, current).ShowFilter(false).Draw())
                            {
                                action.type = static_cast<InputActionType>(current);
                            }
                        });
                    layout.Row([] { Widget::Text("Set"); },
                        [&] {
                            Widget::TextField("##set", action.set).Hint(DefaultInputActionSetName).Draw();
                            Widget::HoveredTooltip(Loc::TextOr(LocKeys::ProjectSettingsInputSetHelp,
                                "Scripts turn action sets on and off. Only Default is on when the game starts"));
                        });
                }
                std::size_t removeBinding = static_cast<std::size_t>(-1);
                for (std::size_t at = 0; at < action.bindings.Size(); ++at)
                {
                    ProjectInputBinding& binding = action.bindings[at];
                    ImGui::PushID(static_cast<int>(at));
                    {
                        Widget::FormLayout layout("##binding");
                        layout.Row([] { Widget::Text("Source"); },
                            [&] {
                                int current = static_cast<int>(binding.source);
                                if (Widget::FilterCombo("##source", InputBindingSourceChoices, current).ShowFilter(false).Draw())
                                {
                                    // 원천이 바뀌면 앞의 값은 다른 목록의 번호다. 첫 항목으로 돌린다.
                                    binding.source = static_cast<InputBindingSource>(current);
                                    binding.code = binding.source == InputBindingSource::Key
                                        ? static_cast<std::uint16_t>(Key::Space) : 0;
                                }
                            });
                        layout.Row([] { Widget::Text("Code"); },
                            [&] {
                                FillInputCodeChoices(binding.source);
                                // 키 목록은 `Unknown` 을 빼고 시작하므로 번호가 하나 밀린다.
                                const int offset = binding.source == InputBindingSource::Key ? 1 : 0;
                                int current = static_cast<int>(binding.code) - offset;
                                if (Widget::FilterCombo("##code", {m_inputCodeChoices.Data(), m_inputCodeChoices.Size()}, current)
                                        .ShowFilter(binding.source == InputBindingSource::Key).Draw())
                                {
                                    binding.code = static_cast<std::uint16_t>(current + offset);
                                }
                            });
                        if (IsGamepadSource(binding.source))
                        {
                            layout.Row([] { Widget::Text("GamepadIndex"); },
                                [&] {
                                    Widget::DragInt("##pad").Range(-1, 3).Draw(binding.gamepad);
                                    Widget::HoveredTooltip(Loc::TextOr(LocKeys::ProjectSettingsInputAnyGamepad,
                                        "-1 means any connected gamepad"));
                                });
                        }
                        if (action.type == InputActionType::Vector2 && binding.source != InputBindingSource::GamepadStick)
                        {
                            layout.Row([] { Widget::Text("Composite"); },
                                [&] {
                                    int current = static_cast<int>(binding.composite);
                                    if (Widget::FilterCombo("##composite", InputCompositeChoices, current).ShowFilter(false).Draw())
                                    {
                                        binding.composite = static_cast<InputComposite>(current);
                                    }
                                });
                        }
                    }
                    if (Widget::ActionButton(Loc::TextOr(LocKeys::ProjectSettingsInputRemoveBinding, "Remove Binding"),
                            Widget::Severity::Error))
                    {
                        removeBinding = at;
                    }
                    ImGui::PopID();
                }
                if (removeBinding < action.bindings.Size())
                {
                    action.bindings.RemoveAt(removeBinding);
                }
                if (Widget::Button(Loc::TextOr(LocKeys::ProjectSettingsInputAddBinding, "Add Binding")))
                {
                    ProjectInputBinding binding;
                    binding.code = static_cast<std::uint16_t>(Key::Space);
                    action.bindings.Add(binding);
                }
                ImGui::SameLine();
                if (Widget::ActionButton(Loc::TextOr(LocKeys::ProjectSettingsInputRemoveAction, "Remove Action"),
                        Widget::Severity::Error))
                {
                    removeAction = index;
                }
                Widget::TreePop();
            }
            ImGui::PopID();
        }
        if (removeAction < m_draft.inputActions.Size())
        {
            m_draft.inputActions.RemoveAt(removeAction);
        }
        if (tooMany)
        {
            Widget::ValidationMessage(Widget::Severity::Warning,
                Loc::TextOr(LocKeys::ProjectSettingsInputTooMany,
                    "The engine uses up to 64 actions and 8 bindings per action. The rest stay only in the file")).Draw();
        }
        if (Widget::Button(Loc::TextOr(LocKeys::ProjectSettingsInputAddAction, "Add Action")))
        {
            String name;
            for (int suffix = 1;; ++suffix)
            {
                char text[24] = {};
                std::snprintf(text, sizeof(text), "Action%d", suffix);
                name = text;
                bool taken = false;
                for (const ProjectInputAction& existing : m_draft.inputActions)
                {
                    taken = taken || existing.name == name;
                }
                if (false == taken)
                {
                    break;
                }
            }
            ProjectInputAction action;
            action.name = name;
            m_draft.inputActions.Add(action);
        }
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
            // 시간(D-233). 범위는 파일을 읽을 때와 같다(`TimeSystem::IsValid`).
            layout.Row(
                [] { Widget::Text("FixedDeltaTime"); },
                [&]
                {
                    Widget::DragFloat("##fixedDelta").Range(0.001f, 1.0f).Speed(0.0005f).Format("%.4f").Draw(m_draft.fixedDeltaTime);
                    Widget::HoveredTooltip(Loc::TextOr(LocKeys::ProjectSettingsFixedDeltaTime,
                        "the length of one fixed step in seconds; physics and OnFixedUpdate run at this interval"));
                });
            layout.Row(
                [] { Widget::Text("MaxFixedSteps"); },
                [&]
                {
                    int value = static_cast<int>(m_draft.maxFixedSteps);
                    if (Widget::DragInt("##maxFixedSteps").Range(1, 64).Draw(value))
                    {
                        m_draft.maxFixedSteps = static_cast<std::uint32_t>(value);
                    }
                    Widget::HoveredTooltip(Loc::TextOr(LocKeys::ProjectSettingsMaxFixedSteps,
                        "the most fixed steps one frame runs; past it the game slows down instead of catching up"));
                });
            layout.Row(
                [] { Widget::Text("MaxDeltaTime"); },
                [&]
                {
                    Widget::DragFloat("##maxDelta").Range(0.001f, 10.0f).Speed(0.005f).Format("%.3f").Draw(m_draft.maxDeltaTime);
                    Widget::HoveredTooltip(Loc::TextOr(LocKeys::ProjectSettingsMaxDeltaTime,
                        "the longest frame delta in seconds; a frame after a window drag or a breakpoint is cut to this"));
                });
            layout.Row(
                [] { Widget::Text("RandomSeed"); },
                [&]
                {
                    // 64 비트라 끌기 칸으로는 담지 못한다. 글자로 받고 숫자로 읽히는 것만 받는다.
                    char digits[24] = {};
                    std::snprintf(digits, sizeof(digits), "%llu", static_cast<unsigned long long>(m_draft.randomSeed));
                    String seed(digits);
                    if (Widget::TextField("##randomSeed", seed).Draw())
                    {
                        char* end = nullptr;
                        const unsigned long long parsed = std::strtoull(seed.c_str(), &end, 10);
                        if (false == seed.empty() && seed[0] != '-' && end != nullptr && *end == '\0')
                        {
                            m_draft.randomSeed = parsed;
                        }
                    }
                    Widget::HoveredTooltip(Loc::TextOr(LocKeys::ProjectSettingsRandomSeed,
                        "the random seed; 0 draws a new one on every play and logs it"));
                });
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
                        // 사슬의 차례대로 늘어놓는다(D-210). 켜는 칸에는 무엇이 끄는 값인지 알린다.
                        const char* zeroOff = Loc::TextOr(LocKeys::ProjectSettingsAudioEffectOff, "0 turns it off");
                        const char* oneOff = Loc::TextOr(LocKeys::ProjectSettingsAudioEffectRatioOff, "1 turns it off");
                        const auto slider = [&](const char* name, const char* id, float& value, float low, float high,
                                                const char* offHint) {
                            effects.Row([name] { Widget::Text(name); },
                                [&value, id, low, high, offHint] {
                                    Widget::SliderFloat(id, value, low, high);
                                    if (offHint != nullptr)
                                    {
                                        Widget::HoveredTooltip(offHint);
                                    }
                                });
                        };
                        AudioBusEffects& chain = bus.effects;
                        slider("HighPass", "##highPass", chain.highPassHz, 0.0f, 5000.0f, zeroOff);
                        slider("LowPass", "##lowPass", chain.lowPassHz, 0.0f, 20000.0f, zeroOff);
                        slider("EqLowGain", "##eqLowGain", chain.eqLowGain, -24.0f, 24.0f, zeroOff);
                        slider("EqLowHz", "##eqLowHz", chain.eqLowHz, 20.0f, 1000.0f, nullptr);
                        slider("EqMidGain", "##eqMidGain", chain.eqMidGain, -24.0f, 24.0f, zeroOff);
                        slider("EqMidHz", "##eqMidHz", chain.eqMidHz, 100.0f, 8000.0f, nullptr);
                        slider("EqHighGain", "##eqHighGain", chain.eqHighGain, -24.0f, 24.0f, zeroOff);
                        slider("EqHighHz", "##eqHighHz", chain.eqHighHz, 1000.0f, 16000.0f, nullptr);
                        slider("Distortion", "##distortion", chain.distortion, 0.0f, 1.0f, zeroOff);
                        slider("DistortionMix", "##distortionMix", chain.distortionMix, 0.0f, 1.0f, nullptr);
                        slider("ChorusMix", "##chorusMix", chain.chorusMix, 0.0f, 1.0f, zeroOff);
                        slider("ChorusRate", "##chorusRate", chain.chorusRate, 0.05f, 10.0f, nullptr);
                        slider("ChorusDepth", "##chorusDepth", chain.chorusDepth, 0.0f, 8.0f, nullptr);
                        slider("PitchShift", "##pitchShift", chain.pitchShift, -12.0f, 12.0f, zeroOff);
                        slider("EchoMix", "##echoMix", chain.echoMix, 0.0f, 1.0f, zeroOff);
                        slider("EchoDelay", "##echoDelay", chain.echoDelay, 0.01f, 2.0f, nullptr);
                        slider("EchoFeedback", "##echoFeedback", chain.echoFeedback, 0.0f, 0.95f, nullptr);
                        slider("ReverbMix", "##reverbMix", chain.reverbMix, 0.0f, 1.0f, zeroOff);
                        slider("ReverbRoom", "##reverbRoom", chain.reverbRoom, 0.0f, 1.0f, nullptr);
                        slider("ReverbDamping", "##reverbDamping", chain.reverbDamping, 0.0f, 1.0f, nullptr);
                        slider("Dry", "##dry", chain.dry, 0.0f, 1.0f, nullptr);
                        slider("CompRatio", "##compRatio", chain.compRatio, 1.0f, 20.0f, oneOff);
                        slider("CompThreshold", "##compThreshold", chain.compThreshold, -60.0f, 0.0f, nullptr);
                        slider("CompAttack", "##compAttack", chain.compAttack, 0.0005f, 0.5f, nullptr);
                        slider("CompRelease", "##compRelease", chain.compRelease, 0.005f, 2.0f, nullptr);
                        slider("CompMakeup", "##compMakeup", chain.compMakeup, 0.0f, 24.0f, nullptr);
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
                        // 더킹(D-205): 고른 버스에 소리가 있는 동안 이 버스가 물러선다.
                        routing.Row([] { Widget::Text("DuckBy"); },
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
                                    if (m_draft.audioBuses[other].name == bus.duckBy)
                                    {
                                        current = static_cast<int>(m_busChoices.Size()) - 1;
                                    }
                                }
                                if (Widget::FilterCombo("##duckBy", {m_busChoices.Data(), m_busChoices.Size()}, current)
                                        .ShowFilter(false)
                                        .Draw())
                                {
                                    bus.duckBy = current <= 0 ? String() : String(m_busChoices[static_cast<std::size_t>(current)]);
                                    if (current > 0 && bus.duckAmount <= 0.0f)
                                    {
                                        bus.duckAmount = 0.5f;
                                    }
                                }
                                Widget::HoveredTooltip(Loc::TextOr(LocKeys::ProjectSettingsAudioDuckHelp,
                                    "While that bus sounds, this one steps back (e.g. music under dialogue)"));
                            });
                        routing.Row([] { Widget::Text("DuckAmount"); },
                            [&] { Widget::SliderFloat("##duckAmount", bus.duckAmount, 0.0f, 1.0f); });
                        routing.Row([] { Widget::Text("DuckRelease"); },
                            [&] { Widget::SliderFloat("##duckRelease", bus.duckRelease, 0.01f, 3.0f); });
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

        // **프로젝트 폰트**(D-200 (6), 기존 설정 창의 Fonts 갈래). 순서가 있는 목록 하나다: 첫 폰트가 `fontId` 가 빈 텍스트를
        // 그리고, 목록 전체가 폰트에 없는 글자를 차례로 찾아보는 폴백이다. 기존은 기본 패밀리와 폴백 목록이 따로였다.
        Widget::SectionHeader(
            Loc::TextOr(LocKeys::ProjectSettingsText, "Text")).SpacingBefore().Draw();
        Widget::HintText(Loc::TextOr(LocKeys::ProjectSettingsFontsHelp,
            "The first font draws texts with no fontId. Letters a font lacks are looked up in this list, in order."));
        // 순서가 뜻을 가지므로 공용 목록 위젯이다(끌어서 순서 바꾸기·번호·지우기·항목 추가). 창이 좁게 도킹되어도(실측 202 px)
        // 칸 하나가 한 줄을 다 쓴다 - 처음 판은 줄마다 "위로"·"삭제" 단추를 붙여 폰트 칸이 보이지 않을 만큼 줄었다.
        // 새 줄은 빈 아이디로 시작하고, 고르지 않은 줄은 저장할 때 빠진다.
        RefreshFontChoices();
        Widget::List("##fonts", m_draft.fonts,
            [&](AssetId& font, int) {
                Widget::AssetField("##font",
                    ArrayView<const char* const>(m_fontNamePointers.Data(), m_fontNamePointers.Size()),
                    ArrayView<const AssetId>(m_fontIds.Data(), m_fontIds.Size()), font)
                    .AllowClear(false)
                    .Draw();
            },
            AssetId{}, Widget::ListFlagsShowIndex);
        DrawGameLanguages();
        DrawInputSettings();

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
            // **물리 스레드**(D-223). 자동은 게임이 시작할 때 정한다. "추천 값 사용" 은 같은 계산을 지금 돌려 값을 고정한다.
            layout.Row(
                [] { Widget::Text("PhysicsThreads"); },
                [&] {
                    const char* choices[] = {
                        Loc::TextOr(LocKeys::ProjectSettingsPhysicsThreadsAuto, "Auto"),
                        Loc::TextOr(LocKeys::ProjectSettingsPhysicsThreadsSingle, "Single thread"),
                        Loc::TextOr(LocKeys::ProjectSettingsPhysicsThreadsWorkers, "Set worker count")};
                    int current = static_cast<int>(m_draft.build.physicsThreadMode);
                    if (Widget::FilterCombo("##physicsThreads", ArrayView<const char* const>(choices, 3), current)
                            .ShowFilter(false)
                            .Draw()
                        && current >= 0)
                    {
                        m_draft.build.physicsThreadMode = static_cast<PhysicsThreadMode>(current);
                        if (m_draft.build.physicsThreadMode == PhysicsThreadMode::Workers && m_draft.build.physicsWorkers == 0)
                        {
                            m_draft.build.physicsWorkers = 1;
                        }
                    }
                    Widget::HoveredTooltip(Loc::TextOr(LocKeys::ProjectSettingsPhysicsThreadsHelp,
                        "Auto picks the worker count from the colliders in the build canvases when the game starts"));
                    ImGui::SameLine();
                    if (Widget::Button(Loc::TextOr(LocKeys::ProjectSettingsPhysicsRecommend, "Use Recommended")))
                    {
                        const std::uint32_t recommended = m_editor->RecommendPhysicsWorkers();
                        m_draft.build.physicsThreadMode =
                            recommended == 0 ? PhysicsThreadMode::Single : PhysicsThreadMode::Workers;
                        m_draft.build.physicsWorkers = recommended;
                    }
                    Widget::HoveredTooltip(Loc::TextOr(LocKeys::ProjectSettingsPhysicsRecommendHelp,
                        "Counts the colliders in the build canvases and fixes the recommended worker count"));
                });
            if (m_draft.build.physicsThreadMode == PhysicsThreadMode::Workers)
            {
                layout.Row(
                    [] { Widget::Text("PhysicsWorkers"); },
                    [&] {
                        int value = static_cast<int>(m_draft.build.physicsWorkers);
                        if (Widget::DragInt("##physicsWorkers").Range(1, MaxPhysicsWorkers).Draw(value))
                        {
                            m_draft.build.physicsWorkers = static_cast<std::uint32_t>(value);
                        }
                    });
            }
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

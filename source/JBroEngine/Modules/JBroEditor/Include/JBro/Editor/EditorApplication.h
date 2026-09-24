#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Canvas/CanvasFile.h>
#include <JBro/Editor/Command/ComponentAddress.h>
#include <JBro/Editor/Command/ObjectTreeSnapshot.h>
#include <JBro/Editor/EditorCommand.h>
#include <JBro/Editor/EditorSpriteContours.h>
#include <JBro/Editor/EditorObjectRegistry.h>
#include <JBro/Editor/EditorPanel.h>
#include <JBro/Editor/EditorPopup.h>
#include <JBro/Editor/EditorUI.h>
#include <JBro/Types/Array.h>
#include <JBro/Host/IFramework.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/Platform/Platform.h>
#include <JBro/RHI/RHI.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/String.h>

namespace JBro
{
    class Canvas;
    class EditorThumbnails;
    // 단축키 표(`EditorShortcuts.h`)는 ImGui 를 끌어온다. 이 헤더는 에디터 호스트처럼 ImGui 를
    // 모르는 쪽도 include 하므로 **열거형만 앞선언한다**(D-158) - 헤더를 통째로 끌어오면
    // 그쪽 빌드가 `imgui.h` 를 찾지 못해 깨진다(실제로 깨져 있었다).
    enum class EditorShortcut : std::uint8_t;
    class SpriteViewerWindow;
    class GameObject;
    class Renderer;
    class EngineInstance;
    class IFramework;
    class IPlatform;
    class AssetRegistry;
    class AssetSystem;
    struct AssetMetaFile;
    struct AssetMetaTarget;

    // `FrameworkKind` 는 `JBro/Host/ProjectFile.h` 에 있다(D-99). 프로젝트 파일이 그 값을
    // 적는 자리이므로 형식과 같이 둔다.

    struct ProjectDescriptor
    {
        JStringView name;
        JStringView projectGuid;
        JStringView engineVersion;
        FrameworkKind framework = FrameworkKind::Framework2D;
        GraphicsApi graphicsApi = GraphicsApi::D3D12;
    };

    // 경로 칸의 "찾아보기" 요청이다(D-164). 대화상자는 막히는 호출이라 프레임이 끝난 뒤 열리고, 고른 경로는
    // `deliver` 로 돌아온다. 고르지 않았으면 부르지 않는다.
    struct PathBrowseRequest
    {
        // 참이면 폴더, 거짓이면 파일을 고른다.
        bool folder = true;
        // 파일일 때의 종류("JBro 캔버스 파일", "*.jcanvas"). 비면 가리지 않는다.
        String filterName;
        String filterPattern;
        // 참이면 기준 폴더 안의 경로를 그 폴더 기준 상대경로로 바꿔 준다. 기준 폴더가 비면 프로젝트 폴더다 -
        // 프로젝트 파일의 경로 값 대부분이 그렇고, 캔버스 경로는 에셋 폴더를 준다.
        bool relative = true;
        String baseFolder;
        void (*deliver)(void* user, const String& path) = nullptr;
        void* user = nullptr;
    };

    struct EditorApplicationConfig
    {
        GraphicsApi graphicsApi = GraphicsApi::D3D12;
        float fixedDeltaTime = 1.0f / 60.0f;
        std::uint32_t maxFixedStepsPerFrame = 4;
        std::uint32_t windowWidth = 1280;
        std::uint32_t windowHeight = 720;
        bool windowVisible = true;
        bool enableValidation = false;
        // 화면 글자를 어디서 읽을지(ProjectRule §11.2). 못 읽어도 에디터는 뜬다 -
        // 그때는 코드에 있는 영어 원문이 나온다. 글자 파일 하나 때문에 아무것도
        // 못 보는 것이 더 나쁘다.
        const char* localizationDirectory = "Localization";
        const char* locale = "ko-KR";
        const char* fallbackLocale = "en-US";
        // 아이콘 글꼴(D-96). 로컬라이징 표와 같이 실행 폴더 기준이다. 없으면 아이콘은 네모다.
        const char* iconFontPath = "ThirdParty/FontAwesome/FontAwesome7-Free-Solid-900.otf";
        // 파일 대화상자를 대신하는 함수. 널이면 플랫폼의 대화상자를 연다. 테스트가 대화상자
        // 없이 저장 경로를 주는 자리다 - 네이티브 대화상자는 사람 없이 닫히지 않는다.
        bool (*fileDialog)(const FileDialogDesc& desc, String& outPath, void* user) = nullptr;
        void* fileDialogUser = nullptr;
        JMemoryContext memory;
    };

    class EditorApplication
    {
    public:
        EditorApplication();
        ~EditorApplication();
        EditorApplication(const EditorApplication&) = delete;
        EditorApplication& operator=(const EditorApplication&) = delete;
        EditorApplication(EditorApplication&&) = delete;
        EditorApplication& operator=(EditorApplication&&) = delete;

        bool Initialize(const EditorApplicationConfig& config);
        bool OpenProject(const ProjectDescriptor& project);

        // `.jproject` 파일을 읽어 연다. 그 파일이 가리키는 스크립트 DLL 까지 실린다.
        //
        // **차원은 파일이 정한다**(D-99). `.jproject` 의 `Framework` 키가 2D 인지 3D 인지
        // 말하고, 그 키가 없는 파일은 애초에 읽히지 않는다. 부르는 쪽이 따로 고르지 않는다 -
        // 고르게 두면 런처와 파일이 어긋난 채로 3D 프로젝트가 2D 로 열릴 수 있다.
        bool OpenProjectFile(const char* projectFilePath, ProjectFileError& error);

        // 이 프로젝트가 2D 인가 3D 인가(D-99). 편집 화면이 평면인지 궤도인지가 여기서 갈린다.
        FrameworkKind GetFrameworkKind() const { return m_frameworkKind; }
        // 마지막으로 연 `.jproject` 의 내용이다. 파일로 열지 않았으면 기본값이다.
        const ProjectFile& GetProjectFile() const;
        // 그 파일의 경로다. 파일로 열지 않았으면 비어 있다.
        const String& GetProjectFilePath() const { return m_projectFilePath; }
        // 프로젝트 설정을 파일에 쓴다(D-137). **원문을 타고 가며 아는 키만 고친다** -
        // 주석도 모르는 키도 그 자리에 남는다. 성공하면 에디터가 든 값도 그것으로 바뀐다.
        bool SaveProjectSettings(const ProjectFile& settings, ProjectFileError& error);

        // ── 에디터 세션(D-146) ──────────────────────────────────────────────
        //
        // **다시 열었을 때 이어서 일할 수 있어야 한다.** 기존 엔진도 보던 캔버스와 보던 자리,
        // 에디터 언어를 프로젝트에 적었다. 값이 사는 곳은 `.jproject` 다.

        // 지금의 세션 값을 프로젝트 파일에 쓴다. 프로젝트를 파일로 열지 않았으면 아무 일도
        // 하지 않고 참이다 - 적을 파일이 없는 것은 실패가 아니다.
        bool SaveEditorSession();
        // 파일에 적힌 캔버스 뷰 카메라다. `size` 가 0 이면 적힌 적이 없다.
        void GetSessionCamera(float& centerX, float& centerY, float& size) const;
        // 지금 캔버스 뷰가 보고 있는 자리다. 패널이 없으면 `size` 가 0 이다.
        void GetCanvasViewCamera(float& centerX, float& centerY, float& size);
        // 창 배치가 사는 파일이다(`<프로젝트파일>.layout.ini`). 프로젝트를 파일로 열지
        // 않았으면 빈 글자다. ImGui 의 형식을 그대로 쓰므로 우리가 파싱할 일은 없다.
        String GetLayoutFilePath() const;

        // 에셋의 작은 그림이다(D-147). 텍스처가 아닌 에셋이나 아직 만들지 못한 것은 빈 핸들이다 -
        // 만드는 일은 프레임마다 몇 개로 막혀 있어, 목록을 처음 열면 몇 프레임에 걸쳐 채워진다.
        // 스프라이트 아이디를 주면 그 짝 텍스처의 그림을 준다.
        TextureHandle GetAssetThumbnail(AssetId asset, std::uint32_t maxSide = 128);
        // 그 그림의 원본 크기다. 그림을 아직 만들지 않았으면 거짓이다.
        bool GetAssetSourceSize(AssetId asset, std::uint32_t& width, std::uint32_t& height) const;

        // **스프라이트 뷰어**에서 연다(D-155). 뿌리 도크에 메인 도크와 나란히 붙는 창이고,
        // 파일마다 탭이 하나다. 그림이 아니면 거짓이다.
        bool OpenSpriteViewer(AssetId asset);

        // **밖의 파일을 에셋 폴더로 가져온다**(D-156, 기존 `SpriteImporterWindow`·`ImporterWindowBase`).
        // 파일을 `relativeFolder` 아래로 **복사**하고 다시 훑어 등록한다 - `.jmeta` 는 스캔이 만든다.
        // 같은 이름이 이미 있으면 거절한다(덮어쓰면 그 파일을 가리키던 아이디가 다른 그림을 가리킨다).
        // 에셋이 아닌 확장자도 거절한다 - 복사는 되는데 목록에 나오지 않으면 가져온 것이 사라진 것처럼 보인다.
        bool ImportAssetFile(const char* sourcePath, const char* relativeFolder, String* importedPath = nullptr);
        // 파일 대화상자로 고르고 가져온다. 대화상자는 프레임 밖에서 뜬다. 그림이면 스프라이트 뷰어로 연다 -
        // 기존 임포터가 가져오기 전에 옵션을 보였듯, 가져온 뒤 바로 자르는 옵션을 고칠 수 있게.
        void RequestImportAsset(const char* relativeFolder);
        std::size_t GetSpriteViewerTabCount() const;
        // 앞에 있는 뷰어 탭의 칸 번호다. 탭이 없으면 거짓이다.
        bool GetSpriteViewerFrame(std::uint32_t& frame) const;
        // 마우스가 시트에서 가리킨 칸이다. 없으면 -1 이다(D-185).
        int GetSpriteViewerHoveredFrame() const;
        // 스프라이트가 그리는 **실제 모양**이다(D-149). 칸 안의 비율 좌표로 된 선분들이고,
        // 아직 재지 못했으면 nullptr 다. 크기·피벗·회전은 부르는 쪽이 얹는다.
        const Array<EditorSpriteContours::Segment>* GetSpriteContour(
            AssetHandle texture, const SpriteFrame& frame);
        // 적힌 배치가 있으면 읽고 기본 배치를 건너뛴다. UI 가 켜진 뒤에만 뜻이 있다.
        void RestoreEditorLayout();

        // 에디터 언어를 바꾼다. 글자 표를 다시 읽고, 성공하면 다음 프레임부터 그 언어다.
        // 파일이 없으면 거짓이고 지금 언어는 그대로다.
        bool SetEditorLocale(const char* locale);
        const String& GetEditorLocale() const { return m_locale; }
        // 글자 표 폴더에 있는 언어들이다(`<로케일>.yaml`). 정렬돼 있다.
        Array<String> GetAvailableLocales() const;
        // 열린 프로젝트의 에셋 레지스트리다. 인스펙터의 에셋 칸이 같은 타입의 목록을 여기서
        // 얻는다(D-116). 프로젝트가 없으면 빈 레지스트리다.
        const AssetRegistry& GetAssetRegistry() const;
        // 열린 프로젝트의 에셋 시스템이다. 프로젝트가 없으면 nullptr 다.
        AssetSystem* GetAssetSystem();
        // 열린 프로젝트의 에셋 폴더가 감시되고 있는가. 거짓이면 밖에서 바꾼 파일이 반영되지 않는다.
        bool IsWatchingAssets() const;

        // ── 에셋 파일 (D-139) ────────────────────────────────────────────
        //
        // 에셋 브라우저가 폴더를 만들고 이름을 바꾸고 옮기고 지우는 길이다. 경로는
        // **에셋 폴더 기준 상대경로**이고, 레지스트리가 적는 것과 같은 모양이다.
        //
        // **`.jmeta` 가 늘 함께 간다.** 짝을 잃으면 그 에셋의 아이디가 사라지고,
        // 그것을 가리키던 컴포넌트의 참조가 전부 풀린다(D-111).
        //
        // 어느 것이든 성공하면 레지스트리를 다시 스캔하고 캔버스의 참조를 다시 잇는다.
        const String& GetAssetRoot() const;
        // 에셋 폴더를 지금 다시 훑는다. 감시가 서지 않은 자리에서 사람이 새로 고치는 길이다.
        bool RescanAssets();
        bool CreateAssetFolder(const char* relativeFolder, const char* name);
        // 이름만 바꾼다. 확장자는 부르는 쪽이 붙인 그대로 쓴다.
        bool RenameAsset(const char* relativePath, const char* newName);
        // 다른 폴더로 옮긴다. `targetFolder` 가 비면 에셋 폴더의 뿌리다.
        bool MoveAsset(const char* relativePath, const char* targetFolder);
        // **복제한다**(D-175, 기존 `Duplicate`). 같은 폴더에 겹치지 않는 이름으로 내용을 복사한다.
        // **`.jmeta` 는 따라가지 않는다** - 그 안의 아이디까지 같아지면 두 파일이 한 에셋 행세를 한다.
        // 새 아이디는 스캔이 매긴다. 만든 파일의 상대경로를 돌려준다. 실패하면 빈 글자다.
        String DuplicateAsset(const char* relativePath);
        // **다른 폴더로 내용을 복사한다**(D-182, 기존 에셋 브라우저의 복사·붙여넣기).
        // 복제와 같은 몸이다 - 복제는 목적지가 제자리일 뿐이다. 그 폴더에 같은 이름이
        // 없으면 이름 그대로 가고, 있으면 뒤에 숫자를 붙인다. `.jmeta` 는 따라가지 않는다.
        String CopyAssetInto(const char* relativePath, const char* targetFolder);
        // **되돌릴 수 없다.** 부르는 쪽이 먼저 물어야 한다.
        bool DeleteAsset(const char* relativePath);
        bool RevealAsset(const char* relativePath);

        // **에셋 선택**(D-120). 에셋 브라우저가 고르고 인스펙터가 임포트 옵션을 보여 준다. 오브젝트 선택과 배타다 -
        // 에셋을 고르면 오브젝트 선택이 비고, 오브젝트를 고르면 에셋 선택이 빈다. 인스펙터는 하나만 보인다.
        void SetSelectedAsset(AssetId id);
        // **캔버스 자신을 고른다**(D-186, 기존 계층의 캔버스 줄). 고르면 인스펙터가
        // 캔버스의 값(배경색)을 보여 준다. 오브젝트·에셋 선택과 배타다 - 인스펙터는 하나만 보인다.
        void SetCanvasSelected(bool selected);
        bool IsCanvasSelected() const;
        AssetId GetSelectedAsset() const;
        // 고른 에셋의 메타(디스크에 있는 그대로)다. 고른 것이 없거나 메타를 읽지 못했으면 nullptr 다. 커맨드가 돌면
        // (판번호) 다시 읽으므로 편집·되돌리기 뒤에도 디스크와 같다.
        const AssetMetaFile* GetSelectedAssetMeta() const;
        // 고른 에셋의 메타를 고쳐 쓰는 커맨드가 가리킬 대상이다. 고른 것이 없으면 거짓이다.
        bool DescribeSelectedAssetMeta(AssetMetaTarget& target) const;
        // 이 프로젝트의 스크립트 DLL 이 실렸는지다. **열렸다고 실린 것은 아니다**(D-98) —
        // 아직 한 번도 빌드하지 않은 프로젝트도 열리므로, 스크립트가 있어야 하는 일은
        // 이것을 먼저 본다.
        bool IsScriptModuleLoaded() const;
        // 스크립트 DLL 을 싣지 못한 사유다. 실었거나 프로젝트가 스크립트를 가리키지
        // 않으면 비어 있다.
        const String& GetScriptModuleError() const;
        // 프로젝트 파일이 있는 폴더를 기준으로 상대경로를 푼다.
        // `.jproject` 의 `LastOpenedCanvasPath` 처럼 그 파일 안의 경로가 전부 상대다.
        String ResolveProjectPath(const char* relativePath) const;

        // 에디터 UI 를 켠다(D-63). 켜면 게임 화면은 `gameViewExtent` 크기의
        // 텍스처로 가고, 창에는 그 텍스처를 패널에 붙인 UI 가 그려진다.
        //
        // **크기는 게임 해상도지 패널 크기가 아니다.** 패널에 맞춰 만들면 창을
        // 끌 때마다 텍스처를 다시 만들게 되고, 무엇보다 게임이 보는 화면 크기가
        // 에디터 창에 따라 달라진다 - 화면 좌표를 쓰는 스크립트가 어긋난다.
        bool EnableEditorUi(const Extent2D& gameViewExtent);
        // 끄면 게임이 다시 백버퍼로 간다. 게임 실행과 같은 경로다.
        void DisableEditorUi();
        bool IsEditorUiEnabled() const;
        // 되돌리기 스택이다. 편집하는 패널은 값을 직접 쓰지 않고 여기에
        // 커맨드를 넣는다 - 그래야 Ctrl+Z 가 그 편집을 안다.
        EditorCommandManager& GetCommands();
        // 오브젝트에 붙는 안정된 번호다. 삭제를 되돌리면 오브젝트가 새로
        // 만들어지므로, 커맨드는 포인터가 아니라 이 번호를 들고 있어야 한다.
        EditorObjectRegistry& GetObjectIds();

        // 인스펙터가 무엇을 보여 줄지 정하는 값이다. 계층 패널이 고르고
        // 인스펙터가 읽는다.
        //
        // **`SafePtr` 인 이유는 오브젝트가 밑에서 사라질 수 있기 때문이다** -
        // 스크립트가 선택된 오브젝트를 지워도 인스펙터가 죽은 주소를 읽지
        // 않는다. 고른 것이 사라지면 선택은 저절로 비워진다.
        void SetSelectedObject(GameObject* object);
        GameObject* GetSelectedObject() const;

        // ── 여럿 고르기 ──────────────────────────────────────────────────
        //
        // 기존 엔진과 같은 모양이다: 고른 것들의 목록과, 그중 **주된 하나**.
        // 인스펙터는 주된 것을 보여 주고, 편집은 목록 전체에 미친다.
        //
        // `SetSelectedObject` 는 목록을 그것 하나로 바꾼다 - 맨 클릭이다.
        void SelectObjects(JArrayView<GameObject*> objects);
        // Ctrl·Shift 클릭이다. 이미 있으면 아무 일도 하지 않는다.
        void AddToSelection(GameObject* object);
        void RemoveFromSelection(const GameObject* object);
        bool IsSelected(const GameObject* object) const;
        void ClearSelection();
        // 살아 있는 것만 센다. 죽은 것은 목록에 남아 있어도 없는 것이다.
        std::size_t GetSelectionCount() const;
        // **고른 것을 한 줄로 적는다**(D-172). 캔버스 뷰의 오버레이가 쓴다 - 여럿 고르면
        // 주된 것과 나머지 수를, 이름이 없으면 `(이름 없음)` 을, 아무것도 없으면 그렇다고 적는다.
        // 로컬라이징 키를 거치므로 언어를 바꾸면 이 글도 바뀐다.
        void DescribeSelection(char* out, std::size_t size) const;
        Array<GameObject*> GetSelectedObjects() const;
        // **조상이 함께 골라졌으면 뺀다.** 부모를 옮기면 자식은 따라 움직이므로,
        // 둘 다 대상으로 삼으면 자식에게 두 번 적용된다. 트랜스폼 편집과 삭제가
        // 이 목록을 쓴다(기존 엔진 `GetSelectedTopLevel` 과 같은 이유다).
        Array<GameObject*> GetTopLevelSelectedObjects() const;

        // 이번 프레임의 입력을 UI 가 가져갔는가. **게임에 입력을 넘길지
        // 판단하는 자리다** - 에디터의 필드에 타자를 치는 중에 게임
        // 스크립트가 같은 키를 받으면 안 된다. UI 가 꺼져 있으면 거짓이다.
        bool UiWantsMouse() const;
        bool UiWantsKeyboard() const;
        // 패널을 들인다. 에디터가 소유하고, UI 를 끌 때 함께 내보낸다.
        // 같은 제목의 패널은 받지 않는다 - ImGui 가 제목으로 창을 식별하므로
        // 둘이 한 창을 나눠 쓰게 된다.
        bool AddPanel(OwnerPtr<EditorPanel> panel);

        // 모달 팝업 큐다(기존 엔진 `ImPopupDesc`). 한 번에 하나만 뜨고, 앞 것이 닫히면 다음
        // 프레임에 다음 것이 뜬다. 같은 Id 가 살아 있으면 그 핸들을 돌려주고 새로 만들지 않는다.
        // 널이거나 UI 가 꺼져 있으면 `InvalidPopupHandle` 이다.
        PopupHandle OpenPopup(OwnerPtr<EditorPopup> popup);
        // 닫기 요청. 뜨지 않고 기다리던 것도 닫힌다. 모르는 핸들은 무시한다.
        void ClosePopup(PopupHandle handle);
        bool IsPopupOpen(PopupHandle handle) const;
        bool IsPopupOpenById(const char* id) const;
        // 제목으로 찾는다. 없으면 nullptr 이다.
        EditorPanel* FindPanel(const char* title);
        std::size_t GetPanelCount() const;

        // 게임 화면이 그려지는 텍스처다. UI 가 꺼져 있으면 비어 있다.
        TextureHandle GetGameViewTexture() const;
        // 게임 뷰 패널이 이 프레임에 게임 화면을 붙였다. 그 프레임에만 게임 뷰를 렌더한다(D-63).
        void RequestGameView();
        // **게임이 지난 프레임에 그릴 것을 냈는가**(D-178). 게임 뷰가 "카메라 없음" 을
        // 언제 말할지 정하는 값이다 - 텍스처가 있는지만 보면 카메라가 없어도 검은 화면을
        // "실행 중" 이라고 말하게 된다.
        bool DidGameSubmitLastFrame() const;
        // 그 텍스처의 크기다. 게임 해상도이고 에디터 창과 무관하다.
        Extent2D GetGameViewExtent() const;

        // ── 캔버스 뷰(편집 화면) ─────────────────────────────────────────
        //
        // 기존 엔진에서 유니티의 씬 뷰 노릇을 하던 화면이다(D-130). 게임 뷰는 게임의
        // 카메라가 보는 것이고 이쪽은 **편집 카메라**가 보는 것이다. 둘은 역할이
        // 다르므로 같은 프레임에 함께 있어야 한다.
        //
        // **크기는 패널 크기다.** 게임 화면과 반대다 - 게임은 해상도가 정해진 화면이라
        // 패널에 맞춰 늘리지만, 편집 화면은 패널이 곧 화면이라 패널 크기로 그려야
        // 넓혔을 때 흐려지지 않는다.
        //
        // 매 프레임 다시 건다. 걸지 않은 프레임에는 그리지 않고 텍스처는 그대로 둔다
        // (게임 뷰와 같은 규칙이다, D-63).
        // ── 시뮬레이션 (D-131) ───────────────────────────────────────────
        //
        // 기존 엔진과 같은 뜻이다: **재생을 누르기 전의 캔버스로 돌아온다.** 게임을 돌리면
        // 스크립트가 값을 바꾸고 오브젝트를 만들고 지우는데, 그것이 편집 중인 캔버스에
        // 그대로 남으면 저장했을 때 게임이 만든 상태가 파일이 된다.
        //
        // **되살릴 값을 먼저 뜨지 못하면 재생하지 않는다**(§11.5). 캔버스를 글자로 뜨지
        // 못하면 거짓을 돌려주고 아무 일도 하지 않는다 - 돌려놓을 수 없는 재생은
        // 편집 내용을 잃는 일이다.
        bool StartSimulation();
        // 멈추고 재생 전의 캔버스로 되돌린다. 돌지 않고 있으면 아무 일도 하지 않는다.
        void StopSimulation();
        bool IsSimulationPlaying() const;
        // 메뉴와 단축키가 함께 쓰는 한 손짓이다. 돌고 있으면 세우고, 아니면 시작한다.
        void ToggleSimulation();
        // 재생 중에만 뜻이 있다. 멈춰 세우면 그린 것은 그대로 두고 게임만 세운다.
        void SetSimulationPaused(bool paused);
        bool IsSimulationPaused() const;

        bool RequestCanvasView(
            const Extent2D& extent, float centerX, float centerY, float orthographicSize);
        // 3D 의 편집 화면이다(D-136). 바라보는 점과 그 둘레를 도는 거리·각을 준다 -
        // 평면을 밀고 당기는 것으로는 3D 의 뒤를 볼 수 없다.
        bool RequestCanvasView3D(
            const Extent2D& extent,
            float centerX, float centerY, float centerZ,
            float distance, float yawDegrees, float pitchDegrees);
        TextureHandle GetCanvasViewTexture() const;
        // 실제로 잡혀 있는 텍스처의 크기다. 요청한 크기를 **올림**한 값이라 패널을
        // 조금 끌 때마다 텍스처를 다시 만들지 않는다.
        Extent2D GetCanvasViewExtent() const;

        bool Tick(float deltaTime);
        void CloseProject();
        void Shutdown();

        // 열려 있는 프로젝트의 캔버스다. 없으면 nullptr 이다.
        Canvas* GetCanvas();
        // 열려 있는 캔버스로 `.jcanvas` 를 읽고 쓴다.
        // 읽기는 **빈 캔버스에만** 들어간다 — 이미 내용이 있으면 거절한다.
        bool LoadCanvas(const char* path, CanvasFileError& error);
        bool SaveCanvas(const char* path, CanvasFileError& error);
        // **복사·붙여넣기.** 고른 것 중 맨 위 것들의 나무를 떠 둔다(뜨지 못하면 거짓이고
        // 클립보드는 그대로다). 붙여넣기는 커맨드 하나로 가고, 붙인 뿌리들을 고른다 -
        // 주된 선택의 형제로 붙이고, 고른 것이 없으면 캔버스 뿌리에 붙인다.
        bool CopySelection();
        // `asChild` 면 고른 것의 자식으로 붙인다(D-166, 기존 `PasteObjectsAsChild`·Ctrl+Shift+V).
        bool PasteClipboard(bool asChild = false);
        bool HasClipboard() const
        {
            return false == m_clipboard.IsEmpty();
        }
        // **컴포넌트 복사·붙여넣기**(D-167, 기존 `DrawCopyComponentMenuItem`·
        // `DrawPasteComponentMenuItem`). 오브젝트 클립보드와 **따로 산다** - 오브젝트를 복사해 둔
        // 채로 컴포넌트를 옮기고 싶은 것이 보통이고, 하나로 합치면 둘 중 하나가 늘 지워진다.
        // 붙여넣기는 같은 타입을 하나 **더** 붙인다 - 있는 것을 덮어쓰면 덮인 값이 어디로
        // 갔는지 보이지 않는다. **다만 하나만 붙는 타입은 거절한다**(D-180) - 기존 엔진은
        // 붙여넣기만 다중성 판정을 지나치지 않아 Transform 이 둘씩 붙었고, 그렇게 되면
        // 조회가 먼저 붙은 쪽만 돌려주어 나중 것은 보이지도 지워지지도 않는다.
        bool CopyComponent(ComponentBase& component);
        bool PasteComponent(GameObject& object);
        // 그 오브젝트에 떠 둔 컴포넌트를 붙일 수 있는가. 메뉴가 회색으로 그릴지 정하는 값이다.
        bool CanPasteComponent(const GameObject& object) const;
        // 이미 붙어 있는 컴포넌트에 값만 덮는다. 떠 둔 것이 그 타입이 아니면 거짓이다.
        bool PasteComponentValues(GameObject& object, ComponentBase& component);
        // 그 컴포넌트에 값을 덮을 수 있는가. 메뉴가 회색으로 그릴지 정하는 값이다.
        bool CanPasteComponentValues(const ComponentBase& component) const;
        bool HasComponentClipboard() const
        {
            return m_hasComponentClipboard;
        }
        // 클립보드에 든 컴포넌트의 타입 이름이다. 없으면 nullptr 이다. 메뉴가 무엇이 붙을지
        // 말하는 데 쓴다.
        const char* GetComponentClipboardTypeName() const;
        // **다른 캔버스를 연다**(D-174, 기존 에셋 브라우저의 캔버스 더블클릭). 프로젝트 파일에 적힌
        // 캔버스 하나만 편집할 수 있었다 - 새로 만들 길도, 다른 것을 열 길도 에디터 안에 없었다.
        //
        // 여는 것은 지금 캔버스의 **내용을 갈아 끼우는 일**이다. 고른 것·오브젝트 번호·되돌리기
        // 더미가 지난 캔버스의 것이므로 함께 비우고, 시뮬레이션 중이면 먼저 멈춘다.
        // 프레임 밖에서 한다(D-93) - 파일을 읽는 동안 UI 가 그 오브젝트들을 그리고 있으면 안 된다.
        void RequestOpenCanvas(const char* assetRelativePath);
        // **새 캔버스 파일을 만든다**(기존 `에셋 추가 ▸ 캔버스`). 에셋 폴더 아래 `folder` 에
        // 겹치지 않는 이름으로 빈 캔버스를 쓰고, 등록한 뒤 그것을 연다.
        // 만든 파일의 에셋 폴더 기준 경로를 돌려준다. 실패하면 빈 글자다.
        String CreateCanvasAsset(const char* folder);
        // 저장하지 않은 변경을 물어본 답이다(D-174). 0 = 저장하고 열기, 1 = 그냥 열기,
        // 그 밖(취소·닫기) = 아무것도 하지 않는다. 팝업이 부른다.
        void AnswerCanvasSwitch(int choice);
        // 저장 메뉴와 Ctrl+S 가 부른다. 이 프레임의 UI 가 끝난 뒤 처리한다 - 아는 경로가 있으면
        // 거기에, 없으면 대화상자로 경로를 받아 저장하고, 실패하면 팝업으로 알린다.
        void RequestSaveCanvas();
        // **프로젝트 저장**(D-151, 기존 `MenuFileSaveProject`). 캔버스를 저장하고, 그것이
        // 되면 세션(보던 캔버스·카메라·언어·창 배치)도 프로젝트 파일에 적는다.
        // 캔버스를 저장하지 못했으면 세션도 적지 않는다 - 저장되지 않은 캔버스를 "마지막으로
        // 연 것" 으로 적어 두면 다음에 열 때 그 파일이 없거나 옛 것이다.
        void RequestSaveProject();
        // 프로젝트를 골라 연다. 대화상자는 프레임 밖에서 뜬다 - 지금 연 프로젝트는 닫힌다.
        void RequestOpenProject();
        // **새 프로젝트**(D-160, 기존 루트 도크의 `MenuFileNewProject`). 프레임 밖에서 폴더를 고르고,
        // 이름과 프레임워크를 받는 팝업을 띄운다.
        void RequestNewProject();
        // `<parentFolder>/<name>/` 에 프로젝트를 세우고, 이 프레임이 끝나면 그 프로젝트로 넘어간다.
        // 실패하면 거짓이고 `failure` 에 까닭이 온다. 팝업이 그것으로 번역된 문장을 고른다.
        bool CreateProject(const char* parentFolder, const char* name, FrameworkKind framework,
            ProjectCreateFailure* failure = nullptr);
        // ── 스프라이트 프레임 고르기(D-165, 기존 `SpriteFramePick`) ─────────────────────────────
        // 인스펙터의 "프레임 고르기" 가 시작하고, 스프라이트 뷰어에서 칸을 누르면 끝난다. 끝나면 대상 컴포넌트의
        // `frameIndex` 를 커맨드로 고친다. 대상은 포인터가 아니라 주소다 - 고르는 사이 지웠다 되살려도 찾는다.
        // 한 번에 하나다. 새로 시작하면 앞의 것은 취소된다.
        bool BeginSpriteFramePick(AssetId sprite, const ComponentAddress& target);
        void CancelSpriteFramePick();
        bool IsSpriteFramePickActive() const
        {
            return m_framePickActive;
        }
        bool IsSpriteFramePickFor(const ComponentAddress& target) const;
        // 고르는 중인 스프라이트의 텍스처다. 뷰어가 어느 탭에서 고르는지 안다.
        AssetId GetSpriteFramePickTexture() const
        {
            return m_framePickTexture;
        }
        // 뷰어가 부른다. 대상이 사라졌거나 쓸 수 없으면 거짓이고, 어느 쪽이든 고르기는 끝난다.
        bool CompleteSpriteFramePick(std::uint32_t frame);

        // 경로 칸의 "찾아보기" 를 프레임 밖에서 처리하게 맡긴다(D-164). 한 프레임에 하나다 - 뒤의 것이 앞의 것을 덮는다.
        void RequestBrowsePath(const PathBrowseRequest& request);
        // 마지막으로 열거나 저장한 캔버스 경로. 없으면 빈 글자다.
        const String& GetCanvasPath() const
        {
            return m_canvasPath;
        }

        // 이 세션의 렌더러다. **진단과 화면 되읽기 경로다** - 매 프레임 도는 길이
        // 아니다. 프레임을 여닫는 것은 여전히 엔진의 일이다.
        Renderer* GetRenderer();
        const Renderer* GetRenderer() const;

        bool IsInitialized() const;
        bool HasOpenProject() const;
        FrameStatus GetLastFrameStatus() const;

    private:
        // 렌더러가 뷰를 다 기록한 뒤, 프레임을 닫기 전에 불린다.
        static bool DrawEditorOverlay(
            IRHICommandContext& commands,
            TextureHandle backBuffer,
            std::uint32_t frameSlot,
            void* user);
        // **메뉴는 두 겹이다**(D-134). 기존 엔진과 같다: 프로젝트에 대한 것은 도크 뿌리의
        // 메뉴이고, 지금 연 캔버스에 대한 것은 메인 도크의 메뉴다.
        void DrawRootMenuBar();
        void DrawMainMenuBar();
        // 창 전체를 덮는 도크 뿌리. 메인 도크 하나만 여기에 붙는다.
        void DrawRootDock(const Extent2D& display);
        // 패널 하나를 여닫는 메뉴 항목이다. 설정·디버그 메뉴가 같은 모양으로 쓴다.
        void DrawPanelMenuItem(const char* panelTitle, const char* label);
        // 도구 창들이 붙는 안쪽 도크. 자기 메뉴 막대를 가진다.
        void DrawMainDock(float deltaTime);
        // 메뉴 항목 하나를 단축키 표의 값으로 그린다: 이름·조합키 글자·할 수 있는지.
        bool DrawShortcutItem(EditorShortcut id, const char* label);
        bool BuildEditorUi(float deltaTime);
        // 큐의 맨 앞 팝업 하나를 그린다. 닫힌 것은 먼저 빼고, 닫히면 그 자리에서 뺀다.
        void DrawPopups();
        // `RequestSaveCanvas` 를 프레임 밖에서 처리한다.
        void PerformSaveRequest();
        // 프로젝트 열기도 저장과 같다: **막히는 대화상자라 프레임 밖에서** 한다(D-93).
        void PerformOpenProjectRequest();
        void PerformNewProjectRequest();
        void PerformBrowseRequest();
        // `RequestOpenCanvas` 를 프레임 밖에서 처리한다(D-174).
        void PerformOpenCanvasRequest();
        // 그림·외곽선 캐시를 지금 프로젝트의 에셋 시스템에 잇는다. 프로젝트가 없으면 끊는다(D-165).
        void BindAssetTools();
        // 지금 연 것을 닫고 그 프로젝트를 연다. 열기와 새 프로젝트가 같은 길로 간다. 프레임 밖에서 부른다.
        bool SwitchToProject(const char* projectFilePath);
        void PerformImportRequest();
        void ReleaseEditorUi();
        void DestroyPanels();
        // 디바이스가 이미 사라진 뒤에 부른다.
        void AbandonEditorUi();

        bool CreateSelectedFramework(FrameworkKind framework);
        void DestroySelectedFramework();
        void ReleaseProcessResources();

        OwnerPtr<IPlatform> m_platform;
        OwnerPtr<IRHIModule> m_rhiModule;
        OwnerPtr<EngineInstance> m_engine;
        OwnerPtr<IFramework> m_framework;
        EditorUI m_ui;
        Array<OwnerPtr<EditorPanel>> m_panels;
        // 앞이 뜨는 것이고 뒤는 기다린다. 닫힌 것은 그리기 전에 뺀다.
        Array<OwnerPtr<EditorPopup>> m_popups;
        PopupHandle m_nextPopupHandle = 1;
        String m_canvasPath;
        bool m_saveRequested = false;
        bool m_openProjectRequested = false;
        bool m_newProjectRequested = false;
        PathBrowseRequest m_browseRequest;
        bool m_browseRequested = false;
        // 열어 달라고 한 캔버스(에셋 폴더 기준 상대경로). 비어 있으면 요청이 없다.
        String m_openCanvasRequest;
        // 저장하지 않은 변경을 묻는 동안 붙들어 둔 것과, 그 답을 받았다는 표시다.
        String m_pendingCanvasPath;
        bool m_canvasSwitchConfirmed = false;
        ComponentAddress m_framePickTarget;
        AssetId m_framePickTexture;
        bool m_framePickActive = false;
        // 만든 프로젝트. 프레임이 끝나면 그리로 넘어간다 - 팝업 안(프레임 안)에서 프로젝트를 닫을 수 없다.
        String m_pendingProjectPath;
        Array<ObjectTreeSnapshot> m_clipboard;
        ComponentSnapshot m_componentClipboard;
        bool m_hasComponentClipboard = false;
        bool (*m_fileDialog)(const FileDialogDesc& desc, String& outPath, void* user) = nullptr;
        void* m_fileDialogUser = nullptr;
        // 고른 것들. 0번이 주된 것은 아니다 - 주된 것은 따로 든다(기존 엔진과
        // 같다). Ctrl 로 빼다 보면 목록의 머리가 바뀌는데, 그때마다 인스펙터가
        // 다른 것을 보여 주면 손이 미끄러진 것처럼 보인다.
        Array<SafePtr<GameObject>> m_selection;
        SafePtr<GameObject> m_selected;
        EditorCommandManager m_commands;
        // 마지막으로 에셋 해석을 돌린 커맨드 판번호다. 판이 바뀌면(실행·되돌리기·다시 실행)
        // 프레임워크의 `BindCanvasAssets` 를 다시 부른다(D-115) - `xxxId` 를 바꾼 커맨드만
        // 골라내지 않는다. 되돌리기와 붙여넣기도 아이디를 바꾼다.
        std::uint64_t m_boundRevision = 0;
        AssetId m_selectedAsset;
        // 캔버스 자신을 골랐는가(D-186). 위의 둘과 배타다.
        bool    m_canvasSelected = false;
        OwnerPtr<AssetMetaFile> m_selectedAssetMeta;
        bool m_selectedAssetMetaLoaded = false;
        void ReloadSelectedAssetMeta();
        EditorObjectRegistry m_objectIds;
        TextureHandle m_gameView;
        Extent2D m_gameViewExtent;
        bool m_gameViewRequested = false;
        // 캔버스 뷰가 그려지는 텍스처와 이번 프레임의 요청(D-130).
        TextureHandle m_canvasView;
        Extent2D m_canvasViewExtent;
        EditorViewDesc m_canvasViewRequest;
        bool m_canvasViewRequested = false;
        // 이번 프레임에 요청된 크기의 텍스처를 마련한다. 이미 그 크기면 아무 일도 하지 않는다.
        bool EnsureCanvasViewTexture(const Extent2D& extent);
        void ReleaseCanvasViewTexture();
        // 재생을 누르기 전의 캔버스 글자다(D-131). 비어 있으면 돌지 않고 있다는 뜻이다.
        String m_simulationSnapshot;
        bool m_simulationPlaying = false;
        bool m_simulationPaused = false;
        // 캔버스를 비운다. 되돌리기 위해 다시 읽어 넣기 전에 부른다.
        void ClearCanvasObjects();
        bool m_uiEnabled = false;
        // 메뉴에서 끝내기를 골랐다. 다음 틱에서 내려간다.
        bool m_exitRequested = false;
        // 첫 프레임에 한 번만 기본 자리를 잡는다. 그 뒤로는 사용자가 옮긴 자리다.
        bool m_dockLayoutBuilt = false;
        // 도크 뿌리의 배치는 한 번만 잡는다. 메인 도크가 거기 붙는 것이 전부다.
        bool m_rootLayoutBuilt = false;
        // 만든 쪽이 무엇을 만들었는지 기억한다. `IFramework` 에는 캔버스로 가는 길이 없고,
        // 그것을 뚫으려면 호스트 계층이 `Canvas` 를 보아야 한다(D-42 가 막는 방향이다).
        FrameworkKind m_frameworkKind = FrameworkKind::Framework2D;
        // 상대경로를 풀 기준이다. 파일로 열었을 때만 채워진다.
        String m_projectFilePath;
        // 글자 표가 사는 곳과 지금 언어·폴백이다(D-146). 언어를 바꾸려면 다시 읽어야 하고,
        // 다시 읽으려면 처음에 어디서 읽었는지를 들고 있어야 한다.
        String m_localizationDirectory;
        // 아이콘 글꼴의 경로다. 실행 파일 기준으로 풀어 둔 값이다(D-176).
        String m_iconFontPath;
        String m_locale;
        String m_fallbackLocale;
        // 프로젝트를 열 때 파일에서 읽은 캔버스 뷰 카메라다. 패널이 만들어질 때 가져간다 -
        // 프로젝트를 여는 시점에는 패널이 아직 없을 수 있다.
        float m_sessionCameraX = 0.0f;
        float m_sessionCameraY = 0.0f;
        float m_sessionCameraSize = 0.0f;
        // 적힌 배치를 읽었는가. 읽었으면 기본 배치를 만들지 않는다 - 둘이 같은 프레임에
        // 겹치면 사람이 옮겨 둔 자리가 매번 지워진다.
        bool m_layoutRestored = false;
        // 이번 저장 요청이 프로젝트 저장인가. 캔버스 저장이 성공하면 세션도 적는다.
        bool m_saveProjectRequested = false;
        // 가져오기 대화상자를 띄울 차례인가, 그리고 어느 폴더로 가져올 것인가.
        bool m_importRequested = false;
        String m_importFolder;
        // 에셋의 작은 그림들. 프로젝트를 닫을 때 비운다 - 다음 프로젝트의 아이디는 다른 파일이다.
        OwnerPtr<EditorThumbnails> m_thumbnails;
        // 스프라이트의 실제 모양. 같은 이유로 프로젝트를 닫을 때 비운다.
        OwnerPtr<EditorSpriteContours> m_contours;
        // 뿌리 도크에 붙는 파일 창들(D-155). 지금은 스프라이트 뷰어 하나다.
        OwnerPtr<SpriteViewerWindow> m_spriteViewer;
        // 뿌리 도크 노드다. 파일 창을 처음 띄울 때 여기 붙인다.
        std::uint32_t m_rootDockId = 0;

        GraphicsApi m_graphicsApi = GraphicsApi::D3D12;
        FrameStatus m_lastFrameStatus = FrameStatus::InvalidState;
        bool m_initialized = false;
    };
}

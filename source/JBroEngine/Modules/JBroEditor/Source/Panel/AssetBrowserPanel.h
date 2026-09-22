#pragma once

#include <JBro/Editor/EditorPanel.h>

#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

namespace JBro
{
    struct AssetRecord;

    // 레지스트리의 에셋을 보여 주고, 고르면 인스펙터가 그 에셋의 임포트 옵션을 받는다(D-116·D-120).
    //
    // **디스크가 아니라 레지스트리를 본다.** 스캔이 걸러 낸 것(숨김 폴더, 무시 패턴, 모르는 타입)은
    // 여기도 없다. 이미지는 Texture 와 Sprite 두 레코드지만 파일은 하나이므로 한 줄이고, 그 줄은
    // Texture 레코드를 가리킨다 - 인스펙터가 짝 Sprite 를 찾는다. 줄은 공용 트리 위젯이 그린다(§11.1).
    //
    // **두 칸이다**(D-139). 기존 엔진 `CAssetBrowserTool` 과 같은 모양이다: 왼쪽은 폴더 나무,
    // 오른쪽은 지금 연 폴더의 내용. 한 나무에 전부 펼치면 폴더가 깊어질수록 파일이 오른쪽으로
    // 밀려 이름이 보이지 않는다.
    //
    // 파일을 **다루기도 한다**: 새 폴더·이름 바꾸기·삭제·폴더로 끌어 옮기기·탐색기에서 보기.
    // 그 길은 `EditorApplication` 에 있고 `.jmeta` 가 늘 함께 움직인다.
    //
    // **파일은 여러 개를 고를 수 있다**(D-141). 옮기고 지우는 일은 하나씩 하는 일이 아니다.
    // Ctrl 은 하나를 더하고 빼며, Shift 는 닻에서 누른 줄까지를 고른다. 폴더는 하나씩만
    // 고른다 - 폴더와 파일을 섞어 고르면 "이 폴더와 그 안의 것" 이 무엇을 뜻하는지 흐려진다.
    class AssetBrowserPanel final : public EditorPanel
    {
    public:
        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        bool OnCreate(EditorApplication& editor) override;
        void OnDraw() override;
        EditorDock GetPreferredDock() const override { return EditorDock::Bottom; }

    private:
        // 이번 프레임에 그릴 파일 하나다. 레지스트리 레코드와 그 경로를 폴더·이름으로 갈라 둔다.
        struct Entry
        {
            const AssetRecord* record = nullptr;
            String folder;
            const char* name = nullptr;
        };

        void Collect();
        // 왼쪽 칸. `folder` 바로 아래의 폴더들을 그린다.
        void DrawFolderTree(const String& folder);
        // 오른쪽 칸. 지금 연 폴더의 하위 폴더와 파일.
        void DrawContents();
        bool FolderHasMatch(const String& folder) const;
        void DrawFile(const Entry& entry);
        // 아이콘 보기의 칸 하나다(D-147). 그림과 이름이 세로로 선다.
        void DrawFileTile(const Entry& entry);
        // 줄에 붙는 손놀림(고르기·끌기·메뉴)은 두 보기가 같이 쓴다 - 보기를 바꿨다고
        // 고르는 법이 달라지면 같은 창이 둘로 나뉜다.
        void HandleEntryInput(const Entry& entry);
        // 고른 것들. 상대경로로 기억한다 - 레코드 포인터는 다시 훑으면 다른 것을 가리킨다.
        bool IsSelected(const String& path) const;
        void SelectOnly(const String& path);
        void ToggleSelected(const String& path);
        // 닻에서 이 줄까지. **이번 프레임에 그린 순서**를 쓴다 - 사람이 보는 순서가 그것이다.
        void SelectRange(const String& to);
        // 고른 것들을 지운다. 하나든 여럿이든 같은 길로 간다.
        void DeleteSelection();
        // 길잡이 줄. 누르면 그 자리로 간다.
        void DrawBreadcrumb();
        // 빈자리·줄의 우클릭 메뉴. 같은 항목을 쓴다.
        void DrawBackgroundMenu();
        void DrawEntryMenu(const String& relativePath, bool isFolder);
        // 이름 바꾸기와 삭제는 물어보고 한다. 삭제는 되돌릴 수 없다.
        void DrawRenamePopup();
        void DrawDeletePopup();
        void DrawNewFolderPopup();
        // 폴더 줄이 받는 자리. 파일을 끌어다 놓으면 그 폴더로 옮긴다.
        void DrawFolderDropTarget(const String& folder);
        // 이 폴더가 저 폴더의 안인가(자기 자신 포함). 폴더를 자기 안으로 옮기지 못하게 한다.
        static bool IsInside(const String& path, const String& folder);

        EditorApplication* m_editor = nullptr;
        String m_filter;
        Array<Entry> m_entries;
        // 마지막으로 모은 레지스트리 판번호다. 같으면 다시 모으지 않는다.
        std::uint64_t m_collectedRevision = 0;
        bool m_collected = false;
        // 모든 폴더(조상 포함)의 상대경로. 정렬돼 있고 겹치지 않는다.
        Array<String> m_folders;
        // 오른쪽 칸이 보여 주는 폴더. 빈 글자면 에셋 폴더의 뿌리다.
        String m_openFolder;
        // 왼쪽 칸의 폭. 사람이 끌어 옮길 수 있다.
        float m_treeWidth = 200.0f;
        // 아이콘으로 볼 것인가. 기존 엔진도 두 보기를 오갔다 - 그림을 고르는 일에는
        // 이름보다 그림이 빠르고, 이름을 훑는 일에는 목록이 빠르다.
        bool m_iconView = false;
        // 아이콘 한 칸의 변 길이(픽셀)다.
        float m_iconSize = 72.0f;

        // 고른 파일들의 상대경로다. 폴더는 여기 들어오지 않는다.
        Array<String> m_selection;
        // 범위 선택의 닻. Shift 로 누르면 여기서부터 누른 줄까지다.
        String m_anchor;
        // 이번 프레임에 오른쪽 칸이 그린 파일들의 차례다. 범위 선택이 이것을 쓴다.
        Array<String> m_visible;
        // 왼쪽 단추를 누른 파일 줄이다. 고르기는 **같은 줄에서 뗐을 때만** 한다.
        String m_pressedPath;

        // 물어보는 중인 것. 비어 있으면 묻지 않는다.
        String m_pending;
        bool m_pendingIsFolder = false;
        String m_nameBuffer;
        // 이번 프레임에 열어야 할 팝업. ImGui 는 메뉴 안에서 `OpenPopup` 을 부르면
        // 그 메뉴가 닫히며 함께 닫히므로, 메뉴 밖에서 한 번 더 연다.
        bool m_openRename = false;
        bool m_openDelete = false;
        bool m_openNewFolder = false;
        String m_message;
    };
}

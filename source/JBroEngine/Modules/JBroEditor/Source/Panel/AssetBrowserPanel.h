#pragma once

#include <JBro/Editor/EditorPanel.h>

#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

namespace JBro
{
    struct AssetRecord;

    // 레지스트리의 에셋을 폴더 나무로 보여 주고, 고르면 인스펙터가 그 에셋의 임포트 옵션을 받는다(D-116·D-120).
    //
    // **디스크가 아니라 레지스트리를 본다.** 스캔이 걸러 낸 것(숨김 폴더, 무시 패턴, 모르는 타입)은 여기도 없다.
    // 이미지는 Texture 와 Sprite 두 레코드지만 파일은 하나이므로 한 줄이고, 그 줄은 Texture 레코드를 가리킨다 -
    // 인스펙터가 짝 Sprite 를 찾는다. 줄은 공용 트리 위젯이 그린다(§11.1).
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
        // `folder` 바로 아래의 폴더들과 파일들을 그린다. 검색 중이면 걸리는 파일이 있는 폴더만 남는다.
        void DrawFolder(const String& folder);
        bool FolderHasMatch(const String& folder) const;
        void DrawFile(const Entry& entry);

        EditorApplication* m_editor = nullptr;
        String m_filter;
        Array<Entry> m_entries;
        // 모든 폴더(조상 포함)의 상대경로. 정렬돼 있고 겹치지 않는다.
        Array<String> m_folders;
    };
}

#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Editor/EditorPanel.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    class EditorApplication;

    // **사운드 이펙트 한 장**이다(D-306, 기존 `CEffectEditorWindow`·`CEffectEditorWidget`). `.jfx` 마다 하나씩 서는 비고유 패널이고,
    // 뿌리에 메인 도크와 나란히 붙는 "사운드 이펙트" 도크에 속한다. 같은 에셋을 다시 열면 그 패널을 앞으로 가져온다
    // (`EditorApplication::OpenSoundEffectEditor`).
    //
    //   파일  ▸ 사운드 이펙트 열기 ▸ …  /  저장  /  디스크에서 다시 읽기  /  닫기
    //   (Reverb) (LowPass) (HighPass) (Echo) …      ← 종류 칩, 고른 것이 강조색이다(D-309)
    //   방이나 홀에서 울리는 잔향을 더합니다          ← 종류의 한 줄 설명
    //   ┌ 응답 그래프 ──────────────────┐            ← 종류마다 주파수 응답·입출력 곡선·되풀이·꼬리(DSP 와 같은 식)
    //   roomSize      [■■■■□□□□□ 0.50]               ← 그 종류의 파라미터 표(`AudioEffectKinds`), 단위와 로그 눈금까지
    //   [적용] [되돌리기]                               ← 바뀐 것이 있을 때만
    //   ── 미리 듣기 ──
    //   테스트 사운드 [오디오 에셋 ▼]  [재생]  반복 재생  바이패스  출력 미터
    //
    // 고치는 것은 창 안의 초안이고 **저장(적용)할 때 파일에 쓴다** - 프로젝트 설정과 같은 모양이다. 저장하면 에셋이 제자리에서 다시
    // 읽혀 재생 중인 소스의 사슬이 다시 시작하지 않고 바뀐다. 미리 듣기는 초안을 건 채로 울리고, 슬라이더를 움직이면 바로 들린다.
    class SoundEffectPanel final : public InstancePanel
    {
    public:
        static constexpr const char* TypeName = "SoundEffect";
        static constexpr const char* DockAreaName = "SoundEffect";

        const char* GetTitle() const override;
        // 탭에는 에셋의 경로가 보인다. 저장하지 않은 것이 있으면 끝에 `*` 가 붙는다.
        const char* GetDisplayTitle() const override;
        Bool OnCreate(EditorApplication& editor) override;
        // 이 창이 튼 미리 듣기를 멈춘다.
        void OnDestroy() override;
        Bool HasMenuBar() const override;
        void OnMenuBar() override;
        void OnDraw() override;

        // 이 패널에 그 에셋을 싣는다(파일을 읽는다). `.jfx` 가 아니거나 읽지 못하면 거짓이다.
        Bool Show(AssetId asset);
        AssetId GetAsset() const { return m_asset; }
        const AudioEffectSettings& GetDraft() const { return m_draft; }
        Bool IsDirty() const;
        // 초안을 파일에 쓴다(기존 `Apply Effect`·메뉴의 저장).
        Bool Save();
        // 파일을 다시 읽는다. 저장하지 않은 것은 버린다.
        Bool ReloadFromDisk();
        // 종류를 바꾼다. 같은 이름의 파라미터는 값을 잇는다(기존 `NormalizeEffectParams`).
        void SetKind(AudioEffectKind kind);
        void SetParameter(UInt32 index, Float value);
        void SetTestSound(AssetId sound) { m_testSound = sound; }
        // 시험 소리를 초안을 건 채로 튼다. 시험 소리가 없으면 거짓이다.
        Bool StartPreview();
        void StopPreview();
        Bool IsPreviewing() const;
        // 초안을 저장된 값으로 되돌린다(파일은 읽지 않는다).
        void Revert();
        // 켜면 미리 듣기가 효과 없이 울린다 - 원음과 견주는 스위치다(D-309). 저장하지 않는 창의 상태다.
        void SetBypass(Bool bypass);
        Bool IsBypassed() const { return m_bypass; }

    private:
        void RefreshChoices();
        void DrawKind();
        void DrawGraph();
        void DrawParameters();
        void DrawPreview();
        // 초안이 바뀌었으면 울리는 미리 듣기에 곧바로 건다.
        void PushDraftToPreview();
        void UpdateTitle();

        EditorApplication* m_editor = nullptr;
        AssetId m_asset;
        String m_path;
        String m_title;
        AudioEffectSettings m_draft;
        AudioEffectSettings m_saved;
        AssetId m_testSound;
        Bool m_loop = false;
        Bool m_bypass = false;
        Bool m_previewing = false;
        // 그래프의 점이다. 그릴 때마다 초안에서 다시 잰다(종류마다 점 수가 다르다).
        static constexpr UInt32 GraphPoints = 96;
        Float m_graphX[GraphPoints] = {};
        Float m_graphY[GraphPoints] = {};
        Float m_guideX[GraphPoints] = {};
        Float m_guideY[GraphPoints] = {};
        String m_message;
        Bool m_messageIsError = false;
        // 시험 소리와 열 이펙트의 고르기 목록이다. 레지스트리가 바뀔 때만 다시 모은다.
        UInt64 m_choicesRevision = 0;
        Bool m_choicesBuilt = false;
        Array<String> m_soundNames;
        Array<const char*> m_soundNamePointers;
        Array<AssetId> m_soundIds;
        Array<String> m_effectNames;
        Array<AssetId> m_effectIds;
    };
}

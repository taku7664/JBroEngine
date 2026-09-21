#pragma once

#include <JBro/Editor/Command/ListEdit.h>
#include <JBro/Editor/Command/SetPropertyCommand.h>
#include <JBro/Editor/ScalarRun.h>

#include <JBro/Editor/EditorPanel.h>

#include <JBro/Asset/AssetMetaFile.h>

namespace JBro
{
    class ComponentBase;
    struct PropertyEditInfo;
    struct PropertyTable;
    struct TypeDescriptor;

    namespace Widget
    {
        class FormLayout;
    }

    // 고른 오브젝트의 컴포넌트와 그 필드를 보여 주고 고치게 한다.
    //
    // **컴포넌트 타입을 하나도 모른다.** 리플렉션(D-56)이 내주는 필드를 타고 내려가
    // 잎사귀에서 코덱을 만난다. 새 컴포넌트를 더해도 이 파일은 그대로다 -
    // 기존 엔진에서 "이 타입은 이렇게 그린다" 는 지식이 여섯 군데 흩어졌던 자리다.
    //
    // **값을 직접 쓰지 않는다.** 위젯이 바꾼 값을 도로 되돌려 놓고 커맨드를 만들어
    // 매니저에 넣는다 - 그래야 Ctrl+Z 가 그 편집을 안다(D-71).
    //
    // **줄은 라벨 칸과 값 칸으로 나뉜다**(ProjectRule §11.3). 위젯에는 `"##이름"` 만
    // 넘긴다 - 보이는 이름은 왼쪽 칸이 그린다.
    class InspectorPanel final : public EditorPanel
    {
    public:
        const char* GetTitle() const override;
        const char* GetDisplayTitle() const override;
        bool OnCreate(EditorApplication& editor) override;
        void OnDraw() override;
        EditorDock GetPreferredDock() const override { return EditorDock::Right; }

    private:
        // 목록 원소 안을 그리는 중이다(D-89). 그 안의 편집은 컴포넌트의 길이 아니라 원소 번호와
        // 원소 안의 필드 길을 든 목록 편집으로 적힌다 - 목록은 컴포넌트 길의 잎사귀이고(D-86),
        // 다 그린 뒤 목록 위젯이 적힌 편집을 고른 대상마다 다시 적용한다.
        struct ElementScope
        {
            Array<ListEdit>* edits = nullptr;
            std::uint32_t index = 0;
            std::uint32_t fieldPath[ListEdit::MaxFieldDepth] = {};
            std::uint32_t fieldDepth = 0;
        };

        // 지금 그리는 컴포넌트와, 거기서 여기까지 내려온 길이다. 잎사귀에서
        // 커맨드를 만들 때 둘 다 필요하다.
        // 에셋의 임포트 옵션을 그리는 중이면 이것이 있다(D-120). 잎사귀 편집은 컴포넌트 커맨드가 아니라
        // 메타 전체의 글자를 뜬 `SetAssetMetaCommand` 로 간다.
        struct AssetEditScope
        {
            // 이번 프레임의 편집본. 원본은 `EditorApplication::GetSelectedAssetMeta` 다.
            AssetMetaFile* scratch = nullptr;
            // 편집 중인 블록. 고치면 그 블록의 `has*Options` 가 참이 된다.
            bool spriteBlock = false;
        };

        struct Context
        {
            AssetEditScope* asset = nullptr;
            // 목록 원소 안이면 그 원소다. 비어 있으면 컴포넌트의 필드를 그리는 중이다.
            ElementScope* element = nullptr;
            // 줄 배치 안에서 열려 있는 트리 마디 수다. 마디가 열린 자리에서는 표를 끊지 못한다 -
            // 표를 닫으면서 마디가 쌓은 Id 를 뺀다.
            std::uint32_t openTrees = 0;
            // 컴포넌트의 주인이다. `ComponentBase` 가 주인을 내주는 길은
            // 핸들뿐이고 원시 포인터 쪽은 private 이라, 그리는 쪽이 이미
            // 알고 있는 것을 여기 담아 온다.
            GameObject* owner = nullptr;
            ComponentBase* component = nullptr;
            ComponentTypeId typeId = 0;
            SetPropertyCommand::Path path;
        };

        // 값 하나를 그린다. 구조를 가진 타입이면 필드를 타고 내려간다.
        void DrawValue(
            const char* label,
            const TypeDescriptor& type,
            void* address,
            const PropertyEditInfo* edit,
            Context& context);
        // 한 줄짜리 실수 묶음. 색이면 색 고르개다.
        bool DrawScalarRun(
            const TypeDescriptor& type,
            const ScalarRun& run,
            const PropertyEditInfo* edit);
        // 타고 내려가야 하는 타입인가. 한 줄에 담기는 것은 아니다. 타입만 보고 정한다.
        static bool NeedsDescent(const TypeDescriptor& type);
        // 배열 하나를 목록 위젯으로 그린다.
        void DrawArray(
            const TypeDescriptor& type, void* address, bool editable, Context& context);
        // 목록 원소 하나. 한 줄에 담기면 필드와 같은 잎사귀 규칙으로 한 줄에 그리고,
        // 필드를 가진 구조체면 접기 마디 안에 필드마다 한 줄씩 그린다(D-89).
        void DrawElement(const TypeDescriptor& type, void* address, Context& context);
        // 원소 안의 잎사귀를 위젯이 바꿨다. 도로 되돌리고 목록 편집으로 적는다.
        static void RecordElementEdit(
            const TypeDescriptor& type, void* address, const String& before, Context& context);
        static void RecordElementRun(
            const ScalarRun& run, const float before[ScalarRun::MaxCount], Context& context);
        static ListEdit MakeElementEdit(const ElementScope& scope);
        // 에셋 칸의 항목(같은 타입의 이름과 아이디)이다. 레지스트리 판번호가 같으면 다시 모으지 않는다.
        struct AssetChoices
        {
            AssetType type = AssetType::Unknown;
            std::uint64_t revision = 0;
            bool built = false;
            Array<String> names;
            Array<const char*> namePointers;
            Array<AssetId> ids;
        };
        const AssetChoices& ChoicesFor(AssetType type);
        Array<AssetChoices> m_assetChoices;

        // 고른 에셋의 임포트 옵션(D-120). 오브젝트가 골라져 있지 않을 때만 온다.
        void DrawAsset(const AssetMetaFile& meta);
        void CommitAssetEdit(Context& context);
        // `AssetId` 필드. 레지스트리의 같은 타입 에셋을 고르는 드롭다운이다(D-116).
        void DrawAssetField(
            const char* fieldName,
            const TypeDescriptor& type,
            void* address,
            Context& context);
        // 코덱 하나짜리 잎사귀.
        bool DrawLeaf(
            const TypeDescriptor& type,
            void* address,
            const PropertyEditInfo* edit,
            const String& before,
            bool snapped);
        // 컴포넌트를 붙이고 떼는 손잡이. 둘 다 커맨드로 간다(D-71).
        void DrawAddComponent(GameObject& object);
        void RemoveComponent(GameObject& object, ComponentBase& component);
        // 슬롯 `from` 의 컴포넌트를 `to` 자리로. 커맨드로 간다.
        void MoveComponent(GameObject& object, std::size_t from, std::size_t to);
        // 표의 필드를 **이미 열려 있는 줄 배치 안에** 그린다. 배치를 밖에서
        // 받는 이유는 중첩 구조가 자기 배치를 따로 열어야 하기 때문이다 -
        // 한 표 안에서 다시 표를 열면 칸 폭이 바깥과 따로 논다.
        void DrawFieldsInto(
            Widget::FormLayout& layout,
            const PropertyTable& table,
            void* owner,
            Context& context);
        // 고른 것 전부에서 **같은 자리의 컴포넌트**를 모은다.
        //
        // 같은 자리란 같은 타입의 같은 번째다 - 주된 것에 스프라이트가 둘이고
        // 둘째를 고치는 중이면, 다른 오브젝트에서도 둘째를 고쳐야 한다.
        // 그 컴포넌트가 없는 오브젝트는 빠진다.
        //
        // 주인도 함께 돌려준다. 커맨드는 컴포넌트를 포인터가 아니라 주인의
        // 번호로 가리키므로(D-72) 주인을 모르면 커맨드를 만들 수 없다.
        struct EditTarget
        {
            GameObject* owner = nullptr;
            ComponentBase* component = nullptr;
        };
        Array<EditTarget> CollectEditTargets(const Context& context) const;

        // 위젯이 값을 바꿨다. 되돌려 놓고 커맨드로 다시 적용한다.
        void CommitEdit(
            const TypeDescriptor& type,
            void* address,
            const String& before,
            Context& context);

        EditorApplication* m_editor = nullptr;

        // 이름 칸이 들고 있는 글자와, 그것이 누구의 것인지. 고른 것이 바뀌면 다시 든다 -
        // 치던 글자를 그대로 두면 다음 오브젝트의 이름이 엉뚱한 것으로 보인다.
        String m_name;
        const GameObject* m_namedObject = nullptr;
        // 지난 프레임에 이름 칸이 글자를 받고 있었는가. 그렇지 않으면 칸의 글자를 다시 든다.
        bool m_nameEditing = false;
    };
}

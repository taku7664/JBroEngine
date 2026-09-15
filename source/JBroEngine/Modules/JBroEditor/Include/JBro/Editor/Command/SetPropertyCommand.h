#pragma once

#include <JBro/Editor/Command/ComponentAddress.h>
#include <JBro/Editor/EditorCommand.h>
#include <JBro/Editor/EditorObjectRegistry.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Types/String.h>

namespace JBro
{
    struct TypeDescriptor;

    // 컴포넌트의 잎사귀 값 하나를 되돌릴 수 있게 쓴다.
    //
    // **값은 바이트가 아니라 코덱의 글자로 잡는다.** 기존 엔진은 바이트 복사용과
    // 직렬화 스냅샷용 커맨드를 둘로 나눠 두었다 - 메모리를 소유하는 타입(배열,
    // 문자열)은 얕은 복사가 되면 먼저 죽는 쪽이 남은 쪽을 망가뜨리기 때문이다.
    // 여기서는 하나면 된다. `ValueCodec` 이 그 구분을 이미 흡수했다.
    //
    // **대상은 포인터가 아니라 주소(오브젝트 번호, 타입, 몇 번째)로 가리킨다**(D-72).
    // 처음에는 `SafePtr` 로 잡았는데, 오브젝트를 지웠다 되살리면 컴포넌트가 새로
    // 만들어져 그 포인터가 죽는다 - 삭제를 되돌린 뒤 앞선 편집을 되돌려도 아무 일도
    // 일어나지 않았다. 쓸 때마다 번호로 다시 찾는다.
    class SetPropertyCommand final : public EditorCommand
    {
    public:
        // 컴포넌트에서 잎사귀까지 내려가는 길이다. `Vec2 position` 의 `y` 면 [0, 1] 이다.
        //
        // 이름이 아니라 인덱스인 이유는 커맨드마다 문자열을 잡지 않기 위해서이고,
        // 표의 순서는 타입이 살아 있는 동안 바뀌지 않는다.
        static constexpr std::uint32_t MaxDepth = 4;

        struct Path
        {
            std::uint32_t indices[MaxDepth] = {};
            std::uint32_t depth = 0;

            bool Equals(const Path& other) const;
        };

        SetPropertyCommand(
            EditorObjectRegistry& registry,
            const ComponentAddress& address,
            const Path& path,
            String oldValue,
            String newValue);

        const char* GetName() const override;
        bool Execute() override;
        void Undo() override;
        void Redo() override;
        bool CanMerge(const EditorCommand& newer) const override;
        bool TryMerge(const EditorCommand& newer) override;

        // 길을 따라 잎사귀의 주소와 타입을 찾는다. 잎사귀는 코덱을 가진 값, **컨테이너**(D-86),
        // **한 줄 숫자 묶음**(`Vec2`·`Color`·`Rect`, D-89) 셋이고, 뒤의 둘의 글자는 전체를 담은
        // YAML 이다. 숫자 묶음은 인스펙터가 한 줄에 한 값으로 그리므로 커맨드도 한 값으로 든다 -
        // 처음에는 가지로 보고 거절해, 인스펙터가 커밋하지 못하고 위젯이 쓴 값이 그대로 남았다.
        // 중간이 사라졌거나 잎사귀가 아니면 거짓이다. **스냅샷을 뜨고 되살리는 쪽도 같은 길을 쓴다** -
        // 두 군데가 따로 걸어 내려가면 한쪽만 고쳐지는 날이 온다.
        static bool ResolveLeaf(
            ComponentBase& component,
            ComponentTypeId typeId,
            const Path& path,
            void*& address,
            const TypeDescriptor*& type);

        // 현재 값을 글자로 읽는다. 편집 전 값을 잡아 두는 데 쓴다.
        static bool ReadValue(
            ComponentBase& component,
            ComponentTypeId typeId,
            const Path& path,
            String& text);
        // 글자를 써 넣는다. 되살리기가 스냅샷을 되돌릴 때도 이 길이다.
        // 글자로 통째 쓰는 값은 전부 되거나 하나도 안 된다 - 못 읽으면 쓰기 전 상태로 돌려놓는다.
        static bool ApplyValue(
            ComponentBase& component,
            ComponentTypeId typeId,
            const Path& path,
            const String& text);

    private:
        bool WriteValue(const String& value);

        EditorObjectRegistry* m_registry = nullptr;
        ComponentAddress m_address;
        Path m_path;
        String m_oldValue;
        String m_newValue;
    };
}

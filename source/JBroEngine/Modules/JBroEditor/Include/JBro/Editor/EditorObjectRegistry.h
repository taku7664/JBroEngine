#pragma once

#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>

namespace JBro
{
    class GameObject;

    // 에디터가 오브젝트에 붙이는 안정된 번호다(D-72).
    //
    // **되돌리기가 삭제를 되살리면 오브젝트는 새로 만들어진다.** 그 전에 쌓인
    // 커맨드들이 들고 있던 포인터는 그 자리에서 죽는다 - 옮기고, 지우고,
    // 되살린 뒤에 그 옮김을 되돌리려 하면 아무 일도 일어나지 않는다.
    // 사용자에게는 Ctrl+Z 가 고장 난 것으로 보인다.
    //
    // 기존 엔진은 파일 직렬화용 GUID 가 이미 있어서 커맨드마다 그것으로 다시 찾았다.
    // 여기에는 그런 것이 없으므로 에디터가 자기 번호를 매긴다 - **저장 파일에는
    // 나가지 않는다.** 편집하는 동안만 사는 값이다.
    using EditorObjectId = std::uint64_t;
    inline constexpr EditorObjectId InvalidEditorObjectId = 0;

    class EditorObjectRegistry
    {
    public:
        // 이미 아는 오브젝트면 그 번호를, 처음 보면 새 번호를 준다.
        EditorObjectId Track(GameObject* object);
        // 번호로 찾는다. 사라졌거나 모르는 번호면 nullptr 이다.
        GameObject* Resolve(EditorObjectId id) const;
        // 되살린 오브젝트를 옛 번호에 다시 건다. 삭제를 되돌릴 때 쓴다.
        bool Rebind(EditorObjectId id, GameObject* object);
        void Clear();
        std::size_t GetCount() const;

    private:
        struct Entry
        {
            EditorObjectId id = InvalidEditorObjectId;
            SafePtr<GameObject> object;
        };

        // 선형 탐색이다. 씬 하나 분량이고 매 프레임 도는 길이 아니다 -
        // 커맨드를 만들거나 되돌릴 때만 들른다.
        Array<Entry> m_entries;
        EditorObjectId m_nextId = 1;
    };
}

#pragma once

#include <JBro/Canvas/Layer.h>
#include <JBro/Editor/Command/ComponentSnapshot.h>
#include <JBro/Editor/EditorObjectRegistry.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

#include <cstdint>

namespace JBro
{
    class Canvas;
    class GameObject;

    // 오브젝트 나무 하나를 글자로 뜬 것이다. 지우기의 되돌리기(D-76)와 복사·붙여넣기가
    // 같은 것을 쓴다 - 둘 다 "나무를 떠 두었다가 컴포넌트를 새로 붙이고 값을 다시 써 넣는"
    // 일이고, 다른 점은 옛 번호에 다시 거는지 새 번호를 받는지뿐이다.
    //
    // **나무를 평평하게 편다.** 자식이 자기 안에 자식 배열을 들면 타입이 자기 자신을 품게
    // 되어 크기를 잴 수 없다 - 캔버스 파일도 같은 이유로 오브젝트를 한 줄로 늘어놓고
    // 부모를 인덱스로 가리킨다. 0번이 뿌리고 뒤로 갈수록 깊으므로, 앞에서부터 만들면
    // 부모가 늘 먼저 있다.
    struct ObjectSnapshotEntry
    {
        EditorObjectId id = InvalidEditorObjectId;
        String name;
        bool active = true;
        // 오브젝트 플래그다(D-163). 없으면 지웠다 되돌린 감춘 오브젝트가 보이는 채로 돌아온다.
        std::uint32_t flags = 0;
        // 어느 레이어에 있었는가(D-168). 없으면 지웠다 되돌린 오브젝트가 기본 레이어로 돌아와,
        // 되돌리기가 레이어를 조용히 바꾼 것이 된다. 그 번호의 레이어가 사라졌으면 기본 레이어다.
        LayerId layer = InvalidLayerId;
        // 이 배열 안에서의 부모 위치다. -1 이면 뜬 나무의 뿌리다.
        std::int64_t parentIndex = -1;
        Array<ComponentSnapshot> components;
    };

    struct ObjectTreeSnapshot
    {
        Array<ObjectSnapshotEntry> objects;

        // `root` 와 그 아래 전부를 뜬다. 컴포넌트 하나라도 뜨지 못하면 거짓이고, 그때의
        // 내용은 믿지 않는다 - 반쪽 스냅샷으로 지우거나 붙이면 조용히 잃는다.
        bool Capture(EditorObjectRegistry& registry, GameObject& root);

        // 나무를 다시 만든다. `outerParent` 아래에 뿌리를 두고(널이면 캔버스 뿌리),
        // `rebind` 가 참이면 옛 번호에 다시 걸고(지우기 되돌리기), 거짓이면 새 번호를 받아
        // 항목에 적는다(붙여넣기의 첫 실행). 그 뒤의 다시 하기는 참으로 부른다.
        bool Restore(
            Canvas& canvas,
            EditorObjectRegistry& registry,
            GameObject* outerParent,
            bool rebind);

        // 뿌리를 번호로 찾아 나무째 지운다. 자식은 캔버스가 함께 지운다.
        bool DestroyRoot(Canvas& canvas, EditorObjectRegistry& registry) const;

        EditorObjectId GetRootId() const
        {
            return objects.IsEmpty() ? InvalidEditorObjectId : objects[0].id;
        }

    private:
        bool CaptureInto(EditorObjectRegistry& registry, GameObject& object, std::int64_t parentIndex);
    };
}

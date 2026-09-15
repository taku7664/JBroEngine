# JBroEngine 위키

이 문서 묶음은 JBroEngine 이 **어떻게 생겼고, 왜 그렇게 생겼는지**를 설명한다.
규칙의 원문은 [ProjectRule.md](../ProjectRule.md)에, 결정의 기록은 [tasks/todo.md](../../tasks/todo.md)의 Decisions 절에 있다.
이 위키는 그 둘을 읽기 전에 그림을 잡는 용도이고, 둘과 어긋나면 원문이 맞다.

본문에서 `D-42` 같은 표시는 Decisions 의 번호다. 그 결정이 왜 났는지 알고 싶으면 그 번호를 찾으면 된다.

## 목차

| 순서 | 문서 | 무엇을 설명하나 |
|---|---|---|
| 1 | [엔진 개요](01-Overview.md) | 이 엔진이 무엇이고 지금 어디까지 왔는지, 자주 나오는 용어 |
| 2 | [모듈 구조](02-Module-Structure.md) | 모듈 열일곱 개의 역할, 의존 방향, 스크립트가 보는 층과 엔진만 보는 층 |
| 3 | [오브젝트 모델](03-Object-Model.md) | `Canvas` · `GameObject` · 컴포넌트 · 레이어 · 시스템 · 서비스와 프레임 순서 |
| 4 | [참조와 식별자](04-References-And-Ids.md) | `InstanceId`, `GameObjectHandle`, `Ref<T>`, `SafePtr`, `OwnerPtr` 를 언제 무엇에 쓰는가 |
| 5 | [스크립트 경계](05-Script-Boundary.md) | 게임 DLL 이 호스트와 어떻게 만나는가. ABI, 로드 컨텍스트, 핫 리로드 |
| 6 | [스크립트 API](06-Script-API.md) | C++ 스크립트가 쓸 수 있는 것 전부. 프렐류드, 훅, 핸들, 컴포넌트 필드, 서비스, 컨테이너 |
| 7 | [리플렉션과 직렬화](07-Reflection.md) | `JBRO_FIELD`, `TypeDescriptor`, `PropertyInfo`, `ValueCodec`, `.jcanvas` 저장 |
| 8 | [렌더링과 플랫폼](08-Rendering-And-Platform.md) | 렌더러의 패킷 수집, RHI 경계, 입력, 검증 레이어 |
| 9 | [에디터](09-Editor.md) | 패널·커맨드·위젯·로컬라이징. 되돌리기가 어떻게 보장되는가 |
| 10 | [JBroScript 문법](10-JBroScript-Syntax.md) | `.jscript` 언어의 사용자 문법. 확정된 것과 열린 것 |
| 11 | [jbroc 컴파일러](11-Jbroc-Compiler.md) | `.jscript` 를 C++ 로 바꾸는 규칙. 타입체커·이미터·빌드 |
| 12 | [설계 결정의 근거](12-Design-Decisions.md) | 방향을 정한 결정들을 주제별로 모아 "왜" 를 설명 |
| 13 | [코딩 규칙](13-Coding-Rules.md) | 이름·네임스페이스·메모리·검증·커밋 규칙 요약 |

## 처음이라면

1. [엔진 개요](01-Overview.md)와 [오브젝트 모델](03-Object-Model.md)을 읽는다. 나머지는 이 둘 위에 서 있다.
2. 스크립트를 쓸 거면 [스크립트 API](06-Script-API.md)만 있으면 된다. 엔진 내부는 몰라도 된다.
3. 엔진 코드를 고칠 거면 [모듈 구조](02-Module-Structure.md)와 [참조와 식별자](04-References-And-Ids.md)를 먼저 보고,
   [ProjectRule.md](../ProjectRule.md) 를 옆에 두고 작업한다.

## 이 문서의 상태 표시

코드에 이미 있는 것과 계획만 있는 것을 구분해서 적었다.

- 아무 표시가 없으면 **코드에 있고 테스트가 있다.**
- **[계획]** 은 문서로 정했지만 코드가 없다. JBroScript 언어 전체가 여기 속한다.
- **[열림]** 은 아직 결정이 나지 않았다.
- **[스텁]** 은 선언은 있는데 몸통이 비어 있다. `AssetSystem::Load` 가 그렇다.

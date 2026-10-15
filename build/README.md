# JBro 씬 에디터 실행본

받아서 바로 켤 수 있는 에디터 빌드다(Windows x64, Release).

## 켜기

- `run.bat` 을 두 번 누른다. (또는 `JBroEditorHost.exe` 를 이 폴더에서 두 번 누른다 — 탐색기로 열면 작업 폴더가 이 폴더라 켜진다.)

명령줄 인자:

```
JBroEditorHost.exe [--project <경로.jproject>] [--frames <수>] [--control-port <포트>]
```

## 들어 있는 것

- `JBroEditorHost.exe` — 에디터(엔진이 정적 링크돼 있다).
- `Localization/` — 화면 글자(ko-KR, en-US)와 컴파일러 메시지.
- `ThirdParty/MaterialDesignIcons/` — 에디터 아이콘 글꼴.
- `msvcp140*.dll`·`vcruntime140*.dll` — Visual C++ 런타임(따로 깔지 않아도 되게 함께 둔다).

## 필요 조건

- Windows 10 이상(x64), Direct3D 12 나 11 을 쓸 수 있는 그래픽 드라이버.

## 참고

- 이 실행본은 엔진 저장소의 `source/JBroEngine` 를 Release 로 빌드해 뽑았다. 다시 뽑으려면 그 솔루션(`JBroEngine.slnx`)을 Release|x64 로 빌드하고 `Build/x64/Release/JBroEditorHost.exe` 와 그 옆의 `Localization/`·`ThirdParty/` 를 여기에 복사한다.
- 스크립트를 빌드하려면 MSVC 빌드 도구가 따로 있어야 한다(에디터가 없으면 무엇을 깔지 알려 준다).

# nlohmann-json

JSON for Modern C++ v3.12.0 · MIT · 원본 수정 없음.

- 출처: [공식 v3.12.0 태그](https://github.com/nlohmann/json/tree/v3.12.0)
- 헤더: 공식 `single_include/nlohmann/json.hpp` → 이 폴더의 `nlohmann/json.hpp`
- 라이선스: [LICENSE.MIT](./LICENSE.MIT)
- 헤더 SHA-256: `AAF127C04CB31C406E5B04A63F1AE89369FCCDE6D8FA7CDDA1ED4F32DFC5DE63`

`JBroScriptLanguageServer`의 표준 LSP/JSON-RPC 메시지 직렬화·역직렬화에 사용한다.
프로젝트 파일·저장 데이터 형식을 JSON으로 바꾸거나 공개 `JBroScriptCompiler` 인터페이스에 JSON 타입을 내보내지 않는다.
헤더 전용 의존을 `nlohmann-json.vcxproj`로 선언한다. 버전을 바꿀 때는 공식 태그·원본 헤더·라이선스·해시를 함께 확인한다.

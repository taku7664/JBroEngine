# TODO

## Goal
Web 빌드가 YAML 기반 scene/import option 코드를 링크할 수 있도록 yaml-cpp Web compile source 를 준비한다.

## Confirmed Facts
- Web source list 에 `SceneSerializer.cpp`, `SpriteAsset.cpp`, `AudioAsset.cpp`, `AssetMetaFile.cpp`, `BuildManifest.cpp` 가 포함되어 있다.
- 이 파일들은 현재 `yaml-cpp` API 를 사용한다.
- repo 의 기존 yaml-cpp 구성은 header 와 Windows `.lib` 중심이며, Web/wasm 에서 Windows `.lib` 는 사용할 수 없다.
- 현재 asset pack 은 cooked scene/import payload 가 아니라 raw source/import option 을 담으므로 Web runtime 에도 YAML parser 가 필요하다.

## Assumptions
- 이번 단계에서는 기존 raw scene/import option 런타임 동작을 유지한다.
- cooked payload 로 YAML runtime 의존성을 제거하는 작업은 후속 단계로 둔다.
- yaml-cpp source 는 기존 header 와 호환되는 공식 0.8.0 tag 기준으로 보강한다.

## Success Criteria
- `Engine/ThirdParty/yaml-cpp/src/*.cpp` 가 존재한다.
- `BuildScripts/Web/web_game_sources.txt` 에 yaml-cpp 구현 cpp 가 포함된다.
- Web `emcc` 호출에 `YAML_CPP_STATIC_DEFINE` 과 yaml-cpp private source include path 가 들어간다.
- 기존 Windows `.lib` 링크 경로는 건드리지 않는다.

## Plan
- [x] 기존 header 와 공식 yaml-cpp 0.8.0 header 호환성을 확인한다.
- [x] yaml-cpp 0.8.0 `src` 와 `LICENSE` 를 ThirdParty 에 보강한다.
- [x] Web source list 에 yaml-cpp cpp 를 추가한다.
- [x] `BuildGame.ps1` Web emcc arguments 에 yaml-cpp include/define 을 추가한다.
- [x] `WebBuild.vcxproj` NMake preprocessor definitions 를 갱신한다.
- [x] 문서와 검증을 갱신한다.

## Verification
- [x] `web_game_sources.txt` 모든 파일 존재 확인
- [x] `BuildGame.ps1` PowerShell parse
- [x] `WebBuild.vcxproj` XML parse
- [x] `git diff --check`
- [ ] 실제 `emcc` Web build

## Review
### Read / Thought / Counterexample / Fix
- 코드를 읽었고: Web source list 가 YAML 의존 파일들을 포함하는데, ThirdParty 에는 yaml-cpp Windows `.lib` 만 있고 wasm 으로 컴파일할 source 가 없었다.
- 어떻게 생각했고: 지금 단계에서 cooked payload 로 바로 전환하면 scene/import pipeline 전체가 커지므로, 기존 raw payload 계약을 유지하면서 Web build 를 살리는 쪽이 더 작은 변경이다.
- 어떤 반례를 찾았고: `emcc` 가 설치되어 있어도 yaml-cpp 구현이 없으면 `YAML::Load`, `YAML::Emitter` 같은 symbol link 에서 실패한다.
- 어떻게 고쳤다: 공식 yaml-cpp 0.8.0 source 를 `Engine/ThirdParty/yaml-cpp/src` 에 보강하고, Web source list 와 emcc include/define 을 맞췄다.

### Changed
- `Engine/ThirdParty/yaml-cpp/src/*`
  - Web/wasm 컴파일용 yaml-cpp 0.8.0 구현 source 를 추가했다.
- `Engine/ThirdParty/yaml-cpp/LICENSE`
  - yaml-cpp MIT license 를 함께 추가했다.
- `BuildScripts/Web/web_game_sources.txt`
  - yaml-cpp 구현 cpp 를 Web build 입력에 추가했다.
- `BuildScripts/BuildGame.ps1`
  - Web emcc arguments 에 `-IEngine\ThirdParty\yaml-cpp\src` 와 `-DYAML_CPP_STATIC_DEFINE` 을 추가했다.
- `BuildScripts/Web/WebBuild.vcxproj`
  - NMake preprocessor definitions 에 `YAML_CPP_STATIC_DEFINE` 을 추가했다.
- `tasks/BuildPipelineDesign.md`
  - Web YAML dependency 와 장기 cooked payload 방향을 기록했다.

### Verified
- `web_game_sources.txt` 의 모든 경로가 실제 파일인지 확인했다.
- `BuildGame.ps1` PowerShell parse 성공.
- `WebBuild.vcxproj` XML parse 성공.
- `git diff --check` 통과. CRLF 경고만 존재.

### Not Verified
- 실제 Web build 는 현재 셸에서 `emcc` 를 찾지 못해 실행하지 못했다. `emsdk_env.bat` 적용 후 재검증해야 한다.

### Risks
- yaml-cpp 를 wasm 에 직접 포함하므로 Web binary size 는 증가한다.
- cooked scene/import payload 가 완성되면 Web runtime 에서 YAML parser 를 제거하는 쪽이 장기적으로 더 좋다.

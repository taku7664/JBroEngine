# miniaudio

`miniaudio` 는 단일 헤더 사운드 라이브러리입니다 — 라이선스 MIT-0 (사실상 public domain).

## 추가 방법

1. https://github.com/mackron/miniaudio 에서 `miniaudio.h` 를 받아
2. **이 폴더(`Engine/ThirdParty/miniaudio/`)** 에 그대로 둡니다.

   ```
   Engine/ThirdParty/miniaudio/
   ├── README.md            (이 파일)
   └── miniaudio.h          (사용자가 추가)
   ```

3. 이미 추가되어 있으면 자동으로 `JBRO_HAS_MINIAUDIO=1` 로 감지되어 `CMiniAudioDevice`
   가 활성화됩니다.  파일이 없으면 `CEmptyAudioDevice` 가 자동 폴백됩니다.

## 라이선스

`miniaudio` 는 MIT-0 (No Attribution) 또는 public domain (unlicense) 중 사용자가 선택.
JBroEngine 배포본의 서드파티 라이선스 목록에 한 줄 추가만으로 충분합니다.

- 저장소: https://github.com/mackron/miniaudio
- 라이선스 파일: miniaudio.h 상단 주석에 포함

# 시험 폰트

`NotoSansKR-Subset.otf` 는 텍스트 커널 테스트(`Tests/TextLayoutTests.cpp`)가 쓰는 폰트다(D-200). 라이선스는 SIL Open Font
License 1.1 이고 원문이 `OFL.txt` 에 있다. 테스트는 이 파일을 직접 읽지 않고 `Embed.ps1` 이 만든
`Tests/TestFontNotoSansKR.generated.h` 의 바이트를 쓴다. 폰트를 바꾸면 `pwsh Tests\Data\Fonts\Embed.ps1` 을 다시 돌린다.

## 어디서 왔나

- 원본: `github.com/notofonts/noto-cjk` 의 `Sans/SubsetOTF/KR/NotoSansKR-Regular.otf`(판 2.004, 4,644,748 바이트, CFF). 2026-09-25 에 받았다.
- 서브셋: fontTools 4.66.0 의 `pyftsubset` 으로 ASCII(U+0020~U+007E)와 한글 32 자만 남겼다(19,224 바이트).
  `kern` 기능(GPOS 조회 형식 2, 부표 형식 1·2, 값 형식 XAdvance)만 남기고 나머지 조회는 뺐다.

```
pyftsubset NotoSansKR-Regular.otf --unicodes="U+0020-007E" --text-file=hangul.txt --layout-features='kern' \
  --name-IDs='*' --name-legacy --notdef-outline --output-file=NotoSansKR-Subset.otf
```

`hangul.txt` 의 글자: `가나다라마바사아자차카타파하안녕하세요한글세계텍스트줄바꿈어절음절`.

OFL 은 수정본이 원래 이름의 예약 이름(Reserved Font Name)을 쓰지 못하게 한다. Noto Sans CJK 의 예약 이름은 `Source` 이고
이 파일의 이름(`Noto Sans KR`)에는 들어 있지 않다.

## 테스트가 기대하는 값

fontTools 로 서브셋을 직접 읽어 뽑은 값이다. 테스트는 stb_truetype 이 같은 값을 읽는지 본다(text-plan §7 의 가정).

| 항목 | 값(폰트 단위, em 1000) |
|---|---|
| unitsPerEm · ascent · descent · lineGap (hhea) | 1000 · 1160 · -288 · 0 |
| 전진 폭 `A` `V` `T` `o` `l` 공백 | 608 · 575 · 599 · 606 · 284 · 224 |
| 전진 폭 `한` `글` `가` `하` | 920 |
| 커닝 `AV` · `VA` · `To` | -15 · -15 · -74 |
| U+FFFD | 없다 |

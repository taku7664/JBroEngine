# 시험 폰트

`NotoSansKR-Subset.otf` 는 텍스트 커널 테스트(`Tests/TextLayoutTests.cpp`)가 쓰는 폰트다(D-200). 라이선스는 SIL Open Font
License 1.1 이고 원문이 `OFL.txt` 에 있다. 테스트는 이 파일을 직접 읽지 않고 `Embed.ps1` 이 만든
`Tests/TestFontNotoSansKR.generated.h` 의 바이트를 쓴다. 폰트를 바꾸면 `pwsh Tests\Data\Fonts\Embed.ps1` 을 다시 돌린다.

## 어디서 왔나

- 원본: `github.com/notofonts/noto-cjk` 의 `Sans/SubsetOTF/KR/NotoSansKR-Regular.otf`(판 2.004, 4,644,748 바이트, CFF). 2026-09-25 에 받았다.
- 서브셋: fontTools 4.66.0 의 `pyftsubset` 으로 ASCII(U+0020~U+007E)와 한글 32 자(겹친 것을 빼면 서로 다른 음절 29 자, 모두 KS X 1001 안)만 남겼다(19,224 바이트).
  `kern` 기능(GPOS 조회 형식 2, 부표 형식 1·2, 값 형식 XAdvance)만 남기고 나머지 조회는 뺐다.

```
pyftsubset NotoSansKR-Regular.otf --unicodes="U+0020-007E" --text-file=hangul.txt --layout-features='kern' \
  --name-IDs='*' --name-legacy --notdef-outline --output-file=NotoSansKR-Subset.otf
```

`hangul.txt` 의 글자: `가나다라마바사아자차카타파하안녕하세요한글세계텍스트줄바꿈어절음절`.

`NotoSansKR-Latin.otf` 는 폴백 시험(`Tests/TextRenderTests.cpp`, text-plan §5 의 3 단계)이 쓰는 **한글이 없는** 폰트다. 위 서브셋에서
ASCII 만 다시 남겼다(15,396 바이트, 같은 fontTools). `Embed.ps1` 이 `Tests/TestFontNotoSansKRLatin.generated.h` 로 넣는다.

```
pyftsubset NotoSansKR-Subset.otf --unicodes="U+0020-007E" --layout-features='kern' \
  --name-IDs='*' --name-legacy --notdef-outline --output-file=NotoSansKR-Latin.otf
```

`NotoSansKR-Extension.otf`·`NotoSansKR-XPlacement.otf` 는 GPOS 쌍 조정을 stb_truetype 이 건너뛰던 두 모양으로 바꾼 판이다(text-plan §7).
`MakeGposVariants.py` 가 위 서브셋에서 만든다: 앞의 것은 모든 조회를 확장 조회(형식 9)로 감쌌고, 뒤의 것은 쌍 조정의 첫 값 형식을 XAdvance 에서
XPlacement|XAdvance 로 넓혔다(XPlacement 는 0). 커닝 값은 둘 다 원본과 같아야 한다.

`NotoSansKR-Marks.otf`·`NotoSansKR-MarksExtension.otf` 는 결합 표시 시험용이다(text-plan §7). 원본에서 ASCII 와 U+0301 만 남긴
`NotoSansKR-Marks-Base.otf` 에 `MakeMarkFont.py` 가 **알려진 앵커**의 mark-to-base 조회를 더한다(뒤의 것은 그 조회를 확장 조회로 감쌌다).
원본의 mark 조회는 라틴 글자를 받침으로 하지 않아 기대값을 fontTools 로 뽑을 수 없기 때문이다.

```
pyftsubset NotoSansKR-Regular.otf --unicodes="U+0020-007E,U+0301" --layout-features='kern,mark' \
  --name-IDs='*' --name-legacy --notdef-outline --output-file=NotoSansKR-Marks-Base.otf
python MakeMarkFont.py
```

`Embed.ps1` 이 여섯 폰트(서브셋·라틴·두 GPOS 판·두 mark 판)를 모두 `Tests/TestFont*.generated.h` 로 넣는다.

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

결합 표시 판(`NotoSansKR-Marks.otf`)의 값이다. 앵커는 `MakeMarkFont.py` 가 정했다.

| 항목 | 값(폰트 단위, em 1000) |
|---|---|
| 전진 폭 `A` · `e` · U+0301 | 608 · 554 · 0 |
| 받침 앵커 `A` · `e` | (304, 800) · (277, 800) |
| 표시 앵커 U+0301 | (-200, 600) |
| 받침 원점에서 표시 원점까지 `A` · `e` | (504, 200) · (477, 200) |
| 커닝 `AV` | -15 |

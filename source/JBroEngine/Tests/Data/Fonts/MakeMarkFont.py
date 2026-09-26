# 결합 문자 시험 폰트를 만든다(text-plan §7 - 결합 문자 위치 잡기).
#
# Noto Sans KR 에는 U+0301(COMBINING ACUTE ACCENT)이 있지만 라틴 글자를 받침으로 하는 mark 조회가 없다(그 조회의 받침은 다른
# 글자들이다). 그래서 ASCII + U+0301 서브셋(NotoSansKR-Marks-Base.otf, pyftsubset)에 **알려진 앵커**로 mark-to-base 조회를 하나
# 더한다: 받침 `A`·`e` 의 앵커는 (글자 폭의 절반, 800), 표시의 앵커는 (-200, 600) 이다. 테스트는 이 값으로 자리를 잰다.
# 확장 조회(형식 9)로 감싼 판도 함께 만든다.
#
#   python Tests/Data/Fonts/MakeMarkFont.py      (README 의 venv, fontTools 4.66)
import os
from fontTools.ttLib import TTFont
from fontTools.otlLib import builder
from fontTools.ttLib.tables import otTables as ot

here = os.path.dirname(os.path.abspath(__file__))
source = os.path.join(here, 'NotoSansKR-Marks-Base.otf')


def add_mark_lookup(font, wrap):
    cmap = font.getBestCmap()
    glyph_map = font.getReverseGlyphMap()
    acute = cmap[0x0301]
    bases = [cmap[ord('A')], cmap[ord('e')]]
    marks = {acute: (0, builder.buildAnchor(-200, 600))}
    base_anchors = {}
    for base in bases:
        advance = font['hmtx'][base][0]
        base_anchors[base] = {0: builder.buildAnchor(advance // 2, 800)}
    subtable = builder.buildMarkBasePosSubtable(marks, base_anchors, glyph_map)
    gpos = font['GPOS'].table
    lookup = ot.Lookup()
    lookup.LookupFlag = 0
    if wrap:
        extension = ot.ExtensionPos()
        extension.Format = 1
        extension.ExtensionLookupType = 4
        extension.ExtSubTable = subtable
        lookup.LookupType = 9
        lookup.SubTable = [extension]
    else:
        lookup.LookupType = 4
        lookup.SubTable = [subtable]
    lookup.SubTableCount = 1
    gpos.LookupList.Lookup.append(lookup)
    gpos.LookupList.LookupCount = len(gpos.LookupList.Lookup)
    index = gpos.LookupList.LookupCount - 1
    feature = ot.FeatureRecord()
    feature.FeatureTag = 'mark'
    feature.Feature = ot.Feature()
    feature.Feature.LookupListIndex = [index]
    feature.Feature.LookupCount = 1
    gpos.FeatureList.FeatureRecord.append(feature)
    gpos.FeatureList.FeatureCount = len(gpos.FeatureList.FeatureRecord)
    feature_index = gpos.FeatureList.FeatureCount - 1
    for script in gpos.ScriptList.ScriptRecord:
        systems = [script.Script.DefaultLangSys] + [record.LangSys for record in script.Script.LangSysRecord]
        for system in systems:
            if system is not None:
                system.FeatureIndex.append(feature_index)
                system.FeatureCount = len(system.FeatureIndex)


for name, wrap in (('NotoSansKR-Marks.otf', False), ('NotoSansKR-MarksExtension.otf', True)):
    font = TTFont(source)
    add_mark_lookup(font, wrap)
    path = os.path.join(here, name)
    font.save(path)
    check = TTFont(path)
    print(name, os.path.getsize(path), 'bytes, lookup types', sorted({l.LookupType for l in check['GPOS'].table.LookupList.Lookup}))

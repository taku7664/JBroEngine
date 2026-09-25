# GPOS 의 쌍 조정을 stb_truetype 이 건너뛰는 모양으로 바꾼 시험 폰트 둘을 만든다(text-plan §7).
#
# - NotoSansKR-Extension.otf: 모든 GPOS 조회를 확장 조회(형식 9)로 감쌌다. 큰 폰트가 64 KB 넘는 오프셋을 피하려고 흔히 쓰는 모양이다.
# - NotoSansKR-XPlacement.otf: 쌍 조정의 첫 값 형식을 XAdvance 에서 XPlacement|XAdvance 로 넓혔다(XPlacement 는 0).
#
# 둘 다 원본 서브셋과 커닝 값이 같아야 한다. fontTools 로 돌린다(README 의 venv).
#
#   python Tests/Data/Fonts/MakeGposVariants.py
import os
from fontTools.ttLib import TTFont
from fontTools.ttLib.tables import otTables as ot

here = os.path.dirname(os.path.abspath(__file__))
source = os.path.join(here, 'NotoSansKR-Subset.otf')


def wrap_in_extension(font):
    lookups = font['GPOS'].table.LookupList.Lookup
    for lookup in lookups:
        wrapped = []
        for subtable in lookup.SubTable:
            extension = ot.ExtensionPos()
            extension.Format = 1
            extension.ExtensionLookupType = lookup.LookupType
            extension.ExtSubTable = subtable
            wrapped.append(extension)
        lookup.SubTable = wrapped
        lookup.LookupType = 9


def widen_value_format(font):
    for lookup in font['GPOS'].table.LookupList.Lookup:
        if lookup.LookupType != 2:
            continue
        for subtable in lookup.SubTable:
            if subtable.ValueFormat1 != 0x0004:
                continue
            subtable.ValueFormat1 = 0x0005
            if subtable.Format == 1:
                for pair_set in subtable.PairSet:
                    for record in pair_set.PairValueRecord:
                        if record.Value1 is not None:
                            record.Value1.XPlacement = 0
            else:
                for class1 in subtable.Class1Record:
                    for class2 in class1.Class2Record:
                        if class2.Value1 is not None:
                            class2.Value1.XPlacement = 0


font = TTFont(source)
wrap_in_extension(font)
font.save(os.path.join(here, 'NotoSansKR-Extension.otf'))

font = TTFont(source)
widen_value_format(font)
font.save(os.path.join(here, 'NotoSansKR-XPlacement.otf'))

for name in ('NotoSansKR-Extension.otf', 'NotoSansKR-XPlacement.otf'):
    check = TTFont(os.path.join(here, name))
    types = sorted({lookup.LookupType for lookup in check['GPOS'].table.LookupList.Lookup})
    print(name, os.path.getsize(os.path.join(here, name)), 'bytes, lookup types', types)

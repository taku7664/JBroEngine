"""기존 핸들 타입에 더한 멤버의 Definitions.cpp 행을 만든다. 인자: 타입 이름들(핸들 헤더 `Handle/<이름>.h`).
헤더의 `Get*`·`Set*`·`Is*` 선언 가운데 표에 없는 멤버를 그 타입의 마지막 행 뒤에 더한다. 멱등이다."""
import re, sys
import os
REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DEFS = REPO + r"\source\JBroEngine\Modules\JBroScriptApiCatalog2D\Source\Definitions.cpp"
ROW = re.compile(r'^(\s*\{)"([^"]*)","([^"]*)","([^"]*)","([^"]*)", (\d+), (\d+), (\d+)(.*)$')

data = open(DEFS, "rb").read()
bom = data.startswith(b"\xef\xbb\xbf")
text = data.decode("utf-8-sig")
crlf = "\r\n" in text
rows = text.replace("\r\n", "\n").split("\n")

for T in sys.argv[1:]:
    rel = "JBroEngine/Modules/JBroFramework2D/Include/JBro/Framework2D/Handle/%s.h" % T
    lines = open(REPO + "\\source\\" + rel.replace("/", "\\"), "rb").read().decode("utf-8-sig").replace("\r\n", "\n").split("\n")
    present = set(ROW.match(r).group(3) for r in rows if ROW.match(r) and ROW.match(r).group(2) == T)
    new_rows = []
    for i, line in enumerate(lines):
        m = re.match(r"^\s+[\w:]+ ((?:Get|Set|Is)\w+)\(", line)
        if m and m.group(1) not in present:
            native = m.group(1)
            col = len(line[:m.start(1)].encode("utf-8")) + 1
            new_rows.append('    {"%s","%s","%s","%s", %d, %d, %d},' % (T, native, native, rel, i + 1, col, len(native)))
            present.add(native)
    last = max(i for i, r in enumerate(rows) if ROW.match(r) and ROW.match(r).group(2) == T)
    rows[last + 1:last + 1] = new_rows
    print(T, "added", len(new_rows))

result = "\n".join(rows)
if crlf:
    result = result.replace("\n", "\r\n")
open(DEFS, "wb").write((b"\xef\xbb\xbf" if bom else b"") + result.encode("utf-8"))

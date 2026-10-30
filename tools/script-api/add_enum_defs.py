"""열거형 타입의 Definitions.cpp 행을 더한다. 인자: 이름=헤더상대경로 ... (예 AspectMode=JBroEngine/Modules/JBroFramework2D/Include/JBro/Framework2D/ComponentTypes2D.h). 멱등이다."""
import re, sys
import os
REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DEFS = REPO + r"\source\JBroEngine\Modules\JBroScriptApiCatalog2D\Source\Definitions.cpp"
ROW = re.compile(r'^(\s*\{)"([^"]*)","([^"]*)","([^"]*)","([^"]*)", (\d+), (\d+), (\d+)(.*)$')
text = open(DEFS, "rb").read().decode("utf-8-sig")
rows = text.replace("\r\n", "\n").split("\n")
present = set(ROW.match(r).group(2) for r in rows if ROW.match(r))
new = []
for arg in sys.argv[1:]:
    name, rel = arg.split("=", 1)
    if name in present:
        continue
    lines = open(REPO + "\\source\\" + rel.replace("/", "\\"), "rb").read().decode("utf-8-sig").replace("\r\n", "\n").split("\n")
    for i, line in enumerate(lines):
        m = re.search(r"enum class " + name + r"\b", line)
        if m:
            col = len(line[:m.start() + len("enum class ")].encode("utf-8")) + 1
            new.append('    {"%s","","%s","%s", %d, %d, %d},' % (name, name, rel, i + 1, col, len(name)))
            break
    else:
        raise SystemExit("enum not found " + name)
last = max(i for i, r in enumerate(rows) if ROW.match(r) and ROW.match(r).group(2) == "LayoutMode")
rows[last + 1:last + 1] = new
open(DEFS, "wb").write(b"\xef\xbb\xbf" + "\r\n".join(rows).encode("utf-8"))
print("enum rows added", len(new))

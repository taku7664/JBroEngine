"""새 핸들 타입의 Definitions.cpp 행을 Button2D 의 행을 본떠 만든다.
인자: 타입 이름(핸들 헤더 `Handle/<이름>.h`)들. 공통 멤버(ComponentHandle.h)는 그대로 베끼고, 헤더의 선언은 토큰을 찾아 적는다."""
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

def find_token(lines, pattern, native):
    for i, line in enumerate(lines):
        if re.search(pattern, line):
            col = line.encode("utf-8").index(native.encode("utf-8")) + 1 if native in line else None
            m = re.search(r"\b" + re.escape(native) + r"\b", line)
            if m:
                col = len(line[:m.start()].encode("utf-8")) + 1
                return i + 1, col
    raise SystemExit("token not found: " + native + " for " + pattern)

for T in sys.argv[1:]:
    rel = "JBroEngine/Modules/JBroFramework2D/Include/JBro/Framework2D/Handle/%s.h" % T
    lines = open(REPO + "\\source\\" + rel.replace("/", "\\"), "rb").read().decode("utf-8-sig").replace("\r\n", "\n").split("\n")
    button_rows = [r for r in rows if ROW.match(r) and ROW.match(r).group(2) == "Button2D"]
    new_rows = []
    for r in button_rows:
        m = ROW.match(r)
        pre, typ, member, native, header, line, col, length, tail = m.groups()
        if header.endswith("ComponentHandle.h"):
            new_rows.append('%s"%s","%s","%s","%s", %s, %s, %s%s' % (pre, T, member, native, header, line, col, length, tail))
    # 클래스 이름
    ln, cl = find_token(lines, r"^\s+class " + T + r" final", T)
    new_rows.append('    {"%s","","%s","%s", %d, %d, %d},' % (T, T, rel, ln, cl, len(T)))
    ln, cl = find_token(lines, r"StaticTypeName\(\)", "StaticTypeName")
    new_rows.append('    {"%s","StaticTypeName","StaticTypeName","%s", %d, %d, %d},' % (T, rel, ln, cl, len("StaticTypeName")))
    for i, line in enumerate(lines):
        m = re.match(r"^\s+[\w:]+ ((?:Get|Set|Is)\w+)\((?:[\w:]+ value)?\) ?(?:const)?;", line)
        if m:
            native = m.group(1)
            col = len(line[:m.start(1)].encode("utf-8")) + 1
            new_rows.append('    {"%s","%s","%s","%s", %d, %d, %d},' % (T, native, native, rel, i + 1, col, len(native)))
    ln, cl = find_token(lines, r"Component::" + T + r"\* Resolve\(", "Resolve")
    new_rows.append('    {"%s","Resolve","Resolve","%s", %d, %d, %d},' % (T, rel, ln, cl, len("Resolve")))
    last = max(i for i, r in enumerate(rows) if ROW.match(r) and ROW.match(r).group(2) == "Button2D")
    rows[last + 1:last + 1] = new_rows
    print(T, "rows", len(new_rows))

result = "\n".join(rows)
if crlf:
    result = result.replace("\n", "\r\n")
open(DEFS, "wb").write((b"\xef\xbb\xbf" if bom else b"") + result.encode("utf-8"))

"""Definitions.cpp 의 위치표를 지금 헤더에 맞춘다.

각 항목의 (Line, Column, Length) 가 가리키는 토큰을 HEAD 판 헤더에서 읽어 Native 와 같은지 확인한 뒤,
difflib 의 같은 구간으로 옛 줄을 새 줄에 잇는다(같은 글자의 줄이 여럿이어도 갈리지 않는다). 줄 안의 열은 그대로다.
항목이 HEAD 위치라고 본다 - 돌리기 전에 Definitions.cpp 를 HEAD 로 되돌린다.
인자: 헤더 상대 경로(JBroEngine/Modules/...)들. 새 멤버는 --add Type Member Native Header Line Column Length 로 더한다.
"""
import re, subprocess, sys, difflib
import os
REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DEFS = REPO + r"\source\JBroEngine\Modules\JBroScriptApiCatalog2D\Source\Definitions.cpp"
ROW = re.compile(r'^(\s*\{)"([^"]*)","([^"]*)","([^"]*)","([^"]*)", (\d+), (\d+), (\d+)(.*)$')

def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()

def head_text(rel):
    return subprocess.run(["git", "show", "HEAD:source/" + rel], cwd=REPO, capture_output=True).stdout

def lines_of(data):
    return data.decode("utf-8-sig", errors="surrogateescape").replace("\r\n", "\n").split("\n")

argv = sys.argv[1:]
headers = argv[:argv.index("--add")] if "--add" in argv else argv
adds = []
if "--add" in argv:
    rest = argv[argv.index("--add") + 1:]
    while len(rest) >= 7:
        adds.append(rest[:7]); rest = rest[7:]

data = read_bytes(DEFS)
bom = data.startswith(b"\xef\xbb\xbf")
text = data.decode("utf-8-sig")
crlf = "\r\n" in text
rows = text.replace("\r\n", "\n").split("\n")

cache = {}
def versions(rel):
    if rel not in cache:
        old = lines_of(head_text(rel))
        new = lines_of(read_bytes(REPO + "\\source\\" + rel.replace("/", "\\")))
        mapping = {}
        for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, old, new, autojunk=False).get_opcodes():
            if tag == "equal":
                for k in range(i2 - i1):
                    mapping[i1 + k] = j1 + k
        cache[rel] = (old, new, mapping)
    return cache[rel]

def token(lines, line, col, length):
    return lines[line - 1].encode("utf-8", "surrogateescape")[col - 1:col - 1 + length].decode("utf-8", "surrogateescape")

changed = 0
removed = 0
out = []
for row in rows:
    m = ROW.match(row)
    if not m or m.group(5) not in headers:
        out.append(row); continue
    pre, typ, member, native, header, line, col, length, tail = m.groups()
    old, new, mapping = versions(header)
    line = int(line); col = int(col); length = int(length)
    if token(old, line, col, length) != native:
        print("TOKEN MISMATCH", typ, member, native); sys.exit(1)
    if (line - 1) not in mapping:
        print("DROP (line gone):", typ, member, native, old[line - 1].strip()); removed += 1; continue
    best = mapping[line - 1] + 1
    if token(new, best, col, length) != native:
        print("MAPPED TOKEN MISMATCH", typ, member, native); sys.exit(1)
    if best != line:
        changed += 1
    out.append('%s"%s","%s","%s","%s", %d, %d, %d%s' % (pre, typ, member, native, header, best, col, length, tail))

for typ, member, native, header, line, col, length in adds:
    _, new, _ = versions(header)
    if token(new, int(line), int(col), int(length)) != native:
        print("ADD TOKEN MISMATCH", native, "in", new[int(line) - 1]); sys.exit(1)
    newrow = '    {"%s","%s","%s","%s", %s, %s, %s},' % (typ, member, native, header, line, col, length)
    same = [i for i, r in enumerate(out) if ROW.match(r) and ROW.match(r).group(2) == typ]
    last = max(same) if same else max(i for i, r in enumerate(out) if ROW.match(r) and ROW.match(r).group(5) == header)
    out.insert(last + 1, newrow)

result = "\n".join(out)
if crlf:
    result = result.replace("\n", "\r\n")
enc = result.encode("utf-8")
if bom:
    enc = b"\xef\xbb\xbf" + enc
with open(DEFS, "wb") as f:
    f.write(enc)
print("moved", changed, "dropped", removed, "added", len(adds))

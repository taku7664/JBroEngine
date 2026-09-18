# -*- coding: utf-8 -*-
"""한 번에 하나씩 겨눈다.

지난번에 한 번에 15개를 돌리다 테스트가 멈췄고, 그 사이에 내가 소스를 고쳤다가
스크립트의 `finally` 가 **내 수정을 지워 버렸다**. 그래서 이렇게 바꿨다.

- 뮤테이션마다 git 에서 원본을 읽는다. 스크립트가 기억하고 있다가 되돌리지 않는다.
- 되돌리는 것도 `git checkout` 이다. 내가 그 사이에 무엇을 했든 커밋된 것으로 돌아간다.
- 테스트에 짧은 시간 제한을 둔다. 멈춘 테스트 하나가 7분을 잡아먹지 않게.
- 인자로 범위를 받는다. 한 번에 서너 개씩 돌린다.

쓰는 법:  python tools/mutate.py <목록파일> [시작] [끝]
목록파일은 한 줄에 `파일경로<TAB>이름<TAB>찾을것<TAB>바꿀것` 이고,
찾을것/바꿀것의 줄바꿈은 `\\n` 으로 적는다.
예시는 `tools/example-mutations.txt`.

**돌리기 전에 커밋한다.** 되돌리기가 `git checkout` 이라 커밋 안 된 수정은 사라진다.

**돌리는 동안 소스도 테스트도 건드리지 않는다.** 테스트를 고치면 남은 뮤테이션이
전부 가짜로 "잡힘" 이 된다.

**`찾을것` 은 파일에 딱 한 번 나와야 한다.** 여러 번이면 건너뛴다(SKIP) —
목록을 만든 뒤 돌리기 전에 세어 보는 것이 낫다.

살아남은 것은 둘 중 하나다(ProjectRule §12):
- **안 잰 것** — 테스트를 채운다
- **죽일 수 없는 것**(동치) — 왜 동치인지 `tasks/todo.md` Decisions 에 적는다.
  숫자를 맞추려고 테스트를 지어내지 않는다.
"""
import io, os, subprocess, sys

# 이 파일의 자리에서 저장소를 찾는다. 기계마다 다른 절대 경로를 박지 않는다.
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.join(REPO, "source", "JBroEngine")
MSBUILD = r"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
TEST_TIMEOUT_SECONDS = 900
NL = chr(10)


def read(rel):
    return io.open(os.path.join(ROOT, rel), encoding="utf-8-sig").read()


def write(rel, text):
    io.open(os.path.join(ROOT, rel), "w", encoding="utf-8-sig",
            newline=chr(13) + chr(10)).write(text)


def restore(rel):
    # 스크립트가 기억한 것이 아니라 커밋된 것으로 돌린다.
    path = ("source/JBroEngine/" + rel.replace("\\", "/"))
    subprocess.run(["git", "checkout", "--", path], cwd=REPO,
                   capture_output=True, text=True)


def kill_strays():
    subprocess.run(["taskkill", "/F", "/IM", "JBroTests.exe"],
                   capture_output=True, text=True)


def run():
    build = subprocess.run(
        [MSBUILD, r"Tests\JBroTests.vcxproj", "/p:Configuration=Debug",
         "/p:Platform=x64", "/m", "/v:q", "/nologo"],
        cwd=ROOT, capture_output=True, text=True, errors="replace")
    if build.returncode != 0:
        return "build"
    try:
        result = subprocess.run([os.path.join(ROOT, r"Build\x64\Debug\JBroTests.exe")],
                                cwd=ROOT, capture_output=True, text=True,
                                errors="replace", timeout=TEST_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        # 멈춘 것도 죽은 것이다. 남은 프로세스를 치워야 다음 빌드가 링크된다.
        kill_strays()
        return "hang"
    return "pass" if result.returncode == 0 else "fail"


def load(listPath):
    entries = []
    for line in io.open(listPath, encoding="utf-8").read().split(NL):
        if not line.strip() or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) != 4:
            raise SystemExit("bad line: " + line[:60])
        rel, name, old, new = parts
        entries.append((rel, name, old.replace("\\n", NL), new.replace("\\n", NL)))
    return entries


def main():
    entries = load(sys.argv[1])
    first = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    last = int(sys.argv[3]) if len(sys.argv) > 3 else len(entries)
    killed, survived = [], []
    for index in range(first, min(last, len(entries))):
        rel, name, old, new = entries[index]
        restore(rel)
        base = read(rel)
        if base.count(old) != 1:
            print("SKIP (%d matches) %2d %s" % (base.count(old), index, name), flush=True)
            survived.append(name + " [SKIPPED]")
            continue
        write(rel, base.replace(old, new, 1))
        outcome = run()
        restore(rel)
        if outcome == "pass":
            survived.append(name)
        else:
            killed.append(name)
        print("%2d %-46s %s" % (index, name, outcome), flush=True)
    # 마지막으로 다시 성한 상태로 빌드해 둔다. 그러지 않으면 디스크에 남는 실행파일이
    # 마지막 뮤테이션이 들어간 것이고, 다음에 그냥 돌리는 사람이 가짜 실패를 본다.
    run()
    print("")
    print("killed %d / %d" % (len(killed), len(killed) + len(survived)))
    for name in survived:
        print("  SURVIVED " + name)


main()

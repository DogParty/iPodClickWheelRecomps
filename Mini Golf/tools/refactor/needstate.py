#!/usr/bin/env python3
"""Give every game header that names a structure the include that defines it."""
import re, glob
home = {}
for header in ("state.h", "records.h"):
    for t in re.findall(r"struct \[\[gnu::packed\]\] (\w+)", open("src/game/" + header).read()):
        home[t] = header
added = []
for path in sorted(glob.glob("src/game/*.h")):
    if path.endswith(("state.h", "records.h", "guest.h", "game_state.h")):
        continue
    s = open(path).read()
    wanted = {home[t] for t in home if re.search(r"\b" + t + r"&", s)}
    wanted = {w for w in wanted if '#include "%s"' % w not in s}
    if not wanted:
        continue
    for header in sorted(wanted):
        s = re.sub(r'(#include "[^"]+"\n)', '#include "%s"\n' % header + r"\1", s, count=1)
    open(path, "w").write(s)
    added.append(path + " (" + ",".join(sorted(wanted)) + ")")
print("includes added:", "; ".join(added) or "(none)")

#!/usr/bin/env python3
"""Drop the original's null-address asserts on parameters that are now references.

A reference cannot be null; a caller that passes `as_x(0)` now fails in `guest()` at the call
site instead, which is where the mistake is. Run after refs.py, once per type.
"""
import re, sys, glob
typename = sys.argv[1]
removed = 0
for path in sorted(glob.glob("src/game/*.cpp")):
    s = open(path).read()
    names = set(re.findall(r"\b" + typename + r"&\s*(\w+)", s))
    if not names:
        continue
    out = s
    for n in names:
        pattern = re.compile(r"[ \t]*if \(" + n + r" == 0\) \{\n[ \t]*assert_trap\(0x[0-9a-f']+u\);\n[ \t]*\}\n")
        out, count = pattern.subn("", out)
        removed += count
    if out != s:
        open(path, "w").write(out)
print(f"{typename}: {removed} null-address asserts dropped")

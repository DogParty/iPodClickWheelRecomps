#!/usr/bin/env python3
"""Turn `uint32_t x = ...` locals that are really pointers into typed references.

    locals.py as_pack PackRecord [--apply] [--min 2]

Inside each function, a local declared `uint32_t p = <expr>;` and then read only through
`as_pack(p)` becomes `PackRecord& p = as_pack(<expr>);`, and the accessor drops out of the body.
Locals used at least `--min` times through the accessor are converted (default 2, so single uses
are left alone).
"""
import re, sys, glob
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from refs import functions, match, split_args  # the same scanner

FILES = sorted(glob.glob("src/game/*.cpp"))

def main():
    accessor, typename = sys.argv[1], sys.argv[2]
    write = "--apply" in sys.argv
    minimum = int(sys.argv[sys.argv.index("--min") + 1]) if "--min" in sys.argv else 2
    use = re.compile(r"\b" + accessor + r"\(\s*([A-Za-z_]\w*)\s*\)")
    decl = re.compile(r"( *)(?:const )?uint32_t (\w+) = ([^;]+);")
    converted = 0
    for path in FILES:
        source = open(path).read()
        while True:
            edit = None
            for name, ps, pe, bs, be in functions(source):
                if bs is None:
                    continue
                body = source[bs:be]
                counts = {}
                for n in use.findall(body):
                    counts[n] = counts.get(n, 0) + 1
                declared = {}
                for d in decl.finditer(body):
                    declared[d.group(2)] = declared.get(d.group(2), 0) + 1
                for m in decl.finditer(body):
                    indent, local, value = m.group(1), m.group(2), m.group(3)
                    if counts.get(local, 0) < minimum or "\n" in value:
                        continue
                    if declared[local] > 1:
                        continue  # the name is reused; each would need its own reference
                    new_body = (body[:m.start()] + f"{indent}{typename}& {local} = {accessor}({value});"
                                + body[m.end():])
                    new_body = re.sub(r"\b" + accessor + r"\(\s*" + local + r"\s*\)", local, new_body)
                    edit = (bs, be, new_body)
                    break
                if edit:
                    break
            if not edit:
                break
            converted += 1
            source = source[:edit[0]] + edit[2] + source[edit[1]:]
        if write:
            open(path, "w").write(source)
    print(f"{'applied' if write else 'dry run'}: {converted} locals become {typename}&")

main()

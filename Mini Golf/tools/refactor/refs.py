#!/usr/bin/env python3
"""Turn `uint32_t x` parameters that are really pointers into typed references.

    refs.py as_image ImageRecord [--apply]

Two passes, each over the whole tree: retype the parameters and drop the `as_image(...)` around
them in the bodies; then wrap the matching argument at every call site, except where the caller
already holds a reference of that type.
"""
import re, sys, glob

FILES = sorted(glob.glob("src/game/*.cpp") + glob.glob("src/game/*.h"))
KEYWORDS = {"if","for","while","switch","return","sizeof","static_cast","reinterpret_cast",
            "const_cast","catch","offsetof","alignof","decltype","noexcept","assert"}
NAME = re.compile(r"(?<![\w:>.])([A-Za-z_]\w*)\s*\(")

def is_char_literal(s, i):
    """True when s[i] is the opening quote of a character literal rather than a digit separator
    (C++14 lets 0xffff'ffff carry one) — the character before a separator is alphanumeric."""
    return i == 0 or not (s[i - 1].isalnum() or s[i - 1] == '_')


def match(s, i, opener, closer):
    depth = 0
    while i < len(s):
        c = s[i]
        if c == opener:
            depth += 1
        elif c == closer:
            depth -= 1
            if depth == 0:
                return i + 1
        elif c == '"' or (c == "'" and is_char_literal(s, i)):
            q = c; i += 1
            while i < len(s) and s[i] != q:
                i += 2 if s[i] == '\\' else 1
        i += 1
    return -1

def split_args(text):
    out, depth, start, i = [], 0, 0, 0
    while i < len(text):
        c = text[i]
        if c in '([{': depth += 1
        elif c in ')]}': depth -= 1
        elif c == '"' or (c == "'" and is_char_literal(text, i)):
            q = c; i += 1
            while i < len(text) and text[i] != q:
                i += 2 if text[i] == '\\' else 1
        elif c == ',' and depth == 0:
            out.append(text[start:i]); start = i + 1
        i += 1
    out.append(text[start:])
    return out

def functions(source):
    for m in NAME.finditer(source):
        name = m.group(1)
        if name in KEYWORDS:
            continue
        op = m.end() - 1
        close = match(source, op, '(', ')')
        if close < 0:
            continue
        rest = source[close:close + 60]
        tail = rest.lstrip()
        if tail[:1] == '{':
            bs = close + (len(rest) - len(rest.lstrip()))
            be = match(source, bs, '{', '}')
            if be > 0:
                yield name, op + 1, close - 1, bs, be
        elif tail[:1] == ';':
            yield name, op + 1, close - 1, None, None

def apply_pieces(source, pieces):
    for start, end, text in sorted(pieces, key=lambda p: -p[0]):
        source = source[:start] + text + source[end:]
    return source

def main():
    accessor, typename = sys.argv[1], sys.argv[2]
    write = "--apply" in sys.argv
    skip = set()
    if "--skip" in sys.argv:
        skip = set(sys.argv[sys.argv.index("--skip") + 1].split(","))
    call = re.compile(r"\b" + accessor + r"\(\s*([A-Za-z_]\w*)\s*\)")
    param_re = re.compile(r"\s*(?:const\s+)?uint32_t\s+(\w+)\s*$")
    texts = {p: open(p).read() for p in FILES}

    # pass 1: which parameters are really references?
    converted = {}
    for source in texts.values():
        for name, ps, pe, bs, be in functions(source):
            if bs is None:
                continue
            used = set(call.findall(source[bs:be]))
            for index, param in enumerate(split_args(source[ps:pe])):
                m = param_re.fullmatch(param)
                if m and m.group(1) in used and name not in skip:
                    converted.setdefault(name, set()).add(index)
    if not converted:
        print("nothing to convert"); return

    # pass 2: retype parameters, drop the accessor around them in the body
    for path, source in texts.items():
        pieces = []
        for name, ps, pe, bs, be in functions(source):
            indices = converted.get(name)
            if not indices:
                continue
            params = split_args(source[ps:pe])
            if max(indices) >= len(params):
                continue
            names, new = [], list(params)
            for i in indices:
                m = param_re.fullmatch(new[i])
                if not m:
                    continue
                names.append(m.group(1))
                new[i] = re.sub(r"(?:const\s+)?uint32_t(\s+\w+)\s*$", typename + r"&\1",
                                new[i].rstrip())
            if not new or new == params:
                continue
            pieces.append((ps, pe, ",".join(new)))
            if bs is not None and names:
                body = source[bs:be]
                for n in names:
                    body = re.sub(r"\b" + accessor + r"\(\s*" + n + r"\s*\)", n, body)
                pieces.append((bs, be, body))
        if pieces:
            texts[path] = apply_pieces(source, pieces)

    # pass 3: wrap the argument at every call site, unless the caller already has a reference
    # of that type in scope — decided per function, not per file.
    wrapped = 0
    for path, source in texts.items():
        scopes = []   # (body start, body end, names already of this type)
        for name, ps, pe, bs, be in functions(source):
            if bs is None:
                continue
            names = set(re.findall(r"\b" + typename + r"&\s*(\w+)", source[ps:pe] + source[bs:be]))
            scopes.append((bs, be, names))
        def held_at(position):
            best, names = None, set()
            for bs, be, ns in scopes:
                if bs <= position < be and (best is None or bs > best):
                    best, names = bs, ns
            return names
        # One wrap at a time, re-scanning after each: call sites nest, and splicing several
        # ranges computed against the same text would discard the inner edits.
        source = texts[path]
        while True:
            scopes = []
            for name, ps, pe, bs, be in functions(source):
                if bs is None:
                    continue
                names = set(re.findall(r"\b" + typename + r"&\s*(\w+)",
                                       source[ps:pe] + source[bs:be]))
                scopes.append((bs, be, names))
            edit = None
            for name, indices in converted.items():
                for m in re.finditer(r"(?<![\w:>.])" + name + r"\s*\(", source):
                    op = m.end() - 1
                    close = match(source, op, '(', ')')
                    if close < 0 or source[close:close + 40].lstrip()[:1] == '{':
                        continue
                    args = split_args(source[op + 1:close - 1])
                    if (len(args) == 1 and not args[0].strip()) or max(indices) >= len(args):
                        continue
                    held = set()
                    best = None
                    for bs, be, ns in scopes:
                        if bs <= op < be and (best is None or bs > best):
                            best, held = bs, ns
                    for i in indices:
                        text = args[i].strip()
                        if (not text or text.startswith(accessor + "(") or text in held
                                or re.fullmatch(typename + r"&\s*\w+", text)):
                            continue
                        lead = args[i][:len(args[i]) - len(args[i].lstrip())]
                        args[i] = lead + accessor + "(" + text + ")"
                        edit = (op + 1, close - 1, ",".join(args))
                        break
                    if edit:
                        break
                if edit:
                    break
            if not edit:
                break
            wrapped += 1
            source = source[:edit[0]] + edit[2] + source[edit[1]:]
        texts[path] = source


    if write:
        for path, source in texts.items():
            open(path, "w").write(source)
    print(f"{'applied' if write else 'dry run'}: {len(converted)} functions "
          f"({', '.join(sorted(converted))}), {wrapped} call sites wrapped")

if __name__ == "__main__":
    main()

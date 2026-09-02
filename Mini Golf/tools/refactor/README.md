# Refactoring tools

One-shot rewriters for the mechanical half of turning decompiled code into idiomatic C++. They
are here because the job is not finished: several of the game's record types still take their
address rather than a reference (see PLAN.md), and these are how the rest will be converted.

Run them from the project root, always in this order, and **build and run the oracle after every
step** — they rewrite code, and a rewriter that is subtly wrong produces code that still
compiles.

```sh
python3 tools/refactor/refs.py as_image ImageRecord            # dry run: what would change
python3 tools/refactor/refs.py as_image ImageRecord --apply    # parameters -> references
python3 tools/refactor/locals.py as_image ImageRecord --apply  # the same for locals
python3 tools/refactor/nullchecks.py ImageRecord               # drop the impossible null checks
python3 tools/refactor/needstate.py                            # add the include for the type
cmake --build build && tests/diff.sh boot && tests/diff.sh menus
```

`refs.py --skip name1,name2` leaves functions alone that genuinely want the address — a
constructor that frees the object it is given, for instance.

Two failure modes cost real time when these were written, and both corrupt code *silently*:

* a `'` in `0xffff'ffff` read as the start of a character literal, which swallowed everything up
  to the next apostrophe and made the scanner's idea of where a function ends wrong;
* a local name declared twice in one function, where rewriting the first declaration stripped the
  accessor from the second one's uses too.

Both are fixed here, but anything else of this shape deserves the same suspicion.

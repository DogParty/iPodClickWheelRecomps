"""Static recompiler for iPod eApp titles: image parsing, function table, ARM decoding, C++ emission.

Entry points are the scripts in `tools/` (`funcs.py`, `emit.py`); this package holds the shared
logic so each step is small and testable. Nothing here depends on the emulator tree.
"""
